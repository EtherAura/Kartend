// Self-test for the shared MigratedDb fixture: both backings must come up
// fully bootstrapped (production createTables + migrations), isolated per
// instance, and clean up their named connections.

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>
#include <QVariant>

#include "migrateddb.h"

class TestMigratedDb : public QObject {
  Q_OBJECT
private slots:
  void onDiskComesUpMigrated();
  void inMemoryComesUpMigrated();
  void instancesAreIsolated();
  void connectionIsRemovedOnDestruction();
};

namespace {
int userVersion(const QSqlDatabase &db) {
  QSqlQuery q(db);
  if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next()) return -1;
  return q.value(0).toInt();
}
bool hasTable(const QSqlDatabase &db, const QString &name) {
  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?"));
  q.addBindValue(name);
  return q.exec() && q.next();
}
} // namespace

void TestMigratedDb::onDiskComesUpMigrated() {
  KartendTest::MigratedDb db;
  QVERIFY(db.isOpen());
  QVERIFY(!db.databasePath().isEmpty());
  // The production bootstrap ran: base tables exist and the migration chain
  // stamped a non-zero user_version.
  QVERIFY(hasTable(db.database(), QStringLiteral("items")));
  QVERIFY(hasTable(db.database(), QStringLiteral("item_metadata")));
  QVERIFY(userVersion(db.database()) > 0);
}

void TestMigratedDb::inMemoryComesUpMigrated() {
  KartendTest::MigratedDb db(KartendTest::MigratedDb::InMemory);
  QVERIFY(db.isOpen());
  QVERIFY(db.databasePath().isEmpty());
  QVERIFY(hasTable(db.database(), QStringLiteral("items")));
  QVERIFY(userVersion(db.database()) > 0);
}

void TestMigratedDb::instancesAreIsolated() {
  KartendTest::MigratedDb a(KartendTest::MigratedDb::InMemory);
  KartendTest::MigratedDb b(KartendTest::MigratedDb::InMemory);
  QVERIFY(a.isOpen() && b.isOpen());
  QVERIFY(a.connectionName() != b.connectionName());
  // The production pragmas enforce foreign keys, so the item needs a parent
  // collections row — exactly as it would in the app.
  QSqlQuery ins(a.database());
  QVERIFY(ins.exec(QStringLiteral(
      "INSERT INTO collections (id, name, last_scanned) VALUES (1, 'C', '1970-01-01')")));
  QVERIFY(ins.exec(QStringLiteral("INSERT INTO items (collection_id, path, name, last_modified) "
                                  "VALUES (1, '/x', 'x', '2026-01-01T00:00:00')")));
  QSqlQuery cnt(b.database());
  QVERIFY(cnt.exec(QStringLiteral("SELECT COUNT(*) FROM items")) && cnt.next());
  QCOMPARE(cnt.value(0).toInt(), 0);
}

void TestMigratedDb::connectionIsRemovedOnDestruction() {
  QString name;
  {
    KartendTest::MigratedDb db(KartendTest::MigratedDb::InMemory);
    QVERIFY(db.isOpen());
    name = db.connectionName();
    QVERIFY(QSqlDatabase::contains(name));
  }
  QVERIFY(!QSqlDatabase::contains(name));
}

QTEST_MAIN(TestMigratedDb)
#include "test_migrateddb.moc"
