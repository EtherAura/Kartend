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
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "applicationcontext.h"
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

private:
  std::unique_ptr<SessionManager> m_session;
};

void TestDatabaseManager::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("Kartend"));
  QCoreApplication::setApplicationName(QStringLiteral("kartend-test-databasemanager"));
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

  // Looking up by display name should return the absolute path stored as the
  // key in the reverse-lookup map.
  const QString resolved = db.resolveRelativeFilePath(QStringLiteral("foo.bin"), fileNames);
  QCOMPARE(resolved, QStringLiteral("/abs/path/to/foo.bin"));
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
  return db;
}

bool runSql(QSqlDatabase &db, const QString &sql) {
  QSqlQuery q(db);
  return q.exec(sql);
}

int scalar(QSqlDatabase &db, const QString &sql) {
  QSqlQuery q(db);
  return (q.exec(sql) && q.next()) ? q.value(0).toInt() : -1;
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
                        "playlist_items", "playlists"}) {
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

  db.migrateCollectionUuid(QStringLiteral("old-uuid"), QStringLiteral("new-uuid"));

  // Nothing left under the old uuid anywhere.
  for (const char *t : {"items", "item_metadata", "item_artwork", "launch_history"}) {
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

  db.purgeOrphanCollectionData({live});

  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM items"), 2);
  QCOMPARE(
      scalar(insp,
             QStringLiteral("SELECT COUNT(*) FROM items WHERE collection_uuid='%1'").arg(liveUuid)),
      2);
  QCOMPARE(scalar(insp, "SELECT COUNT(*) FROM collections"), 1);
}

QTEST_MAIN(TestDatabaseManager)
#include "test_databasemanager.moc"
