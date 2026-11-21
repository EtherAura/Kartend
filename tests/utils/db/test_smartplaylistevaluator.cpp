// Tests for SmartPlaylistEvaluator. Seeds an in-memory items table that
// matches the v1+ schema, runs each filter Kind against it, and checks
// the (collection_uuid, path) result set. The Most Launched tie-break case
// uses the shared on-disk MigratedDb fixture (production bootstrap).
#include <tuple>

#include <QDateTime>
#include <QFileInfo>
#include <QList>
#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVariant>

#include "../../support/migrateddb.h"
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
  void topPlayed_equalCountsOrderByLastPlayedDesc();
  void neverPlayed_returnsZeroOrNullPlayCount();
  void byExtension_matchesAllListedAndIsCaseInsensitive();
  void byExtension_emptyListReturnsNothing();
  void byExtension_underscoreIsLiteralNotWildcard();
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

  // Multi-rule composition (Kartend-r5dbe)
  void matchAll_intersectsRulesAndKeepsFirstRuleOrder();
  void matchAny_unionsRulesWithoutDuplicates();
  void singleRuleSet_matchesTheBareFilter();
  void emptyRuleSet_returnsNothing();

  // Three-rule composition (Kartend-sp3ju): the fold loop runs more than once
  void matchAll_threeRules_keepsOnlyItemsMatchingEvery();
  void matchAny_threeRules_unionsInRuleOrder();
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

void TestSmartPlaylistEvaluator::topPlayed_equalCountsOrderByLastPlayedDesc() {
  // Kartend-zcexs: Most Launched breaks play_count ties on last_played DESC.
  // Real on-disk SQLite through the production bootstrap.
  KartendTest::MigratedDb fixture(KartendTest::MigratedDb::OnDisk);
  QVERIFY(fixture.isOpen());
  QVERIFY(!fixture.databasePath().isEmpty());
  auto db = fixture.database();
  QVERIFY(db.isOpen());
  QCOMPARE(db.databaseName(), fixture.databasePath());
  QVERIFY(QFileInfo::exists(fixture.databasePath()));

  QSqlQuery q(db);
  QVERIFY(q.exec("INSERT INTO collections (id, name, last_scanned, uuid) "
                 "VALUES (1, 'Games', '2026-01-01', 'u1')"));

  // The older tied item sorts first by path and name. Without the
  // last_played key, SQLite's LIMIT sorter emits the later-inserted of two
  // tied rows first, so the newer tied item is inserted BEFORE the older one;
  // only the last_played key then puts it first. The higher-count item is
  // inserted last and played longest ago.
  struct Seed {
    const char *path;
    const char *name;
    int playCount;
    const char *lastPlayed;
  };
  const Seed seeds[] = {
      {"/g/b-tied-newer", "B Tied Newer", 2, "2026-05-01T00:00:00Z"},
      {"/g/a-tied-older", "A Tied Older", 2, "2026-04-01T00:00:00Z"},
      {"/g/c-most", "C Most", 5, "2026-03-01T00:00:00Z"},
  };
  for (const auto &seed : seeds) {
    q.prepare("INSERT INTO items (collection_id, collection_uuid, path, name, last_modified, "
              "play_count, last_played) VALUES (1, 'u1', ?, ?, '2026-01-01', ?, ?)");
    q.addBindValue(QString::fromUtf8(seed.path));
    q.addBindValue(QString::fromUtf8(seed.name));
    q.addBindValue(seed.playCount);
    q.addBindValue(QString::fromUtf8(seed.lastPlayed));
    QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
  }

  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::TopPlayed;
  f.limit = 10;
  const auto result = SmartPlaylistEvaluator::evaluate(db, f);
  QCOMPARE(result.size(), 3);
  QCOMPARE(result[0].path, QStringLiteral("/g/c-most"));
  QCOMPARE(result[1].path, QStringLiteral("/g/b-tied-newer"));
  QCOMPARE(result[2].path, QStringLiteral("/g/a-tied-older"));
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

void TestSmartPlaylistEvaluator::byExtension_underscoreIsLiteralNotWildcard() {
  const QString conn = "spe_byext_underscore";
  auto db = openMemoryDb(conn);
  createItemsTable(db);
  insertItem(db, "u1", "Video", "/a/video.mp4");
  insertItem(db, "u1", "Odd", "/a/odd.m_4");

  // The extension is escaped before binding: "_" is a literal underscore,
  // not a single-character LIKE wildcard, so "m_4" must NOT match "mp4".
  SmartFilter::Filter f;
  f.kind = SmartFilter::Kind::ByExtension;
  f.extensions = {"m_4"};
  const auto result = SmartPlaylistEvaluator::evaluate(db, f);
  QCOMPARE(result.size(), 1);
  QCOMPARE(result[0].path, QStringLiteral("/a/odd.m_4"));

  // And a literal-underscore extension still matches itself alongside a
  // plain one.
  f.extensions = {"m_4", "mp4"};
  const auto both = SmartPlaylistEvaluator::evaluate(db, f);
  QCOMPARE(both.size(), 2);

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

namespace {
SmartFilter::Filter ruleOf(SmartFilter::Kind kind) {
  SmartFilter::Filter f;
  f.kind = kind;
  return f;
}

// Alpha is pinned AND has artwork, Beta is pinned only, Gamma has artwork
// only, Delta neither — every cell of the two-rule truth table.
void seedTruthTable(QSqlDatabase &db) {
  createItemsTable(db);
  createItemMetadataTable(db);
  insertItem(db, "u1", "Alpha", "/a/alpha.mp4", 0, QString(), "/art/alpha.png");
  insertItem(db, "u1", "Beta", "/a/beta.mp4");
  insertItem(db, "u1", "Gamma", "/a/gamma.mp4", 0, QString(), "/art/gamma.png");
  insertItem(db, "u1", "Delta", "/a/delta.mp4");
  insertItemMetadata(db, "u1", "/a/alpha.mp4", true, false, false);
  insertItemMetadata(db, "u1", "/a/beta.mp4", true, false, false);
}

QStringList pathsOf(const QList<SmartPlaylistEvaluator::Match> &matches) {
  QStringList out;
  out.reserve(matches.size());
  for (const auto &m : matches) {
    out.append(m.path);
  }
  return out;
}
} // namespace

void TestSmartPlaylistEvaluator::matchAll_intersectsRulesAndKeepsFirstRuleOrder() {
  const QString conn = "spe_match_all";
  auto db = openMemoryDb(conn);
  seedTruthTable(db);

  SmartFilter::FilterSet set;
  set.match = SmartFilter::MatchMode::All;
  set.rules = {ruleOf(SmartFilter::Kind::Pinned), ruleOf(SmartFilter::Kind::HasArtwork)};

  // Only Alpha satisfies both. Beta (pinned, no art) and Gamma (art, not
  // pinned) each satisfy exactly one rule and must NOT survive an
  // intersection — the failure this guards against is a union masquerading
  // as "all".
  QCOMPARE(pathsOf(SmartPlaylistEvaluator::evaluate(db, set)),
           QStringList{QStringLiteral("/a/alpha.mp4")});

  closeAndRemove(db, conn);
}

void TestSmartPlaylistEvaluator::matchAny_unionsRulesWithoutDuplicates() {
  const QString conn = "spe_match_any";
  auto db = openMemoryDb(conn);
  seedTruthTable(db);

  SmartFilter::FilterSet set;
  set.match = SmartFilter::MatchMode::Any;
  set.rules = {ruleOf(SmartFilter::Kind::Pinned), ruleOf(SmartFilter::Kind::HasArtwork)};

  // Alpha matches BOTH rules and must appear exactly once — a naive
  // concatenation would list it twice and the playlist would show a
  // duplicate tile. Order follows the first rule (pinned, alphabetical:
  // Alpha then Beta), then the second rule's newcomers (Gamma). Delta
  // matches neither and stays out.
  const QStringList expected{QStringLiteral("/a/alpha.mp4"), QStringLiteral("/a/beta.mp4"),
                             QStringLiteral("/a/gamma.mp4")};
  QCOMPARE(pathsOf(SmartPlaylistEvaluator::evaluate(db, set)), expected);

  closeAndRemove(db, conn);
}

void TestSmartPlaylistEvaluator::singleRuleSet_matchesTheBareFilter() {
  const QString conn = "spe_single_rule";
  auto db = openMemoryDb(conn);
  seedTruthTable(db);

  // A one-rule set is the pre-composition playlist. It must return exactly
  // what the bare Filter overload returns, or every playlist written before
  // composition would quietly change what it shows.
  const auto bare = SmartPlaylistEvaluator::evaluate(db, ruleOf(SmartFilter::Kind::Pinned));
  SmartFilter::FilterSet set;
  set.rules = {ruleOf(SmartFilter::Kind::Pinned)};
  QCOMPARE(pathsOf(SmartPlaylistEvaluator::evaluate(db, set)), pathsOf(bare));
  QCOMPARE(bare.size(), 2);

  closeAndRemove(db, conn);
}

void TestSmartPlaylistEvaluator::emptyRuleSet_returnsNothing() {
  const QString conn = "spe_empty_rules";
  auto db = openMemoryDb(conn);
  seedTruthTable(db);

  // Parsing rejects an empty rules array, so this shape only reaches the
  // evaluator from hand-built code. "Nothing" is the safe reading: an
  // unconstrained set must never be treated as "match the whole library".
  SmartFilter::FilterSet set;
  QCOMPARE(SmartPlaylistEvaluator::evaluate(db, set).size(), 0);

  closeAndRemove(db, conn);
}

namespace {
// Three-rule truth table over Pinned (P), HasArtwork (A) and ContinueLater (C),
// seeded into the real on-disk production schema:
//
//   Mango   P A C   Kiwi  P A C   (all three)
//   Lemon   P A     (first two only — must not survive an "all" of three)
//   Olive   P   C   Fig     A C   (two)
//   Cherry    A     Banana    C   Apricot   C   (one)
//   Grape           (none)
//
// Every rule orders by name, so each rule alone is alphabetical. Rows are
// inserted in a deliberately non-alphabetical order, and the path prefixes
// follow insertion order, so an insertion-order or path-sorted result differs
// from the expected one. The later rules' newcomers (Cherry, Fig from rule
// two; Apricot, Banana from rule three) all sort BEFORE the first rule's
// items, so a globally name-sorted union is distinguishable from rule order.
void seedThreeRuleTable(QSqlDatabase &db) {
  QSqlQuery q(db);
  QVERIFY2(q.exec("INSERT INTO collections (id, name, last_scanned, uuid) "
                  "VALUES (1, 'Games', '2026-01-01', 'u1')"),
           qPrintable(q.lastError().text()));

  struct Seed {
    const char *path;
    const char *name;
    bool pinned;
    bool artwork;
    bool continueLater;
  };
  const Seed seeds[] = {
      {"/t/01-mango", "Mango", true, true, true},
      {"/t/02-olive", "Olive", true, false, true},
      {"/t/03-lemon", "Lemon", true, true, false},
      {"/t/04-kiwi", "Kiwi", true, true, true},
      {"/t/05-fig", "Fig", false, true, true},
      {"/t/06-grape", "Grape", false, false, false},
      {"/t/07-banana", "Banana", false, false, true},
      {"/t/08-cherry", "Cherry", false, true, false},
      {"/t/09-apricot", "Apricot", false, false, true},
  };
  for (const auto &seed : seeds) {
    const QString path = QString::fromUtf8(seed.path);
    q.prepare("INSERT INTO items (collection_id, collection_uuid, path, name, last_modified, "
              "artwork_path) VALUES (1, 'u1', ?, ?, '2026-01-01', ?)");
    q.addBindValue(path);
    q.addBindValue(QString::fromUtf8(seed.name));
    q.addBindValue(seed.artwork ? QVariant(path + QStringLiteral(".png"))
                                : QVariant(QMetaType(QMetaType::QString)));
    QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
    if (seed.pinned || seed.continueLater) {
      insertItemMetadata(db, "u1", path, seed.pinned, false, seed.continueLater);
    }
  }
}

SmartFilter::FilterSet threeRuleSet(SmartFilter::MatchMode mode) {
  SmartFilter::FilterSet set;
  set.match = mode;
  set.rules = {ruleOf(SmartFilter::Kind::Pinned), ruleOf(SmartFilter::Kind::HasArtwork),
               ruleOf(SmartFilter::Kind::ContinueLater)};
  return set;
}
} // namespace

void TestSmartPlaylistEvaluator::matchAll_threeRules_keepsOnlyItemsMatchingEvery() {
  KartendTest::MigratedDb fixture(KartendTest::MigratedDb::OnDisk);
  QVERIFY(fixture.isOpen());
  QVERIFY(QFileInfo::exists(fixture.databasePath()));
  auto db = fixture.database();
  QVERIFY(db.isOpen());
  seedThreeRuleTable(db);
  if (QTest::currentTestFailed()) {
    return;
  }

  // Only Kiwi and Mango match all three, in the first rule's alphabetical
  // order (they were inserted Mango first). Lemon matches the first two
  // rules only: a fold that stops after two rules (or an off-by-one loop
  // bound) would keep it.
  const QStringList expected{QStringLiteral("/t/04-kiwi"), QStringLiteral("/t/01-mango")};
  const QStringList actual =
      pathsOf(SmartPlaylistEvaluator::evaluate(db, threeRuleSet(SmartFilter::MatchMode::All)));
  QCOMPARE(actual, expected);
  QVERIFY(!actual.contains(QStringLiteral("/t/03-lemon")));
}

void TestSmartPlaylistEvaluator::matchAny_threeRules_unionsInRuleOrder() {
  KartendTest::MigratedDb fixture(KartendTest::MigratedDb::OnDisk);
  QVERIFY(fixture.isOpen());
  QVERIFY(QFileInfo::exists(fixture.databasePath()));
  auto db = fixture.database();
  QVERIFY(db.isOpen());
  seedThreeRuleTable(db);
  if (QTest::currentTestFailed()) {
    return;
  }

  // First rule (Pinned) alphabetically: Kiwi, Lemon, Mango, Olive. Then the
  // second rule's newcomers (Cherry, Fig), then the third rule's (Apricot,
  // Banana). Items matching several rules appear once, at their first rule.
  // Grape matches none and stays out. Dropping the third rule would lose
  // Apricot and Banana; a global sort would put Apricot first.
  const QStringList expected{QStringLiteral("/t/04-kiwi"),    QStringLiteral("/t/03-lemon"),
                             QStringLiteral("/t/01-mango"),   QStringLiteral("/t/02-olive"),
                             QStringLiteral("/t/08-cherry"),  QStringLiteral("/t/05-fig"),
                             QStringLiteral("/t/09-apricot"), QStringLiteral("/t/07-banana")};
  const QStringList actual =
      pathsOf(SmartPlaylistEvaluator::evaluate(db, threeRuleSet(SmartFilter::MatchMode::Any)));
  QCOMPARE(actual, expected);
  QCOMPARE(QSet<QString>(actual.begin(), actual.end()).size(), actual.size());
  QVERIFY(!actual.contains(QStringLiteral("/t/06-grape")));
}

QTEST_MAIN(TestSmartPlaylistEvaluator)
#include "test_smartplaylistevaluator.moc"
