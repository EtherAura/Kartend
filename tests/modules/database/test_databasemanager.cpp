/**
 * @file test_databasemanager.cpp
 * @brief Lifecycle and threading-safety tests for DatabaseManager.
 *
 * The destructor is the most crash-relevant surface (worker-thread shutdown,
 * intentionally-leaked threads when wait() times out, SQLite connection
 * cleanup). These tests exercise construct -> destruct paths under the same
 * QStandardPaths-test-mode setup the rest of the suite uses, and validate the
 * thread-safe path-resolution helpers that may be called from any thread.
 */

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "../../support/testsandbox.h"
#include "applicationcontext.h"
#include "batchsizes.h"
#include "collection/collectioncontext.h"
#include "collection/typehelpers.h"
#include "databasemanager.h"
#include "sessionmanager.h"

namespace {
/// Builds a minimal ApplicationContext for tests — only sessionManager is
/// populated since DatabaseManager is the only consumer of ctx in the
/// tested surface. Returned by value; tests pass its address to the ctor.
ApplicationContext makeCtxWithSession(SessionManager *session) {
  ApplicationContext ctx;
  ctx.managers.sessionManager = session;
  return ctx;
}
} // namespace

class TestDatabaseManager : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanup();

  // Lifecycle ---------------------------------------------------------------
  void testConstructDestruct_doesNotCrash();
  void testRepeatedConstructDestruct_doesNotLeakConnection();
  void testDestructAfterInitDatabase_closesConnectionCleanly();
  void testDestructDuringActiveScan_returnsWithinBoundedTime();

  // Path resolution (thread-safe, no DB needed) -----------------------------
  void testResolveFilePath_absolutePathPassthrough();
  void testResolveRelativeFilePath_emptyMapReturnsEmpty();
  void testResolveRelativeFilePath_resolvesViaMap();

  // Collection-uuid reconcile -----------------------------------------------
  void testMainConnectionUsesWalAndNormalSync();
  void testMigrateCollectionUuid_movesItemAndCollectionRows();
  void testMigrateCollectionUuid_mergesConflictsAndChildTables();
  void testPurgeOrphanCollectionData_dropsRowsNotInLiveSet();
  void testPurgeOrphanCollectionData_largeLiveSetUsesTempTable();

  // Worker-write contention (Kartend-cbtml) ----------------------------------
  void testRecordItemLaunch_survivesHeldWriteLock();
  void testRecordItemLaunch_survivesWriteLockOutlastingInlineLadder();
  void testRecordItemLaunch_deferredWriteKeepsItsLaunchTimeOrder();

private:
  std::unique_ptr<SessionManager> m_session;
};

void TestDatabaseManager::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-databasemanager"));
}

void TestDatabaseManager::cleanup() {
  // Clear any test artifacts left by a previous case.
  m_session.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void TestDatabaseManager::testConstructDestruct_doesNotCrash() {
  // Bare construction spins up two QThread workers and opens a SQLite
  // connection. Destruction must cancel any in-flight scans, quit the
  // workers, and tear the connection down without leaving the connection
  // pool dirty.
  m_session = std::make_unique<SessionManager>();
  {
    auto appCtx = makeCtxWithSession(m_session.get());
    DatabaseManager db(&appCtx);
    Q_UNUSED(db);
  }
  // If we get here, the destructor returned within its bounded wait without
  // crashing or qFataling on a still-running QThread.
  QVERIFY(true);
}

void TestDatabaseManager::testRepeatedConstructDestruct_doesNotLeakConnection() {
  // The destructor calls QSqlDatabase::removeDatabase(connectionName).
  // Repeated construct/destruct cycles must not accumulate stale entries
  // in the connection pool. Connection names are suffixed per-instance, so
  // the leak check captures each manager's name before destruct and
  // verifies it's been removed from the registry afterwards.
  m_session = std::make_unique<SessionManager>();

  QStringList connNames;
  for (int i = 0; i < 3; ++i) {
    auto appCtx = makeCtxWithSession(m_session.get());
    DatabaseManager db(&appCtx);
    connNames.append(db.connectionName());
  }

  for (const QString &connName : connNames) {
    QVERIFY2(!QSqlDatabase::contains(connName),
             qPrintable(QStringLiteral("DatabaseManager destructor leaked the "
                                       "SQL connection registration: %1")
                            .arg(connName)));
  }
}

void TestDatabaseManager::testDestructAfterInitDatabase_closesConnectionCleanly() {
  m_session = std::make_unique<SessionManager>();
  QString connName;
  {
    auto appCtx = makeCtxWithSession(m_session.get());
    DatabaseManager db(&appCtx);
    connName = db.connectionName();
    // Constructor already calls initDatabase(); calling it again exercises
    // the close+reopen path.
    db.initDatabase();
    QVERIFY2(QSqlDatabase::contains(connName),
             "initDatabase() did not register the SQL connection");
  }
  QVERIFY2(!QSqlDatabase::contains(connName),
           "Destructor did not remove the SQL connection after a re-init");
}

void TestDatabaseManager::testDestructDuringActiveScan_returnsWithinBoundedTime() {
  // Kicks off a scan against a populated media directory, then destructs
  // mid-flight. The destructor calls requestCancelScan() on both worker and
  // scan QueryManagers, then quit()+wait(SHUTDOWN_WAIT_MS=2000ms) on each
  // QThread, falling through to an intentional thread-leak rather than
  // qFatal-on-running-QThread if the budget expires. Either branch must
  // return within a reasonable wall-clock bound.
  m_session = std::make_unique<SessionManager>();

  // Build a media directory large enough that a scan does meaningful work.
  QTemporaryDir mediaDir;
  QVERIFY2(mediaDir.isValid(), "Failed to create temporary media directory");
  const QDir dir(mediaDir.path());
  constexpr int fileCount = 1500;
  for (int i = 0; i < fileCount; ++i) {
    const QString fileName = QStringLiteral("file_%1.bin").arg(i, 6, 10, QChar('0'));
    QFile f(dir.filePath(fileName));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();
  }

  CollectionConfig collection;
  collection.name = QStringLiteral("DestructDuringScan");
  collection.mediaDirectory = mediaDir.path();
  collection.folderBrowsing.includeContentSubfolders = false;
  collection.extensions = {QStringLiteral("bin")};

  QList<CollectionConfig> allCollections;
  allCollections.append(collection);

  CollectionContext ctx;
  ctx.currentIndex = 0;
  ctx.config = collection;

  auto dbCtx = makeCtxWithSession(m_session.get());
  auto *db = new DatabaseManager(&dbCtx);
  const QString connName = db->connectionName();
  // Trigger a scan via the public API; this dispatches to the worker thread
  // through queued connections.
  db->fetchItemCount(ctx, allCollections, QString());

  // Yield once so the queued request has a chance to land on the worker
  // thread before we tear DatabaseManager down. We deliberately do *not*
  // wait for completion — the goal is to destruct mid-scan.
  QCoreApplication::processEvents();

  // Destruction must return within the per-thread 2000ms budget × 2 threads,
  // plus connection teardown slack. 6000ms is generous on slow CI.
  QElapsedTimer timer;
  timer.start();
  delete db;
  const qint64 elapsedMs = timer.elapsed();

  QVERIFY2(elapsedMs < 6000,
           qPrintable(QStringLiteral("DatabaseManager destructor took %1 ms during active "
                                     "scan — bounded-wait fallback may be regressing")
                          .arg(elapsedMs)));

  // The connection must be cleaned up regardless of whether the worker
  // threads were waited or leaked.
  QVERIFY2(!QSqlDatabase::contains(connName),
           "DatabaseManager destructor did not remove the SQL connection "
           "after a mid-scan teardown");
}

// ─────────────────────────────────────────────────────────────────────────────
// Path resolution (thread-safe helpers)
// ─────────────────────────────────────────────────────────────────────────────

void TestDatabaseManager::testResolveFilePath_absolutePathPassthrough() {
  m_session = std::make_unique<SessionManager>();
  auto appCtx = makeCtxWithSession(m_session.get());
  DatabaseManager db(&appCtx);

  CollectionContext ctx;
  ctx.currentIndex = 0;

  // Absolute paths must be returned unchanged regardless of context state.
  const QString absPath = QStringLiteral("/tmp/some/absolute/file.bin");
  QCOMPARE(db.resolveFilePath(absPath, ctx), absPath);
}

void TestDatabaseManager::testResolveRelativeFilePath_emptyMapReturnsEmpty() {
  m_session = std::make_unique<SessionManager>();
  auto appCtx = makeCtxWithSession(m_session.get());
  DatabaseManager db(&appCtx);

  // No mapping available -> empty string (caller treats as "not resolvable").
  QHash<QString, QString> emptyMap;
  QCOMPARE(db.resolveRelativeFilePath(QStringLiteral("foo.bin"), emptyMap), QString());
}

void TestDatabaseManager::testResolveRelativeFilePath_resolvesViaMap() {
  m_session = std::make_unique<SessionManager>();
  auto appCtx = makeCtxWithSession(m_session.get());
  DatabaseManager db(&appCtx);

  QHash<QString, QString> fileNames;
  fileNames.insert(QStringLiteral("/abs/path/to/foo.bin"), QStringLiteral("foo.bin"));
  fileNames.insert(QStringLiteral("/abs/path/to/bar.bin"), QStringLiteral("bar.bin"));

  // An exact key in the caller's map passes through unchanged.
  QCOMPARE(db.resolveRelativeFilePath(QStringLiteral("/abs/path/to/foo.bin"), fileNames),
           QStringLiteral("/abs/path/to/foo.bin"));

  // Kartend-ardm7: leaf-name / relative-path resolution now answers from the
  // FileMapCache reverse index built on items load — the per-call linear
  // suffix scan over the caller's map is gone (it made widget creation
  // O(items^2)). With no load delivered to this manager the index is empty,
  // so a leaf-name lookup misses instead of scanning. The index-backed
  // resolution contract is covered by FileMapCacheResolve
  // (test_filemapcache.cpp), which drives replaceFromItemsLoaded directly.
  QCOMPARE(db.resolveRelativeFilePath(QStringLiteral("foo.bin"), fileNames), QString());
}

// ─────────────────────────────────────────────────────────────────────────────
// Collection-uuid reconcile
// ─────────────────────────────────────────────────────────────────────────────

namespace {
/// Opens a second connection to DatabaseManager's media.db so a test
/// can seed rows and read counts back independently of the manager.
QSqlDatabase openInspector() {
  const QString dbDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString dbPath = QDir(dbDir).absoluteFilePath(QStringLiteral("media.db"));
  const QString conn = QStringLiteral("test_dbmgr_reconcile_inspect");
  if (QSqlDatabase::contains(conn)) {
    QSqlDatabase::removeDatabase(conn);
  }
  QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
  db.setDatabaseName(dbPath);
  db.open();
  if (db.isOpen()) {
    // The purge/write paths now run on the DB worker thread (Kartend-dbqt5),
    // so inspector statements can overlap worker commits/checkpoints. Without
    // a busy timeout those overlaps surface as spurious instant
    // "database is locked" failures.
    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
  }
  return db;
}

bool runSql(QSqlDatabase &db, const QString &sql) {
  QSqlQuery q(db);
  if (!q.exec(sql)) {
    qWarning() << "runSql failed:" << q.lastError().text() << "sql:" << sql.left(120);
    return false;
  }
  return true;
}

int scalar(QSqlDatabase &db, const QString &sql) {
  QSqlQuery q(db);
  return (q.exec(sql) && q.next()) ? q.value(0).toInt() : -1;
}

/// Text-column counterpart of scalar(), for the ISO-8601 timestamp columns.
/// Empty string on no-row/failure so callers can assert non-emptiness first.
QString scalarText(QSqlDatabase &db, const QString &sql) {
  QSqlQuery q(db);
  return (q.exec(sql) && q.next()) ? q.value(0).toString() : QString();
}
} // namespace

void TestDatabaseManager::testMainConnectionUsesWalAndNormalSync() {
  // Kartend-fkvs: the GUI-thread connection must run WAL (so it doesn't block
  // worker readers/writers more than necessary) and synchronous=NORMAL (so the
  // same media.db isn't written at two durability levels). The connection is
  // created on this (main) thread, so querying it here is thread-safe.
  m_session = std::make_unique<SessionManager>();
  auto appCtx = makeCtxWithSession(m_session.get());
  DatabaseManager db(&appCtx);

  QSqlDatabase main = QSqlDatabase::database(db.connectionName());
  QVERIFY2(main.isValid() && main.isOpen(), "main media.db connection is not open");

  QSqlQuery jm(main);
  QVERIFY(jm.exec(QStringLiteral("PRAGMA journal_mode")) && jm.next());
  QCOMPARE(jm.value(0).toString().toLower(), QStringLiteral("wal"));

  QSqlQuery sync(main);
  QVERIFY(sync.exec(QStringLiteral("PRAGMA synchronous")) && sync.next());
  QCOMPARE(sync.value(0).toInt(), 1); // 1 == NORMAL
}

void TestDatabaseManager::testMigrateCollectionUuid_movesItemAndCollectionRows() {
  m_session = std::make_unique<SessionManager>();
  auto appCtx = makeCtxWithSession(m_session.get());
  DatabaseManager db(&appCtx);
  db.initDatabase();

  QSqlDatabase insp = openInspector();
  QVERIFY(insp.isValid() && insp.isOpen());
  QVERIFY(runSql(insp, "DELETE FROM items"));
  QVERIFY(runSql(insp, "DELETE FROM collections"));
  QVERIFY(runSql(insp, "INSERT INTO collections (id, name, last_scanned, uuid) "
                       "VALUES (1, 'C', 'x', 'old-uuid')"));
  QVERIFY(runSql(insp, "INSERT INTO items (collection_id, path, name, last_modified, "
                       "play_count, collection_uuid) VALUES "
                       "(1, '/m/a.bin', 'a', 'x', 7, 'old-uuid'), "
                       "(1, '/m/b.bin', 'b', 'x', 0, 'old-uuid')"));

  db.migrateCollectionUuid(QStringLiteral("old-uuid"), QStringLiteral("new-uuid"));

  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM items WHERE collection_uuid='old-uuid'"), 0);
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM items WHERE collection_uuid='new-uuid'"), 2);
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM collections WHERE uuid='new-uuid'"), 1);
  // Play history rides along with the migrated row.
  QCOMPARE(scalar(insp, "SELECT play_count FROM items WHERE path='/m/a.bin'"), 7);
}

void TestDatabaseManager::testMigrateCollectionUuid_mergesConflictsAndChildTables() {
  // Kartend-ndyf: a rename must re-key every collection-uuid-keyed table, merge
  // a conflicting fresh stub's usage instead of dropping it, and leave no rows
  // (or orphans) under the old uuid.
  m_session = std::make_unique<SessionManager>();
  auto appCtx = makeCtxWithSession(m_session.get());
  DatabaseManager db(&appCtx);
  db.initDatabase();

  QSqlDatabase insp = openInspector();
  QVERIFY(insp.isValid() && insp.isOpen());
  for (const char *t : {"items", "collections", "item_metadata", "item_artwork", "launch_history",
                        "playlist_items", "playlists", "dat_audit_profile"}) {
    QVERIFY(runSql(insp, QStringLiteral("DELETE FROM %1").arg(t)));
  }

  // id=1 is the real (renamed) collection; id=2 is the fresh row a post-rename
  // rescan created under the new uuid before the migration ran.
  QVERIFY(runSql(insp, "INSERT INTO collections (id, name, last_scanned, uuid) VALUES "
                       "(1, 'C', 'x', 'old-uuid'), (2, 'C', 'x', 'new-uuid')"));
  // Real (old-uuid) rows carrying the history/curation.
  QVERIFY(runSql(insp, "INSERT INTO items (collection_id, path, name, last_modified, play_count, "
                       "last_played, rating, collection_uuid) VALUES "
                       "(1, '/m/a.bin', 'a', 'x', 7, '2026-05-20T00:00:00Z', 5, 'old-uuid'), "
                       "(1, '/m/b.bin', 'b', 'x', 3, NULL, 0, 'old-uuid')"));
  // Fresh stub under new-uuid (distinct collection_id), same path as /m/a.bin,
  // with a small post-rename play_count that must survive the merge.
  QVERIFY(runSql(insp, "INSERT INTO items (collection_id, path, name, last_modified, play_count, "
                       "collection_uuid) VALUES (2, '/m/a.bin', 'a', 'x', 2, 'new-uuid')"));
  QVERIFY(runSql(insp, "INSERT INTO item_metadata (collection_uuid, path, updated_at) "
                       "VALUES ('old-uuid', '/m/a.bin', 'x')"));
  QVERIFY(runSql(insp, "INSERT INTO item_artwork (collection_uuid, path, artwork_type, updated_at) "
                       "VALUES ('old-uuid', '/m/a.bin', 'box', 'x')"));
  QVERIFY(runSql(insp, "INSERT INTO launch_history (collection_uuid, path, name, launched_at) "
                       "VALUES ('old-uuid', '/m/a.bin', 'a', 'x')"));
  QVERIFY(runSql(insp, "INSERT INTO playlists (id, name, parent_collection_uuid) "
                       "VALUES ('p1', 'PL', 'old-uuid')"));
  QVERIFY(runSql(insp, "INSERT INTO playlist_items (playlist_id, position, source_collection_uuid, "
                       "source_path, added_at) VALUES ('p1', 0, 'old-uuid', '/m/a.bin', 'x')"));
  // A linked DAT-audit profile must ride along too — omitting it from the
  // re-key list silently severed the link on every rename (Kartend-m6qsb.1).
  QVERIFY(runSql(insp, "INSERT INTO dat_audit_profile (name, collection_uuid) "
                       "VALUES ('Audit', 'old-uuid')"));

  db.migrateCollectionUuid(QStringLiteral("old-uuid"), QStringLiteral("new-uuid"));

  // Nothing left under the old uuid anywhere.
  for (const char *t :
       {"items", "item_metadata", "item_artwork", "launch_history", "dat_audit_profile"}) {
    QCOMPARE(
        scalar(insp,
               QStringLiteral("SELECT COUNT(*) FROM %1 WHERE collection_uuid='old-uuid'").arg(t)),
        0);
  }
  QCOMPARE(
      scalar(insp, "SELECT COUNT(*) FROM playlist_items WHERE source_collection_uuid='old-uuid'"),
      0);

  // items: exactly one /m/a.bin row under new-uuid, with the MERGED (max) usage.
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM items WHERE collection_uuid='new-uuid' "
                        "AND path='/m/a.bin'"),
           1);
  QCOMPARE(scalar(insp, "SELECT play_count FROM items WHERE collection_uuid='new-uuid' "
                        "AND path='/m/a.bin'"),
           7); // max(7, 2)
  QCOMPARE(scalar(insp, "SELECT rating FROM items WHERE collection_uuid='new-uuid' "
                        "AND path='/m/a.bin'"),
           5);
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM items WHERE collection_uuid='new-uuid'"), 2);

  // Child tables rode along to the new uuid (no orphaning).
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM item_metadata WHERE collection_uuid='new-uuid'"), 1);
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM item_artwork WHERE collection_uuid='new-uuid'"), 1);
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM launch_history WHERE collection_uuid='new-uuid'"), 1);
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM dat_audit_profile WHERE collection_uuid='new-uuid'"),
           1);
  QCOMPARE(
      scalar(insp, "SELECT COUNT(*) FROM playlist_items WHERE source_collection_uuid='new-uuid'"),
      1);
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM playlists WHERE parent_collection_uuid='new-uuid'"),
           1);
  // The stub collections row was dropped and the real one re-keyed: one row.
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM collections WHERE uuid='new-uuid'"), 1);
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM collections WHERE uuid='old-uuid'"), 0);
}

void TestDatabaseManager::testPurgeOrphanCollectionData_dropsRowsNotInLiveSet() {
  m_session = std::make_unique<SessionManager>();
  auto appCtx = makeCtxWithSession(m_session.get());
  DatabaseManager db(&appCtx);
  db.initDatabase();

  CollectionConfig live;
  live.name = QStringLiteral("Live");
  live.mediaDirectory = QStringLiteral("/media/live");
  const QString liveUuid = CollectionUtils::computeCollectionUuid(live.name, live.mediaDirectory);

  QSqlDatabase insp = openInspector();
  QVERIFY(insp.isValid() && insp.isOpen());
  QVERIFY(runSql(insp, "DELETE FROM items"));
  QVERIFY(runSql(insp, "DELETE FROM collections"));
  // The purge is gated on a hash of the live-uuid set stamped into meta
  // (Kartend-dbqt5); this suite's sandbox db survives across runs, so clear
  // the stamp or a re-run with the same deterministic live set would skip.
  QVERIFY(runSql(insp, "DELETE FROM meta WHERE key='orphan_purge_uuid_set_hash'"));
  QVERIFY(runSql(insp, QStringLiteral("INSERT INTO collections (id, name, last_scanned, uuid) "
                                      "VALUES (1, 'Live', 'x', '%1'), "
                                      "(2, 'Stale', 'x', 'orphan-uuid')")
                           .arg(liveUuid)));
  // Two live items, one orphan, one with an empty uuid — all orphans
  // except the live pair.
  QVERIFY(runSql(insp, QStringLiteral("INSERT INTO items (collection_id, path, name, "
                                      "last_modified, collection_uuid) VALUES "
                                      "(1, '/m/a.bin', 'a', 'x', '%1'), "
                                      "(1, '/m/b.bin', 'b', 'x', '%1'), "
                                      "(2, '/m/c.bin', 'c', 'x', 'orphan-uuid'), "
                                      "(1, '/m/d.bin', 'd', 'x', '')")
                           .arg(liveUuid)));
  // Child-table rows under the orphan uuid must be purged too
  // (Kartend-yyudl); previously they leaked forever. Clear first so a
  // suite re-run against the surviving sandbox db can't collide on keys.
  QVERIFY(runSql(insp, "DELETE FROM item_metadata WHERE collection_uuid='orphan-uuid'"));
  QVERIFY(runSql(insp, "DELETE FROM item_artwork WHERE collection_uuid='orphan-uuid'"));
  QVERIFY(runSql(insp, "DELETE FROM launch_history WHERE collection_uuid='orphan-uuid'"));
  QVERIFY(runSql(insp, "DELETE FROM playlist_items WHERE playlist_id='pp1'"));
  QVERIFY(runSql(insp, "DELETE FROM playlists WHERE id='pp1'"));
  QVERIFY(runSql(insp, "INSERT INTO item_metadata (collection_uuid, path, title) "
                       "VALUES ('orphan-uuid', '/m/c.bin', 't')"));
  QVERIFY(runSql(insp, "INSERT INTO item_artwork (collection_uuid, path, artwork_type, "
                       "manual_path, updated_at) "
                       "VALUES ('orphan-uuid', '/m/c.bin', 'cover', '/x.png', 'x')"));
  QVERIFY(runSql(insp, "INSERT INTO launch_history (collection_uuid, path, name, launched_at) "
                       "VALUES ('orphan-uuid', '/m/c.bin', 'c', 'x')"));
  QVERIFY(runSql(insp, "INSERT INTO playlists (id, name, parent_collection_uuid) "
                       "VALUES ('pp1', 'PL', 'orphan-uuid')"));
  QVERIFY(runSql(insp, "INSERT INTO playlist_items (playlist_id, position, "
                       "source_collection_uuid, source_path, added_at) "
                       "VALUES ('pp1', 0, 'orphan-uuid', '/m/c.bin', 'x')"));

  db.purgeOrphanCollectionData({live});

  // Kartend-dbqt5: the purge now runs as a queued write on the DB worker
  // thread — wait for it to land instead of asserting synchronously.
  QTRY_COMPARE_WITH_TIMEOUT(scalar(insp, "SELECT COUNT(*) FROM items"), 2, 10000);
  QCOMPARE(
      scalar(insp,
             QStringLiteral("SELECT COUNT(*) FROM items WHERE collection_uuid='%1'").arg(liveUuid)),
      2);
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM collections"), 1);
  // Child tables purged of the orphan uuid (Kartend-yyudl).
  for (const char *t : {"item_metadata", "item_artwork", "launch_history"}) {
    QCOMPARE(
        scalar(
            insp,
            QStringLiteral("SELECT COUNT(*) FROM %1 WHERE collection_uuid='orphan-uuid'").arg(t)),
        0);
  }
  QCOMPARE(scalar(insp,
                  "SELECT COUNT(*) FROM playlist_items WHERE source_collection_uuid='orphan-uuid'"),
           0);

  // Gate behavior (Kartend-dbqt5): with the live set unchanged, a second
  // purge call is skipped — a freshly re-inserted orphan row survives it.
  QVERIFY(runSql(insp, "INSERT INTO items (collection_id, path, name, last_modified, "
                       "collection_uuid) VALUES (2, '/m/e.bin', 'e', 'x', 'orphan-uuid')"));
  db.purgeOrphanCollectionData({live});
  // Queue an observable write BEHIND the (skipped) purge op — worker FIFO
  // ordering guarantees the purge decision happened once this lands.
  db.recordItemLaunch(liveUuid, QStringLiteral("/m/a.bin"));
  QTRY_COMPARE_WITH_TIMEOUT(
      scalar(insp, QStringLiteral("SELECT play_count FROM items WHERE collection_uuid='%1' "
                                  "AND path='/m/a.bin'")
                       .arg(liveUuid)),
      1, 10000);
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM items WHERE collection_uuid='orphan-uuid'"), 1);

  // A CHANGED live set (different hash) re-arms the gate and purges again.
  CollectionConfig second;
  second.name = QStringLiteral("Second");
  second.mediaDirectory = QStringLiteral("/media/second");
  const QString secondUuid =
      CollectionUtils::computeCollectionUuid(second.name, second.mediaDirectory);
  QVERIFY(runSql(insp, QStringLiteral("INSERT INTO collections (id, name, last_scanned, uuid) "
                                      "VALUES (3, 'Second', 'x', '%1')")
                           .arg(secondUuid)));
  db.purgeOrphanCollectionData({live, second});
  QTRY_COMPARE_WITH_TIMEOUT(
      scalar(insp, "SELECT COUNT(*) FROM items WHERE collection_uuid='orphan-uuid'"), 0, 10000);
}

void TestDatabaseManager::testPurgeOrphanCollectionData_largeLiveSetUsesTempTable() {
  m_session = std::make_unique<SessionManager>();
  auto appCtx = makeCtxWithSession(m_session.get());
  DatabaseManager db(&appCtx);
  db.initDatabase();

  QSqlDatabase insp = openInspector();
  QVERIFY(insp.isValid() && insp.isOpen());
  QVERIFY(runSql(insp, "DELETE FROM items"));
  QVERIFY(runSql(insp, "DELETE FROM collections"));
  // See testPurgeOrphanCollectionData_dropsRowsNotInLiveSet — clear the
  // Kartend-dbqt5 gate stamp so a suite re-run against the surviving
  // sandbox db doesn't skip the purge.
  QVERIFY(runSql(insp, "DELETE FROM meta WHERE key='orphan_purge_uuid_set_hash'"));

  // More live collections than the inline-NOT-IN threshold, so the purge takes
  // the temp-table path. With one bind per uuid the old inline form would
  // approach SQLite's 999-variable cap here and the DELETE would fail, leaving
  // orphans behind.
  constexpr int kLiveCount = KartendDb::BatchSizes::UuidListBatch + 100; // 600 > 500
  QList<CollectionConfig> liveCollections;
  liveCollections.reserve(kLiveCount);
  QStringList collRows;
  QStringList itemRows;
  for (int i = 0; i < kLiveCount; ++i) {
    CollectionConfig c;
    c.name = QStringLiteral("Coll%1").arg(i);
    c.mediaDirectory = QStringLiteral("/media/coll%1").arg(i);
    const QString uuid = CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory);
    liveCollections.append(c);
    collRows << QStringLiteral("(%1, 'Coll%2', 'x', '%3')").arg(i + 1).arg(i).arg(uuid);
    itemRows << QStringLiteral("(%1, '/m/%2.bin', 'n%2', 'x', '%3')").arg(i + 1).arg(i).arg(uuid);
  }
  // One orphan collection + item that must be purged.
  collRows << QStringLiteral("(999999, 'Orphan', 'x', 'orphan-uuid')");
  itemRows << QStringLiteral("(999999, '/m/orphan.bin', 'orphan', 'x', 'orphan-uuid')");

  QVERIFY(runSql(insp, QStringLiteral("INSERT INTO collections (id, name, last_scanned, uuid) "
                                      "VALUES ") +
                           collRows.join(QStringLiteral(", "))));
  QVERIFY(runSql(insp, QStringLiteral("INSERT INTO items (collection_id, path, name, "
                                      "last_modified, collection_uuid) VALUES ") +
                           itemRows.join(QStringLiteral(", "))));
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM collections"), kLiveCount + 1);

  db.purgeOrphanCollectionData(liveCollections);

  // Every live collection + item survives; the single orphan pair is purged —
  // i.e. the temp-table NOT IN (SELECT …) form handled all the uuids without
  // tripping the bind-variable cap. (QTRY: the purge is a queued worker
  // write now, Kartend-dbqt5.)
  QTRY_COMPARE_WITH_TIMEOUT(scalar(insp, "SELECT COUNT(*) FROM collections"), kLiveCount, 10000);
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM items"), kLiveCount);
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM collections WHERE uuid='orphan-uuid'"), 0);
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM items WHERE collection_uuid='orphan-uuid'"), 0);
}

void TestDatabaseManager::testRecordItemLaunch_survivesHeldWriteLock() {
  // Kartend-cbtml: recordItemLaunch / recordHistoryEntry run on the query
  // worker connection (busy_timeout 500ms) while a scan transaction on the
  // SEPARATE scan-worker connection can hold the SQLite write lock for
  // longer. Pre-fix, the worker write failed once with "database is locked"
  // and was dropped silently — the launch never landed in play_count or the
  // history log. The fix retries with bounded backoff in
  // QueryManager::runWrite. Simulate the scan's lock with an inspector
  // connection holding BEGIN IMMEDIATE past the 500ms busy window.
  m_session = std::make_unique<SessionManager>();
  auto appCtx = makeCtxWithSession(m_session.get());
  DatabaseManager db(&appCtx);
  db.initDatabase();

  QSqlDatabase insp = openInspector();
  QVERIFY(insp.isValid() && insp.isOpen());
  QVERIFY(runSql(insp, "DELETE FROM items"));
  QVERIFY(runSql(insp, "DELETE FROM collections"));
  QVERIFY(runSql(insp, "DELETE FROM launch_history"));
  const QString uuid = QStringLiteral("locked-write-uuid");
  QVERIFY(runSql(insp, QStringLiteral("INSERT INTO collections (id, name, last_scanned, uuid) "
                                      "VALUES (1, 'Locked', 'x', '%1')")
                           .arg(uuid)));
  QVERIFY(runSql(insp, QStringLiteral("INSERT INTO items (collection_id, path, name, "
                                      "last_modified, collection_uuid) "
                                      "VALUES (1, '/m/locked.bin', 'locked', 'x', '%1')")
                           .arg(uuid)));

  // Take the write lock and keep it held well past the worker connection's
  // 500ms busy_timeout, so the first write attempt deterministically fails
  // with SQLITE_BUSY (the pre-fix code dropped the write right there).
  QVERIFY(runSql(insp, "BEGIN IMMEDIATE"));
  db.recordItemLaunch(uuid, QStringLiteral("/m/locked.bin"));
  db.recordHistoryEntry(uuid, QStringLiteral("/m/locked.bin"), QStringLiteral("locked"),
                        /*maxEntries=*/50);
  QTest::qWait(1200); // worker is inside the retry ladder during this window
  QVERIFY(runSql(insp, "COMMIT"));

  // With the retry ladder both writes land once the lock clears.
  QTRY_COMPARE_WITH_TIMEOUT(
      scalar(insp, QStringLiteral("SELECT play_count FROM items WHERE collection_uuid='%1' "
                                  "AND path='/m/locked.bin'")
                       .arg(uuid)),
      1, 10000);
  QTRY_COMPARE_WITH_TIMEOUT(
      scalar(insp, QStringLiteral("SELECT COUNT(*) FROM launch_history WHERE collection_uuid='%1'")
                       .arg(uuid)),
      1, 10000);
}

void TestDatabaseManager::testRecordItemLaunch_survivesWriteLockOutlastingInlineLadder() {
  // Kartend-rctcv: the sibling test above holds the lock for 1.2s, comfortably
  // inside runWrite's INLINE ladder (5 attempts x 500ms busy_timeout + 1.5s of
  // backoff ~= 4s). A real scan transaction can outlast that, and when it did,
  // the inline ladder exhausted and the write was DROPPED — permanently, with
  // only a log line. That is what failed CI on macOS Release: playCount stayed
  // 0 and no later retry ever recovered it.
  //
  // Hold the lock PAST the inline budget so the write can only survive via the
  // deferred requeue ladder (1/2/4/8/16s on the worker's event loop). Pre-fix
  // this test fails; the write is gone by ~4s and no timeout can wait it back.
  m_session = std::make_unique<SessionManager>();
  auto appCtx = makeCtxWithSession(m_session.get());
  DatabaseManager db(&appCtx);
  db.initDatabase();

  QSqlDatabase insp = openInspector();
  QVERIFY(insp.isValid() && insp.isOpen());
  QVERIFY(runSql(insp, "DELETE FROM items"));
  QVERIFY(runSql(insp, "DELETE FROM collections"));
  QVERIFY(runSql(insp, "DELETE FROM launch_history"));
  const QString uuid = QStringLiteral("long-locked-write-uuid");
  QVERIFY(runSql(insp, QStringLiteral("INSERT INTO collections (id, name, last_scanned, uuid) "
                                      "VALUES (1, 'LongLocked', 'x', '%1')")
                           .arg(uuid)));
  QVERIFY(runSql(insp, QStringLiteral("INSERT INTO items (collection_id, path, name, "
                                      "last_modified, collection_uuid) "
                                      "VALUES (1, '/m/longlocked.bin', 'longlocked', 'x', '%1')")
                           .arg(uuid)));

  QVERIFY(runSql(insp, "BEGIN IMMEDIATE"));
  db.recordItemLaunch(uuid, QStringLiteral("/m/longlocked.bin"));
  db.recordHistoryEntry(uuid, QStringLiteral("/m/longlocked.bin"), QStringLiteral("longlocked"),
                        /*maxEntries=*/50);
  // 6s > the ~4s inline budget, so the inline ladder is guaranteed to exhaust
  // and hand off to the deferred rungs (which retry at ~5s, ~7.5s, ~12s, ~20s,
  // ~36s — any release before ~36s is picked up, so a slow runner that stretches
  // the schedule still lands the write rather than flaking).
  QTest::qWait(6000);
  QVERIFY(runSql(insp, "COMMIT"));

  // Both writes land on a deferred rung once the lock clears.
  QTRY_COMPARE_WITH_TIMEOUT(
      scalar(insp, QStringLiteral("SELECT play_count FROM items WHERE collection_uuid='%1' "
                                  "AND path='/m/longlocked.bin'")
                       .arg(uuid)),
      1, 45000);
  QTRY_COMPARE_WITH_TIMEOUT(
      scalar(insp, QStringLiteral("SELECT COUNT(*) FROM launch_history WHERE collection_uuid='%1'")
                       .arg(uuid)),
      1, 45000);
}

void TestDatabaseManager::testRecordItemLaunch_deferredWriteKeepsItsLaunchTimeOrder() {
  // Kartend-neb85: end-to-end proof that a deferred write records WHEN THE
  // LAUNCH HAPPENED, not when its SQL finally ran. Launch A goes into a held
  // write lock and is requeued past the inline ladder; launch B is issued only
  // after the lock clears. A therefore lands in the table AFTER B, but happened
  // BEFORE it — so A's last_played must still sort earlier. Pre-fix the stores
  // stamped at execution time and A would read as the newer launch.
  m_session = std::make_unique<SessionManager>();
  auto appCtx = makeCtxWithSession(m_session.get());
  DatabaseManager db(&appCtx);
  db.initDatabase();

  QSqlDatabase insp = openInspector();
  QVERIFY(insp.isValid() && insp.isOpen());
  QVERIFY(runSql(insp, "DELETE FROM items"));
  QVERIFY(runSql(insp, "DELETE FROM collections"));
  QVERIFY(runSql(insp, "DELETE FROM launch_history"));
  const QString uuid = QStringLiteral("stamp-order-uuid");
  QVERIFY(runSql(insp, QStringLiteral("INSERT INTO collections (id, name, last_scanned, uuid) "
                                      "VALUES (1, 'StampOrder', 'x', '%1')")
                           .arg(uuid)));
  for (const char *p : {"/m/first.bin", "/m/second.bin"}) {
    QVERIFY(runSql(insp, QStringLiteral("INSERT INTO items (collection_id, path, name, "
                                        "last_modified, collection_uuid) "
                                        "VALUES (1, '%1', 'n', 'x', '%2')")
                             .arg(QString::fromLatin1(p), uuid)));
  }

  // A: issued into a held lock, so its write is deferred past the ~4s inline
  // ladder. launched_at is stamped NOW, at queue time.
  QVERIFY(runSql(insp, "BEGIN IMMEDIATE"));
  db.recordItemLaunch(uuid, QStringLiteral("/m/first.bin"));
  db.recordHistoryEntry(uuid, QStringLiteral("/m/first.bin"), QStringLiteral("first"),
                        /*maxEntries=*/50);
  // Well past the inline budget AND past launched_at's one-second resolution,
  // so A and B cannot collide on the same timestamp string.
  QTest::qWait(6000);
  QVERIFY(runSql(insp, "COMMIT"));

  // B: issued after the lock cleared, so it writes promptly — and is genuinely
  // the later launch.
  db.recordItemLaunch(uuid, QStringLiteral("/m/second.bin"));
  db.recordHistoryEntry(uuid, QStringLiteral("/m/second.bin"), QStringLiteral("second"),
                        /*maxEntries=*/50);

  const QString playCountSql =
      QStringLiteral("SELECT play_count FROM items WHERE collection_uuid='%1' AND path='%2'");
  QTRY_COMPARE_WITH_TIMEOUT(scalar(insp, playCountSql.arg(uuid, "/m/first.bin")), 1, 45000);
  QTRY_COMPARE_WITH_TIMEOUT(scalar(insp, playCountSql.arg(uuid, "/m/second.bin")), 1, 45000);
  QTRY_COMPARE_WITH_TIMEOUT(
      scalar(insp, QStringLiteral("SELECT COUNT(*) FROM launch_history WHERE collection_uuid='%1'")
                       .arg(uuid)),
      2, 45000);

  const QString lastPlayedSql =
      QStringLiteral("SELECT last_played FROM items WHERE collection_uuid='%1' AND path='%2'");
  const QString aPlayed = scalarText(insp, lastPlayedSql.arg(uuid, "/m/first.bin"));
  const QString bPlayed = scalarText(insp, lastPlayedSql.arg(uuid, "/m/second.bin"));
  QVERIFY(!aPlayed.isEmpty() && !bPlayed.isEmpty());
  // ISO-8601 UTC is fixed-width, so lexicographic order IS chronological order.
  QVERIFY2(aPlayed < bPlayed,
           qPrintable(QStringLiteral("deferred launch stamped at/after the later launch: "
                                     "A=%1 B=%2")
                          .arg(aPlayed, bPlayed)));

  // Same guarantee on the history log, which additionally has to ORDER BY
  // launched_at rather than id for this to surface correctly (Kartend-neb85).
  const QString aHist = scalarText(
      insp,
      QStringLiteral("SELECT launched_at FROM launch_history WHERE path='%1'").arg("/m/first.bin"));
  const QString bHist =
      scalarText(insp, QStringLiteral("SELECT launched_at FROM launch_history WHERE path='%1'")
                           .arg("/m/second.bin"));
  QVERIFY(!aHist.isEmpty() && !bHist.isEmpty());
  QVERIFY2(aHist < bHist, qPrintable(QStringLiteral("deferred history row stamped at/after the "
                                                    "later launch: A=%1 B=%2")
                                         .arg(aHist, bHist)));
}

QTEST_MAIN(TestDatabaseManager)
#include "test_databasemanager.moc"
