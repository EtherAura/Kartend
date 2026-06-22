// Tests for StatisticsService::gather — the off-thread aggregation that backs
// the Statistics dialog. gather() opens its OWN private read-only connection to
// a db FILE path (not a live QSqlDatabase handle), so unlike the per-store
// tests these run against a real on-disk SQLite database: a writer connection
// applies the migration chain and seeds rows, then gather() is pointed at the
// same file and the returned StatsSnapshot is asserted field-by-field. No DB
// mocking (project rule) — every number comes from real SQLite queries.
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include "dbmigrations.h"
#include "historystore.h"
#include "statisticsservice.h"
#include "usagestatsstore.h"

namespace {

// Opens a writer connection to a real on-disk file and applies the full
// migration chain so the schema matches what gather()'s read-only connection
// will later see on the same file.
auto openFileDb(const QString &connName, const QString &path) -> QSqlDatabase {
  auto db = QSqlDatabase::addDatabase("QSQLITE", connName);
  db.setDatabaseName(path);
  if (!db.open()) {
    qWarning("Failed to open file db: %s", qPrintable(db.lastError().text()));
    return db;
  }
  QSqlQuery q(db);
  q.exec("PRAGMA journal_mode = WAL");
  q.exec("CREATE TABLE collections (id INTEGER PRIMARY KEY, name TEXT NOT NULL)");
  q.exec("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
         "path TEXT NOT NULL, last_modified TEXT)");
  DbMigrations::applySchemaMigrations(db, "test");
  return db;
}

auto closeAndRemove(QSqlDatabase &db, const QString &connName) -> void {
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connName);
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

class TestStatisticsService : public QObject {
  Q_OBJECT
private slots:
  void gather_emptyPath_returnsNotOk();
  void gather_aggregatesEveryFigureAcrossLibrary();
  void gather_played7Days_honoursCutoff();
  void gather_listsAndHistory_populated();
};

void TestStatisticsService::gather_emptyPath_returnsNotOk() {
  // An empty path short-circuits before touching SQLite; the caller relies on
  // ok == false to know the worker couldn't read anything.
  const auto snap = StatisticsService::gather(QString(), QStringLiteral("2026-01-01T00:00:00Z"));
  QVERIFY(!snap.ok);
  QCOMPARE(snap.agg.totalItems, qint64(0));
  QCOMPARE(snap.historyCount, qint64(0));
}

void TestStatisticsService::gather_aggregatesEveryFigureAcrossLibrary() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("media.db");
  const QString conn = "stats_aggregate_writer";
  auto db = openFileDb(conn, path);
  QVERIFY(db.isOpen());

  // Two collections. /g/1 launched 3x with 100s; /g/2 launched 1x; /m/1 never.
  insertItem(db, "u1", "/g/1", "Game 1");
  insertItem(db, "u1", "/g/2", "Game 2");
  insertItem(db, "u2", "/m/1", "Movie 1");
  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/1").isOk());
  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/1").isOk());
  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/1").isOk());
  QVERIFY(UsageStatsStore::recordPlaySession(db, "u1", "/g/1", 100).isOk());
  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/2").isOk());

  // Cutoff in the far past so the recent-window counter sees every played row.
  const auto snap = StatisticsService::gather(path, QStringLiteral("2000-01-01T00:00:00Z"));
  QVERIFY(snap.ok);

  // Aggregate header figures.
  QCOMPARE(snap.agg.totalItems, qint64(3));
  QCOMPARE(snap.agg.totalLaunches, qint64(4));
  QCOMPARE(snap.agg.totalPlaySeconds, qint64(100));
  QCOMPARE(snap.agg.itemsLaunchedAtLeastOnce, qint64(2));

  // totalNever is derived in gather() as max(0, totalItems - launchedAtLeastOnce).
  QCOMPARE(snap.totalNever, qint64(1));

  // Per-collection breakdown groups by uuid: u1 holds both games, u2 the movie.
  QCOMPARE(snap.byCollection.size(), 2);
  const auto u1 = snap.byCollection.value("u1");
  QCOMPARE(u1.itemCount, qint64(2));
  QCOMPARE(u1.totalLaunches, qint64(4));
  QCOMPARE(u1.totalPlaySeconds, qint64(100));
  const auto u2 = snap.byCollection.value("u2");
  QCOMPARE(u2.itemCount, qint64(1));
  QCOMPARE(u2.totalLaunches, qint64(0));

  closeAndRemove(db, conn);
}

void TestStatisticsService::gather_played7Days_honoursCutoff() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("media.db");
  const QString conn = "stats_cutoff_writer";
  auto db = openFileDb(conn, path);
  QVERIFY(db.isOpen());

  insertItem(db, "u1", "/g/old", "Old");
  insertItem(db, "u1", "/g/recent", "Recent");
  insertItem(db, "u1", "/g/never", "Never");

  // Direct timestamp writes so the cutoff comparison is deterministic rather
  // than depending on wall-clock at test time.
  QSqlQuery q(db);
  q.prepare("UPDATE items SET play_count = 1, last_played = ? WHERE path = ?");
  q.addBindValue("2026-01-01T00:00:00Z");
  q.addBindValue("/g/old");
  QVERIFY(q.exec());
  q.prepare("UPDATE items SET play_count = 1, last_played = ? WHERE path = ?");
  q.addBindValue("2026-05-10T12:00:00Z");
  q.addBindValue("/g/recent");
  QVERIFY(q.exec());

  // Cutoff between the two played rows: only the recent one falls in the window.
  const auto snap = StatisticsService::gather(path, QStringLiteral("2026-05-01T00:00:00Z"));
  QVERIFY(snap.ok);
  QCOMPARE(snap.played7Days, qint64(1));
  // Two items were played at least once; one never -> totalNever == 1.
  QCOMPARE(snap.agg.itemsLaunchedAtLeastOnce, qint64(2));
  QCOMPARE(snap.totalNever, qint64(1));

  closeAndRemove(db, conn);
}

void TestStatisticsService::gather_listsAndHistory_populated() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("media.db");
  const QString conn = "stats_lists_writer";
  auto db = openFileDb(conn, path);
  QVERIFY(db.isOpen());

  insertItem(db, "u1", "/g/1", "Game 1");
  insertItem(db, "u1", "/g/2", "Game 2");
  insertItem(db, "u1", "/g/3", "Game 3"); // never launched

  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/1").isOk());
  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/2").isOk());
  QVERIFY(UsageStatsStore::recordLaunch(db, "u1", "/g/2").isOk());

  // History is a separate append-only table; gather() rolls its rows + count
  // into the same snapshot.
  QVERIFY(HistoryStore::recordLaunch(db, "u1", "/g/1", "Game 1").isOk());
  QVERIFY(HistoryStore::recordLaunch(db, "u1", "/g/2", "Game 2").isOk());

  const auto snap = StatisticsService::gather(path, QStringLiteral("2000-01-01T00:00:00Z"));
  QVERIFY(snap.ok);

  // Top played: only launched items, descending by play count -> /g/2 first.
  QCOMPARE(snap.topPlayed.size(), 2);
  QCOMPARE(snap.topPlayed[0].path, QStringLiteral("/g/2"));
  QCOMPARE(snap.topPlayed[0].playCount, qint64(2));
  QCOMPARE(snap.topPlayed[1].path, QStringLiteral("/g/1"));

  // Recently played: same two launched rows, never-played excluded.
  QCOMPARE(snap.recentlyPlayed.size(), 2);

  // Never played: the single unlaunched item.
  QCOMPARE(snap.neverPlayed.size(), 1);
  QCOMPARE(snap.neverPlayed[0].name, QStringLiteral("Game 3"));

  // History rows + count.
  QCOMPARE(snap.historyCount, qint64(2));
  QCOMPARE(snap.history.size(), 2);

  closeAndRemove(db, conn);
}

QTEST_MAIN(TestStatisticsService)
#include "test_statisticsservice.moc"
