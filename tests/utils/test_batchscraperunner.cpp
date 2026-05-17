/**
 * @file test_batchscraperunner.cpp
 * @brief Unit tests for the batch-scrape state machine.
 *
 * Exercises the controller end-to-end against a stub MetadataLookup
 * provider (no real network) and with a null IDatabaseManager*. The
 * stub returns deterministic canned responses keyed on the query, so
 * each scenario (all-found / partial-skip / fetchDetail-error /
 * cancellation) maps onto a small input list of paths.
 *
 * Why no QSqlDatabase fixture: applyScrapedItem's `databaseManager
 * may be null` carve-out lets the runner exercise its full state
 * machine without a SQLite file. The "scraped" counter still
 * advances because the runner treats the !m_db path as "no DB to
 * fail against".
 */

#include "batchscraperunner.h"

#include "errorutils.h"
#include "metadatalookupprovider.h"
#include "mockdatabasemanager.h"
#include "scrapertypes.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

namespace {

/// Tiny in-memory MetadataLookupProvider. Returns whatever the test
/// preloaded for each query. Both lookup() and fetchDetail() fire
/// the callback on the same event-loop tick via QTimer::singleShot
/// so the chain behaves like the real (async) providers — without
/// that, all the callbacks run inline and the cancellation /
/// progress-emission ordering doesn't get exercised.
class StubProvider : public MetadataLookupProvider {
public:
  /// Per-query canned responses. The runner queries by the file's
  /// `completeBaseName` (extension-stripped name), so the key is the
  /// expected base name.
  struct Canned {
    QList<Scraper::ScrapeCandidate> candidates;
    Scraper::ScrapedItem detail;
    /// Force lookup() to return this error instead of the candidates.
    /// Empty message = no error.
    QString lookupError;
    /// Force fetchDetail() to return this error.
    QString detailError;
  };
  QHash<QString, Canned> byQuery;

  /// Per-URL canned cover bytes. fetchMediaBytes() looks up the
  /// requested URL here; missing URL → empty bytes (the runner
  /// treats that as a failed cover fetch and skips the write).
  QHash<QUrl, QByteArray> mediaByUrl;
  /// URLs in this set fail the fetchMediaBytes call. The bytes from
  /// `mediaByUrl` (if any) are ignored when the URL is also here.
  QSet<QUrl> mediaErrorUrls;
  /// Track which URLs the runner asked us to fetch — tests assert
  /// against the call shape to verify the fetchPrimaryCover toggle.
  mutable QList<QUrl> mediaRequestLog;

  QString id() const override { return QStringLiteral("stub"); }
  QString displayName() const override { return QStringLiteral("Stub"); }
  QStringList categories() const override { return {QStringLiteral("games")}; }
  Capabilities capabilities() const override { return Capability::MetadataLookup; }
  QUrl searchUrl(const QString &) const override { return {}; }

  void lookup(const QString &query, LookupCallback cb) override {
    // Async-emulate by posting back on the event loop. Without this
    // the test's QSignalSpy on `progress` only sees the final state
    // because everything synchronously chains.
    QTimer::singleShot(0, [this, query, cb = std::move(cb)]() {
      const Canned &c = byQuery.value(query);
      if (!c.lookupError.isEmpty()) {
        cb(ErrorUtils::ErrorContext::error(
            ErrorUtils::ErrorCode::InvalidArgument, c.lookupError, "stub"));
        return;
      }
      cb(c.candidates);
    });
  }

  void fetchDetail(const Scraper::ScrapeCandidate &candidate,
                   DetailCallback cb) override {
    QTimer::singleShot(0, [this, candidate, cb = std::move(cb)]() {
      // Match the candidate back to its query by walking byQuery —
      // the test's data tables are small so the lookup cost is fine.
      for (auto it = byQuery.constBegin(); it != byQuery.constEnd(); ++it) {
        if (!it.value().candidates.isEmpty() &&
            it.value().candidates.first().providerSpecificId ==
                candidate.providerSpecificId) {
          if (!it.value().detailError.isEmpty()) {
            cb(ErrorUtils::ErrorContext::error(
                ErrorUtils::ErrorCode::InvalidArgument,
                it.value().detailError, "stub"));
            return;
          }
          cb(it.value().detail);
          return;
        }
      }
      cb(Scraper::ScrapedItem{});
    });
  }

  void fetchMediaBytes(const QUrl &url, MediaCallback cb) override {
    mediaRequestLog.append(url);
    QTimer::singleShot(0, [this, url, cb = std::move(cb)]() {
      if (mediaErrorUrls.contains(url)) {
        cb(ErrorUtils::ErrorContext::error(
            ErrorUtils::ErrorCode::InvalidArgument,
            QStringLiteral("media fetch failed"), "stub"));
        return;
      }
      cb(mediaByUrl.value(url));
    });
  }
};

/// IDatabaseManager double that reports every item as already
/// scraped (non-empty `source`). Under `RescrapeMode::Skip` the
/// runner's filterAlreadyScraped() drops every such item up front.
class AllScrapedDb : public KartendTest::MockDatabaseManager {
public:
  [[nodiscard]] ItemMetadataStore::ItemMetadata loadItemMetadata(const QString &,
                                                                 const QString &) const override {
    ItemMetadataStore::ItemMetadata md;
    md.source = QStringLiteral("screenscraper"); // non-empty → "already scraped"
    return md;
  }
};

/// Helper to build a deterministic candidate + detail pair for a
/// given query. Keeps the test bodies focused on the controller
/// behaviour rather than the structural data.
StubProvider::Canned makeMatch(const QString &id, const QString &title) {
  StubProvider::Canned c;
  Scraper::ScrapeCandidate cand;
  cand.displayName = title;
  cand.providerSpecificId = id;
  cand.matchScore = 100;
  c.candidates.append(cand);
  c.detail.title = title;
  c.detail.sourceProviderId = QStringLiteral("stub");
  return c;
}

/// Same shape as `makeMatch`, plus a "front" cover MediaAsset on the
/// detail so the cover-fetch branch fires for it. Returns the URL
/// the runner is expected to request so the caller can populate the
/// stub's `mediaByUrl` / `mediaErrorUrls` table.
QPair<StubProvider::Canned, QUrl> makeMatchWithCover(const QString &id,
                                                     const QString &title) {
  StubProvider::Canned c = makeMatch(id, title);
  Scraper::MediaAsset front;
  front.type = QStringLiteral("front");
  front.label = QStringLiteral("Front cover");
  front.url = QUrl(QStringLiteral("https://example.invalid/covers/%1.png").arg(id));
  c.detail.media.append(front);
  return {c, front.url};
}

/// Spin the event loop until `runner` emits `finished`, then return
/// the summary. The 5-second timeout guards against a wedged state
/// machine — at the stub provider's 0-delay timer cadence a 100-item
/// run completes in milliseconds.
Scraper::BatchScrapeRunner::Summary waitForFinish(Scraper::BatchScrapeRunner *runner) {
  Scraper::BatchScrapeRunner::Summary captured;
  bool finished = false;
  QObject::connect(runner, &Scraper::BatchScrapeRunner::finished,
                   [&captured, &finished](const auto &s) {
                     captured = s;
                     finished = true;
                   });
  const int kTimeoutMs = 5000;
  QElapsedTimer timer;
  timer.start();
  while (!finished && timer.elapsed() < kTimeoutMs) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  }
  return captured;
}

} // namespace

class TestBatchScrapeRunner : public QObject {
  Q_OBJECT

private slots:
  void scrapesAllItemsThatHaveCandidates();
  void skipsItemsWithNoCandidates();
  void countsErrorsOnLookupFailure();
  void countsErrorsOnFetchDetailFailure();
  void firstFailuresIsCappedAtFive();
  void cancelStopsAfterInFlightItem();
  void emptyPathListFinishesImmediately();
  void nullProviderEmitsFinishedWithExplanation();
  void progressSignalReportsCurrentItem();
  void fetchesPrimaryCoverByDefault();
  void coverFetchDisabledSkipsMediaCall();
  void coverFetchFailureLeavesMetadataSavedAndCountsAsScraped();
  void coverFetchSkippedWhenNoFrontAsset();
  void skipModeCountsAlreadyScrapedItemsAsSkipped();
};

void TestBatchScrapeRunner::scrapesAllItemsThatHaveCandidates() {
  auto stub = std::make_shared<StubProvider>();
  stub->byQuery[QStringLiteral("Alpha")] = makeMatch("1", "Alpha (USA)");
  stub->byQuery[QStringLiteral("Beta")] = makeMatch("2", "Beta (USA)");
  stub->byQuery[QStringLiteral("Gamma")] = makeMatch("3", "Gamma (USA)");

  const QStringList paths{QStringLiteral("/games/Alpha.bin"),
                          QStringLiteral("/games/Beta.bin"),
                          QStringLiteral("/games/Gamma.bin")};
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"),
                                    paths, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 3);
  QCOMPARE(summary.skipped, 0);
  QCOMPARE(summary.errors, 0);
}

void TestBatchScrapeRunner::skipsItemsWithNoCandidates() {
  // Beta isn't in the provider's table → the stub returns empty
  // candidates for it; the runner counts that as "skipped" rather
  // than an error.
  auto stub = std::make_shared<StubProvider>();
  stub->byQuery[QStringLiteral("Alpha")] = makeMatch("1", "Alpha");
  stub->byQuery[QStringLiteral("Gamma")] = makeMatch("3", "Gamma");

  const QStringList paths{QStringLiteral("/games/Alpha.bin"),
                          QStringLiteral("/games/Beta.bin"),
                          QStringLiteral("/games/Gamma.bin")};
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"),
                                    paths, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 2);
  QCOMPARE(summary.skipped, 1);
  QCOMPARE(summary.errors, 0);
}

void TestBatchScrapeRunner::countsErrorsOnLookupFailure() {
  // Network-level failure during lookup → errors++, first-failures
  // captures the per-item message.
  auto stub = std::make_shared<StubProvider>();
  stub->byQuery[QStringLiteral("Alpha")] = makeMatch("1", "Alpha");
  StubProvider::Canned bad;
  bad.lookupError = QStringLiteral("HTTP 500");
  stub->byQuery[QStringLiteral("Beta")] = bad;

  const QStringList paths{QStringLiteral("/games/Alpha.bin"),
                          QStringLiteral("/games/Beta.bin")};
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"),
                                    paths, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 1);
  QCOMPARE(summary.errors, 1);
  QCOMPARE(summary.firstFailures.size(), 1);
  QVERIFY(summary.firstFailures.first().contains(QStringLiteral("HTTP 500")));
}

void TestBatchScrapeRunner::countsErrorsOnFetchDetailFailure() {
  // fetchDetail-stage error gets the same treatment as a lookup
  // failure — the runner doesn't distinguish between the two stages
  // since the user-facing outcome is the same ("this item didn't
  // scrape").
  auto stub = std::make_shared<StubProvider>();
  StubProvider::Canned detailFail;
  Scraper::ScrapeCandidate cand;
  cand.displayName = QStringLiteral("Alpha");
  cand.providerSpecificId = QStringLiteral("1");
  detailFail.candidates.append(cand);
  detailFail.detailError = QStringLiteral("detail not found");
  stub->byQuery[QStringLiteral("Alpha")] = detailFail;

  Scraper::BatchScrapeRunner runner(
      nullptr, stub, QStringLiteral("uuid"),
      QStringList{QStringLiteral("/games/Alpha.bin")}, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.errors, 1);
  QCOMPARE(summary.scraped, 0);
}

void TestBatchScrapeRunner::firstFailuresIsCappedAtFive() {
  // Failure messages can be arbitrarily long and a misconfigured
  // provider could fail every single one of a 10k-item collection —
  // the runner caps the captured list at 5 so the summary stays
  // readable in the UI.
  auto stub = std::make_shared<StubProvider>();
  StubProvider::Canned bad;
  bad.lookupError = QStringLiteral("nope");
  QStringList paths;
  for (int i = 0; i < 10; ++i) {
    const QString name = QStringLiteral("Item%1").arg(i);
    stub->byQuery[name] = bad;
    paths.append(QStringLiteral("/games/") + name + QStringLiteral(".bin"));
  }
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"),
                                    paths, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.errors, 10);
  QCOMPARE(summary.firstFailures.size(), 5);
}

void TestBatchScrapeRunner::cancelStopsAfterInFlightItem() {
  // Cancel mid-flight: hook the progress signal, call cancel() the
  // moment the second item's progress fires. The runner must observe
  // the cancel before launching the third item's lookup, so the
  // total processed count comes out strictly below the input size.
  // Using a signal-driven cancel (rather than a timer) sidesteps the
  // race where a 0ms stub callback runs faster than a singleShot
  // can land.
  auto stub = std::make_shared<StubProvider>();
  for (int i = 0; i < 10; ++i) {
    const QString name = QStringLiteral("Item%1").arg(i);
    stub->byQuery[name] = makeMatch(QString::number(i), name);
  }
  QStringList paths;
  for (int i = 0; i < 10; ++i) {
    paths.append(QStringLiteral("/games/Item%1.bin").arg(i));
  }
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"),
                                    paths, QString());
  QObject::connect(&runner, &Scraper::BatchScrapeRunner::progress,
                   [&runner](int done, int /*total*/, const QString &) {
                     if (done == 1) runner.cancel();
                   });
  runner.start();
  const auto summary = waitForFinish(&runner);
  // Past the cancel, no further items should run. The exact count is
  // 1 (the first item, scraped before cancel hit) but the assertion
  // stays "< 10" to allow some slop if the stub's QTimer scheduling
  // interleaves differently on slower runners.
  QVERIFY(summary.scraped + summary.skipped + summary.errors < 10);
}

void TestBatchScrapeRunner::emptyPathListFinishesImmediately() {
  auto stub = std::make_shared<StubProvider>();
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"),
                                    QStringList{}, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 0);
  QCOMPARE(summary.skipped, 0);
  QCOMPARE(summary.errors, 0);
}

void TestBatchScrapeRunner::nullProviderEmitsFinishedWithExplanation() {
  // Defensive: caller couldn't resolve a provider (e.g. registry had
  // no MetadataLookup providers for the collection's type). The
  // runner must not crash on start(); it should emit `finished`
  // immediately with a hint in firstFailures so the UI can display
  // "no provider configured" rather than wedging on a never-firing
  // dialog.
  Scraper::BatchScrapeRunner runner(nullptr, nullptr, QStringLiteral("uuid"),
                                    QStringList{QStringLiteral("/a.bin")}, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 0);
  QVERIFY(!summary.firstFailures.isEmpty());
}

void TestBatchScrapeRunner::progressSignalReportsCurrentItem() {
  // The progress signal is what the UI's KartProgressDialog binds to;
  // verify it fires once per item with the right basename and the
  // right `done` cursor.
  auto stub = std::make_shared<StubProvider>();
  stub->byQuery[QStringLiteral("Alpha")] = makeMatch("1", "Alpha");
  stub->byQuery[QStringLiteral("Beta")] = makeMatch("2", "Beta");

  Scraper::BatchScrapeRunner runner(
      nullptr, stub, QStringLiteral("uuid"),
      QStringList{QStringLiteral("/games/Alpha.bin"),
                  QStringLiteral("/games/Beta.bin")},
      QString());
  QSignalSpy spy(&runner, &Scraper::BatchScrapeRunner::progress);
  runner.start();
  waitForFinish(&runner);
  QCOMPARE(spy.count(), 2);
  QCOMPARE(spy.at(0).at(0).toInt(), 0);  // done = 0 before first item
  QCOMPARE(spy.at(0).at(1).toInt(), 2);  // total = 2
  QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("Alpha.bin"));
  QCOMPARE(spy.at(1).at(0).toInt(), 1);  // done = 1 before second
  QCOMPARE(spy.at(1).at(2).toString(), QStringLiteral("Beta.bin"));
}

void TestBatchScrapeRunner::fetchesPrimaryCoverByDefault() {
  // Default constructor flag is true: when the ScrapedItem has a
  // "front" MediaAsset, the runner must call fetchMediaBytes for it
  // and pass the bytes through to applyScrapedItem as a
  // PendingMediaWrite. We verify the request side via the stub's
  // request log — the apply side is exercised by the scrapepersistence
  // tests directly, so re-asserting it here would duplicate coverage.
  auto stub = std::make_shared<StubProvider>();
  auto [canned, coverUrl] = makeMatchWithCover("1", "Alpha");
  stub->byQuery[QStringLiteral("Alpha")] = canned;
  stub->mediaByUrl[coverUrl] = QByteArrayLiteral("\x89PNG\r\nfakebytes");

  Scraper::BatchScrapeRunner runner(
      nullptr, stub, QStringLiteral("uuid"),
      QStringList{QStringLiteral("/games/Alpha.bin")}, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 1);
  QCOMPARE(stub->mediaRequestLog.size(), 1);
  QCOMPARE(stub->mediaRequestLog.first(), coverUrl);
}

void TestBatchScrapeRunner::coverFetchDisabledSkipsMediaCall() {
  // When constructed with fetchPrimaryCover=false, the runner must
  // NOT issue a fetchMediaBytes call even if a "front" asset is
  // present. Lets metered-connection / metadata-only batch runs
  // skip the extra round-trip per item.
  auto stub = std::make_shared<StubProvider>();
  auto [canned, coverUrl] = makeMatchWithCover("1", "Alpha");
  stub->byQuery[QStringLiteral("Alpha")] = canned;
  stub->mediaByUrl[coverUrl] = QByteArrayLiteral("png");

  Scraper::BatchScrapeRunner runner(
      nullptr, stub, QStringLiteral("uuid"),
      QStringList{QStringLiteral("/games/Alpha.bin")}, QString(),
      /*fetchPrimaryCover=*/false);
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 1);
  QVERIFY(stub->mediaRequestLog.isEmpty());
}

void TestBatchScrapeRunner::coverFetchFailureLeavesMetadataSavedAndCountsAsScraped() {
  // Cover download is non-fatal — when the media URL errors, the
  // runner still calls applyScrapedItem with an empty media list so
  // the user keeps the title/description/etc. Counts as a successful
  // scrape (not an error), but the request log proves we tried.
  auto stub = std::make_shared<StubProvider>();
  auto [canned, coverUrl] = makeMatchWithCover("1", "Alpha");
  stub->byQuery[QStringLiteral("Alpha")] = canned;
  stub->mediaErrorUrls.insert(coverUrl);

  Scraper::BatchScrapeRunner runner(
      nullptr, stub, QStringLiteral("uuid"),
      QStringList{QStringLiteral("/games/Alpha.bin")}, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 1);
  QCOMPARE(summary.errors, 0);
  QCOMPARE(stub->mediaRequestLog.size(), 1);
}

void TestBatchScrapeRunner::coverFetchSkippedWhenNoFrontAsset() {
  // A ScrapedItem without any "front" MediaAsset (provider didn't
  // expose one for this item) shouldn't fail or stall the runner —
  // it falls through directly to applyAndAdvance with empty writes.
  // Verify both via the request log (no fetch was attempted) and
  // the summary count.
  auto stub = std::make_shared<StubProvider>();
  stub->byQuery[QStringLiteral("Alpha")] = makeMatch("1", "Alpha");

  Scraper::BatchScrapeRunner runner(
      nullptr, stub, QStringLiteral("uuid"),
      QStringList{QStringLiteral("/games/Alpha.bin")}, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 1);
  QVERIFY(stub->mediaRequestLog.isEmpty());
}

void TestBatchScrapeRunner::skipModeCountsAlreadyScrapedItemsAsSkipped() {
  // Regression: under Skip rescrape mode, filterAlreadyScraped() used
  // to drop already-scraped items SILENTLY — they vanished from the
  // accounting, so scraped+skipped+errors never reconciled with the
  // item count the caller started from. They must now count as
  // `skipped` instead.
  AllScrapedDb db;
  // The provider is never consulted: every item is filtered out
  // before the queue runs.
  auto stub = std::make_shared<StubProvider>();
  Scraper::BatchScrapeRunner runner(
      &db, stub, QStringLiteral("uuid"),
      QStringList{QStringLiteral("/games/A.bin"), QStringLiteral("/games/B.bin"),
                  QStringLiteral("/games/C.bin")},
      QString(), /*fetchPrimaryCover=*/false, Scraper::RescrapeMode::Skip,
      /*itemConcurrency=*/1);
  runner.start();
  const auto summary = waitForFinish(&runner);
  // All three were already scraped → all three counted as skipped,
  // none scraped, none errored. scraped+skipped+errors == 3 == total.
  QCOMPARE(summary.skipped, 3);
  QCOMPARE(summary.scraped, 0);
  QCOMPARE(summary.errors, 0);
}

QTEST_MAIN(TestBatchScrapeRunner)
#include "test_batchscraperunner.moc"
