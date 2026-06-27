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

#include "applicationcontext.h"
#include "errorutils.h"
#include "metadatalookupprovider.h"
#include "mockdatabasemanager.h"
#include "scrapertypes.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
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
    /// HTTP status to stamp onto the lookup error's ErrorContext.
    /// 0 = leave it unset. 430/431 mark a ScreenScraper quota
    /// exhaustion the runner is expected to stop the batch on.
    int lookupErrorHttpStatus = 0;
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
        auto err = ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                                   c.lookupError, "stub");
        if (c.lookupErrorHttpStatus != 0) {
          err.withHttpStatus(c.lookupErrorHttpStatus);
        }
        cb(err);
        return;
      }
      cb(c.candidates);
    });
  }

  void fetchDetail(const Scraper::ScrapeCandidate &candidate, DetailCallback cb) override {
    QTimer::singleShot(0, [this, candidate, cb = std::move(cb)]() {
      // Match the candidate back to its query by walking byQuery —
      // the test's data tables are small so the lookup cost is fine.
      for (auto it = byQuery.constBegin(); it != byQuery.constEnd(); ++it) {
        if (!it.value().candidates.isEmpty() &&
            it.value().candidates.first().providerSpecificId == candidate.providerSpecificId) {
          if (!it.value().detailError.isEmpty()) {
            cb(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
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
        cb(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
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

/// IDatabaseManager double that returns a non-empty source AND a fixed
/// `updated_at` timestamp. Lets tests stage the runner's
/// recent-window filter against a known scrape age — pass an offset
/// (in days from "now") to simulate items just scraped vs. items
/// scraped beyond the configured Skip window.
class TimestampedScrapedDb : public KartendTest::MockDatabaseManager {
public:
  explicit TimestampedScrapedDb(int daysAgo) {
    m_updatedAt = QDateTime::currentDateTimeUtc().addDays(-daysAgo).toString(Qt::ISODate);
  }
  [[nodiscard]] ItemMetadataStore::ItemMetadata loadItemMetadata(const QString &,
                                                                 const QString &) const override {
    ItemMetadataStore::ItemMetadata md;
    md.source = QStringLiteral("screenscraper");
    md.updatedAt = m_updatedAt;
    return md;
  }

private:
  QString m_updatedAt;
};

/// Writes a stub metadata sidecar to `{artworkDir}/metadata/{baseName}.json`
/// so the runner's on-disk skip branch has something to find. Returns the
/// absolute path so the caller can stat it (e.g. to confirm mtime
/// adjustment landed). Failures are reported via QVERIFY in the caller.
QString writeStubSidecar(const QString &artworkDir, const QString &baseName,
                         qint64 mtimeOffsetSeconds = 0) {
  const QString metadataDir = QDir(artworkDir).filePath(QStringLiteral("metadata"));
  if (!QDir().mkpath(metadataDir)) return {};
  const QString sidecarPath = QDir(metadataDir).filePath(baseName + QStringLiteral(".json"));
  QFile f(sidecarPath);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
  f.write("{\"title\":\"stub\"}");
  f.close();
  if (mtimeOffsetSeconds != 0) {
    const QDateTime mtime = QDateTime::currentDateTime().addSecs(mtimeOffsetSeconds);
    // setFileTime returns false silently on filesystems that don't
    // support sub-second precision; callers compare day-scale offsets
    // so the granularity is fine. We swallow the bool — best-effort.
    QFile rewriter(sidecarPath);
    rewriter.open(QIODevice::ReadWrite);
    rewriter.setFileTime(mtime, QFileDevice::FileModificationTime);
    rewriter.close();
  }
  return sidecarPath;
}

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
QPair<StubProvider::Canned, QUrl> makeMatchWithCover(const QString &id, const QString &title) {
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
  void firstFailuresRecordsEveryFailure();
  void cancelStopsAfterInFlightItem();
  void emptyPathListFinishesImmediately();
  void nullProviderEmitsFinishedWithExplanation();
  void progressSignalReportsCurrentItem();
  void fetchesPrimaryCoverByDefault();
  void coverFetchDisabledSkipsMediaCall();
  void coverFetchFailureLeavesMetadataSavedAndCountsAsScraped();
  void coverFetchSkippedWhenNoFrontAsset();
  void sidecarWriteFailureCountedInSummary();
  void decideScrapeSkipBranches();
  void skipModeCountsAlreadyScrapedItemsAsSkipped();
  void skipModeAlsoSkipsItemsWithSidecarButNoDbRow();
  void skipModeWindowKeepsRecentScrapesSkipped();
  void skipModeWindowReleasesStaleScrapesForRefresh();
  void skipModeWindowZeroPreservesLegacyBehaviour();
  void fillMissingPreSkipsItemsWithEveryTickedFieldCovered();
  void fillMissingScrapesItemsMissingAnyTickedField();
  void fillMissingHonoursRefreshWindowSameAsSkip();
  void quotaExhaustedStopsBatchAndSkipsRemainingItems();
  void quotaExhaustedAbortsInFlightItemsAtConcurrency();
  void skipCurrentItemSkipsOnlyTheDisplayedItem();
};

void TestBatchScrapeRunner::scrapesAllItemsThatHaveCandidates() {
  auto stub = std::make_shared<StubProvider>();
  stub->byQuery[QStringLiteral("Alpha")] = makeMatch("1", "Alpha (USA)");
  stub->byQuery[QStringLiteral("Beta")] = makeMatch("2", "Beta (USA)");
  stub->byQuery[QStringLiteral("Gamma")] = makeMatch("3", "Gamma (USA)");

  const QStringList paths{QStringLiteral("/games/Alpha.bin"), QStringLiteral("/games/Beta.bin"),
                          QStringLiteral("/games/Gamma.bin")};
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
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

  const QStringList paths{QStringLiteral("/games/Alpha.bin"), QStringLiteral("/games/Beta.bin"),
                          QStringLiteral("/games/Gamma.bin")};
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 2);
  QCOMPARE(summary.skipped, 1);
  QCOMPARE(summary.errors, 0);
}

void TestBatchScrapeRunner::skipCurrentItemSkipsOnlyTheDisplayedItem() {
  // Both items have candidates, so absent a skip both would scrape. Skipping
  // the first in-flight item (from the progress slot, which fires inside
  // startItem before that item's lookup callback lands) flips only that item's
  // per-item token: it's counted as skipped while the batch carries on and
  // scrapes the second. m_cancelled stays false, so the run is not cancelled.
  auto stub = std::make_shared<StubProvider>();
  stub->byQuery[QStringLiteral("Alpha")] = makeMatch("1", "Alpha");
  stub->byQuery[QStringLiteral("Beta")] = makeMatch("2", "Beta");

  const QStringList paths{QStringLiteral("/games/Alpha.bin"), QStringLiteral("/games/Beta.bin")};
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
  bool skippedFirst = false;
  QObject::connect(&runner, &Scraper::BatchScrapeRunner::progress, &runner,
                   [&runner, &skippedFirst](int, int, const QString &) {
                     if (!skippedFirst) {
                       skippedFirst = true;
                       runner.skipCurrentItem();
                     }
                   });
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.skipped, 1);
  QCOMPARE(summary.scraped, 1);
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

  const QStringList paths{QStringLiteral("/games/Alpha.bin"), QStringLiteral("/games/Beta.bin")};
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
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

  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"),
                                    QStringList{QStringLiteral("/games/Alpha.bin")}, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.errors, 1);
  QCOMPARE(summary.scraped, 0);
}

void TestBatchScrapeRunner::firstFailuresRecordsEveryFailure() {
  // Every per-item failure message is retained so the scrape-error
  // details view can list them all — a misconfigured provider that
  // fails 10 items must surface all 10, not just the first few. The
  // runner caps the list at 1000 only to guard against a pathological
  // all-failing run of a huge collection; that bound is far above any
  // count worth materialising in a unit test.
  auto stub = std::make_shared<StubProvider>();
  StubProvider::Canned bad;
  bad.lookupError = QStringLiteral("nope");
  QStringList paths;
  for (int i = 0; i < 10; ++i) {
    const QString name = QStringLiteral("Item%1").arg(i);
    stub->byQuery[name] = bad;
    paths.append(QStringLiteral("/games/") + name + QStringLiteral(".bin"));
  }
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.errors, 10);
  QCOMPARE(summary.firstFailures.size(), 10);
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
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
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
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), QStringList{},
                                    QString());
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
      QStringList{QStringLiteral("/games/Alpha.bin"), QStringLiteral("/games/Beta.bin")},
      QString());
  QSignalSpy spy(&runner, &Scraper::BatchScrapeRunner::progress);
  runner.start();
  waitForFinish(&runner);
  QCOMPARE(spy.count(), 2);
  QCOMPARE(spy.at(0).at(0).toInt(), 0); // done = 0 before first item
  QCOMPARE(spy.at(0).at(1).toInt(), 2); // total = 2
  QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("Alpha.bin"));
  QCOMPARE(spy.at(1).at(0).toInt(), 1); // done = 1 before second
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

  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"),
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

  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"),
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

  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"),
                                    QStringList{QStringLiteral("/games/Alpha.bin")}, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 1);
  QCOMPARE(summary.errors, 0);
  QCOMPARE(stub->mediaRequestLog.size(), 1);
}

void TestBatchScrapeRunner::sidecarWriteFailureCountedInSummary() {
  // A failed sidecar (.json) write is auxiliary — the DB metadata still
  // saves, so it is not an item error — but it must be counted in the
  // summary, not silently dropped (Kartend audit hhr5x). Force the failure
  // by occupying the sidecar's "metadata" subdir path with a regular file,
  // so writeMetadataSidecar's mkpath fails.
  QTemporaryDir artworkRoot;
  QVERIFY(artworkRoot.isValid());
  const QString artworkDir = artworkRoot.path();
  QFile blocker(QDir(artworkDir).filePath(QStringLiteral("metadata")));
  QVERIFY(blocker.open(QIODevice::WriteOnly)); // occupy {artworkDir}/metadata as a file
  blocker.close();

  auto stub = std::make_shared<StubProvider>();
  stub->byQuery[QStringLiteral("Alpha")] = makeMatch("1", "Alpha"); // title set, no media

  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"),
                                    QStringList{QStringLiteral("/games/Alpha.bin")}, artworkDir);
  runner.start();
  const auto summary = waitForFinish(&runner);

  QCOMPARE(summary.scraped, 1);         // DB-metadata path still succeeds
  QCOMPARE(summary.errors, 0);          // a failed sidecar is not an item error
  QCOMPARE(summary.sidecarFailures, 1); // ...but it IS surfaced
}

void TestBatchScrapeRunner::decideScrapeSkipBranches() {
  // The skip decision, factored out of shouldSkipScrapedItem so its branches
  // are testable without DB/filesystem context (Kartend audit 2w4wz). Field
  // order: {mode, writeMetadata, metaPresent, metaWithinWindow, allMediaCovered}.
  using Inputs = Scraper::BatchScrapeRunner::SkipDecisionInputs;
  using Mode = Scraper::RescrapeMode;
  const auto skip = [](Inputs in) { return Scraper::BatchScrapeRunner::decideScrapeSkip(in); };

  // Skip mode: any present marker within the window is enough; stale or absent
  // markers release the item back for a re-scrape.
  QVERIFY(skip({Mode::Skip, true, /*present=*/true, /*within=*/true, /*media=*/true}));
  QVERIFY(!skip({Mode::Skip, true, true, /*within=*/false, true}));  // stale → released
  QVERIFY(!skip({Mode::Skip, true, /*present=*/false, true, true})); // never scraped

  // FillMissing (metadata + media wanted): skip only when everything is covered.
  QVERIFY(skip({Mode::FillMissing, true, true, true, true}));
  QVERIFY(!skip({Mode::FillMissing, true, /*present=*/false, true, true})); // metadata missing
  QVERIFY(!skip({Mode::FillMissing, true, true, true, /*media=*/false}));   // a media type missing
  QVERIFY(!skip({Mode::FillMissing, true, true, /*within=*/false, true}));  // metadata stale

  // FillMissing media-only (writeMetadata=false): media coverage decides, but a
  // stale freshness anchor (sidecar/DB timestamp) still releases the item.
  QVERIFY(skip({Mode::FillMissing, /*writeMeta=*/false, true, true, true}));
  QVERIFY(!skip({Mode::FillMissing, false, true, true, /*media=*/false})); // media missing
  QVERIFY(
      !skip({Mode::FillMissing, false, /*present=*/true, /*within=*/false, true})); // stale anchor
}

void TestBatchScrapeRunner::coverFetchSkippedWhenNoFrontAsset() {
  // A ScrapedItem without any "front" MediaAsset (provider didn't
  // expose one for this item) shouldn't fail or stall the runner —
  // it falls through directly to applyAndAdvance with empty writes.
  // Verify both via the request log (no fetch was attempted) and
  // the summary count.
  auto stub = std::make_shared<StubProvider>();
  stub->byQuery[QStringLiteral("Alpha")] = makeMatch("1", "Alpha");

  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"),
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
  // Kartend-m02z: BatchScrapeRunner now takes the full ApplicationContext.
  ApplicationContext ctx;
  ctx.managers.databaseManager = &db;
  Scraper::BatchScrapeRunner runner(
      &ctx, stub, QStringLiteral("uuid"),
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

void TestBatchScrapeRunner::skipModeAlsoSkipsItemsWithSidecarButNoDbRow() {
  // Regression: items where the metadata sidecar JSON is on disk but
  // the DB row was lost (kart import, partial scrape mid-write,
  // upgrade from a version pre-dating item_metadata) used to slip past
  // the Skip pre-filter and burn one provider lookup each before the
  // per-asset gate skipped the file writes. The fix is to also check
  // the on-disk sidecar at `{artworkDir}/metadata/{baseName}.json`.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  // Plant a sidecar for Alpha; leave Beta with no on-disk trace.
  QVERIFY(!writeStubSidecar(tmp.path(), QStringLiteral("Alpha")).isEmpty());

  // Stub provider would happily answer either query — we assert below
  // that only Beta's lookup actually fires.
  auto stub = std::make_shared<StubProvider>();
  stub->byQuery[QStringLiteral("Alpha")] = makeMatch("1", "Alpha");
  stub->byQuery[QStringLiteral("Beta")] = makeMatch("2", "Beta");

  // No DB → the DB-row branch is a no-op; only the on-disk sidecar
  // gate decides. Alpha must be pre-skipped; Beta must scrape.
  Scraper::BatchScrapeRunner runner(
      nullptr, stub, QStringLiteral("uuid"),
      QStringList{QStringLiteral("/games/Alpha.bin"), QStringLiteral("/games/Beta.bin")},
      tmp.path(), /*fetchPrimaryCover=*/false, Scraper::RescrapeMode::Skip,
      /*itemConcurrency=*/1, /*skipRecentDays=*/0);
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 1);
  QCOMPARE(summary.skipped, 1);
  QCOMPARE(summary.errors, 0);
}

void TestBatchScrapeRunner::skipModeWindowKeepsRecentScrapesSkipped() {
  // With skipRecentDays > 0, items whose updated_at falls INSIDE the
  // window must stay skipped (the user just scraped them, no point
  // burning quota again). Stage a DB that reports a 5-day-old scrape
  // and a 30-day Skip window — all three items must be filtered out.
  TimestampedScrapedDb db(/*daysAgo=*/5);
  auto stub = std::make_shared<StubProvider>();
  ApplicationContext ctx;
  ctx.managers.databaseManager = &db;
  Scraper::BatchScrapeRunner runner(
      &ctx, stub, QStringLiteral("uuid"),
      QStringList{QStringLiteral("/games/A.bin"), QStringLiteral("/games/B.bin"),
                  QStringLiteral("/games/C.bin")},
      QString(), /*fetchPrimaryCover=*/false, Scraper::RescrapeMode::Skip,
      /*itemConcurrency=*/1, /*skipRecentDays=*/30);
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.skipped, 3);
  QCOMPARE(summary.scraped, 0);
  QCOMPARE(summary.errors, 0);
}

void TestBatchScrapeRunner::skipModeWindowReleasesStaleScrapesForRefresh() {
  // Once an item ages past skipRecentDays, the Skip filter must let it
  // back through — that is the entire point of a refresh window. We
  // back the freshness check with the on-disk sidecar branch (null DB,
  // so no write-worker / SQLite schema worry) and rewind both sidecar
  // mtimes by 90 days. With a 30-day window the runner must release
  // both items to the provider; null DB takes the synchronous-success
  // apply path so the scraped count ticks up.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  // ~90 days in seconds; far enough beyond any 30-day window that
  // small filesystem mtime quantisation can't drag the value back in.
  constexpr qint64 kNinetyDaysSec = qint64{90} * 24 * 60 * 60;
  QVERIFY(!writeStubSidecar(tmp.path(), QStringLiteral("Alpha"), -kNinetyDaysSec).isEmpty());
  QVERIFY(!writeStubSidecar(tmp.path(), QStringLiteral("Beta"), -kNinetyDaysSec).isEmpty());

  auto stub = std::make_shared<StubProvider>();
  stub->byQuery[QStringLiteral("Alpha")] = makeMatch("1", "Alpha");
  stub->byQuery[QStringLiteral("Beta")] = makeMatch("2", "Beta");

  Scraper::BatchScrapeRunner runner(
      nullptr, stub, QStringLiteral("uuid"),
      QStringList{QStringLiteral("/games/Alpha.bin"), QStringLiteral("/games/Beta.bin")},
      tmp.path(), /*fetchPrimaryCover=*/false, Scraper::RescrapeMode::Skip,
      /*itemConcurrency=*/1, /*skipRecentDays=*/30);
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 2);
  QCOMPARE(summary.skipped, 0);
  QCOMPARE(summary.errors, 0);
}

void TestBatchScrapeRunner::skipModeWindowZeroPreservesLegacyBehaviour() {
  // skipRecentDays == 0 means "no time gate" — every item with a
  // non-empty source is dropped regardless of age. This is the
  // pre-fix behaviour and must stay reachable for users that never
  // want refreshes. A 90-day-old scrape under a zero window stays
  // skipped despite being well outside any reasonable refresh
  // window the user might set later.
  TimestampedScrapedDb db(/*daysAgo=*/90);
  auto stub = std::make_shared<StubProvider>();
  ApplicationContext ctx;
  ctx.managers.databaseManager = &db;
  Scraper::BatchScrapeRunner runner(
      &ctx, stub, QStringLiteral("uuid"),
      QStringList{QStringLiteral("/games/A.bin"), QStringLiteral("/games/B.bin")}, QString(),
      /*fetchPrimaryCover=*/false, Scraper::RescrapeMode::Skip,
      /*itemConcurrency=*/1, /*skipRecentDays=*/0);
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.skipped, 2);
  QCOMPARE(summary.scraped, 0);
  QCOMPARE(summary.errors, 0);
}

void TestBatchScrapeRunner::fillMissingPreSkipsItemsWithEveryTickedFieldCovered() {
  // The core "skip existing" fix: under FillMissing, an item whose
  // ticked checkboxes (metadata + selected media types) are all
  // already on disk must NOT generate a provider request. Set up an
  // artwork directory where Alpha has metadata sidecar + front cover
  // + screenshot files (the three ticked fields), and Beta has only
  // the sidecar. Alpha must pre-skip; Beta must still scrape because
  // it is missing the two media types.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  // Alpha: sidecar + front (flat) + screenshot (subdir)
  QVERIFY(!writeStubSidecar(tmp.path(), QStringLiteral("Alpha")).isEmpty());
  {
    QFile front(QDir(tmp.path()).filePath(QStringLiteral("Alpha.png")));
    QVERIFY(front.open(QIODevice::WriteOnly));
    front.write("png");
  }
  QVERIFY(QDir().mkpath(QDir(tmp.path()).filePath(QStringLiteral("screenshot"))));
  {
    QFile shot(QDir(tmp.path()).filePath(QStringLiteral("screenshot/Alpha.png")));
    QVERIFY(shot.open(QIODevice::WriteOnly));
    shot.write("png");
  }
  // Beta: sidecar only — missing front + screenshot
  QVERIFY(!writeStubSidecar(tmp.path(), QStringLiteral("Beta")).isEmpty());

  auto stub = std::make_shared<StubProvider>();
  stub->byQuery[QStringLiteral("Beta")] = makeMatch("2", "Beta");
  // Alpha is intentionally absent from the stub — if the runner
  // contacts the provider for it, the test would surface a "no
  // candidates" result and the skipped count would be > 1.

  Scraper::BatchScrapeRunner runner(
      nullptr, stub, QStringLiteral("uuid"),
      QStringList{QStringLiteral("/games/Alpha.bin"), QStringLiteral("/games/Beta.bin")},
      tmp.path(), /*fetchPrimaryCover=*/true, Scraper::RescrapeMode::FillMissing,
      /*itemConcurrency=*/1, /*skipRecentDays=*/0);
  runner.setMediaTypeFilter({QStringLiteral("front"), QStringLiteral("screenshot")});
  runner.setWriteMetadata(true);
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.skipped, 1); // Alpha pre-skipped
  QCOMPARE(summary.scraped, 1); // Beta scraped through
  QCOMPARE(summary.errors, 0);
}

void TestBatchScrapeRunner::fillMissingScrapesItemsMissingAnyTickedField() {
  // A single missing field is enough to force the provider request —
  // FillMissing is "fill the gap", and the only way the runner knows
  // what the gap is, is to ask. Plant the sidecar + front cover but
  // NOT the ticked screenshot. The item must flow through to scrape.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(!writeStubSidecar(tmp.path(), QStringLiteral("Alpha")).isEmpty());
  {
    QFile front(QDir(tmp.path()).filePath(QStringLiteral("Alpha.png")));
    QVERIFY(front.open(QIODevice::WriteOnly));
    front.write("png");
  }
  // No screenshot subdir → screenshot is uncovered for Alpha.

  auto stub = std::make_shared<StubProvider>();
  stub->byQuery[QStringLiteral("Alpha")] = makeMatch("1", "Alpha");

  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"),
                                    QStringList{QStringLiteral("/games/Alpha.bin")}, tmp.path(),
                                    /*fetchPrimaryCover=*/true, Scraper::RescrapeMode::FillMissing,
                                    /*itemConcurrency=*/1, /*skipRecentDays=*/0);
  runner.setMediaTypeFilter({QStringLiteral("front"), QStringLiteral("screenshot")});
  runner.setWriteMetadata(true);
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.skipped, 0);
  QCOMPARE(summary.scraped, 1);
  QCOMPARE(summary.errors, 0);
}

void TestBatchScrapeRunner::fillMissingHonoursRefreshWindowSameAsSkip() {
  // The refresh window applies to FillMissing too. Plant a fully-
  // covered Alpha (sidecar + front) but stamp the sidecar mtime 90
  // days in the past. Under a 30-day window the item must be
  // released for refresh, exactly like the Skip-mode counterpart.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  constexpr qint64 kNinetyDaysSec = qint64{90} * 24 * 60 * 60;
  QVERIFY(!writeStubSidecar(tmp.path(), QStringLiteral("Alpha"), -kNinetyDaysSec).isEmpty());
  {
    QFile front(QDir(tmp.path()).filePath(QStringLiteral("Alpha.png")));
    QVERIFY(front.open(QIODevice::WriteOnly));
    front.write("png");
  }

  auto stub = std::make_shared<StubProvider>();
  stub->byQuery[QStringLiteral("Alpha")] = makeMatch("1", "Alpha");

  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"),
                                    QStringList{QStringLiteral("/games/Alpha.bin")}, tmp.path(),
                                    /*fetchPrimaryCover=*/true, Scraper::RescrapeMode::FillMissing,
                                    /*itemConcurrency=*/1, /*skipRecentDays=*/30);
  runner.setMediaTypeFilter({QStringLiteral("front")});
  runner.setWriteMetadata(true);
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 1);
  QCOMPARE(summary.skipped, 0);
  QCOMPARE(summary.errors, 0);
}

void TestBatchScrapeRunner::quotaExhaustedStopsBatchAndSkipsRemainingItems() {
  // ScreenScraper returns HTTP 430 when the account's daily request
  // quota is spent (431 for the failed-lookup quota). The runner must
  // treat that NOT as an ordinary per-item error-and-continue, but as
  // a hard stop: every item after the one that hit 430 must be left
  // unprocessed (otherwise the rest of a 10k-item batch just burns
  // against an exhausted quota). The first item's lookup returns a
  // 430; the remaining nine must never run.
  auto stub = std::make_shared<StubProvider>();
  StubProvider::Canned quotaHit;
  quotaHit.lookupError = QStringLiteral("daily request quota exhausted");
  quotaHit.lookupErrorHttpStatus = 430;
  // Item0 hits the quota wall; Item1..9 would scrape fine IF reached.
  stub->byQuery[QStringLiteral("Item0")] = quotaHit;
  for (int i = 1; i < 10; ++i) {
    const QString name = QStringLiteral("Item%1").arg(i);
    stub->byQuery[name] = makeMatch(QString::number(i), name);
  }
  QStringList paths;
  for (int i = 0; i < 10; ++i) {
    paths.append(QStringLiteral("/games/Item%1.bin").arg(i));
  }
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  // The quota wall is recorded in the summary so the service can
  // leave a resume point instead of advancing the queue.
  QVERIFY(summary.quotaExhausted);
  // Only the quota-failing item was accounted for; the run stopped
  // dispatching before the other nine — so the total processed count
  // is far below the 10 queued. (With itemConcurrency 1 it is exactly
  // 1, but the assertion stays loose to tolerate scheduler slop.)
  const int processed = summary.scraped + summary.skipped + summary.errors;
  QVERIFY(processed < 10);
  QCOMPARE(summary.errors, processed); // every processed item was the error
  QCOMPARE(summary.scraped, 0);        // nothing after the wall scraped
}

void TestBatchScrapeRunner::quotaExhaustedAbortsInFlightItemsAtConcurrency() {
  // Kartend-fv3yr: with itemConcurrency > 1, several items are dispatched and
  // in flight (the stub's lookups are async) before the first 430 lands. Those
  // in-flight siblings must NOT fire their follow-up detail/media requests into
  // the now-exhausted quota — they must skip. Pre-fix, ~7 of them scraped
  // (m_quotaStopped was only checked at NEW dispatch, never in the per-item
  // callbacks); post-fix the lookup callback skips them.
  auto stub = std::make_shared<StubProvider>();
  StubProvider::Canned quotaHit;
  quotaHit.lookupError = QStringLiteral("daily request quota exhausted");
  quotaHit.lookupErrorHttpStatus = 430;
  stub->byQuery[QStringLiteral("Item0")] = quotaHit;
  for (int i = 1; i < 8; ++i) {
    const QString name = QStringLiteral("Item%1").arg(i);
    stub->byQuery[name] = makeMatch(QString::number(i), name);
  }
  QStringList paths;
  for (int i = 0; i < 8; ++i) {
    paths.append(QStringLiteral("/games/Item%1.bin").arg(i));
  }
  // Concurrency == item count, so all eight lookups are in flight before any
  // completes — exactly the race the fix addresses.
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString(),
                                    /*fetchPrimaryCover=*/false, Scraper::RescrapeMode::Skip,
                                    /*itemConcurrency=*/8);
  runner.start();
  const auto summary = waitForFinish(&runner);

  QVERIFY(summary.quotaExhausted);
  // The crux: not one in-flight sibling scraped despite all eight being
  // dispatched before the 430 landed (pre-fix this was ~7).
  QCOMPARE(summary.scraped, 0);
}

QTEST_MAIN(TestBatchScrapeRunner)
#include "test_batchscraperunner.moc"
