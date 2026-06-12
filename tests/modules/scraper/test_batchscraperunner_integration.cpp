/**
 * @file test_batchscraperunner_integration.cpp
 * @brief End-to-end test for BatchScrapeRunner against a real DatabaseManager.
 *
 * Why this exists: test_batchscraperunner uses a null IDatabaseManager so it
 * exercises the runner state machine without exercising the cross-thread
 * write path that gnkb introduced (Scraper::ScrapeWriteWorker on its own
 * QThread + QSqlDatabase, with cache invalidation routed back via
 * IDatabaseManager::invalidateMetadataCacheItem). The unit-level coverage
 * of the direct write function lives in test_scrapepersistence_direct;
 * this test adds the missing behavioural assertion that the cache
 * invalidation actually fires end-to-end after a real runner pass.
 *
 * Strategy: construct a real DatabaseManager (which sets up the SQLite
 * file + schema + metadata cache); pre-seed an item metadata row; load
 * it via DatabaseManager.loadItemMetadata so the cache populates with
 * the OLD value; run BatchScrapeRunner with a stub provider that returns
 * a NEW title; wait for finish; re-load via DatabaseManager.loadItemMetadata.
 * If the new title comes back, both the worker write AND the cache
 * invalidation worked. If the old (cached) title comes back, the
 * invalidation hook regressed.
 */

#include <memory>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "../../support/testsandbox.h"
#include "applicationcontext.h"
#include "batchscraperunner.h"
#include "databasemanager.h"
#include "itemmetadata.h"
#include "scrapertypes.h"
#include "sessionmanager.h"
#include "stubmetadataprovider.h"

namespace {

/// Spin the event loop until the runner emits `finished`, then return
/// the captured summary. 5s timeout matches the unit-test fixture.
Scraper::BatchScrapeRunner::Summary waitForFinish(Scraper::BatchScrapeRunner *runner) {
  Scraper::BatchScrapeRunner::Summary captured;
  bool done = false;
  QObject::connect(runner, &Scraper::BatchScrapeRunner::finished,
                   [&captured, &done](const auto &s) {
                     captured = s;
                     done = true;
                   });
  constexpr int kTimeoutMs = 5000;
  QElapsedTimer timer;
  timer.start();
  while (!done && timer.elapsed() < kTimeoutMs) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  }
  return captured;
}

ApplicationContext makeCtx(SessionManager *session) {
  // Mirrors the helper in test_databasemanager.cpp — DatabaseManager is the
  // only ctx consumer here and only the SessionManager pointer is read.
  ApplicationContext ctx;
  ctx.managers.sessionManager = session;
  return ctx;
}

} // namespace

class TestBatchScrapeRunnerIntegration : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void cleanup();
  void writesLandInDatabaseAndCacheGetsInvalidated();
  void cancelMidBatchAtHighConcurrencyDrainsCleanly();

private:
  std::unique_ptr<SessionManager> m_session;
};

void TestBatchScrapeRunnerIntegration::cleanup() {
  // Each test owns its own SessionManager + DatabaseManager so failures
  // in one don't bleed connection state into the next. cleanup() runs
  // between cases regardless of pass/fail.
  m_session.reset();
}

void TestBatchScrapeRunnerIntegration::initTestCase() {
  // Test-mode QStandardPaths so AppDataLocation resolves to an isolated
  // per-test directory rather than the user's real ~/.local/share/Kartend.
  // Same setup test_databasemanager uses; the worker thread inherits
  // the same writableLocation so its connection lands in the same file
  // DatabaseManager opened.
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-batchscraperunner-integration"));
}

void TestBatchScrapeRunnerIntegration::writesLandInDatabaseAndCacheGetsInvalidated() {
  const QString uuid = QStringLiteral("u-integration");
  const QString sourcePath = QStringLiteral("/m/song.flac");
  const QString baseName = QStringLiteral("song"); // matches sourcePath's
                                                   // completeBaseName, which
                                                   // is what the runner
                                                   // queries the provider for.

  m_session = std::make_unique<SessionManager>();
  auto ctx = makeCtx(m_session.get());
  auto db = std::make_unique<DatabaseManager>(&ctx);
  // Kartend-m02z: BatchScrapeRunner reads DB through ctx; publish it now
  // that DatabaseManager has been constructed.
  ctx.managers.databaseManager = db.get();

  // ── Step 1: pre-seed the row with the OLD title via the public API.
  // saveItemMetadata invalidates the cache itself, so after this the
  // cache is empty for this key.
  ItemMetadataStore::ItemMetadata seed;
  seed.collectionUuid = uuid;
  seed.path = sourcePath;
  seed.title = QStringLiteral("OLD TITLE");
  seed.publisher = QStringLiteral("OLD PUB");
  QVERIFY(db->saveItemMetadata(seed));

  // ── Step 2: load via DatabaseManager → populates the cache with the
  // OLD title. If the next runner pass regresses on cache invalidation,
  // the second load below would return this stale value.
  const auto firstLoad = db->loadItemMetadata(uuid, sourcePath);
  QCOMPARE(firstLoad.title, QStringLiteral("OLD TITLE"));

  // ── Step 3: run the scrape with a stub provider that returns a
  // DIFFERENT title for the same item. The runner's write worker writes
  // through its own connection (not via DatabaseManager), so the only way
  // for the next loadItemMetadata to see the new title is if
  // onWriteCompleted's invalidateMetadataCacheItem call ran on the main
  // thread after the queued reply landed.
  auto stub = std::make_shared<KartendTest::StubMetadataProvider>();
  stub->byQuery[baseName] = KartendTest::makeStubMatch("1", QStringLiteral("NEW TITLE"));

  Scraper::BatchScrapeRunner runner(&ctx, stub, uuid, QStringList{sourcePath},
                                    /*artworkDir=*/QString(), /*fetchPrimaryCover=*/false);
  runner.start();
  const auto summary = waitForFinish(&runner);
  QCOMPARE(summary.scraped, 1);
  QCOMPARE(summary.errors, 0);
  QCOMPARE(summary.skipped, 0);

  // ── Step 4: re-load via DatabaseManager. If the cache was invalidated
  // correctly the load goes through to the underlying SQLite file (which
  // the worker thread wrote to via its own connection) and returns the
  // NEW title. If the invalidate hook regressed, the cached OLD title
  // would come back instead.
  const auto secondLoad = db->loadItemMetadata(uuid, sourcePath);
  QCOMPARE(secondLoad.title, QStringLiteral("NEW TITLE"));
  // OLD publisher is preserved because the scrape didn't return one
  // (pickNonEmpty merge semantics) — sanity-check that the merge ran
  // with the existing row, not on a blank slate.
  QCOMPARE(secondLoad.publisher, QStringLiteral("OLD PUB"));
  QCOMPARE(secondLoad.source, QStringLiteral("stub"));

  // ── Cleanup. Destroy the runner FIRST so its worker thread + queued
  // reply slot are torn down before DatabaseManager's connection — the
  // worker holds a separate connection but a clean order avoids any
  // race in shutdown logging.
  // (runner is a stack object, drops at end of scope; explicit reset
  // for db here to make the intended order obvious.)
  db.reset();
}

void TestBatchScrapeRunnerIntegration::cancelMidBatchAtHighConcurrencyDrainsCleanly() {
  // Stress test for the cancellation race that gnkb's write-worker
  // architecture introduces: with itemConcurrency=8 multiple items are
  // simultaneously hopping between the QtConcurrent file-write pool, the
  // ScrapeWriteWorker QThread, and the main thread (queued
  // writeCompleted). cancel() flips m_cancelled and every in-flight
  // path is supposed to observe it and drain to itemFinished without
  // double-ticking counters or double-emitting `finished`.
  //
  // The assertions are deliberately about INVARIANTS rather than exact
  // counts — exact counts depend on the cancel-vs-dispatch race, which
  // is not deterministic. What MUST hold under any race outcome:
  //   1. `finished` fires exactly once.
  //   2. scraped + skipped + errors <= total queued (no double-counting).
  //   3. Items that completed before cancel landed are persisted in the
  //      DB (we sample one to confirm).
  //   4. Runner destruction (and worker-thread join) returns within the
  //      bounded shutdown wait — no UI hang on close mid-batch.
  const QString uuid = QStringLiteral("u-cancel-stress");
  constexpr int kQueueSize = 100;
  constexpr int kConcurrency = 8;

  m_session = std::make_unique<SessionManager>();
  auto ctx = makeCtx(m_session.get());
  auto db = std::make_unique<DatabaseManager>(&ctx);
  // Kartend-m02z: BatchScrapeRunner reads DB through ctx; publish it now
  // that DatabaseManager has been constructed.
  ctx.managers.databaseManager = db.get();

  // Build a stub provider that returns success for every queued item.
  // The stub's QTimer::singleShot(0) per-callback cadence means many
  // items flow through the chain on each event-loop tick — exactly
  // the dispatch pattern needed to get multiple in-flight saves
  // queued at the worker before cancel lands.
  auto stub = std::make_shared<KartendTest::StubMetadataProvider>();
  QStringList paths;
  paths.reserve(kQueueSize);
  for (int i = 0; i < kQueueSize; ++i) {
    const QString base = QStringLiteral("stress_%1").arg(i, 3, 10, QLatin1Char('0'));
    paths.append(QStringLiteral("/m/%1.flac").arg(base));
    stub->byQuery[base] =
        KartendTest::makeStubMatch(QString::number(i), QStringLiteral("Title %1").arg(i));
  }

  // Track `finished` emissions via a counter so a double-emit would
  // fail the test loudly rather than silently lose an event.
  int finishedCount = 0;
  Scraper::BatchScrapeRunner::Summary captured;
  {
    Scraper::BatchScrapeRunner runner(&ctx, stub, uuid, paths,
                                      /*artworkDir=*/QString(),
                                      /*fetchPrimaryCover=*/false, Scraper::RescrapeMode::Overwrite,
                                      kConcurrency);
    QObject::connect(&runner, &Scraper::BatchScrapeRunner::finished,
                     [&captured, &finishedCount](const auto &s) {
                       captured = s;
                       ++finishedCount;
                     });
    runner.start();

    // Spin briefly to let some items dispatch into the worker thread
    // (otherwise cancel runs before any callback has had a chance to
    // fire and we test nothing). 100ms is enough to get a healthy
    // batch in flight at the stub's 0-delay cadence — a few dozen
    // items typically complete or are mid-flight by then.
    QElapsedTimer warmup;
    warmup.start();
    while (warmup.elapsed() < 100) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    runner.cancel();

    // Drain to the finished signal. The bounded wait (5s) is generous —
    // the queue should drain in well under a second under any race
    // outcome since the worker thread joins in <= kWriteWorkerShutdownWaitMs.
    QElapsedTimer drain;
    drain.start();
    while (finishedCount == 0 && drain.elapsed() < 5000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
  }
  // Runner destructed at end of scope — shutdownWriteWorker() must have
  // returned by here (delete-as-stack-object blocks until the dtor
  // returns). If the worker-thread shutdown wedged we'd have hit the
  // 5s wait above well before reaching here.

  QCOMPARE(finishedCount, 1);
  const int totalAccounted = captured.scraped + captured.skipped + captured.errors;
  QVERIFY2(totalAccounted <= kQueueSize,
           qPrintable(QStringLiteral("totalAccounted=%1 > queueSize=%2 — "
                                     "indicates double-counting under cancel race")
                          .arg(totalAccounted)
                          .arg(kQueueSize)));
  QVERIFY2(totalAccounted >= 0, "summary counters went negative");

  // Spot-check: at least one item should have completed before cancel
  // landed (otherwise the warmup window was too short and we didn't
  // actually exercise the in-flight cancel race we set out to test).
  // The check uses scraped, not totalAccounted, because skipped/errors
  // are paths that didn't go through the worker.
  QVERIFY2(captured.scraped > 0, "no items completed before cancel — warmup window too short, "
                                 "test is not exercising the cancel-vs-write race");

  // For one item that DID complete, verify the metadata actually
  // landed. Catches a regression where a cancel-race causes the runner
  // to tick scraped++ but the worker's write was dropped.
  const QString completedBase = QStringLiteral("stress_%1").arg(0, 3, 10, QLatin1Char('0'));
  const QString completedPath = QStringLiteral("/m/%1.flac").arg(completedBase);
  const auto loaded = db->loadItemMetadata(uuid, completedPath);
  if (loaded.title.isEmpty()) {
    // Item 0 wasn't necessarily one of the completed ones — the queue
    // may have dispatched in any order. Walk the queue looking for any
    // item that DID land.
    bool foundAny = false;
    for (int i = 0; i < kQueueSize; ++i) {
      const QString p = QStringLiteral("/m/stress_%1.flac").arg(i, 3, 10, QLatin1Char('0'));
      const auto m = db->loadItemMetadata(uuid, p);
      if (!m.title.isEmpty()) {
        foundAny = true;
        QVERIFY(m.title.startsWith(QStringLiteral("Title ")));
        break;
      }
    }
    QVERIFY2(foundAny, "scraped > 0 but no metadata rows found in DB — "
                       "indicates the worker write was dropped under cancel");
  } else {
    QVERIFY(loaded.title.startsWith(QStringLiteral("Title ")));
  }

  db.reset();
}

QTEST_MAIN(TestBatchScrapeRunnerIntegration)
#include "test_batchscraperunner_integration.moc"
