// Round-trip persistence tests for ScanService (Kartend-7vubk).
//
// tests/integration/test_scanservice.cpp covers only the filesystem walk
// (scanMediaDirectory) and the cancel flag. The persist/reconcile half —
// saveItemsToDatabase -> commitStagedScanResults / prepareCollectionForItemsInsert
// and the deleteItemsMissingFromScan prune step — was untested.
//
// These tests drive the full scan -> persist -> prune pipeline against a REAL
// on-disk SQLite database (the project forbids DB mocking) seeded with the
// production schema (DatabaseSchema::createTables/createIndexes — exactly what
// DatabaseManager::initDatabase runs). ScanService persistence is standalone:
// it only borrows a QSqlDatabase + a PreparedStatementCache, so no
// DatabaseManager/QueryManager is needed. We assert:
//   1. files -> scan -> rows land in `items`
//   2. delete a file -> rescan -> its row is pruned
//   3. a cancelled save applies nothing — the last committed state is intact
#include <memory>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include "collection/collectionconfig.h"
#include "databaseschema.h"
#include "preparedstatementcache.h"
#include "scanservice.h"

namespace {

// Real on-disk SQLite + the production schema wired to a standalone
// ScanService. Each instance owns a private temp dir for the db file, so
// parallel test processes never share state.
class PersistFixture {
public:
  PersistFixture() {
    m_dbPath = QDir(m_tmp.path()).filePath(QStringLiteral("scan.db"));
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QStringLiteral("test_scanservice_persist"));
    m_db.setDatabaseName(m_dbPath);
    m_opened = m_db.open();
    if (m_opened) {
      // Mirror DatabaseManager::initDatabase: pragmas, then schema + indexes.
      DatabaseSchema::applyConnectionPragmas(m_db);
      DatabaseSchema::createTables(m_db);
      DatabaseSchema::createIndexes(m_db);
    }
    m_cache = std::make_unique<PreparedStatementCache>();
    m_cache->setDatabase(m_db);
    m_svc = std::make_unique<ScanService>(m_db, *m_cache, m_txnDepth);
  }
  ~PersistFixture() {
    // Tear down everything that holds the connection (ScanService's queries
    // and the prepared-statement cache) before removeDatabase, otherwise Qt
    // warns the connection is still in use.
    m_svc.reset();
    m_cache.reset();
    if (m_opened) m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("test_scanservice_persist"));
  }
  PersistFixture(const PersistFixture &) = delete;
  PersistFixture &operator=(const PersistFixture &) = delete;

  ScanService *service() { return m_svc.get(); }
  [[nodiscard]] bool opened() const { return m_opened; }
  QSqlDatabase &db() { return m_db; }

private:
  QTemporaryDir m_tmp;
  QString m_dbPath;
  QSqlDatabase m_db;
  bool m_opened = false;
  std::unique_ptr<PreparedStatementCache> m_cache;
  // Stands in for QueryManager::m_txnDepth (the guard's shared depth counter).
  int m_txnDepth = 0;
  std::unique_ptr<ScanService> m_svc;
};

bool writeFile(const QString &path, const QByteArray &bytes) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.write(bytes);
  f.close();
  return true;
}

// The test db holds a single collection, so an unfiltered count is the
// collection's item count.
int itemCount(QSqlDatabase &db) {
  QSqlQuery q(db);
  if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM items")) || !q.next()) return -1;
  return q.value(0).toInt();
}

QSet<QString> itemBaseNames(QSqlDatabase &db) {
  QSet<QString> names;
  QSqlQuery q(db);
  if (q.exec(QStringLiteral("SELECT path FROM items"))) {
    while (q.next()) {
      names.insert(QFileInfo(q.value(0).toString()).fileName());
    }
  }
  return names;
}

CollectionConfig makeConfig(const QString &mediaDir) {
  CollectionConfig cfg;
  cfg.name = QStringLiteral("RoundTrip");
  cfg.mediaDirectory = mediaDir;
  cfg.extensions = QStringList{QStringLiteral("bin")};
  return cfg;
}

// Full scan + save round trip, exactly as the app's load-or-scan path does:
// scanMediaDirectory returns the relative paths + timestamps + signature that
// saveItemsToDatabase consumes verbatim.
void scanAndSave(ScanService *svc, const CollectionConfig &cfg) {
  QHash<QString, QDateTime> timestamps;
  QString signature;
  const QStringList found = svc->scanMediaDirectory(cfg, timestamps, &signature);
  svc->saveItemsToDatabase(/*collectionIndex=*/0, found, timestamps, cfg, signature);
}

} // namespace

class TestScanServicePersist : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void scanThenSave_persistsMatchingFiles();
  void deleteThenRescan_prunesMissingFile();
  void cancelledSave_leavesCommittedStateIntact();
  void emptiedDirectoryRescan_prunesAllAndStampsMetadata();
  void missingDirectoryScan_reportsNotCompleted();
  void fileSize_persistsAndUpdatesOnRescan();
};

void TestScanServicePersist::initTestCase() {
  // Keep any incidental QStandardPaths use off the real profile; the db/media
  // themselves live in per-test QTemporaryDirs.
  QStandardPaths::setTestModeEnabled(true);
}

void TestScanServicePersist::scanThenSave_persistsMatchingFiles() {
  PersistFixture fx;
  QVERIFY2(fx.opened(), "failed to open + seed the test SQLite database");

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir dir(media.path());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("game-a.bin")), "a"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("game-b.bin")), "bb"));
  // Wrong extension — must not be persisted.
  QVERIFY(writeFile(dir.filePath(QStringLiteral("art.png")), "png"));

  scanAndSave(fx.service(), makeConfig(media.path()));

  QCOMPARE(itemCount(fx.db()), 2);
  QCOMPARE(itemBaseNames(fx.db()),
           (QSet<QString>{QStringLiteral("game-a.bin"), QStringLiteral("game-b.bin")}));
}

void TestScanServicePersist::deleteThenRescan_prunesMissingFile() {
  PersistFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir dir(media.path());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("a.bin")), "a"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("b.bin")), "b"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("c.bin")), "c"));

  const CollectionConfig cfg = makeConfig(media.path());
  scanAndSave(fx.service(), cfg);
  QCOMPARE(itemCount(fx.db()), 3);

  // Remove one file on disk, then rescan: its row must be pruned, the others
  // must survive.
  QVERIFY(QFile::remove(dir.filePath(QStringLiteral("b.bin"))));
  scanAndSave(fx.service(), cfg);

  QCOMPARE(itemCount(fx.db()), 2);
  const QSet<QString> names = itemBaseNames(fx.db());
  QVERIFY2(!names.contains(QStringLiteral("b.bin")), "deleted file's row should be pruned");
  QVERIFY(names.contains(QStringLiteral("a.bin")));
  QVERIFY(names.contains(QStringLiteral("c.bin")));
}

void TestScanServicePersist::cancelledSave_leavesCommittedStateIntact() {
  PersistFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir dir(media.path());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("a.bin")), "a"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("b.bin")), "b"));

  const CollectionConfig cfg = makeConfig(media.path());
  // First scan commits two items.
  scanAndSave(fx.service(), cfg);
  QCOMPARE(itemCount(fx.db()), 2);

  // Add a third file, then request cancellation before the next save. The
  // apply phase is gated on !isScanCancelled(), so saveItemsToDatabase must
  // apply nothing: no new row for c.bin and no prune — the items table stays
  // exactly at the last committed state.
  QVERIFY(writeFile(dir.filePath(QStringLiteral("c.bin")), "c"));
  fx.service()->requestCancelScan();
  QVERIFY(fx.service()->isScanCancelled());
  scanAndSave(fx.service(), cfg);

  QCOMPARE(itemCount(fx.db()), 2);
  const QSet<QString> names = itemBaseNames(fx.db());
  QVERIFY2(!names.contains(QStringLiteral("c.bin")), "a cancelled scan must not apply staged rows");
  QVERIFY(names.contains(QStringLiteral("a.bin")));
  QVERIFY(names.contains(QStringLiteral("b.bin")));
}

void TestScanServicePersist::emptiedDirectoryRescan_prunesAllAndStampsMetadata() {
  // Kartend-fys4o: a directory the user emptied is a COMPLETED zero-file
  // scan — it must persist (prune every stale row + stamp last_scanned) so
  // the items disappear from DB-backed counts and needsRescan stops
  // re-walking the directory on every load.
  PersistFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir dir(media.path());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("a.bin")), "a"));
  QVERIFY(writeFile(dir.filePath(QStringLiteral("b.bin")), "b"));

  const CollectionConfig cfg = makeConfig(media.path());
  scanAndSave(fx.service(), cfg);
  QCOMPARE(itemCount(fx.db()), 2);

  // Empty the directory (it still exists) and rescan, mirroring
  // loadOrScanCollection's gating: persist when non-empty OR completed.
  QVERIFY(QFile::remove(dir.filePath(QStringLiteral("a.bin"))));
  QVERIFY(QFile::remove(dir.filePath(QStringLiteral("b.bin"))));

  QHash<QString, QDateTime> timestamps;
  QString signature;
  bool scanCompleted = false;
  const QStringList found =
      fx.service()->scanMediaDirectory(cfg, timestamps, &signature, &scanCompleted);
  QVERIFY(found.isEmpty());
  QVERIFY2(scanCompleted, "empty result from an existing directory is a completed scan");

  fx.service()->saveItemsToDatabase(/*collectionIndex=*/0, found, timestamps, cfg, signature);

  QCOMPARE(itemCount(fx.db()), 0);
  // Scan metadata was stamped: last_scanned is no longer NULL/empty.
  QSqlQuery q(fx.db());
  QVERIFY(q.exec(QStringLiteral("SELECT last_scanned FROM collections LIMIT 1")));
  QVERIFY(q.next());
  QVERIFY2(!q.value(0).toString().isEmpty(), "last_scanned must be stamped by the empty save");
}

void TestScanServicePersist::missingDirectoryScan_reportsNotCompleted() {
  // Kartend-fys4o: a missing / unmounted directory must NOT be treated as a
  // completed zero-file scan — loadOrScanCollection's gate would otherwise
  // prune the whole collection while a network share is briefly down.
  PersistFixture fx;
  QVERIFY(fx.opened());

  CollectionConfig cfg = makeConfig(QStringLiteral("/nonexistent/kartend-test-missing-dir"));
  QHash<QString, QDateTime> timestamps;
  QString signature;
  bool scanCompleted = true; // must be reset to false by the call
  const QStringList found =
      fx.service()->scanMediaDirectory(cfg, timestamps, &signature, &scanCompleted);
  QVERIFY(found.isEmpty());
  QVERIFY2(!scanCompleted, "missing directory must not report a completed scan");
}

void TestScanServicePersist::fileSize_persistsAndUpdatesOnRescan() {
  // Kartend-3gb7e: the streaming pipeline staged file_size into scanned_items
  // but the commitStagedScanResults INSERT omitted the column, so every row
  // it persisted kept file_size DEFAULT 0 — the ORDER BY file_size fast
  // paths silently degraded to name order and the metadata sort path had to
  // stat-fallback. file_size must land on first scan AND be refreshed by the
  // ON CONFLICT update on rescan (sizes change, unlike date_added).
  PersistFixture fx;
  QVERIFY(fx.opened());

  QTemporaryDir media;
  QVERIFY(media.isValid());
  const QDir dir(media.path());
  QVERIFY(writeFile(dir.filePath(QStringLiteral("a.bin")), "aaa"));   // 3 bytes
  QVERIFY(writeFile(dir.filePath(QStringLiteral("b.bin")), "bbbbb")); // 5 bytes

  const CollectionConfig cfg = makeConfig(media.path());
  scanAndSave(fx.service(), cfg);

  const auto sizeOf = [&fx](const QString &baseName) -> qint64 {
    QSqlQuery q(fx.db());
    q.prepare(QStringLiteral("SELECT file_size FROM items WHERE path LIKE ?"));
    q.addBindValue(QStringLiteral("%/") + baseName);
    if (!q.exec() || !q.next()) return -1;
    return q.value(0).toLongLong();
  };
  QCOMPARE(sizeOf(QStringLiteral("a.bin")), qint64(3));
  QCOMPARE(sizeOf(QStringLiteral("b.bin")), qint64(5));

  // Grow a.bin and rescan: the conflict-update arm must refresh the stored
  // size for the existing row (a stale value is as bad as a missing one).
  QVERIFY(writeFile(dir.filePath(QStringLiteral("a.bin")), "aaaaaaaaaa")); // 10 bytes
  scanAndSave(fx.service(), cfg);
  QCOMPARE(sizeOf(QStringLiteral("a.bin")), qint64(10));
  QCOMPARE(sizeOf(QStringLiteral("b.bin")), qint64(5));
}

QTEST_MAIN(TestScanServicePersist)
#include "test_scanservice_persist.moc"
