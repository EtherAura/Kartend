// Tests for FileHashCache (schema v16 file_hash_cache).
//
// Uses an in-memory SQLite database seeded with the minimal base schema, then
// run through the real DbMigrations so the v16 table is created exactly as in
// production. No DB mocking, no filesystem for the store itself (real temp
// files are used only to exercise hashFileCached end-to-end).

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "dbmigrations.h"
#include "filehashcache.h"
#include "romhasher.h"

namespace {

auto openMemoryDb(const QString &connName) -> QSqlDatabase {
  auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
  db.setDatabaseName(QStringLiteral(":memory:"));
  if (!db.open()) {
    qWarning("Failed to open in-memory db: %s", qPrintable(db.lastError().text()));
  }
  return db;
}

// Minimal pre-migration schema the v1+ migrations expect to find (mirrors
// tests/utils/db/test_dbmigrations.cpp).
auto createBaseSchema(QSqlDatabase &db) -> void {
  QSqlQuery q(db);
  QVERIFY(q.exec(QStringLiteral("CREATE TABLE collections (id INTEGER PRIMARY KEY, name TEXT)")));
  QVERIFY(q.exec(QStringLiteral("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, "
                                "path TEXT, last_modified TEXT)")));
}

auto rowCount(QSqlDatabase &db) -> int {
  QSqlQuery q(db);
  if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM file_hash_cache")) || !q.next()) {
    return -1;
  }
  return q.value(0).toInt();
}

} // namespace

class TestFileHashCache : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void lookupMissOnEmptyTable();
  void storeThenLookupHit();
  void lookupMissOnStaleSize();
  void lookupMissOnStaleMtime();
  void storeReplacesStaleRow();
  void hashFileCachedComputesThenServesFromCache();
  void hashFileCachedReHashesWhenFileChanges();
  void clearAllEmptiesTable();

private:
  QSqlDatabase m_db;
  QString m_conn;
  static int s_counter;

  static QString writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes);
};

int TestFileHashCache::s_counter = 0;

QString TestFileHashCache::writeFile(const QTemporaryDir &dir, const QString &name,
                                     const QByteArray &bytes) {
  const QString path = dir.filePath(name);
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    return {};
  }
  f.write(bytes);
  f.close();
  return path;
}

void TestFileHashCache::init() {
  m_conn = QStringLiteral("test_filehashcache_%1").arg(s_counter++);
  m_db = openMemoryDb(m_conn);
  QVERIFY(m_db.isOpen());
  createBaseSchema(m_db);
  DbMigrations::applySchemaMigrations(m_db, QStringLiteral("test_filehashcache"));
}

void TestFileHashCache::cleanup() {
  m_db.close();
  m_db = QSqlDatabase();
  QSqlDatabase::removeDatabase(m_conn);
}

void TestFileHashCache::lookupMissOnEmptyTable() {
  auto hit = FileHashCache::lookup(m_db, QStringLiteral("/no/such/file"), 10, 20);
  QVERIFY(!hit.has_value());
}

void TestFileHashCache::storeThenLookupHit() {
  const auto res =
      FileHashCache::store(m_db, QStringLiteral("/a/b.bin"), 1234, 5678, QStringLiteral("aabbccdd"),
                           QStringLiteral("md5hex"), QStringLiteral("sha1hex"));
  QVERIFY(res.isOk());

  auto hit = FileHashCache::lookup(m_db, QStringLiteral("/a/b.bin"), 1234, 5678);
  QVERIFY(hit.has_value());
  QCOMPARE(hit->size, qint64(1234));
  QCOMPARE(hit->crc, QStringLiteral("aabbccdd"));
  QCOMPARE(hit->md5, QStringLiteral("md5hex"));
  QCOMPARE(hit->sha1, QStringLiteral("sha1hex"));
}

void TestFileHashCache::lookupMissOnStaleSize() {
  QVERIFY(FileHashCache::store(m_db, QStringLiteral("/a/b.bin"), 1234, 5678, QStringLiteral("crc"),
                               QStringLiteral("md5"), QStringLiteral("sha1"))
              .isOk());
  // Same path + mtime, different size => stale => miss.
  QVERIFY(!FileHashCache::lookup(m_db, QStringLiteral("/a/b.bin"), 9999, 5678).has_value());
}

void TestFileHashCache::lookupMissOnStaleMtime() {
  QVERIFY(FileHashCache::store(m_db, QStringLiteral("/a/b.bin"), 1234, 5678, QStringLiteral("crc"),
                               QStringLiteral("md5"), QStringLiteral("sha1"))
              .isOk());
  // Same path + size, different mtime => stale => miss.
  QVERIFY(!FileHashCache::lookup(m_db, QStringLiteral("/a/b.bin"), 1234, 1111).has_value());
}

void TestFileHashCache::storeReplacesStaleRow() {
  QVERIFY(FileHashCache::store(m_db, QStringLiteral("/a/b.bin"), 1, 1, QStringLiteral("old"),
                               QStringLiteral("old"), QStringLiteral("old"))
              .isOk());
  QVERIFY(FileHashCache::store(m_db, QStringLiteral("/a/b.bin"), 2, 2, QStringLiteral("new"),
                               QStringLiteral("new"), QStringLiteral("new"))
              .isOk());
  QVERIFY(!FileHashCache::lookup(m_db, QStringLiteral("/a/b.bin"), 1, 1).has_value());
  auto hit = FileHashCache::lookup(m_db, QStringLiteral("/a/b.bin"), 2, 2);
  QVERIFY(hit.has_value());
  QCOMPARE(hit->crc, QStringLiteral("new"));
  QCOMPARE(rowCount(m_db), 1); // REPLACE, not a second row.
}

void TestFileHashCache::hashFileCachedComputesThenServesFromCache() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QByteArray content = QByteArrayLiteral("hello dat audit world");
  const QString path = writeFile(dir, QStringLiteral("rom.bin"), content);
  QVERIFY(!path.isEmpty());

  auto first = FileHashCache::hashFileCached(m_db, path);
  QVERIFY(first.isOk());
  const QString expectedSha1 =
      QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha1).toHex());
  QCOMPARE(first.value().sha1, expectedSha1);
  QCOMPARE(first.value().size, qint64(content.size()));
  QCOMPARE(rowCount(m_db), 1);

  // Poison the cached sha1, then re-request: an unchanged (size,mtime) must be
  // served straight from the cache, proving the second call did NOT re-hash.
  const QString canonical = QFileInfo(path).canonicalFilePath();
  QSqlQuery poison(m_db);
  poison.prepare(QStringLiteral("UPDATE file_hash_cache SET sha1 = ? WHERE path = ?"));
  poison.addBindValue(QStringLiteral("POISONED"));
  poison.addBindValue(canonical);
  QVERIFY(poison.exec());

  auto second = FileHashCache::hashFileCached(m_db, path);
  QVERIFY(second.isOk());
  QCOMPARE(second.value().sha1, QStringLiteral("POISONED"));
}

void TestFileHashCache::hashFileCachedReHashesWhenFileChanges() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = writeFile(dir, QStringLiteral("rom.bin"), QByteArrayLiteral("version one"));
  QVERIFY(!path.isEmpty());

  auto first = FileHashCache::hashFileCached(m_db, path);
  QVERIFY(first.isOk());
  const QString crc1 = first.value().crc;

  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    // Different length => the (size) half of the key changes on its own; also
    // bump mtime so the cache is invalidated even if a future edit kept the
    // size identical.
    f.write(QByteArrayLiteral("a completely different, longer payload"));
    f.setFileTime(QDateTime::currentDateTime().addSecs(5), QFileDevice::FileModificationTime);
    f.close();
  }

  auto second = FileHashCache::hashFileCached(m_db, path);
  QVERIFY(second.isOk());
  QVERIFY2(second.value().crc != crc1, "changed file content should yield a different CRC");
}

void TestFileHashCache::clearAllEmptiesTable() {
  QVERIFY(FileHashCache::store(m_db, QStringLiteral("/a"), 1, 1, QStringLiteral("c"),
                               QStringLiteral("m"), QStringLiteral("s"))
              .isOk());
  FileHashCache::clearAll(m_db);
  QCOMPARE(rowCount(m_db), 0);
}

QTEST_MAIN(TestFileHashCache)
#include "test_filehashcache.moc"
