/**
 * @file test_scrapewriteworker.cpp
 * @brief Integration tests for `Scraper::ScrapeWriteWorker` — the
 *        worker-thread DB writer BatchScrapeRunner uses for per-item
 *        metadata + artwork saves.
 *
 * Why this exists: the worker owns a *per-instance* QSqlDatabase connection
 * that it opens (addDatabase) on a worker thread and is expected to remove
 * (removeDatabase) on shutdown / destruction. The free function it delegates
 * to (`saveScrapedMetadataDirect`) is unit-covered against `:memory:` in
 * test_scrapepersistence_direct; what is NOT covered there is the worker's
 * own behaviour:
 *   - It opens a *real on-disk* connection at
 *     `<AppDataLocation>/media.db` (no path argument — it derives the path
 *     from QStandardPaths), so these tests run under
 *     `QStandardPaths::setTestModeEnabled(true)` to land that file in an
 *     isolated per-test sandbox.
 *   - It drives the save across a queued cross-thread invocation and emits
 *     `writeCompleted(requestId, ok)` back on the worker thread.
 *   - Its connection lifecycle: the connection name must appear in
 *     `QSqlDatabase::connectionNames()` while the worker is live and must be
 *     gone once it is destroyed, or every subsequent worker that reuses a
 *     name trips Qt's "duplicate connection name" warning and silently
 *     reuses a stale handle.
 *
 * HARD constraint (docs/dev/testing.md "no-DB-mocking rule"): a REAL on-disk
 * SQLite file is used throughout. The schema is seeded via the production
 * `DatabaseSchema::createTables` path on a separate setup connection, exactly
 * the way DatabaseManager seeds it in production before any worker connects.
 */
#include <memory>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QMetaObject>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QString>
#include <QTest>
#include <QThread>
#include <QTimer>

#include "../../support/testsandbox.h"
#include "databaseschema.h"
#include "itemartwork.h"
#include "itemmetadata.h"
#include "scrapepersistence.h"
#include "scrapertypes.h"
#include "scrapewriteworker.h"

namespace {

/// Absolute directory the worker derives its `media.db` path from. The
/// worker opens `<AppDataLocation>/media.db`, so the verification /
/// schema-seed connection must point at the same directory.
QString appDataDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

/// Open a throwaway connection to the SAME on-disk media.db the worker
/// uses and run the production schema-creation path so the worker's
/// schema-readiness probe (item_metadata table must exist) passes. This
/// mirrors DatabaseManager seeding the schema on the main connection
/// before the worker ever connects.
void seedSchemaOnDisk() {
  const QString conn = QStringLiteral("test_writeworker_seed");
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    QVERIFY(DatabaseSchema::openConnection(db, appDataDir()));
    DatabaseSchema::applyConnectionPragmas(db);
    DatabaseSchema::createTables(db);
    db.close();
    db = QSqlDatabase();
  }
  QSqlDatabase::removeDatabase(conn);
}

/// Drive a single performWrite to completion on the worker's thread and
/// return the `ok` flag the worker reported. Posts openConnection +
/// performWrite as queued invocations (so they run on the worker thread
/// that owns the connection) and blocks the calling thread on a
/// QEventLoop until `writeCompleted` arrives — no fixed sleeps.
///
/// Why not QSignalSpy: the worker emits `writeCompleted` from the WORKER
/// thread, and QSignalSpy records each emission by appending to an internal
/// QList. A directly-connected spy would run that append on the worker
/// thread while this (main) thread constructed/reads the same QList — a
/// genuine cross-thread data race on the spy's container (TSan flagged it,
/// and it can tear reads). Instead we connect our own slot via an explicit
/// Qt::QueuedConnection to a main-thread context object (`loop`): Qt posts
/// the (requestId, ok) payload through the event queue and the slot runs on
/// THIS thread, so the capture below is single-threaded and ordered by Qt's
/// event loop.
[[nodiscard]] bool runWrite(Scraper::ScrapeWriteWorker &worker, quint64 requestId,
                            const QString &uuid, const QString &sourcePath,
                            const Scraper::ScrapedItem &scraped,
                            const Scraper::NonStandardArtworkList &artwork) {
  QEventLoop loop;
  quint64 gotRequestId = 0;
  bool gotOk = false;
  bool fired = false;

  // Receive the worker's reply on the main thread. `&loop` is the context
  // object (lives on the main thread), and Qt::QueuedConnection forces the
  // payload across the thread boundary via the event queue, so the lambda —
  // and the captured-by-reference locals it writes — execute single-threaded
  // here. quitting the loop hands control back to the wait below.
  QObject::connect(
      &worker, &Scraper::ScrapeWriteWorker::writeCompleted, &loop,
      [&](quint64 id, bool ok) {
        gotRequestId = id;
        gotOk = ok;
        fired = true;
        loop.quit();
      },
      Qt::QueuedConnection);

  // Queue openConnection first (idempotent) so the connection is created on
  // the worker thread, then the write. Both are Qt::QueuedConnection because
  // the worker lives on another thread.
  QMetaObject::invokeMethod(&worker, "openConnection", Qt::QueuedConnection);
  QMetaObject::invokeMethod(&worker, "performWrite", Qt::QueuedConnection,
                            Q_ARG(quint64, requestId), Q_ARG(QString, uuid),
                            Q_ARG(QString, sourcePath), Q_ARG(Scraper::ScrapedItem, scraped),
                            Q_ARG(Scraper::NonStandardArtworkList, artwork));

  // Bounded wait: a QTimer single-shot quits the loop if the worker never
  // replies, so a regression can't hang the whole ctest run on an unbounded
  // event loop. The connection above quits it first on the happy path.
  QTimer::singleShot(5000, &loop, [&loop] { loop.quit(); });
  loop.exec();

  if (!fired) {
    return false; // timed out — caller asserts on the result
  }
  // Sanity: the reply correlates to the request we posted.
  Q_ASSERT(gotRequestId == requestId);
  return gotOk;
}

} // namespace

class TestScrapeWriteWorker : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void init();

  void writePersistsMetadataAndArtworkRowsAtomically();
  void failedWriteLeavesNoMetadataRow();
  void connectionRemovedOnWorkerDestruction();
  void closeConnectionRemovesConnectionName();

private:
  /// Start a worker on its own QThread, mirroring BatchScrapeRunner's
  /// contract (moveToThread BEFORE start). The returned thread is owned
  /// by the caller and must be quit()+wait()ed (see stopWorkerThread).
  static void startWorkerThread(Scraper::ScrapeWriteWorker &worker, QThread &thread) {
    worker.moveToThread(&thread);
    thread.start();
    QVERIFY(thread.isRunning());
  }

  /// Run closeConnection ON the worker thread and BLOCK until it completes,
  /// then quit + join. BlockingQueuedConnection (not Queued) is deliberate: a
  /// queued close isn't guaranteed to run before quit() exits the event loop —
  /// on Windows it didn't, leaving the connection name registered when a case
  /// asserted it was gone. Blocking makes the removeDatabase deterministic and
  /// keeps it on the connection's owning thread.
  static void stopWorkerThread(Scraper::ScrapeWriteWorker &worker, QThread &thread) {
    QMetaObject::invokeMethod(&worker, "closeConnection", Qt::BlockingQueuedConnection);
    thread.quit();
    QVERIFY(thread.wait(5000));
  }
};

void TestScrapeWriteWorker::initTestCase() {
  // Isolated QStandardPaths so AppDataLocation (and therefore the worker's
  // media.db) resolves to a per-test sandbox, never the developer's real
  // ~/.local/share/Kartend. Same approach as
  // test_batchscraperunner_integration.
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-scrapewriteworker"));
}

void TestScrapeWriteWorker::init() {
  // Start each case from a clean on-disk DB so a row written by one case
  // can't satisfy another's assertions. Remove the file, then re-seed the
  // schema so the worker's readiness probe passes.
  QFile::remove(appDataDir() + QStringLiteral("/media.db"));
  QFile::remove(appDataDir() + QStringLiteral("/media.db-wal"));
  QFile::remove(appDataDir() + QStringLiteral("/media.db-shm"));
  seedSchemaOnDisk();
}

// ── Failure mode 1: a scraped item persists atomically ─────────────────────
// A successful worker write lands the metadata row AND every non-standard
// artwork row, readable back through a fresh connection to the same on-disk
// file. The worker is driven on a real QThread via queued invocation; the
// reply is awaited on a main-thread QEventLoop (no fixed sleep).
void TestScrapeWriteWorker::writePersistsMetadataAndArtworkRowsAtomically() {
  const QString uuid = QStringLiteral("u-write");
  const QString sourcePath = QStringLiteral("/m/song.flac");

  auto worker =
      std::make_unique<Scraper::ScrapeWriteWorker>(QStringLiteral("test_writeworker_persist"));
  QThread thread;
  startWorkerThread(*worker, thread);

  Scraper::ScrapedItem scraped;
  scraped.title = QStringLiteral("Persisted Title");
  scraped.publisher = QStringLiteral("Persisted Publisher");
  scraped.sourceProviderId = QStringLiteral("teststub");

  Scraper::NonStandardArtworkList artwork;
  artwork.append({QStringLiteral("back"), QStringLiteral("/disk/back.png")});
  artwork.append({QStringLiteral("manual"), QStringLiteral("/disk/manual.pdf")});

  const bool ok = runWrite(*worker, /*requestId=*/1, uuid, sourcePath, scraped, artwork);
  QVERIFY2(ok, "worker reported writeCompleted(ok=false) for a valid on-disk write");

  stopWorkerThread(*worker, thread);
  worker.reset();

  // Read back through a fresh, independent connection to the same file.
  const QString verifyConn = QStringLiteral("test_writeworker_persist_verify");
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), verifyConn);
    QVERIFY(DatabaseSchema::openConnection(db, appDataDir()));

    auto loaded = ItemMetadataStore::load(db, uuid, sourcePath);
    QVERIFY(loaded.isOk());
    QCOMPARE(loaded.value().title, QStringLiteral("Persisted Title"));
    QCOMPARE(loaded.value().publisher, QStringLiteral("Persisted Publisher"));
    QCOMPARE(loaded.value().source, QStringLiteral("teststub"));

    // Both non-standard artwork rows landed together with the metadata.
    auto rows = ItemArtworkStore::loadAllForItem(db, uuid, sourcePath);
    QVERIFY(rows.isOk());
    QCOMPARE(rows.value().size(), 2);
    QSet<QString> types;
    for (const auto &row : rows.value()) {
      types.insert(row.artworkType);
    }
    QVERIFY(types.contains(QStringLiteral("back")));
    QVERIFY(types.contains(QStringLiteral("manual")));

    db.close();
    db = QSqlDatabase();
  }
  QSqlDatabase::removeDatabase(verifyConn);
}

// ── Failure mode 2: a failed write does not leave a partial metadata row ────
//
// Scope / limitation: `saveScrapedMetadataDirect` does NOT wrap the
// metadata + artwork writes in a single SQL transaction — each store call is
// its own auto-commit statement, atomic at the per-row level (the metadata
// row is one UPSERT). The worker's public API exposes no fault-injection
// seam to force a *mid-statement* crash, and the no-DB-mocking rule forbids
// substituting a failing driver. So the strongest atomicity property
// reachable through the real public API is: a write the worker reports as
// FAILED must not have written a metadata row.
//
// We force that failure cleanly via the function's own guard — an empty
// collectionUuid makes `saveScrapedMetadataDirect` return false before the
// metadata UPSERT runs. After the worker reports ok=false, a fresh
// connection confirms no metadata row exists for that item: the failed write
// did not half-commit a metadata row.
void TestScrapeWriteWorker::failedWriteLeavesNoMetadataRow() {
  const QString sourcePath = QStringLiteral("/m/partial.flac");

  auto worker =
      std::make_unique<Scraper::ScrapeWriteWorker>(QStringLiteral("test_writeworker_partial"));
  QThread thread;
  startWorkerThread(*worker, thread);

  Scraper::ScrapedItem scraped;
  scraped.title = QStringLiteral("Should Not Land");
  scraped.publisher = QStringLiteral("Should Not Land");

  // Empty collectionUuid → the direct-save guard returns false before any
  // metadata UPSERT. No artwork rows requested, so nothing should persist.
  const bool ok = runWrite(*worker, /*requestId=*/2, /*uuid=*/QString(), sourcePath, scraped, {});
  QVERIFY2(!ok, "empty collectionUuid should make the worker report ok=false");

  stopWorkerThread(*worker, thread);
  worker.reset();

  // No metadata row for the empty-uuid item: the failed write did not
  // half-commit. We also confirm the real source path key is absent under a
  // non-empty uuid (defensive — the guard rejected the whole write).
  const QString verifyConn = QStringLiteral("test_writeworker_partial_verify");
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), verifyConn);
    QVERIFY(DatabaseSchema::openConnection(db, appDataDir()));

    auto loadedEmpty = ItemMetadataStore::load(db, QString(), sourcePath);
    QVERIFY(loadedEmpty.isOk());
    // load() returns an empty-but-keyed row when nothing is stored; a
    // half-committed write would surface the scraped title here.
    QVERIFY2(loadedEmpty.value().title.isEmpty(),
             "failed write half-committed a metadata row under the empty uuid");

    db.close();
    db = QSqlDatabase();
  }
  QSqlDatabase::removeDatabase(verifyConn);
}

// ── Failure mode 3: the per-worker connection is removed on destruction ─────
// The worker's connection name must be registered while the worker is live
// and gone once it is destroyed, so a later worker reusing the name doesn't
// collide with a leaked handle. ~ScrapeWriteWorker is the best-effort path
// (no explicit closeConnection here) — destruction alone must clean up.
//
// Destruction MUST happen on the worker thread, not the main thread. The
// worker opened m_db (a QSQLITE connection) on the worker thread, and "a
// QSqlDatabase instance must only be accessed by the thread it was created
// in" (Qt SQL docs). ~ScrapeWriteWorker's best-effort branch calls
// m_db.close() + QSqlDatabase::removeDatabase() — destroying that connection
// from the main thread after the worker thread has exited is undefined
// behaviour. Linux tolerates it; Windows takes an access violation (the test
// crashed there with no QTest output). So we reproduce production's exact
// teardown (BatchScrapeRunner connects QThread::finished → worker
// deleteLater): the worker self-deletes on its own thread as the event loop
// drains, running the destructor's DB cleanup on the connection's owning
// thread. The property under test is unchanged — destruction alone, with no
// queued closeConnection, must still remove the connection name.
void TestScrapeWriteWorker::connectionRemovedOnWorkerDestruction() {
  const QString connName = QStringLiteral("test_writeworker_lifecycle");
  QVERIFY2(!QSqlDatabase::connectionNames().contains(connName),
           "connection name leaked from a previous run");

  {
    // Heap-allocated (not unique_ptr) so it can be destroyed ON the worker
    // thread via deleteLater rather than on this (main) thread. Ownership is
    // handed to the QThread::finished → deleteLater connection below.
    auto *worker = new Scraper::ScrapeWriteWorker(connName);
    QThread thread;
    startWorkerThread(*worker, thread);
    // Production's destruction path: the worker self-destructs on its own
    // thread when the thread's event loop winds down, so ~ScrapeWriteWorker
    // (and its m_db.close()/removeDatabase) run on the connection's owning
    // thread. No explicit closeConnection is queued — the destructor's
    // best-effort cleanup is exactly what we're exercising.
    QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);

    Scraper::ScrapedItem scraped;
    scraped.title = QStringLiteral("Lifecycle");
    const bool ok = runWrite(*worker, /*requestId=*/3, QStringLiteral("u-life"),
                             QStringLiteral("/m/life.flac"), scraped, {});
    QVERIFY(ok);

    // The worker opened its connection on the worker thread; it is now
    // registered in Qt's global driver registry under connName.
    QVERIFY2(QSqlDatabase::connectionNames().contains(connName),
             "worker connection not registered after a successful write");

    // Quit the thread WITHOUT queuing closeConnection. quit() lets the event
    // loop process the posted deleteLater, so ~ScrapeWriteWorker runs on the
    // worker thread before finished() returns; wait() then joins the fully
    // torn-down thread.
    thread.quit();
    QVERIFY(thread.wait(5000));
  }

  // Connection name is gone — no leaked handle for the next worker to clash
  // with.
  QVERIFY2(!QSqlDatabase::connectionNames().contains(connName),
           "worker connection name still registered after destruction (leaked handle)");
}

// Companion to the destruction case: the explicit closeConnection slot (the
// path BatchScrapeRunner queues at shutdown) also removes the connection
// name, before the worker object is destroyed.
void TestScrapeWriteWorker::closeConnectionRemovesConnectionName() {
  const QString connName = QStringLiteral("test_writeworker_close");
  QVERIFY(!QSqlDatabase::connectionNames().contains(connName));

  auto worker = std::make_unique<Scraper::ScrapeWriteWorker>(connName);
  QThread thread;
  startWorkerThread(*worker, thread);

  Scraper::ScrapedItem scraped;
  scraped.title = QStringLiteral("Close");
  const bool ok = runWrite(*worker, /*requestId=*/4, QStringLiteral("u-close"),
                           QStringLiteral("/m/close.flac"), scraped, {});
  QVERIFY(ok);
  QVERIFY(QSqlDatabase::connectionNames().contains(connName));

  // Queue closeConnection on the worker thread + join. After this the
  // connection name must be gone even though the worker object still lives.
  stopWorkerThread(*worker, thread);
  QVERIFY2(!QSqlDatabase::connectionNames().contains(connName),
           "closeConnection did not remove the connection name");

  worker.reset();
}

QTEST_MAIN(TestScrapeWriteWorker)
#include "test_scrapewriteworker.moc"
