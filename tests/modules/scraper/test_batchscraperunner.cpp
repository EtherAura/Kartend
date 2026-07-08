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
#include "scrapeskipdecision.h"

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
    /// Optional server-detail string stamped onto the lookup error's
    /// ErrorContext (Qt errorString / response body) — lets a test assert the
    /// failure summary surfaces it rather than a bare message (Kartend-e6oyu).
    QString lookupErrorDetails;
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
  /// HTTP status stamped onto every mediaErrorUrls failure. 0 = leave it
  /// unset. 429 exercises the runner's consecutive-429 escalation on the
  /// media path (Kartend-jjyst.3).
  int mediaErrorHttpStatus = 0;
  /// Track which URLs the runner asked us to fetch — tests assert
  /// against the call shape to verify the fetchPrimaryCover toggle.
  mutable QList<QUrl> mediaRequestLog;

  /// Queries whose lookup() should silently drop the callback instead of
  /// answering — simulates a wedged ROM hash-read that never returns, so the
  /// per-item stall watchdog (Kartend audit xnm8a) is the only thing that can
  /// free the slot.
  QSet<QString> hangLookupQueries;

  QString id() const override { return QStringLiteral("stub"); }
  QString displayName() const override { return QStringLiteral("Stub"); }
  QStringList categories() const override { return {QStringLiteral("games")}; }
  Capabilities capabilities() const override { return Capability::MetadataLookup; }
  QUrl searchUrl(const QString &) const override { return {}; }

  void lookup(const QString &query, LookupCallback cb) override {
    // Async-emulate by posting back on the event loop. Without this
    // the test's QSignalSpy on `progress` only sees the final state
    // because everything synchronously chains.
    if (hangLookupQueries.contains(query)) {
      // Drop the callback on the floor: the lookup leg never completes, the
      // way a blocked read()/hash on a wedged mount would.
      return;
    }
    QTimer::singleShot(0, [this, query, cb = std::move(cb)]() {
      const Canned &c = byQuery.value(query);
      if (!c.lookupError.isEmpty()) {
        auto err = ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                                   c.lookupError, "stub");
        if (c.lookupErrorHttpStatus != 0) {
          err.withHttpStatus(c.lookupErrorHttpStatus);
        }
        if (!c.lookupErrorDetails.isEmpty()) {
          err.withDetails(c.lookupErrorDetails);
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
        auto err = ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                                   QStringLiteral("media fetch failed"), "stub");
        if (mediaErrorHttpStatus != 0) {
          err.withHttpStatus(mediaErrorHttpStatus);
        }
        cb(err);
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
Scraper::BatchScrapeRunner::Summary waitForFinish(Scraper::BatchScrapeRunner *runner,
                                                  int timeoutMs = 5000) {
  Scraper::BatchScrapeRunner::Summary captured;
  bool finished = false;
  QObject::connect(runner, &Scraper::BatchScrapeRunner::finished,
                   [&captured, &finished](const auto &s) {
                     captured = s;
                     finished = true;
                   });
  QElapsedTimer timer;
  timer.start();
  while (!finished && timer.elapsed() < timeoutMs) {
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
  void stalledStepTimesOutAndAdvancesBatch();
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
  void coreScrapedFieldsCompletePredicate();
  void fillMissingSkipsOnlyItemsWithAllCoreFields();
  void mediaCoverageMatchesMixedCaseFolders();
  void mediaCoverageProbesKindDirectoryForVideoVariants();
  void fillMissingKnownAbsentMediaCountsAsCovered();
  void computeMediaAbsentTypesDerivesUnsuppliedWantedTypes();
  void fillMissingIgnoresCollectionWideUnobtainableMedia();
  void computePrevalentMediaTypesAppliesThreshold();
  void quotaExhaustedStopsBatchAndSkipsRemainingItems();
  void quotaExhaustedAbortsInFlightItemsAtConcurrency();
  void defaultQuotaClassificationExcludes429();
  void single429CountsAsErrorWithoutStoppingBatch();
  void repeated429sEscalateToQueueStop();
  void media429sEscalateToQueueStop();
  void mediaFetchFailuresCountedInSummary();
  void mediaWriteFailuresCountedInSummary();
  void notFoundReportedSeparatelyFromErrors();
  void failureMessageIncludesStatusAndDetail();
  void failedPathsCapturedForErrorsOnly();
  void failedPathsRespectMaxReportedCap();
  void skipCurrentItemSkipsOnlyTheDisplayedItem();
  void remainingPathsEmptyAfterMixedOutcomes();
  void remainingPathsKeepsQuotaStoppedWork();
  void consecutiveFatalErrorsTripBreaker();
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
  // candidates for it. Post Kartend-e8aag the runner counts that as
  // "not found" (the provider ran and had no match) rather than "skipped"
  // or an error.
  auto stub = std::make_shared<StubProvider>();
  stub->byQuery[QStringLiteral("Alpha")] = makeMatch("1", "Alpha");
  stub->byQuery[QStringLiteral("Gamma")] = makeMatch("3", "Gamma");

  const QStringList paths{QStringLiteral("/games/Alpha.bin"), QStringLiteral("/games/Beta.bin"),
                          QStringLiteral("/games/Gamma.bin")};
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 2);
  QCOMPARE(summary.notFound, 1);
  QCOMPARE(summary.skipped, 0);
  QCOMPARE(summary.errors, 0);
}

void TestBatchScrapeRunner::notFoundReportedSeparatelyFromErrors() {
  // Kartend-e8aag: a provider "not found" — an HTTP 404, or an empty
  // candidate list (TMDB/MusicBrainz/OpenLibrary signal a miss that way) — is
  // a routine outcome counted in `notFound`, NOT `errors`. A genuine failure
  // (here a 403) still lands in `errors` and the failure list.
  auto stub = std::make_shared<StubProvider>();
  // Alpha: an HTTP 404 not-found.
  StubProvider::Canned notFound404;
  notFound404.lookupError = QStringLiteral("ScreenScraper has no entry for this game (HTTP 404).");
  notFound404.lookupErrorHttpStatus = 404;
  stub->byQuery[QStringLiteral("Alpha")] = notFound404;
  // Beta: an empty candidate list (no match) — also not-found.
  stub->byQuery[QStringLiteral("Beta")] = StubProvider::Canned{};
  // Gamma: a real error (HTTP 403 auth) — stays in errors.
  StubProvider::Canned authFail;
  authFail.lookupError = QStringLiteral("forbidden");
  authFail.lookupErrorHttpStatus = 403;
  stub->byQuery[QStringLiteral("Gamma")] = authFail;
  // Delta: a clean scrape.
  stub->byQuery[QStringLiteral("Delta")] = makeMatch("4", "Delta");

  const QStringList paths{QStringLiteral("/games/Alpha.bin"), QStringLiteral("/games/Beta.bin"),
                          QStringLiteral("/games/Gamma.bin"), QStringLiteral("/games/Delta.bin")};
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.notFound, 2); // the 404 and the empty-candidate miss
  QCOMPARE(summary.errors, 1);   // only the 403
  QCOMPARE(summary.scraped, 1);  // Delta
  QCOMPARE(summary.skipped, 0);
  // Not-found items are kept OUT of the failure list — only the real error.
  QCOMPARE(summary.firstFailures.size(), 1);
  QVERIFY(summary.firstFailures.first().contains(QStringLiteral("Gamma")));
}

void TestBatchScrapeRunner::defaultQuotaClassificationExcludes429() {
  // Kartend-jjyst.3 (revisits the Kartend-oa1ry design): the default
  // isQuotaExhausted no longer counts RFC 6585's 429 — that's burst
  // throttling handled by retry + the runner's consecutive-429 escalation.
  // ScreenScraper's non-standard 430/431 daily quotas stay immediate stops.
  StubProvider p;
  const auto err = [](int status) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                           QStringLiteral("x"), QStringLiteral("t"))
        .withHttpStatus(status);
  };
  QVERIFY(!p.isQuotaExhausted(err(429)));
  QVERIFY(p.isQuotaExhausted(err(430)));
  QVERIFY(p.isQuotaExhausted(err(431)));
}

void TestBatchScrapeRunner::single429CountsAsErrorWithoutStoppingBatch() {
  // Kartend-jjyst.3: a lone HTTP 429 is a seconds-long burst limit, not
  // quota exhaustion — it must NOT strand the rest of an unattended run.
  // The 429'd items land in `errors` (re-queueable via failedPaths); every
  // success in between resets the consecutive-429 streak, so alternating
  // 429/success never trips the escalation.
  auto stub = std::make_shared<StubProvider>();
  StubProvider::Canned rateLimited;
  rateLimited.lookupError = QStringLiteral("rate limited");
  rateLimited.lookupErrorHttpStatus = 429;
  stub->byQuery[QStringLiteral("Item0")] = rateLimited;
  stub->byQuery[QStringLiteral("Item1")] = makeMatch("1", "Item1");
  stub->byQuery[QStringLiteral("Item2")] = rateLimited;
  stub->byQuery[QStringLiteral("Item3")] = makeMatch("3", "Item3");
  QStringList paths;
  for (int i = 0; i < 4; ++i) {
    paths.append(QStringLiteral("/games/Item%1.bin").arg(i));
  }
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QVERIFY(!summary.quotaExhausted); // the run kept going
  QCOMPARE(summary.scraped, 2);
  QCOMPARE(summary.errors, 2);
  QCOMPARE(summary.processedItems(), 4); // every item got its shot
}

void TestBatchScrapeRunner::repeated429sEscalateToQueueStop() {
  // Kartend-jjyst.3: a limiter that answers 429 to every consecutive request
  // isn't a burst we can ride out — after the escalation threshold (3) the
  // runner stops new dispatch through the quota-stop machinery, so the
  // un-dispatched work persists as the resume point.
  auto stub = std::make_shared<StubProvider>();
  StubProvider::Canned rateLimited;
  rateLimited.lookupError = QStringLiteral("rate limited");
  rateLimited.lookupErrorHttpStatus = 429;
  QStringList paths;
  for (int i = 0; i < 10; ++i) {
    stub->byQuery[QStringLiteral("Item%1").arg(i)] = rateLimited;
    paths.append(QStringLiteral("/games/Item%1.bin").arg(i));
  }
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QVERIFY(summary.quotaExhausted);
  QCOMPARE(summary.errors, 3);  // exactly the escalation threshold
  QCOMPARE(summary.scraped, 0); // nothing scraped against the throttle wall
  QVERIFY(summary.processedItems() < 10);
  // The escalation left its own explanation in the failure list.
  QVERIFY(summary.firstFailures.join(QLatin1Char('\n'))
              .contains(QStringLiteral("consecutive HTTP 429")));
  // Item2 (the streak-tripping error) and the 7 never-dispatched items stay
  // in the resume list; the two pre-stop errors are terminal.
  QCOMPARE(runner.remainingPaths().size(), 8);
  QVERIFY(runner.remainingPaths().contains(QStringLiteral("/games/Item2.bin")));
}

void TestBatchScrapeRunner::media429sEscalateToQueueStop() {
  // Kartend-jjyst.3, media path: image CDNs are the realistic 429 source.
  // Three parallel asset fetches all answering 429 trip the same escalation
  // — the item itself still commits (metadata is not hostage to throttled
  // art), but no new items dispatch.
  auto stub = std::make_shared<StubProvider>();
  StubProvider::Canned canned = makeMatch("1", "Alpha");
  for (const QString &type :
       {QStringLiteral("front"), QStringLiteral("screenshot"), QStringLiteral("fanart")}) {
    Scraper::MediaAsset asset;
    asset.type = type;
    asset.url = QUrl(QStringLiteral("https://example.invalid/%1/1.png").arg(type));
    canned.detail.media.append(asset);
    stub->mediaErrorUrls.insert(asset.url);
  }
  stub->mediaErrorHttpStatus = 429;
  stub->byQuery[QStringLiteral("Alpha")] = canned;
  stub->byQuery[QStringLiteral("Beta")] = makeMatch("2", "Beta");

  Scraper::BatchScrapeRunner runner(
      nullptr, stub, QStringLiteral("uuid"),
      QStringList{QStringLiteral("/games/Alpha.bin"), QStringLiteral("/games/Beta.bin")},
      QString());
  runner.setMediaTypeFilter(
      {QStringLiteral("front"), QStringLiteral("screenshot"), QStringLiteral("fanart")});
  runner.start();
  const auto summary = waitForFinish(&runner);
  QVERIFY(summary.quotaExhausted);
  QCOMPARE(summary.scraped, 1); // Alpha committed (metadata kept)
  QCOMPARE(summary.errors, 0);
  QCOMPARE(summary.mediaFetchFailures, 3);
  QCOMPARE(summary.processedItems(), 1); // Beta never dispatched
}

void TestBatchScrapeRunner::mediaFetchFailuresCountedInSummary() {
  // Kartend-jjyst.4: a failed asset fetch used to tick nothing — a run full
  // of dead media URLs reported "scraped N, 0 media" with zero diagnostics.
  // Both failure shapes count: an errored fetch and an ok-but-empty payload.
  auto stub = std::make_shared<StubProvider>();
  auto [alphaCanned, alphaUrl] = makeMatchWithCover("1", "Alpha");
  stub->byQuery[QStringLiteral("Alpha")] = alphaCanned;
  stub->mediaErrorUrls.insert(alphaUrl); // errored fetch
  auto [betaCanned, betaUrl] = makeMatchWithCover("2", "Beta");
  stub->byQuery[QStringLiteral("Beta")] = betaCanned;
  Q_UNUSED(betaUrl); // absent from mediaByUrl → empty payload

  Scraper::BatchScrapeRunner runner(
      nullptr, stub, QStringLiteral("uuid"),
      QStringList{QStringLiteral("/games/Alpha.bin"), QStringLiteral("/games/Beta.bin")},
      QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 2); // fetch failures stay non-fatal per item
  QCOMPARE(summary.errors, 0);
  QCOMPARE(summary.mediaFetchFailures, 2);
  // Each loss carries a bounded diagnosis in the failure list.
  const QString joined = summary.firstFailures.join(QLatin1Char('\n'));
  QVERIFY2(joined.contains(QStringLiteral("fetch failed")), qPrintable(joined));
  QVERIFY2(joined.contains(QStringLiteral("returned no data")), qPrintable(joined));
}

void TestBatchScrapeRunner::mediaWriteFailuresCountedInSummary() {
  // Kartend-jjyst.4: writeMediaFiles' genuine failures (here: mkpath blocked
  // by a regular file occupying the {artworkDir}/front subdir path — same
  // trick as the sidecar test) must tick mediaWriteFailures and fold the
  // per-asset reason into firstFailures instead of vanishing.
  QTemporaryDir artworkRoot;
  QVERIFY(artworkRoot.isValid());
  const QString artworkDir = artworkRoot.path();
  QFile blocker(QDir(artworkDir).filePath(QStringLiteral("front")));
  QVERIFY(blocker.open(QIODevice::WriteOnly)); // occupy {artworkDir}/front as a file
  blocker.close();

  auto stub = std::make_shared<StubProvider>();
  auto [canned, coverUrl] = makeMatchWithCover("1", "Alpha");
  stub->byQuery[QStringLiteral("Alpha")] = canned;
  stub->mediaByUrl[coverUrl] = QByteArrayLiteral("\x89PNG\r\nfakebytes");

  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"),
                                    QStringList{QStringLiteral("/games/Alpha.bin")}, artworkDir);
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 1); // metadata still lands; only the write is lost
  QCOMPARE(summary.errors, 0);
  QCOMPARE(summary.mediaWritten, 0);
  QCOMPARE(summary.mediaFetchFailures, 0); // the bytes arrived fine
  QCOMPARE(summary.mediaWriteFailures, 1);
  const QString joined = summary.firstFailures.join(QLatin1Char('\n'));
  QVERIFY2(joined.contains(QStringLiteral("could not create")), qPrintable(joined));
}

void TestBatchScrapeRunner::failureMessageIncludesStatusAndDetail() {
  // Kartend-e6oyu: a generic HTTP failure surfaces the status code and a
  // server-detail snippet in the recorded failure line, not a bare message.
  auto stub = std::make_shared<StubProvider>();
  StubProvider::Canned httpFail;
  httpFail.lookupError = QStringLiteral("HTTP request failed");
  httpFail.lookupErrorHttpStatus = 500;
  httpFail.lookupErrorDetails = QStringLiteral("Internal Server Error\nResponse: {\"error\":1}");
  stub->byQuery[QStringLiteral("Alpha")] = httpFail;

  const QStringList paths{QStringLiteral("/games/Alpha.bin")};
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.errors, 1);
  QCOMPARE(summary.firstFailures.size(), 1);
  const QString line = summary.firstFailures.first();
  QVERIFY2(line.contains(QStringLiteral("HTTP 500")), qPrintable(line));
  QVERIFY2(line.contains(QStringLiteral("Internal Server Error")), qPrintable(line));
  // The multi-line response body is collapsed to its first line so the raw
  // blob doesn't bloat the summary list.
  QVERIFY2(!line.contains(QStringLiteral("Response:")), qPrintable(line));
}

void TestBatchScrapeRunner::failedPathsCapturedForErrorsOnly() {
  // Kartend-jjjo5: only items in the `errors` bucket land in failedPaths, by
  // FULL path. Not-found (404 or empty candidates) and clean scrapes are
  // excluded — re-scraping a genuine not-found item would just fail again.
  auto stub = std::make_shared<StubProvider>();
  StubProvider::Canned err500; // real error → captured
  err500.lookupError = QStringLiteral("HTTP request failed");
  err500.lookupErrorHttpStatus = 500;
  stub->byQuery[QStringLiteral("Alpha")] = err500;
  StubProvider::Canned notFound404; // 404 → excluded
  notFound404.lookupError = QStringLiteral("missing");
  notFound404.lookupErrorHttpStatus = 404;
  stub->byQuery[QStringLiteral("Beta")] = notFound404;
  stub->byQuery[QStringLiteral("Gamma")] = StubProvider::Canned{};  // empty miss → excluded
  stub->byQuery[QStringLiteral("Delta")] = makeMatch("4", "Delta"); // scraped → excluded

  const QStringList paths{QStringLiteral("/games/Alpha.bin"), QStringLiteral("/games/Beta.bin"),
                          QStringLiteral("/games/Gamma.bin"), QStringLiteral("/games/Delta.bin")};
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.errors, 1);
  QCOMPARE(summary.notFound, 2);
  QCOMPARE(summary.scraped, 1);
  QCOMPARE(summary.failedPaths.size(), 1);
  QCOMPARE(summary.failedPaths.first(), QStringLiteral("/games/Alpha.bin"));
}

void TestBatchScrapeRunner::failedPathsRespectMaxReportedCap() {
  // Kartend-jjjo5, criterion 6: failedPaths is bounded by kMaxReportedFailures,
  // the same cap firstFailures uses (batchscraperunner.cpp:930). This is where
  // the cap is FIRST applied — the service rollup can never receive more than
  // the runner reports, so its identical guard is defensive. Drive kMax+2 errored
  // items: the (uncapped) errors count reflects every failure, but the
  // re-queueable list stops at the cap. lookupErrorHttpStatus stays 0 so each
  // error takes the streak-resetting branch and the fatal-error circuit breaker
  // (401/403/423/426 only) never stops the batch early.
  auto stub = std::make_shared<StubProvider>();
  StubProvider::Canned boom;
  boom.lookupError = QStringLiteral("boom"); // httpStatus 0 → error bucket, no breaker
  const int n = Scraper::kMaxReportedFailures + 2;
  QStringList paths;
  paths.reserve(n);
  for (int i = 0; i < n; ++i) {
    const QString base = QStringLiteral("Item%1").arg(i, 5, 10, QLatin1Char('0'));
    stub->byQuery[base] = boom;
    paths.append(QStringLiteral("/games/%1.bin").arg(base));
  }
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
  runner.start();
  const auto summary = waitForFinish(&runner, /*timeoutMs=*/60000);
  QCOMPARE(summary.errors, n);                                         // count is uncapped
  QCOMPARE(summary.failedPaths.size(), Scraper::kMaxReportedFailures); // list is bounded
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

void TestBatchScrapeRunner::stalledStepTimesOutAndAdvancesBatch() {
  // Regression for the live 99%-frozen scrape (Kartend audit xnm8a): a wedged
  // storage mount makes an unbounded local step — the provider's ROM hash-read
  // inside lookup(), here, or the QtConcurrent artwork write — block with no
  // timeout. Pre-fix the item's concurrency slot never freed, pump() couldn't
  // advance, and the batch hung below 100% indefinitely. The per-item stall
  // watchdog must error the stalled item(s) and still reach `finished`.
  //
  // The env guard scopes the tiny budget to this case so a QtTest early-return
  // on a failed QCOMPARE can't leak a 60ms watchdog into later tests.
  struct EnvGuard {
    ~EnvGuard() { qunsetenv("KARTEND_SCRAPE_STEP_TIMEOUT_MS"); }
  } guard;
  qputenv("KARTEND_SCRAPE_STEP_TIMEOUT_MS", "60");

  auto stub = std::make_shared<StubProvider>();
  // Both items "have" candidates, but their lookups never answer — the slot
  // can only be freed by the watchdog.
  stub->byQuery[QStringLiteral("Alpha")] = makeMatch("1", "Alpha");
  stub->byQuery[QStringLiteral("Beta")] = makeMatch("2", "Beta");
  stub->hangLookupQueries.insert(QStringLiteral("Alpha"));
  stub->hangLookupQueries.insert(QStringLiteral("Beta"));

  const QStringList paths{QStringLiteral("/games/Alpha.bin"), QStringLiteral("/games/Beta.bin")};
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
  runner.start();
  // Without the watchdog `finished` never fires and this returns a default
  // (zeroed) summary at the 5s timeout, failing the QCOMPAREs below.
  const auto summary = waitForFinish(&runner);

  QCOMPARE(summary.errors, 2);
  QCOMPARE(summary.scraped, 0);
  QCOMPARE(summary.skipped, 0);
  // The message names the stall rather than a generic provider error.
  QVERIFY(!summary.firstFailures.isEmpty());
  QVERIFY(summary.firstFailures.first().contains(QStringLiteral("timed out")));
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
  // order: {mode, writeMetadata, metaPresent, metaComplete, metaWithinWindow,
  // allMediaCovered}.
  using Inputs = Scraper::SkipDecisionInputs;
  using Mode = Scraper::RescrapeMode;
  const auto skip = [](Inputs in) { return Scraper::decideScrapeSkip(in); };

  // Skip mode: any present marker within the window is enough — field
  // completeness is irrelevant; stale or absent markers release the item.
  QVERIFY(skip({Mode::Skip, true, /*present=*/true, /*complete=*/false, /*within=*/true, true}));
  QVERIFY(!skip({Mode::Skip, true, true, false, /*within=*/false, true}));  // stale → released
  QVERIFY(!skip({Mode::Skip, true, /*present=*/false, false, true, true})); // never scraped

  // FillMissing (metadata + media wanted): skip only when metadata is COMPLETE,
  // every wanted media type is on disk, and it is within the window. A
  // present-but-incomplete row is NOT enough — it stays queued to fill its gaps
  // (Kartend-em3jc).
  QVERIFY(skip({Mode::FillMissing, true, /*present=*/true, /*complete=*/true, true, true}));
  QVERIFY(!skip({Mode::FillMissing, true, /*present=*/true, /*complete=*/false, true,
                 true})); // partial metadata
  QVERIFY(
      !skip({Mode::FillMissing, true, /*present=*/false, false, true, true})); // metadata missing
  QVERIFY(!skip(
      {Mode::FillMissing, true, true, /*complete=*/true, true, /*media=*/false})); // media missing
  QVERIFY(!skip({Mode::FillMissing, true, true, true, /*within=*/false, true}));   // metadata stale

  // FillMissing media-only (writeMetadata=false): completeness is irrelevant;
  // media coverage decides, but a stale freshness anchor still releases the item.
  QVERIFY(skip(
      {Mode::FillMissing, /*writeMeta=*/false, /*present=*/true, /*complete=*/false, true, true}));
  QVERIFY(!skip({Mode::FillMissing, false, true, false, true, /*media=*/false})); // media missing
  QVERIFY(!skip(
      {Mode::FillMissing, false, /*present=*/true, false, /*within=*/false, true})); // stale anchor
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
  // A second item's front + screenshot make those types prevalent (>= 85% of the
  // 2-item batch) so they count as "reliably supplied" and their absence blocks the
  // skip (Kartend-ib46d). Without this a type present for only 1 of 2 items (50%)
  // would be treated as optional and Beta would wrongly pre-skip.
  {
    QFile front(QDir(tmp.path()).filePath(QStringLiteral("Common.png")));
    QVERIFY(front.open(QIODevice::WriteOnly));
    front.write("png");
    QFile shot(QDir(tmp.path()).filePath(QStringLiteral("screenshot/Common.png")));
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
  // NOT the ticked screenshot for Alpha. The item must flow through to scrape.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(!writeStubSidecar(tmp.path(), QStringLiteral("Alpha")).isEmpty());
  {
    QFile front(QDir(tmp.path()).filePath(QStringLiteral("Alpha.png")));
    QVERIFY(front.open(QIODevice::WriteOnly));
    front.write("png");
  }
  // screenshot exists for a DIFFERENT item, so it is obtainable collection-wide
  // (not auto-ignored, Kartend-ib46d) but is missing for Alpha → Alpha uncovered.
  {
    const QString ssDir = QDir(tmp.path()).filePath(QStringLiteral("screenshot"));
    QVERIFY(QDir().mkpath(ssDir));
    QFile ss(QDir(ssDir).filePath(QStringLiteral("OtherGame.png")));
    QVERIFY(ss.open(QIODevice::WriteOnly));
    ss.write("png");
  }

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

void TestBatchScrapeRunner::coreScrapedFieldsCompletePredicate() {
  // Kartend-em3jc: exactly the six core scraped fields gate FillMissing's
  // "metadata already complete" decision; clearing any one breaks completeness.
  // The two rarely-filled scraped fields (content rating, players) and
  // user-authored / optional fields don't affect it.
  using MD = ItemMetadataStore::ItemMetadata;
  MD md;
  md.title = md.description = md.genre = md.developer = md.publisher = md.releaseDate =
      md.contentRating = md.players = QStringLiteral("x");
  QVERIFY(Scraper::coreScrapedFieldsComplete(md));

  for (QString MD::*field : {&MD::title, &MD::description, &MD::genre, &MD::developer,
                             &MD::publisher, &MD::releaseDate}) {
    MD partial = md;
    partial.*field = QString();
    QVERIFY2(!Scraper::coreScrapedFieldsComplete(partial),
             "clearing any single required field must break completeness");
  }

  // Excluded fields don't affect completeness: the rarely-filled scraped fields
  // (content rating, players) and user-authored / optional fields.
  MD extras = md;
  extras.contentRating.clear();
  extras.players.clear();
  extras.notes.clear();
  extras.tags.clear();
  extras.rating = -1;
  extras.source.clear();
  QVERIFY(Scraper::coreScrapedFieldsComplete(extras));
}

void TestBatchScrapeRunner::fillMissingSkipsOnlyItemsWithAllCoreFields() {
  // Kartend-em3jc: shouldSkipScrapedItem's FillMissing metadata gate uses the DB
  // row's field COMPLETENESS, not mere presence. A row with all six core fields →
  // pre-skip; a scraped-but-partial row (empty publisher) → kept for a gap-filling
  // re-scrape. contentRating + players are left blank on both rows to confirm they
  // are NOT part of the criterion. Exercised at the context level — the gate is a
  // pure read over ScrapeSkipContext, so no runner / write-worker / real DB is
  // needed. Regression: the old !source.isEmpty() check skipped BOTH rows, so the
  // partial one's gap never filled.
  using MD = ItemMetadataStore::ItemMetadata;
  const auto row = [](bool withPublisher) {
    MD md;
    md.source = QStringLiteral("screenscraper");
    md.title = QStringLiteral("T");
    md.description = QStringLiteral("D");
    md.genre = QStringLiteral("G");
    md.developer = QStringLiteral("Dev");
    md.releaseDate = QStringLiteral("1990");
    // contentRating + players deliberately blank — not part of the criterion.
    if (withPublisher) md.publisher = QStringLiteral("Pub");
    return md;
  };

  Scraper::ScrapeSkipContext ctx;
  ctx.mode = Scraper::RescrapeMode::FillMissing;
  ctx.writeMetadata = true;
  ctx.dbCheckPossible = true; // field completeness is only consulted with a DB row
  ctx.sidecarCheckPossible = false;
  // No media wanted → the metadata-completeness half alone decides.
  ctx.metadataByPath[QStringLiteral("/games/Complete.bin")] = row(/*withPublisher=*/true);
  ctx.metadataByPath[QStringLiteral("/games/Partial.bin")] = row(/*withPublisher=*/false);

  QVERIFY(Scraper::shouldSkipScrapedItem(QStringLiteral("/games/Complete.bin"),
                                         ctx)); // all core fields → skip
  QVERIFY(!Scraper::shouldSkipScrapedItem(QStringLiteral("/games/Partial.bin"),
                                          ctx)); // missing publisher → kept
}

void TestBatchScrapeRunner::mediaCoverageMatchesMixedCaseFolders() {
  // Kartend-em3jc: media subdirs are named with the provider's ORIGINAL casing
  // (e.g. "box-2D-back"), but wanted types are lowercased. The coverage index
  // must resolve the folder case-insensitively — otherwise present art on a
  // case-sensitive filesystem reads as "missing" and FillMissing re-scrapes the
  // item on every run (0 skips despite complete artwork).
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString sub = QDir(tmp.path()).filePath(QStringLiteral("box-2D-back"));
  QVERIFY(QDir().mkpath(sub));
  QFile f(QDir(sub).filePath(QStringLiteral("Some Game (USA).png")));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("png");
  f.close();

  // Wanted type is lowercased ("box-2d-back"); the folder is mixed case.
  const auto index = Scraper::buildMediaCoverageIndex(tmp.path(), {QStringLiteral("box-2d-back")},
                                                      /*sidecarCheckPossible=*/true);
  QVERIFY2(index.presentByType.contains(QStringLiteral("box-2d-back")),
           "mixed-case media folder not resolved for the lowercased wanted type");
  QVERIFY(index.presentByType.value(QStringLiteral("box-2d-back"))
              .contains(QStringLiteral("some game (usa)")));
}

void TestBatchScrapeRunner::mediaCoverageProbesKindDirectoryForVideoVariants() {
  // Kartend-jjyst.1: writeMediaFiles routes every video-* asset into video/
  // (and manuals into manual/) by MediaKind, so the coverage index must probe
  // that kind directory for those wanted types. Probing a literal
  // "video-normalized/" folder would read the present video as missing and
  // FillMissing would re-download it on every run.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString sub = QDir(tmp.path()).filePath(QStringLiteral("video"));
  QVERIFY(QDir().mkpath(sub));
  QFile f(QDir(sub).filePath(QStringLiteral("Some Game (USA).mp4")));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("mp4");
  f.close();

  const auto index = Scraper::buildMediaCoverageIndex(
      tmp.path(), {QStringLiteral("video-normalized"), QStringLiteral("video")},
      /*sidecarCheckPossible=*/true);
  // Both wanted video types resolve to the shared video/ slot.
  QVERIFY2(index.presentByType.value(QStringLiteral("video-normalized"))
               .contains(QStringLiteral("some game (usa)")),
           "video-normalized coverage did not probe the kind-routed video/ directory");
  QVERIFY(index.presentByType.value(QStringLiteral("video"))
              .contains(QStringLiteral("some game (usa)")));
}

void TestBatchScrapeRunner::fillMissingKnownAbsentMediaCountsAsCovered() {
  // Kartend-kihyx: FillMissing must not re-chase media types the provider
  // doesn't supply. A wanted type recorded in the item's mediaAbsent set counts
  // as "covered" so the item skips even though that art isn't on disk — but a
  // genuinely-missing type the provider CAN supply still forces a re-scrape, and
  // the exemption applies only with a DB row (sidecar-only runs can't carry it).
  using MD = ItemMetadataStore::ItemMetadata;
  const auto completeRow = [](const QStringList &absent) {
    MD md;
    md.source = QStringLiteral("screenscraper");
    md.title = QStringLiteral("T");
    md.description = QStringLiteral("D");
    md.genre = QStringLiteral("G");
    md.developer = QStringLiteral("Dev");
    md.publisher = QStringLiteral("Pub");
    md.releaseDate = QStringLiteral("1990");
    md.mediaAbsent = absent;
    return md;
  };
  const QString path = QStringLiteral("/games/PS1.bin");
  const QString baseLower = QStringLiteral("ps1"); // completeBaseName("PS1.bin").toLower()

  Scraper::ScrapeSkipContext ctx;
  ctx.mode = Scraper::RescrapeMode::FillMissing;
  ctx.writeMetadata = true;
  ctx.dbCheckPossible = true;
  ctx.sidecarCheckPossible = false;
  ctx.wantedTypes = {QStringLiteral("front"), QStringLiteral("marquee")};
  ctx.presentByType[QStringLiteral("front")] = {baseLower}; // front on disk for this item
  // marquee is a REQUIRED (prevalent) media type — the provider reliably supplies
  // it collection-wide — but it is missing on disk for THIS item, exactly the case
  // the per-item known-absent set must handle.
  ctx.requiredMediaTypes = {QStringLiteral("front"), QStringLiteral("marquee")};

  // marquee required + missing here, but known-absent → treated as covered → skip.
  ctx.metadataByPath[path] = completeRow({QStringLiteral("marquee")});
  QVERIFY(Scraper::shouldSkipScrapedItem(path, ctx));

  // A required type that is NOT known-absent and NOT on disk still forces a scrape.
  ctx.wantedTypes = {QStringLiteral("front"), QStringLiteral("screenshot")};
  ctx.requiredMediaTypes = {QStringLiteral("front"), QStringLiteral("screenshot")};
  ctx.metadataByPath[path] = completeRow({QStringLiteral("marquee")}); // screenshot not absent
  QVERIFY(!Scraper::shouldSkipScrapedItem(path, ctx));

  // Sidecar-only (no DB): the known-absent set is unavailable, so the required
  // marquee is not exempted and the item is kept. writeMetadata off isolates the
  // media half from the (now DB-less) metadata gate.
  ctx.wantedTypes = {QStringLiteral("front"), QStringLiteral("marquee")};
  ctx.requiredMediaTypes = {QStringLiteral("front"), QStringLiteral("marquee")};
  ctx.dbCheckPossible = false;
  ctx.writeMetadata = false;
  QVERIFY(!Scraper::shouldSkipScrapedItem(path, ctx));
}

void TestBatchScrapeRunner::computeMediaAbsentTypesDerivesUnsuppliedWantedTypes() {
  // Kartend-kihyx: direct coverage for the PRODUCER of the known-absent set (the
  // runner's write path can't be observed via the mock, so the logic is pulled
  // into this pure function). A wanted type is "absent" iff the provider returned
  // no VALID-URL asset of that type. Guarded to FillMissing + non-empty filter.
  // Result order is QSet-iteration-defined, so compare as sets.
  using Scraper::computeMediaAbsentTypes;
  const auto asSet = [](const QStringList &l) { return QSet<QString>(l.begin(), l.end()); };

  Scraper::MediaAsset front; // genuinely supplied
  front.type = QStringLiteral("front");
  front.url = QUrl(QStringLiteral("https://example.invalid/f.png"));
  Scraper::MediaAsset marqueeBroken; // present type but invalid URL → NOT supplied
  marqueeBroken.type = QStringLiteral("marquee");
  marqueeBroken.url = QUrl();
  const QList<Scraper::MediaAsset> media{front, marqueeBroken};
  const QSet<QString> wanted{QStringLiteral("front"), QStringLiteral("marquee"),
                             QStringLiteral("map")};

  // FillMissing: front supplied → covered; marquee has only an invalid URL → absent;
  // map never offered → absent. (An inverted set-difference would yield {front}.)
  QCOMPARE(asSet(computeMediaAbsentTypes(Scraper::RescrapeMode::FillMissing, wanted, media)),
           (QSet<QString>{QStringLiteral("marquee"), QStringLiteral("map")}));

  // Guard: non-FillMissing modes record nothing (a dropped guard would populate here).
  QVERIFY(computeMediaAbsentTypes(Scraper::RescrapeMode::Overwrite, wanted, media).isEmpty());
  QVERIFY(computeMediaAbsentTypes(Scraper::RescrapeMode::Skip, wanted, media).isEmpty());

  // Short-circuit: an empty filter records nothing (legacy front-only path).
  QVERIFY(computeMediaAbsentTypes(Scraper::RescrapeMode::FillMissing, {}, media).isEmpty());

  // Every wanted type supplied → empty absent set.
  QVERIFY(
      computeMediaAbsentTypes(Scraper::RescrapeMode::FillMissing, {QStringLiteral("front")}, media)
          .isEmpty());
}

void TestBatchScrapeRunner::fillMissingIgnoresCollectionWideUnobtainableMedia() {
  // Kartend-ib46d: a wanted media type that NO item in the collection has on disk
  // (0 files collection-wide) is unobtainable here and must not block the skip. A
  // type present for OTHER items but missing for this one still blocks — until the
  // per-item known-absent set records it.
  using MD = ItemMetadataStore::ItemMetadata;
  const auto completeRow = []() {
    MD md;
    md.source = QStringLiteral("screenscraper");
    md.title = QStringLiteral("T");
    md.description = QStringLiteral("D");
    md.genre = QStringLiteral("G");
    md.developer = QStringLiteral("Dev");
    md.publisher = QStringLiteral("Pub");
    md.releaseDate = QStringLiteral("1990");
    return md;
  };
  const QString path = QStringLiteral("/games/PS1.bin");
  const QString baseLower = QStringLiteral("ps1"); // completeBaseName("PS1.bin").toLower()

  Scraper::ScrapeSkipContext ctx;
  ctx.mode = Scraper::RescrapeMode::FillMissing;
  ctx.writeMetadata = true;
  ctx.dbCheckPossible = true;
  ctx.wantedTypes = {QStringLiteral("front"), QStringLiteral("marquee")};
  ctx.presentByType[QStringLiteral("front")] = {baseLower}; // front on disk for this item
  // marquee is NOT prevalent (absent from requiredMediaTypes) → optional → skip.
  ctx.requiredMediaTypes = {QStringLiteral("front")};
  ctx.metadataByPath[path] = completeRow();
  QVERIFY(Scraper::shouldSkipScrapedItem(path, ctx));

  // marquee now required (prevalent) but missing for this item and not known-absent
  // → it blocks the skip.
  ctx.requiredMediaTypes = {QStringLiteral("front"), QStringLiteral("marquee")};
  QVERIFY(!Scraper::shouldSkipScrapedItem(path, ctx));

  // ...unless recorded absent for this item (kihyx) → covered again.
  MD withAbsent = completeRow();
  withAbsent.mediaAbsent = {QStringLiteral("marquee")};
  ctx.metadataByPath[path] = withAbsent;
  QVERIFY(Scraper::shouldSkipScrapedItem(path, ctx));
}

void TestBatchScrapeRunner::computePrevalentMediaTypesAppliesThreshold() {
  // Kartend-ib46d: only media types present for >= threshold of the collection are
  // "required" (their absence blocks a FillMissing skip); sparse types fall out.
  using Scraper::computePrevalentMediaTypes;
  const auto bases = [](int n) {
    QSet<QString> s;
    for (int i = 0; i < n; ++i) s.insert(QString::number(i));
    return s;
  };
  QHash<QString, QSet<QString>> present;
  present[QStringLiteral("front")] = bases(90);      // 90/100 = 90% >= 85% → required
  present[QStringLiteral("screenshot")] = bases(85); // exactly 85% → required (inclusive)
  present[QStringLiteral("video")] = bases(3);       // 3% → optional
  // "map" absent from the hash → 0% → optional.
  const QSet<QString> wanted{QStringLiteral("front"), QStringLiteral("screenshot"),
                             QStringLiteral("video"), QStringLiteral("map")};

  QCOMPARE(computePrevalentMediaTypes(present, {}, wanted, 100, 0.85),
           (QSet<QString>{QStringLiteral("front"), QStringLiteral("screenshot")}));

  // itemCount 0 → nothing is "reliably supplied".
  QVERIFY(computePrevalentMediaTypes(present, {}, wanted, 0, 0.85).isEmpty());

  // front counts its flat-mirror basenames too (subdir empty, flat carries it).
  QCOMPARE(computePrevalentMediaTypes({}, bases(90), {QStringLiteral("front")}, 100, 0.85),
           (QSet<QString>{QStringLiteral("front")}));
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

void TestBatchScrapeRunner::remainingPathsEmptyAfterMixedOutcomes() {
  // Resume bookkeeping: every real terminal verdict (success, error,
  // not-found) leaves the remaining list, trimmed by identity — a finished
  // batch has nothing left to resume even when failures interleave with
  // successes. (The old service-side positional removeFirst dropped the head
  // whenever ANY item succeeded, so an errored head item leaked a completed
  // one into the resume snapshot.)
  auto stub = std::make_shared<StubProvider>();
  StubProvider::Canned bad;
  bad.lookupError = QStringLiteral("HTTP 500");
  stub->byQuery[QStringLiteral("Beta")] = bad;
  stub->byQuery[QStringLiteral("Alpha")] = makeMatch("1", "Alpha (USA)");
  stub->byQuery[QStringLiteral("Gamma")] = makeMatch("3", "Gamma (USA)");

  // The errored item sits at the head — exactly the interleaving that used to
  // trim the wrong entry.
  const QStringList paths{QStringLiteral("/games/Beta.bin"), QStringLiteral("/games/Alpha.bin"),
                          QStringLiteral("/games/Gamma.bin")};
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 2);
  QCOMPARE(summary.errors, 1);
  QVERIFY2(runner.remainingPaths().isEmpty(),
           qPrintable(runner.remainingPaths().join(QLatin1Char(','))));
}

void TestBatchScrapeRunner::remainingPathsKeepsQuotaStoppedWork() {
  // Quota stop: the quota-erroring item never really got scraped and the rest
  // were never dispatched — all of them must stay in the remaining list so
  // the service's persisted resume point retries exactly the killed work.
  auto stub = std::make_shared<StubProvider>();
  StubProvider::Canned quota;
  quota.lookupError = QStringLiteral("quota exhausted");
  quota.lookupErrorHttpStatus = 430;
  stub->byQuery[QStringLiteral("Alpha")] = quota;
  stub->byQuery[QStringLiteral("Beta")] = makeMatch("2", "Beta (USA)");
  stub->byQuery[QStringLiteral("Gamma")] = makeMatch("3", "Gamma (USA)");

  const QStringList paths{QStringLiteral("/games/Alpha.bin"), QStringLiteral("/games/Beta.bin"),
                          QStringLiteral("/games/Gamma.bin")};
  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);
  QVERIFY(summary.quotaExhausted);
  QCOMPARE(runner.remainingPaths(), paths);
}

void TestBatchScrapeRunner::consecutiveFatalErrorsTripBreaker() {
  // Circuit breaker: persistent identical fatal responses (403 bad
  // credentials here) must stop dispatch after the threshold instead of
  // firing one doomed request per item — and the un-dispatched work must
  // stay in the resume list, like a quota stop.
  auto stub = std::make_shared<StubProvider>();
  StubProvider::Canned denied;
  denied.lookupError = QStringLiteral("forbidden");
  denied.lookupErrorHttpStatus = 403;
  QStringList paths;
  for (int i = 0; i < 8; ++i) {
    const QString name = QStringLiteral("Item%1").arg(i);
    stub->byQuery[name] = denied;
    paths.append(QStringLiteral("/games/%1.bin").arg(name));
  }

  Scraper::BatchScrapeRunner runner(nullptr, stub, QStringLiteral("uuid"), paths, QString());
  runner.start();
  const auto summary = waitForFinish(&runner);

  // The 5th consecutive 403 trips the breaker (its own item is counted as an
  // error, then dispatch stops), so exactly the threshold count errored and
  // everything from the tripping item onward stays queued for resume.
  QVERIFY(summary.quotaExhausted);
  QCOMPARE(summary.errors, 5);
  QCOMPARE(summary.scraped, 0);
  QCOMPARE(runner.remainingPaths().size(), paths.size() - 4);
  QVERIFY(runner.remainingPaths().contains(QStringLiteral("/games/Item7.bin")));
}

QTEST_MAIN(TestBatchScrapeRunner)
#include "test_batchscraperunner.moc"
