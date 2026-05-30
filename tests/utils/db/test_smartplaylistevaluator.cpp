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

void createItemMetadataTable(QSqlDatabase &db) {
  // Subset of the v5+v15 item_metadata schema — only the columns the
  // state-flag evaluator joins against. Independent of the migration suite
  // so this test stays a focused unit check.
  QSqlQuery q(db);
  QVERIFY(q.exec("CREATE TABLE item_metadata ("
                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                 "collection_uuid TEXT NOT NULL DEFAULT '', "
                 "path TEXT NOT NULL, "
                 "is_pinned INTEGER NOT NULL DEFAULT 0, "
                 "is_hidden INTEGER NOT NULL DEFAULT 0, "
                 "continue_later INTEGER NOT NULL DEFAULT 0, "
                 "UNIQUE(collection_uuid, path))"));
}

void insertItemMetadata(QSqlDatabase &db, const QString &uuid, const QString &path, bool pinned,
                        bool hidden, bool continueLater) {
  QSqlQuery q(db);
  q.prepare("INSERT INTO item_metadata "
            "(collection_uuid, path, is_pinned, is_hidden, continue_later) "
            "VALUES (?, ?, ?, ?, ?)");
  q.addBindValue(uuid);
  q.addBindValue(path);
  q.addBindValue(pinned ? 1 : 0);
  q.addBindValue(hidden ? 1 : 0);
  q.addBindValue(continueLater ? 1 : 0);
  QVERIFY(q.exec());
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
  q.addBindValue(lastPlayed.isEmpty() ? QVariant(QMetaType(QMetaType::QString))
                                      : QVariant(lastPlayed));
  q.addBindValue(artworkPath.isEmpty() ? QVariant(QMetaType(QMetaType::QString))
                                       : QVariant(artworkPath));
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
  void stateFlag_pinnedReturnsOnlyPinned();
  void stateFlag_hiddenIsIndependentOfPinned();
  void stateFlag_continueLaterReturnsOnlyMarked();
  void byCollection_returnsOnlyMatchingUuid();
  void byCollection_emptyUuidReturnsNothing();
  void byTitleSearch_substringCaseInsensitive();
  void byTitleSearch_emptyNeedleReturnsNothing();
  void missingArtwork_returnsNullOrEmptyArtwork();
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
  for (const auto &row :
       QList<std::tuple<QString, QString, qint64>>{{"Recent", "/a/recent.mp4", now - 3600},
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

void TestSmartPlaylistEvaluator::stateFlag_pinnedReturnsOnlyPinned() {
  const QString conn = "spl_pinned";
  auto db = openMemoryDb(conn);
  QVERIFY(db.isOpen());
  createItemsTable(db);
  createItemMetadataTable(db);

  insertItem(db, "u1", "Alpha", "/a/alpha.mp4");
  insertItem(db, "u1", "Beta", "/a/beta.mp4");
  insertItem(db, "u1", "Gamma", "/a/gamma.mp4");
  // Only Alpha is pinned; Beta is hidden but not pinned; Gamma has no row.
  insertItemMetadata(db, "u1", "/a/alpha.mp4", true, false, false);
  insertItemMetadata(db, "u1", "/a/beta.mp4", false, true, false);

  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::Pinned;
  auto out = SmartPlaylistEvaluator::evaluate(db, f);
  QCOMPARE(out.size(), 1);
  QCOMPARE(out.first().path, QStringLiteral("/a/alpha.mp4"));

  closeAndRemove(db, conn);
}

void TestSmartPlaylistEvaluator::stateFlag_hiddenIsIndependentOfPinned() {
  // Confirms the evaluator picks the right column — pinning an item
  // doesn't accidentally surface it in the Hidden playlist, and vice
  // versa. Catches a copy/paste regression in evalStateFlag.
  const QString conn = "spl_hidden_independence";
  auto db = openMemoryDb(conn);
  QVERIFY(db.isOpen());
  createItemsTable(db);
  createItemMetadataTable(db);

  insertItem(db, "u1", "Alpha", "/a/alpha.mp4");
  insertItem(db, "u1", "Beta", "/a/beta.mp4");
  insertItemMetadata(db, "u1", "/a/alpha.mp4", true, false, false); // pinned only
  insertItemMetadata(db, "u1", "/a/beta.mp4", false, true, false);  // hidden only

  SmartFilter::Filter pinned;
  pinned.kind = SmartFilter::Kind::Pinned;
  SmartFilter::Filter hidden;
  hidden.kind = SmartFilter::Kind::Hidden;

  auto pinnedOut = SmartPlaylistEvaluator::evaluate(db, pinned);
  auto hiddenOut = SmartPlaylistEvaluator::evaluate(db, hidden);
  QCOMPARE(pinnedOut.size(), 1);
  QCOMPARE(pinnedOut.first().path, QStringLiteral("/a/alpha.mp4"));
  QCOMPARE(hiddenOut.size(), 1);
  QCOMPARE(hiddenOut.first().path, QStringLiteral("/a/beta.mp4"));

  closeAndRemove(db, conn);
}

void TestSmartPlaylistEvaluator::stateFlag_continueLaterReturnsOnlyMarked() {
  const QString conn = "spl_continue_later";
  auto db = openMemoryDb(conn);
  QVERIFY(db.isOpen());
  createItemsTable(db);
  createItemMetadataTable(db);

  insertItem(db, "u1", "Alpha", "/a/alpha.mp4");
  insertItem(db, "u1", "Beta", "/a/beta.mp4");
  insertItemMetadata(db, "u1", "/a/alpha.mp4", false, false, true);
  insertItemMetadata(db, "u1", "/a/beta.mp4", false, false, false);

  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::ContinueLater;
  auto out = SmartPlaylistEvaluator::evaluate(db, f);
  QCOMPARE(out.size(), 1);
  QCOMPARE(out.first().path, QStringLiteral("/a/alpha.mp4"));

  closeAndRemove(db, conn);
}

void TestSmartPlaylistEvaluator::byCollection_returnsOnlyMatchingUuid() {
  const QString conn = "spl_by_collection";
  auto db = openMemoryDb(conn);
  QVERIFY(db.isOpen());
  createItemsTable(db);

  insertItem(db, "uuid-A", "First", "/a/first.mp4");
  insertItem(db, "uuid-A", "Second", "/a/second.mp4");
  insertItem(db, "uuid-B", "Third", "/b/third.mp4");

  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::ByCollection;
  f.collectionUuid = "uuid-A";
  const auto out = SmartPlaylistEvaluator::evaluate(db, f);
  QCOMPARE(out.size(), 2);
  for (const auto &m : out) {
    QCOMPARE(m.collectionUuid, QStringLiteral("uuid-A"));
  }

  closeAndRemove(db, conn);
}

void TestSmartPlaylistEvaluator::byCollection_emptyUuidReturnsNothing() {
  // Unsaved dialog state (empty uuid) must NOT silently return the whole
  // library — a default-constructed filter at startup would surface every
  // item across every collection, which looks like a bug.
  const QString conn = "spl_by_collection_empty";
  auto db = openMemoryDb(conn);
  QVERIFY(db.isOpen());
  createItemsTable(db);
  insertItem(db, "uuid-A", "First", "/a/first.mp4");

  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::ByCollection;
  const auto out = SmartPlaylistEvaluator::evaluate(db, f);
  QVERIFY(out.isEmpty());

  closeAndRemove(db, conn);
}

void TestSmartPlaylistEvaluator::byTitleSearch_substringCaseInsensitive() {
  const QString conn = "spl_title_search";
  auto db = openMemoryDb(conn);
  QVERIFY(db.isOpen());
  createItemsTable(db);
  insertItem(db, "u1", "Concert at the Park", "/a/concert.mp4");
  insertItem(db, "u1", "Studio Session", "/a/studio.mp4");
  // ASCII LIKE is case-insensitive by default in SQLite; "concert" should
  // match "Concert at the Park" but not "Studio Session".
  insertItem(db, "u1", "Stage Concert Special", "/a/special.mp4");

  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::ByTitleSearch;
  f.titleSearch = "concert";
  const auto out = SmartPlaylistEvaluator::evaluate(db, f);
  QCOMPARE(out.size(), 2);

  closeAndRemove(db, conn);
}

void TestSmartPlaylistEvaluator::byTitleSearch_emptyNeedleReturnsNothing() {
  const QString conn = "spl_title_search_empty";
  auto db = openMemoryDb(conn);
  QVERIFY(db.isOpen());
  createItemsTable(db);
  insertItem(db, "u1", "First", "/a/first.mp4");

  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::ByTitleSearch;
  // Whitespace-only needle counts as empty (trimmed).
  f.titleSearch = "   ";
  const auto out = SmartPlaylistEvaluator::evaluate(db, f);
  QVERIFY(out.isEmpty());

  closeAndRemove(db, conn);
}

void TestSmartPlaylistEvaluator::missingArtwork_returnsNullOrEmptyArtwork() {
  const QString conn = "spl_missing_artwork";
  auto db = openMemoryDb(conn);
  QVERIFY(db.isOpen());
  createItemsTable(db);
  insertItem(db, "u1", "Has", "/a/has.mp4", 0, QString(), "/art/has.jpg");
  insertItem(db, "u1", "Missing", "/a/missing.mp4", 0, QString(), QString());
  // Empty string is also "no artwork" — the scanner can leave the column
  // empty (not just NULL) when the lookup misses.
  QSqlQuery upd(db);
  QVERIFY(upd.exec("UPDATE items SET artwork_path = '' WHERE path = '/a/missing.mp4'"));

  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::MissingArtwork;
  const auto out = SmartPlaylistEvaluator::evaluate(db, f);
  QCOMPARE(out.size(), 1);
  QCOMPARE(out.first().path, QStringLiteral("/a/missing.mp4"));

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
