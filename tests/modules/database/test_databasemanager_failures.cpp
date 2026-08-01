/**
 * @file test_databasemanager_failures.cpp
 * @brief Failure-injection tests for the DatabaseManager init + collection
 *        maintenance paths.
 *
 * The happy path is covered by test_databasemanager.cpp; these cases force
 * the error branches that otherwise never execute in CI:
 *
 *  - initDatabase() with an unopenable media.db (a directory squatting on
 *    the filename, and a zero-permission file): the open fails, the error is
 *    logged, and every query API degrades to a guarded no-op — no crash, no
 *    half-open state, clean connection teardown.
 *  - initDatabase() over a file of garbage bytes: SQLite opens lazily so the
 *    connection reports open, but the first statement fails with "file is
 *    not a database". Recovery (Kartend-kcakv): the corruption probe detects
 *    it, the damaged file is quarantined under media.db.corrupt-<timestamp>
 *    (renamed, never deleted — the bytes may be hand-salvageable), a fresh
 *    schema is created in its place, and a one-time errorOccurred announces
 *    the reset so the UI can tell the user a rescan is coming.
 *  - the clearCollectionFromDatabaseByUuid throw branch, reached through the
 *    public updateCachedCounts entry point (an unexpandable media directory
 *    routes into the clear) with the items table dropped out from under it
 *    via an inspector connection: the exec failure throws, the catch
 *    classifies it as a non-lock error, and the transaction rolls back — the
 *    collections row must survive untouched.
 *
 * Real SQLite throughout (no DB mocking, per docs/dev/testing.md); the
 * injections manipulate the on-disk file / schema, not the code under test.
 */

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

#include "../../support/inspectordb.h"
#include "../../support/testsandbox.h"
#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "databasemanager.h"
#include "sessionmanager.h"

namespace {

ApplicationContext makeCtxWithSession(SessionManager *session) {
  ApplicationContext ctx;
  ctx.managers.sessionManager = session;
  return ctx;
}

/// Absolute path of the media.db the manager will open inside this suite's
/// QStandardPaths sandbox.
QString mediaDbPath() {
  const QString dbDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return QDir(dbDir).absoluteFilePath(QStringLiteral("media.db"));
}

/// Removes whatever currently sits at the media.db path (file or directory),
/// restoring owner permissions first so a zero-permission leftover can't
/// wedge the sandbox for later cases / reruns.
void removeMediaDbArtifact() {
  const QString path = mediaDbPath();
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  QFile::remove(path);
  QDir(path).removeRecursively();
  // WAL side files from a previous healthy run of this sandbox.
  QFile::remove(path + QStringLiteral("-wal"));
  QFile::remove(path + QStringLiteral("-shm"));
  // Quarantine files from the recovery path (Kartend-kcakv) — stale ones
  // would break the exactly-one assertion on a rerun.
  const QDir dir(QFileInfo(path).absolutePath());
  const QStringList stale = dir.entryList({QStringLiteral("media.db.corrupt-*")}, QDir::Files);
  for (const QString &name : stale) {
    QFile::remove(dir.absoluteFilePath(name));
  }
}

} // namespace

class TestDatabaseManagerFailures : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanup();

  void directoryAsDbFile_openFailsAndQueriesDegrade();
  void unreadableDbFile_openFailsAndQueriesDegrade();
  void garbageBytesDbFile_isQuarantinedAndRecreated();
  void clearCollection_droppedTableRollsBackTransaction();

private:
  std::unique_ptr<SessionManager> m_session;
};

void TestDatabaseManagerFailures::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-databasemanager-failures"));
  // Make sure the sandbox's AppData dir exists so the cases can plant their
  // poisoned media.db before the first manager construction.
  QVERIFY(QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)));
}

void TestDatabaseManagerFailures::cleanup() {
  m_session.reset();
  removeMediaDbArtifact();
}

void TestDatabaseManagerFailures::directoryAsDbFile_openFailsAndQueriesDegrade() {
  // A directory squatting on the media.db filename: sqlite3_open_v2 cannot
  // open it, so DatabaseSchema::openConnection fails and initDatabase bails
  // after logging.
  removeMediaDbArtifact();
  QVERIFY(QDir().mkpath(mediaDbPath()));

  m_session = std::make_unique<SessionManager>();
  QString connName;
  {
    auto appCtx = makeCtxWithSession(m_session.get());
    DatabaseManager db(&appCtx);
    connName = db.connectionName();

    // The connection is registered but never opened.
    QVERIFY(QSqlDatabase::contains(connName));
    QVERIFY(!QSqlDatabase::database(connName, /*open=*/false).isOpen());

    // Every guarded API degrades to its empty/no-op contract instead of
    // crashing on the dead connection.
    QVERIFY(db.loadAllItemPathsForCollection(QStringLiteral("any-uuid")).isEmpty());
    QVERIFY(db.loadCollectionLastScanned(QStringLiteral("any-uuid")).isNull());
    db.migrateCollectionUuid(QStringLiteral("a"), QStringLiteral("b"));
  }
  // Teardown must still deregister the never-opened connection.
  QVERIFY(!QSqlDatabase::contains(connName));
}

void TestDatabaseManagerFailures::unreadableDbFile_openFailsAndQueriesDegrade() {
#if defined(Q_OS_UNIX)
  if (::geteuid() == 0) {
    QSKIP("root bypasses file permissions");
  }
  // A media.db with all permission bits stripped: open fails with
  // SQLITE_CANTOPEN and the manager must degrade exactly like the
  // directory case.
  removeMediaDbArtifact();
  {
    QFile f(mediaDbPath());
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();
  }
  QVERIFY(QFile::setPermissions(mediaDbPath(), QFileDevice::Permissions()));

  m_session = std::make_unique<SessionManager>();
  QString connName;
  {
    auto appCtx = makeCtxWithSession(m_session.get());
    DatabaseManager db(&appCtx);
    connName = db.connectionName();

    QVERIFY(QSqlDatabase::contains(connName));
    QVERIFY(!QSqlDatabase::database(connName, /*open=*/false).isOpen());
    QVERIFY(db.loadAllItemPathsForCollection(QStringLiteral("any-uuid")).isEmpty());
    QVERIFY(db.loadCollectionLastScanned(QStringLiteral("any-uuid")).isNull());
  }
  QVERIFY(!QSqlDatabase::contains(connName));
#else
  QSKIP("POSIX permission semantics; not meaningful on this platform");
#endif
}

void TestDatabaseManagerFailures::garbageBytesDbFile_isQuarantinedAndRecreated() {
  // A media.db full of non-SQLite bytes. sqlite3_open is lazy, so the
  // connection opens; the first statement then fails with SQLITE_NOTADB.
  // Recovery (Kartend-kcakv): initDatabase's corruption probe quarantines
  // the file (rename, never delete), recreates the schema, and announces
  // the reset once through errorOccurred. Before this path existed, the app
  // ran indefinitely with a dead database returning empty results and no
  // user-visible explanation.
  removeMediaDbArtifact();
  const QByteArray garbage =
      QByteArrayLiteral("This is definitely not a SQLite database.\n").repeated(64);
  {
    QFile f(mediaDbPath());
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(garbage), qint64(garbage.size()));
    f.close();
  }

  m_session = std::make_unique<SessionManager>();
  QString connName;
  {
    auto appCtx = makeCtxWithSession(m_session.get());
    DatabaseManager db(&appCtx);
    connName = db.connectionName();

    // The announcement is deferred through a zero-timer (initDatabase runs
    // in the constructor, before a real host could wire errorOccurred), so
    // a spy attached right after construction still catches it on the first
    // event-loop spin.
    QSignalSpy errorSpy(&db, &DatabaseManager::errorOccurred);
    QTRY_COMPARE(errorSpy.count(), 1);
    const auto announced = errorSpy.at(0).at(0).value<ErrorUtils::ErrorContext>();
    QCOMPARE(announced.code, ErrorUtils::ErrorCode::DatabaseCorruptQuarantined);

    // The recreated database is fully usable: empty results now mean "empty
    // database", not "dead database".
    QVERIFY(QSqlDatabase::contains(connName));
    QVERIFY(QSqlDatabase::database(connName, /*open=*/false).isOpen());
    QVERIFY(db.loadAllItemPathsForCollection(QStringLiteral("any-uuid")).isEmpty());
    QVERIFY(db.loadCollectionLastScanned(QStringLiteral("any-uuid")).isNull());
    db.migrateCollectionUuid(QStringLiteral("a"), QStringLiteral("b"));
  }
  QVERIFY(!QSqlDatabase::contains(connName));

  // media.db is a real SQLite database again...
  {
    QFile f(mediaDbPath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(f.readAll().startsWith(QByteArrayLiteral("SQLite format 3")));
  }
  // ...and the damaged bytes were preserved for hand-salvage, not deleted.
  const QDir dbDir(QFileInfo(mediaDbPath()).absolutePath());
  const QStringList quarantined =
      dbDir.entryList({QStringLiteral("media.db.corrupt-*")}, QDir::Files);
  QCOMPARE(quarantined.size(), 1);
  QFile q(dbDir.absoluteFilePath(quarantined.first()));
  QVERIFY(q.open(QIODevice::ReadOnly));
  QCOMPARE(q.readAll(), garbage);
}

void TestDatabaseManagerFailures::clearCollection_droppedTableRollsBackTransaction() {
  // Healthy database, then the items table is dropped through an inspector
  // connection mid-flight (stand-in for external corruption / a hostile
  // schema change). updateCachedCounts routes a collection whose media
  // directory fails path expansion into clearCollectionFromDatabaseByUuid
  // (uuid computed against the empty expanded dir); with the table gone the
  // DELETE throws inside the transaction, the catch must classify it as a
  // non-lock error, and the guard rolls back — proven by the collections
  // row surviving.
  removeMediaDbArtifact();
  m_session = std::make_unique<SessionManager>();
  auto appCtx = makeCtxWithSession(m_session.get());
  DatabaseManager db(&appCtx);
  db.initDatabase();

  CollectionConfig doomed;
  doomed.name = QStringLiteral("Doomed");
  // Non-existent directory: validateAndExpandPath returns empty, which is
  // exactly the updateCachedCounts condition that triggers the clear.
  doomed.mediaDirectory = QStringLiteral("/kartend-test-no-such-dir/media");
  const QString uuid = CollectionUtils::computeCollectionUuid(doomed.name, QString());

  {
    KartendTest::InspectorDb insp(mediaDbPath(), QStringLiteral("test_dbmgr_failures_inspect"));
    QVERIFY(insp.isOpen());
    QSqlQuery q(insp.db());
    QVERIFY(q.exec(QStringLiteral("INSERT INTO collections (id, name, last_scanned, uuid) "
                                  "VALUES (1, 'Doomed', 'x', '%1')")
                       .arg(uuid)));
    QVERIFY(q.exec(QStringLiteral("DROP TABLE items")));

    db.updateCachedCounts({doomed});

    // Rolled back, not half-applied: the collections DELETE never ran
    // because the items DELETE threw first.
    QSqlQuery check(insp.db());
    QVERIFY(
        check.exec(QStringLiteral("SELECT COUNT(*) FROM collections WHERE uuid='%1'").arg(uuid)));
    QVERIFY(check.next());
    QCOMPARE(check.value(0).toInt(), 1);
  }

  // Repair the schema for whatever runs against this sandbox next (the
  // suite db survives across runs).
  db.initDatabase();
}

QTEST_MAIN(TestDatabaseManagerFailures)
#include "test_databasemanager_failures.moc"
