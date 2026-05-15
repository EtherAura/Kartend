// Tests for SmartPlaylistEvaluator. Seeds an in-memory items table that
// matches the v1+ schema, runs each filter Kind against it, and checks
// the (collection_uuid, path) result set.
#include <tuple>

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QTest>

#include "smartfilter.h"
#include "smartplaylistevaluator.h"

namespace {

QSqlDatabase openMemoryDb(const QString &conn) {
  auto db = QSqlDatabase::addDatabase("QSQLITE", conn);
  db.setDatabaseName(":memory:");
  if (!db.open()) {
    qWarning("Failed to open in-memory db: %s", qPrintable(db.lastError().text()));
  }
  return db;
}

void closeAndRemove(QSqlDatabase &db, const QString &conn) {
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(conn);
}

void createItemsTable(QSqlDatabase &db) {
  // Mirror the post-v12 columns the evaluator queries. We don't need
  // the full migration suite — just the columns each Kind reads.
  QSqlQuery q(db);
  QVERIFY(q.exec("CREATE TABLE items ("
                 "id INTEGER PRIMARY KEY, "
                 "collection_uuid TEXT NOT NULL DEFAULT '', "
                 "name TEXT NOT NULL DEFAULT '', "
                 "path TEXT NOT NULL DEFAULT '', "
                 "play_count INTEGER DEFAULT 0, "
                 "last_played TEXT, "
                 "total_play_seconds INTEGER DEFAULT 0, "
                 "artwork_path TEXT, "
                 "date_added INTEGER NOT NULL DEFAULT 0)"));
}

void insertItem(QSqlDatabase &db, const QString &uuid, const QString &name, const QString &path,
                qint64 playCount = 0, const QString &lastPlayed = QString(),
                const QString &artworkPath = QString()) {
  QSqlQuery q(db);
  q.prepare("INSERT INTO items (collection_uuid, name, path, play_count, last_played, "
            "artwork_path) VALUES (?, ?, ?, ?, ?, ?)");
  q.addBindValue(uuid);
  q.addBindValue(name);
  q.addBindValue(path);
  q.addBindValue(playCount);
  q.addBindValue(lastPlayed.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(lastPlayed));
  q.addBindValue(artworkPath.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(artworkPath));
  QVERIFY(q.exec());
}

} // namespace

class TestSmartPlaylistEvaluator : public QObject {
  Q_OBJECT
private slots:
  void recentlyLaunched_returnsOrderedByLastPlayedDesc();
  void topPlayed_returnsOrderedByPlayCountDesc();
  void neverPlayed_returnsZeroOrNullPlayCount();
  void byExtension_matchesAllListedAndIsCaseInsensitive();
  void byExtension_emptyListReturnsNothing();
  void hasArtwork_returnsItemsWithNonEmptyArtworkPath();
  void byDateAdded_filtersByRecencyWindowAndExcludesUnknownDates();
  void evaluate_unopenedDb_returnsEmptyWithoutCrashing();
};

void TestSmartPlaylistEvaluator::recentlyLaunched_returnsOrderedByLastPlayedDesc() {
  const QString conn = "spe_recent";
  auto db = openMemoryDb(conn);
  createItemsTable(db);
  insertItem(db, "u1", "Alpha", "/a/alpha.mp4", 1, "2026-04-01T00:00:00Z");
  insertItem(db, "u1", "Beta", "/a/beta.mp4", 1, "2026-05-01T00:00:00Z");
  insertItem(db, "u1", "Gamma", "/a/gamma.mp4", 0, QString()); // never played, excluded

  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::RecentlyLaunched;
  f.limit = 10;
  const auto result = SmartPlaylistEvaluator::evaluate(db, f);
  QCOMPARE(result.size(), 2);
  QCOMPARE(result[0].path, QStringLiteral("/a/beta.mp4"));
  QCOMPARE(result[1].path, QStringLiteral("/a/alpha.mp4"));

  closeAndRemove(db, conn);
}

void TestSmartPlaylistEvaluator::topPlayed_returnsOrderedByPlayCountDesc() {
  const QString conn = "spe_top";
  auto db = openMemoryDb(conn);
  createItemsTable(db);
  insertItem(db, "u1", "Alpha", "/a/alpha.mp4", 5);
  insertItem(db, "u1", "Beta", "/a/beta.mp4", 12);
  insertItem(db, "u1", "Never", "/a/never.mp4", 0); // excluded

  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::TopPlayed;
  f.limit = 10;
  const auto result = SmartPlaylistEvaluator::evaluate(db, f);
  QCOMPARE(result.size(), 2);
  QCOMPARE(result[0].path, QStringLiteral("/a/beta.mp4"));
  QCOMPARE(result[1].path, QStringLiteral("/a/alpha.mp4"));

  closeAndRemove(db, conn);
}

void TestSmartPlaylistEvaluator::neverPlayed_returnsZeroOrNullPlayCount() {
  const QString conn = "spe_never";
  auto db = openMemoryDb(conn);
  createItemsTable(db);
  insertItem(db, "u1", "Played", "/a/played.mp4", 3);
  insertItem(db, "u1", "Zero", "/a/zero.mp4", 0);
  insertItem(db, "u1", "Null", "/a/null.mp4", 0);
  // Force NULL so the OR-NULL branch of the SQL is exercised.
  QSqlQuery q(db);
  QVERIFY(q.exec("UPDATE items SET play_count = NULL WHERE path = '/a/null.mp4'"));

  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::NeverPlayed;
  f.limit = 10;
  const auto result = SmartPlaylistEvaluator::evaluate(db, f);
  QCOMPARE(result.size(), 2);
  // Sorted alphabetically by name (NOCASE) — Null then Zero.
  QCOMPARE(result[0].path, QStringLiteral("/a/null.mp4"));
  QCOMPARE(result[1].path, QStringLiteral("/a/zero.mp4"));

  closeAndRemove(db, conn);
}

void TestSmartPlaylistEvaluator::byExtension_matchesAllListedAndIsCaseInsensitive() {
  const QString conn = "spe_byext";
  auto db = openMemoryDb(conn);
  createItemsTable(db);
  insertItem(db, "u1", "Video", "/a/video.mp4");
  insertItem(db, "u1", "Loud", "/a/LOUD.MP4");
  insertItem(db, "u1", "Markdown", "/a/notes.md");
  insertItem(db, "u1", "Backup", "/a/backup.mkv");

  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::ByExtension;
  f.extensions = {"mp4", "mkv"};
  const auto result = SmartPlaylistEvaluator::evaluate(db, f);
  QCOMPARE(result.size(), 3);
  // Ordering is alphabetical by name COLLATE NOCASE — Backup, Loud, Video.
  QCOMPARE(result[0].path, QStringLiteral("/a/backup.mkv"));
  QCOMPARE(result[1].path, QStringLiteral("/a/LOUD.MP4"));
  QCOMPARE(result[2].path, QStringLiteral("/a/video.mp4"));

  closeAndRemove(db, conn);
}

void TestSmartPlaylistEvaluator::byExtension_emptyListReturnsNothing() {
  const QString conn = "spe_byext_empty";
  auto db = openMemoryDb(conn);
  createItemsTable(db);
  insertItem(db, "u1", "Video", "/a/video.mp4");

  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::ByExtension;
  // Empty extensions list — without the early return this would build
  // an unparseable WHERE clause and crash. Verify the early return.
  QCOMPARE(SmartPlaylistEvaluator::evaluate(db, f).size(), 0);

  closeAndRemove(db, conn);
}

void TestSmartPlaylistEvaluator::hasArtwork_returnsItemsWithNonEmptyArtworkPath() {
  const QString conn = "spe_artwork";
  auto db = openMemoryDb(conn);
  createItemsTable(db);
  insertItem(db, "u1", "WithCover", "/a/with.mp4", 0, QString(), "/covers/with.png");
  insertItem(db, "u1", "Empty", "/a/empty.mp4", 0, QString(), "");
  insertItem(db, "u1", "Null", "/a/null.mp4", 0, QString(), QString());

  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::HasArtwork;
  const auto result = SmartPlaylistEvaluator::evaluate(db, f);
  QCOMPARE(result.size(), 1);
  QCOMPARE(result[0].path, QStringLiteral("/a/with.mp4"));

  closeAndRemove(db, conn);
}

void TestSmartPlaylistEvaluator::byDateAdded_filtersByRecencyWindowAndExcludesUnknownDates() {
  const QString conn = "spe_date_added";
  auto db = openMemoryDb(conn);
  createItemsTable(db);

  // Stamp three rows with explicit date_added epochs and one with 0
  // (unknown — pre-v12 backfill semantics).
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  const qint64 fiveDaysAgo = now - (5 * 86400);
  const qint64 fortyDaysAgo = now - (40 * 86400);

  QSqlQuery ins(db);
  ins.prepare("INSERT INTO items (collection_uuid, name, path, date_added) "
              "VALUES (?, ?, ?, ?)");
  for (const auto &row : QList<std::tuple<QString, QString, qint64>>{
           {"Recent", "/a/recent.mp4", now - 3600},
           {"Five", "/a/five.mp4", fiveDaysAgo},
           {"Forty", "/a/forty.mp4", fortyDaysAgo},
           {"Unknown", "/a/unknown.mp4", 0}}) {
    ins.bindValue(0, "u1");
    ins.bindValue(1, std::get<0>(row));
    ins.bindValue(2, std::get<1>(row));
    ins.bindValue(3, std::get<2>(row));
    QVERIFY(ins.exec());
  }

  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::ByDateAdded;
  f.days = 30;
  const auto result = SmartPlaylistEvaluator::evaluate(db, f);
  QCOMPARE(result.size(), 2);
  // Ordered by date_added DESC — most recent first.
  QCOMPARE(result[0].path, QStringLiteral("/a/recent.mp4"));
  QCOMPARE(result[1].path, QStringLiteral("/a/five.mp4"));

  // 0-stamped rows must NOT appear even when the cutoff is wide open —
  // "unknown date" is the v12 sentinel for pre-existing rows that we
  // intentionally don't claim to know the add-date for.
  f.days = 3650;
  const auto wide = SmartPlaylistEvaluator::evaluate(db, f);
  QCOMPARE(wide.size(), 3);
  for (const auto &m : wide) {
    QVERIFY(m.path != QStringLiteral("/a/unknown.mp4"));
  }

  closeAndRemove(db, conn);
}

void TestSmartPlaylistEvaluator::evaluate_unopenedDb_returnsEmptyWithoutCrashing() {
  QSqlDatabase db; // never opened
  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::TopPlayed;
  f.limit = 10;
  // Returns empty (logging the error). The contract is "no exceptions,
  // no UB" so the QueryManager smart-scope branch can degrade silently.
  QCOMPARE(SmartPlaylistEvaluator::evaluate(db, f).size(), 0);
}

QTEST_MAIN(TestSmartPlaylistEvaluator)
#include "test_smartplaylistevaluator.moc"
