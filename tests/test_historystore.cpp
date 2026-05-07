// Tests for HistoryStore (Kartend-fse).
//
// Uses an in-memory SQLite database with the full migration chain applied so
// the schema mirrors what the live app sees.
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QTest>

#include "dbmigrations.h"
#include "historystore.h"

namespace {

auto openMemoryDb(const QString &connName) -> QSqlDatabase {
  auto db = QSqlDatabase::addDatabase("QSQLITE", connName);
  db.setDatabaseName(":memory:");
  if (!db.open()) {
    qWarning("Failed to open in-memory db: %s", qPrintable(db.lastError().text()));
  }
  return db;
}

auto closeAndRemove(QSqlDatabase &db, const QString &connName) -> void {
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connName);
}

auto seedDatabase(QSqlDatabase &db) -> void {
  QSqlQuery q(db);
  QVERIFY(q.exec("CREATE TABLE collections (id INTEGER PRIMARY KEY, name TEXT NOT NULL)"));
  QVERIFY(q.exec("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
                 "path TEXT NOT NULL, last_modified TEXT)"));
  DbMigrations::applySchemaMigrations(db, "test");
}

} // namespace

class TestHistoryStore : public QObject {
  Q_OBJECT
private slots:
  void recordLaunch_appendsRowAndStampsTimestamp();
  void recordLaunch_allowsDuplicateUuidPath();
  void recordLaunch_rejectsEmptyKeys();
  void recordLaunch_fallsBackToBasenameWhenNameEmpty();
  void loadRecent_returnsNewestFirst();
  void loadRecent_clampsLimit();
  void count_returnsCurrentRowCount();
  void trimToMaxEntries_dropsOldestRowsOnly();
  void trimToMaxEntries_isNoopWhenWithinCap();
  void trimToMaxEntries_unlimitedForNonPositiveCap();
  void clearAll_wipesEverything();
};

void TestHistoryStore::recordLaunch_appendsRowAndStampsTimestamp() {
  const QString conn = "history_record";
  auto db = openMemoryDb(conn);
  seedDatabase(db);

  QVERIFY(HistoryStore::recordLaunch(db, "u1", "/g/1", "Game 1").isOk());

  auto rows = HistoryStore::loadRecent(db, 10).value();
  QCOMPARE(rows.size(), 1);
  QCOMPARE(rows[0].collectionUuid, QStringLiteral("u1"));
  QCOMPARE(rows[0].path, QStringLiteral("/g/1"));
  QCOMPARE(rows[0].name, QStringLiteral("Game 1"));
  QVERIFY(!rows[0].launchedAt.isEmpty());
  QVERIFY(QDateTime::fromString(rows[0].launchedAt, Qt::ISODate).isValid());

  closeAndRemove(db, conn);
}

void TestHistoryStore::recordLaunch_allowsDuplicateUuidPath() {
  // The history table is append-only — relaunches must produce distinct
  // rows or the dialog can't show "I played X, then Y, then X again".
  const QString conn = "history_dup";
  auto db = openMemoryDb(conn);
  seedDatabase(db);

  QVERIFY(HistoryStore::recordLaunch(db, "u1", "/g/1", "Game 1").isOk());
  QVERIFY(HistoryStore::recordLaunch(db, "u1", "/g/1", "Game 1").isOk());
  QVERIFY(HistoryStore::recordLaunch(db, "u1", "/g/1", "Game 1").isOk());

  QCOMPARE(HistoryStore::count(db).value(), qint64(3));

  closeAndRemove(db, conn);
}

void TestHistoryStore::recordLaunch_rejectsEmptyKeys() {
  const QString conn = "history_empty";
  auto db = openMemoryDb(conn);
  seedDatabase(db);

  QVERIFY(HistoryStore::recordLaunch(db, "", "/g/1", "Game").isError());
  QVERIFY(HistoryStore::recordLaunch(db, "u1", "", "Game").isError());
  QCOMPARE(HistoryStore::count(db).value(), qint64(0));

  closeAndRemove(db, conn);
}

void TestHistoryStore::recordLaunch_fallsBackToBasenameWhenNameEmpty() {
  const QString conn = "history_basename";
  auto db = openMemoryDb(conn);
  seedDatabase(db);

  QVERIFY(HistoryStore::recordLaunch(db, "u1", "/some/path/Awesome.iso", QString()).isOk());

  auto rows = HistoryStore::loadRecent(db, 10).value();
  QCOMPARE(rows.size(), 1);
  QCOMPARE(rows[0].name, QStringLiteral("Awesome"));

  closeAndRemove(db, conn);
}

void TestHistoryStore::loadRecent_returnsNewestFirst() {
  const QString conn = "history_order";
  auto db = openMemoryDb(conn);
  seedDatabase(db);

  QVERIFY(HistoryStore::recordLaunch(db, "u1", "/g/1", "First").isOk());
  QVERIFY(HistoryStore::recordLaunch(db, "u1", "/g/2", "Second").isOk());
  QVERIFY(HistoryStore::recordLaunch(db, "u1", "/g/3", "Third").isOk());

  auto rows = HistoryStore::loadRecent(db, 10).value();
  QCOMPARE(rows.size(), 3);
  // Newest first.
  QCOMPARE(rows[0].name, QStringLiteral("Third"));
  QCOMPARE(rows[1].name, QStringLiteral("Second"));
  QCOMPARE(rows[2].name, QStringLiteral("First"));
  // Ids are monotonic so a later row always has a larger id.
  QVERIFY(rows[0].id > rows[1].id);
  QVERIFY(rows[1].id > rows[2].id);

  closeAndRemove(db, conn);
}

void TestHistoryStore::loadRecent_clampsLimit() {
  const QString conn = "history_clamp";
  auto db = openMemoryDb(conn);
  seedDatabase(db);

  for (int i = 0; i < 5; ++i) {
    QVERIFY(HistoryStore::recordLaunch(db, "u1", QStringLiteral("/g/%1").arg(i), "n").isOk());
  }

  // Limit clamps to >= 1 internally; passing 0 must not return everything
  // or zero — it lands at 1 and returns one row.
  auto rows = HistoryStore::loadRecent(db, 0).value();
  QCOMPARE(rows.size(), 1);

  closeAndRemove(db, conn);
}

void TestHistoryStore::count_returnsCurrentRowCount() {
  const QString conn = "history_count";
  auto db = openMemoryDb(conn);
  seedDatabase(db);

  QCOMPARE(HistoryStore::count(db).value(), qint64(0));
  QVERIFY(HistoryStore::recordLaunch(db, "u1", "/g/1", "n").isOk());
  QVERIFY(HistoryStore::recordLaunch(db, "u1", "/g/2", "n").isOk());
  QCOMPARE(HistoryStore::count(db).value(), qint64(2));

  closeAndRemove(db, conn);
}

void TestHistoryStore::trimToMaxEntries_dropsOldestRowsOnly() {
  const QString conn = "history_trim";
  auto db = openMemoryDb(conn);
  seedDatabase(db);

  for (int i = 1; i <= 5; ++i) {
    QVERIFY(HistoryStore::recordLaunch(db, "u1", QStringLiteral("/g/%1").arg(i),
                                        QStringLiteral("Game %1").arg(i))
                .isOk());
  }

  // Keep the most recent 2 rows; trim should remove 3.
  auto removed = HistoryStore::trimToMaxEntries(db, 2);
  QVERIFY(removed.isOk());
  QCOMPARE(removed.value(), qint64(3));

  auto rows = HistoryStore::loadRecent(db, 10).value();
  QCOMPARE(rows.size(), 2);
  QCOMPARE(rows[0].name, QStringLiteral("Game 5"));
  QCOMPARE(rows[1].name, QStringLiteral("Game 4"));

  closeAndRemove(db, conn);
}

void TestHistoryStore::trimToMaxEntries_isNoopWhenWithinCap() {
  const QString conn = "history_trim_under";
  auto db = openMemoryDb(conn);
  seedDatabase(db);

  QVERIFY(HistoryStore::recordLaunch(db, "u1", "/g/1", "n").isOk());
  QVERIFY(HistoryStore::recordLaunch(db, "u1", "/g/2", "n").isOk());

  auto removed = HistoryStore::trimToMaxEntries(db, 10);
  QVERIFY(removed.isOk());
  QCOMPARE(removed.value(), qint64(0));
  QCOMPARE(HistoryStore::count(db).value(), qint64(2));

  closeAndRemove(db, conn);
}

void TestHistoryStore::trimToMaxEntries_unlimitedForNonPositiveCap() {
  const QString conn = "history_trim_zero";
  auto db = openMemoryDb(conn);
  seedDatabase(db);

  for (int i = 0; i < 5; ++i) {
    QVERIFY(HistoryStore::recordLaunch(db, "u1", QStringLiteral("/g/%1").arg(i), "n").isOk());
  }

  // Zero / negative caps mean "unlimited" — keep everything.
  QCOMPARE(HistoryStore::trimToMaxEntries(db, 0).value(), qint64(0));
  QCOMPARE(HistoryStore::trimToMaxEntries(db, -7).value(), qint64(0));
  QCOMPARE(HistoryStore::count(db).value(), qint64(5));

  closeAndRemove(db, conn);
}

void TestHistoryStore::clearAll_wipesEverything() {
  const QString conn = "history_clear";
  auto db = openMemoryDb(conn);
  seedDatabase(db);

  QVERIFY(HistoryStore::recordLaunch(db, "u1", "/g/1", "n").isOk());
  QVERIFY(HistoryStore::recordLaunch(db, "u1", "/g/2", "n").isOk());

  QVERIFY(HistoryStore::clearAll(db).isOk());
  QCOMPARE(HistoryStore::count(db).value(), qint64(0));

  closeAndRemove(db, conn);
}

QTEST_MAIN(TestHistoryStore)
#include "test_historystore.moc"
