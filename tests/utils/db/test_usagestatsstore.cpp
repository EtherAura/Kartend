// Tests for UsageStatsStore.
//
// Uses an in-memory SQLite database with the full migration chain applied so
// the schema mirrors what the live app sees.
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QTest>

#include "dbmigrations.h"
#include "usagestatsstore.h"

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

auto insertItem(QSqlDatabase &db, const QString &uuid, const QString &path, const QString &name)
    -> void {
  QSqlQuery q(db);
  q.prepare("INSERT INTO items (collection_uuid, path, name, last_modified) "
            "VALUES (?, ?, ?, '2026-01-01')");
  q.addBindValue(uuid);
  q.addBindValue(path);
  q.addBindValue(name);
  QVERIFY(q.exec());
}

} // namespace

class TestUsageStatsStore : public QObject {
  Q_OBJECT
private slots:
  void loadForItem_returnsZeroForUnseenItem();
  void recordLaunch_incrementsCountAndStampsTimestamp();
  void recordPlaySession_accumulatesSeconds();
  void recordPlaySession_rejectsNonPositiveDuration();
  void loadAggregate_summarizesAcrossLibrary();
  void loadTopPlayed_filtersUnplayedAndSortsDescending();
  void loadRecentlyPlayed_filtersUnplayedAndSortsByLastPlayed();
  void loadNeverPlayed_returnsItemsWithZeroOrNullPlayCount();
  void countPlayedSince_filtersByCutoff();
  void loadCollectionBreakdown_groupsByUuid();
  void resetAll_clearsAllTrackingColumns();
  void formatDuration_handlesAllRanges();
  void formatTimestamp_returnsInputForUnparseable();
};

void TestUsageStatsStore::loadForItem_returnsZeroForUnseenItem() {
  const QString conn = "usage_zero";
  auto db = openMemoryDb(conn);
  seedDatabase(db);

  auto result = UsageStatsStore::loadForItem(db, "u1", "/missing");
  QVERIFY(result.isOk());
  const auto stats = result.value();
  QCOMPARE(stats.playCount, qint64(0));
  QVERIFY(stats.lastPlayed.isEmpty());
  QCOMPARE(stats.totalPlaySeconds, qint64(0));
  QVERIFY(stats.isEmpty());

  closeAndRemove(db, conn);
}

void TestUsageStatsStore::recordLaunch_incrementsCountAndStampsTimestamp() {
  const QString conn = "usage_launch";
  auto db = openMemoryDb(conn);
  seedDatabase(db);
  insertItem(db, "u1", "/g/1", "Game 1");

  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/1").isOk());
  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/1").isOk());
  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/1").isOk());

  auto stats = UsageStatsStore::loadForItem(db, "u1", "/g/1").value();
  QCOMPARE(stats.playCount, qint64(3));
  QVERIFY(!stats.lastPlayed.isEmpty());
  // ISO-8601 round-trip parses successfully.
  QVERIFY(QDateTime::fromString(stats.lastPlayed, Qt::ISODate).isValid());

  closeAndRemove(db, conn);
}

void TestUsageStatsStore::recordPlaySession_accumulatesSeconds() {
  const QString conn = "usage_session";
  auto db = openMemoryDb(conn);
  seedDatabase(db);
  insertItem(db, "u1", "/g/1", "Game 1");

  QVERIFY(UsageStatsStore::recordPlaySession(db, "u1", "/g/1", 60).isOk());
  QVERIFY(UsageStatsStore::recordPlaySession(db, "u1", "/g/1", 30).isOk());
  QVERIFY(UsageStatsStore::recordPlaySession(db, "u1", "/g/1", 10).isOk());

  auto stats = UsageStatsStore::loadForItem(db, "u1", "/g/1").value();
  QCOMPARE(stats.totalPlaySeconds, qint64(100));

  closeAndRemove(db, conn);
}

void TestUsageStatsStore::recordPlaySession_rejectsNonPositiveDuration() {
  const QString conn = "usage_session_negative";
  auto db = openMemoryDb(conn);
  seedDatabase(db);
  insertItem(db, "u1", "/g/1", "Game 1");

  // Zero or negative durations are noisy noise from racy QProcess teardown;
  // the store rejects them rather than writing 0/negative SUMs into the column.
  auto zeroResult = UsageStatsStore::recordPlaySession(db, "u1", "/g/1", 0);
  QVERIFY(zeroResult.isError());
  auto negativeResult = UsageStatsStore::recordPlaySession(db, "u1", "/g/1", -5);
  QVERIFY(negativeResult.isError());

  auto stats = UsageStatsStore::loadForItem(db, "u1", "/g/1").value();
  QCOMPARE(stats.totalPlaySeconds, qint64(0));

  closeAndRemove(db, conn);
}

void TestUsageStatsStore::loadAggregate_summarizesAcrossLibrary() {
  const QString conn = "usage_aggregate";
  auto db = openMemoryDb(conn);
  seedDatabase(db);
  insertItem(db, "u1", "/g/1", "Game 1");
  insertItem(db, "u1", "/g/2", "Game 2");
  insertItem(db, "u2", "/m/1", "Movie 1");

  // /g/1 launched 3x with 100s; /g/2 launched 1x; /m/1 never launched.
  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/1").isOk());
  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/1").isOk());
  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/1").isOk());
  QVERIFY(UsageStatsStore::recordPlaySession(db, "u1", "/g/1", 100).isOk());
  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/2").isOk());

  auto agg = UsageStatsStore::loadAggregate(db).value();
  QCOMPARE(agg.totalItems, qint64(3));
  QCOMPARE(agg.totalLaunches, qint64(4));
  QCOMPARE(agg.totalPlaySeconds, qint64(100));
  QCOMPARE(agg.itemsLaunchedAtLeastOnce, qint64(2));

  closeAndRemove(db, conn);
}

void TestUsageStatsStore::loadTopPlayed_filtersUnplayedAndSortsDescending() {
  const QString conn = "usage_top";
  auto db = openMemoryDb(conn);
  seedDatabase(db);
  insertItem(db, "u1", "/g/1", "Game 1");
  insertItem(db, "u1", "/g/2", "Game 2");
  insertItem(db, "u1", "/g/3", "Game 3"); // never launched

  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/1").isOk());
  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/2").isOk());
  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/2").isOk());

  auto rows = UsageStatsStore::loadTopPlayed(db, 10).value();
  QCOMPARE(rows.size(), 2);
  QCOMPARE(rows[0].path, QStringLiteral("/g/2"));
  QCOMPARE(rows[0].playCount, qint64(2));
  QCOMPARE(rows[1].path, QStringLiteral("/g/1"));
  QCOMPARE(rows[1].playCount, qint64(1));

  closeAndRemove(db, conn);
}

void TestUsageStatsStore::loadRecentlyPlayed_filtersUnplayedAndSortsByLastPlayed() {
  const QString conn = "usage_recent";
  auto db = openMemoryDb(conn);
  seedDatabase(db);
  insertItem(db, "u1", "/g/1", "Game 1");
  insertItem(db, "u1", "/g/2", "Game 2");

  // Direct timestamp writes so the test order is deterministic — recordLaunch
  // would stamp both with the same wall-clock second on a fast machine.
  QSqlQuery q(db);
  q.prepare("UPDATE items SET play_count = 1, last_played = ? WHERE path = ?");
  q.addBindValue("2026-04-18T12:00:00Z");
  q.addBindValue("/g/1");
  QVERIFY(q.exec());
  q.prepare("UPDATE items SET play_count = 1, last_played = ? WHERE path = ?");
  q.addBindValue("2026-04-19T08:30:00Z");
  q.addBindValue("/g/2");
  QVERIFY(q.exec());

  auto rows = UsageStatsStore::loadRecentlyPlayed(db, 10).value();
  QCOMPARE(rows.size(), 2);
  QCOMPARE(rows[0].path, QStringLiteral("/g/2"));
  QCOMPARE(rows[1].path, QStringLiteral("/g/1"));

  closeAndRemove(db, conn);
}

void TestUsageStatsStore::loadNeverPlayed_returnsItemsWithZeroOrNullPlayCount() {
  const QString conn = "usage_never";
  auto db = openMemoryDb(conn);
  seedDatabase(db);
  insertItem(db, "u1", "/g/1", "Game 1"); // launched -> excluded
  insertItem(db, "u1", "/g/2", "Game 2"); // never -> included (play_count default 0)
  insertItem(db, "u1", "/g/3", "Game 3"); // null override -> included
  insertItem(db, "u1", "/g/4", "Game 4"); // never -> included

  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/1").isOk());

  // Force one row to NULL so the IS NULL branch of the query is exercised
  // (legacy rows from before the v1 migration could land here).
  QSqlQuery q(db);
  QVERIFY(q.exec("UPDATE items SET play_count = NULL WHERE path = '/g/3'"));

  auto rows = UsageStatsStore::loadNeverPlayed(db, 10).value();
  QCOMPARE(rows.size(), 3);
  // Alphabetical by name.
  QCOMPARE(rows[0].name, QStringLiteral("Game 2"));
  QCOMPARE(rows[1].name, QStringLiteral("Game 3"));
  QCOMPARE(rows[2].name, QStringLiteral("Game 4"));

  closeAndRemove(db, conn);
}

void TestUsageStatsStore::countPlayedSince_filtersByCutoff() {
  const QString conn = "usage_played_since";
  auto db = openMemoryDb(conn);
  seedDatabase(db);
  insertItem(db, "u1", "/g/old", "Old");
  insertItem(db, "u1", "/g/recent", "Recent");
  insertItem(db, "u1", "/g/never", "Never");

  // Direct timestamp writes so cutoff comparisons are deterministic.
  QSqlQuery q(db);
  q.prepare("UPDATE items SET play_count = 1, last_played = ? WHERE path = ?");
  q.addBindValue("2026-01-01T00:00:00Z");
  q.addBindValue("/g/old");
  QVERIFY(q.exec());
  q.prepare("UPDATE items SET play_count = 1, last_played = ? WHERE path = ?");
  q.addBindValue("2026-05-10T12:00:00Z");
  q.addBindValue("/g/recent");
  QVERIFY(q.exec());

  // Cutoff that captures only the recent row.
  QCOMPARE(UsageStatsStore::countPlayedSince(db, "2026-05-01T00:00:00Z").value(), qint64(1));
  // Cutoff before everything captures both played rows but never the unplayed one.
  QCOMPARE(UsageStatsStore::countPlayedSince(db, "2025-01-01T00:00:00Z").value(), qint64(2));
  // Cutoff after everything captures nothing.
  QCOMPARE(UsageStatsStore::countPlayedSince(db, "2027-01-01T00:00:00Z").value(), qint64(0));
  // Empty cutoff short-circuits to zero without hitting the DB.
  QCOMPARE(UsageStatsStore::countPlayedSince(db, QString()).value(), qint64(0));

  closeAndRemove(db, conn);
}

void TestUsageStatsStore::loadCollectionBreakdown_groupsByUuid() {
  const QString conn = "usage_breakdown";
  auto db = openMemoryDb(conn);
  seedDatabase(db);
  insertItem(db, "u1", "/g/1", "Game 1");
  insertItem(db, "u1", "/g/2", "Game 2");
  insertItem(db, "u2", "/m/1", "Movie 1");

  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/1").isOk());
  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/2").isOk());
  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/2").isOk());
  QVERIFY(UsageStatsStore::recordPlaySession(db, "u1", "/g/1", 90).isOk());
  QVERIFY(UsageStatsStore::recordLaunch(db, "u2", "/m/1").isOk());

  auto byUuid = UsageStatsStore::loadCollectionBreakdown(db).value();
  QCOMPARE(byUuid.size(), 2);

  const auto u1 = byUuid.value("u1");
  QCOMPARE(u1.itemCount, qint64(2));
  QCOMPARE(u1.totalLaunches, qint64(3));
  QCOMPARE(u1.totalPlaySeconds, qint64(90));

  const auto u2 = byUuid.value("u2");
  QCOMPARE(u2.itemCount, qint64(1));
  QCOMPARE(u2.totalLaunches, qint64(1));
  QCOMPARE(u2.totalPlaySeconds, qint64(0));

  closeAndRemove(db, conn);
}

void TestUsageStatsStore::resetAll_clearsAllTrackingColumns() {
  const QString conn = "usage_reset";
  auto db = openMemoryDb(conn);
  seedDatabase(db);
  insertItem(db, "u1", "/g/1", "Game 1");

  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/1").isOk());
  QVERIFY(UsageStatsStore::recordPlaySession(db, "u1", "/g/1", 200).isOk());

  QVERIFY(UsageStatsStore::resetAll(db).isOk());

  auto stats = UsageStatsStore::loadForItem(db, "u1", "/g/1").value();
  QCOMPARE(stats.playCount, qint64(0));
  QVERIFY(stats.lastPlayed.isEmpty());
  QCOMPARE(stats.totalPlaySeconds, qint64(0));

  closeAndRemove(db, conn);
}

void TestUsageStatsStore::formatDuration_handlesAllRanges() {
  QCOMPARE(UsageStatsStore::formatDuration(0), QString());
  QCOMPARE(UsageStatsStore::formatDuration(-5), QString());
  QCOMPARE(UsageStatsStore::formatDuration(45), QStringLiteral("45s"));
  QCOMPARE(UsageStatsStore::formatDuration(75), QStringLiteral("1m 15s"));
  QCOMPARE(UsageStatsStore::formatDuration(3661), QStringLiteral("1h 01m"));
}

void TestUsageStatsStore::formatTimestamp_returnsInputForUnparseable() {
  // Empty -> empty.
  QCOMPARE(UsageStatsStore::formatTimestamp(QString()), QString());
  // Garbage -> input verbatim (defensive fallback).
  QCOMPARE(UsageStatsStore::formatTimestamp("not a date"), QStringLiteral("not a date"));
  // Valid ISO -> non-empty (locale-dependent, so just check non-empty).
  QVERIFY(!UsageStatsStore::formatTimestamp("2026-04-18T12:00:00Z").isEmpty());
}

QTEST_MAIN(TestUsageStatsStore)
#include "test_usagestatsstore.moc"
