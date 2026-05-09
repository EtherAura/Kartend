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
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "collectionutils.h"
#include "databasemanager.h"
#include "sessionmanager.h"

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

private:
  std::unique_ptr<SessionManager> m_session;
};

void TestDatabaseManager::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("Kartend"));
  QCoreApplication::setApplicationName(
      QStringLiteral("kartend-test-databasemanager"));
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
    DatabaseManager db(m_session.get());
    Q_UNUSED(db);
  }
  // If we get here, the destructor returned within its bounded wait without
  // crashing or qFataling on a still-running QThread.
  QVERIFY(true);
}

void TestDatabaseManager::testRepeatedConstructDestruct_doesNotLeakConnection() {
  // The destructor calls QSqlDatabase::removeDatabase(connectionName).
  // Repeated construct/destruct cycles must not accumulate stale entries
  // in the connection pool.
  m_session = std::make_unique<SessionManager>();

  const QString connName = QStringLiteral("kartend_main");
  for (int i = 0; i < 3; ++i) {
    DatabaseManager db(m_session.get());
    Q_UNUSED(db);
  }

  // After all destructors have run, the connection must not still be
  // registered.
  QVERIFY2(!QSqlDatabase::contains(connName),
           "DatabaseManager destructor leaked the SQL connection registration");
}

void TestDatabaseManager::testDestructAfterInitDatabase_closesConnectionCleanly() {
  m_session = std::make_unique<SessionManager>();
  const QString connName = QStringLiteral("kartend_main");
  {
    DatabaseManager db(m_session.get());
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
  collection.includeContentSubfolders = false;
  collection.extensions = {QStringLiteral("bin")};

  QList<CollectionConfig> allCollections;
  allCollections.append(collection);

  CollectionContext ctx;
  ctx.currentIndex = 0;
  ctx.config = collection;

  auto *db = new DatabaseManager(m_session.get());
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
           qPrintable(QStringLiteral(
                          "DatabaseManager destructor took %1 ms during active "
                          "scan — bounded-wait fallback may be regressing")
                          .arg(elapsedMs)));

  // The connection must be cleaned up regardless of whether the worker
  // threads were waited or leaked.
  QVERIFY2(!QSqlDatabase::contains(QStringLiteral("kartend_main")),
           "DatabaseManager destructor did not remove the SQL connection "
           "after a mid-scan teardown");
}

// ─────────────────────────────────────────────────────────────────────────────
// Path resolution (thread-safe helpers)
// ─────────────────────────────────────────────────────────────────────────────

void TestDatabaseManager::testResolveFilePath_absolutePathPassthrough() {
  m_session = std::make_unique<SessionManager>();
  DatabaseManager db(m_session.get());

  CollectionContext ctx;
  ctx.currentIndex = 0;

  // Absolute paths must be returned unchanged regardless of context state.
  const QString absPath = QStringLiteral("/tmp/some/absolute/file.bin");
  QCOMPARE(db.resolveFilePath(absPath, ctx), absPath);
}

void TestDatabaseManager::testResolveRelativeFilePath_emptyMapReturnsEmpty() {
  m_session = std::make_unique<SessionManager>();
  DatabaseManager db(m_session.get());

  // No mapping available -> empty string (caller treats as "not resolvable").
  QHash<QString, QString> emptyMap;
  QCOMPARE(db.resolveRelativeFilePath(QStringLiteral("foo.bin"), emptyMap),
           QString());
}

void TestDatabaseManager::testResolveRelativeFilePath_resolvesViaMap() {
  m_session = std::make_unique<SessionManager>();
  DatabaseManager db(m_session.get());

  QHash<QString, QString> fileNames;
  fileNames.insert(QStringLiteral("/abs/path/to/foo.bin"),
                   QStringLiteral("foo.bin"));
  fileNames.insert(QStringLiteral("/abs/path/to/bar.bin"),
                   QStringLiteral("bar.bin"));

  // Looking up by display name should return the absolute path stored as the
  // key in the reverse-lookup map.
  const QString resolved =
      db.resolveRelativeFilePath(QStringLiteral("foo.bin"), fileNames);
  QCOMPARE(resolved, QStringLiteral("/abs/path/to/foo.bin"));
}

QTEST_MAIN(TestDatabaseManager)
#include "test_databasemanager.moc"
