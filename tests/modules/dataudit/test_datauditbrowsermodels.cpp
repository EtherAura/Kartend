// Tests for the DAT-audit browser models (Kartend-34lab): the global tree's
// rollup, the game list's aggregation + completeness filter, and the ROM
// detail's catalogue↔result join. Pure model logic, fed via setters — no DB,
// no widgets — so QTEST_GUILESS_MAIN (a QCoreApplication) suffices.

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "datauditbrowsermodels.h"
#include "datauditbuckets.h"
#include "datauditprofile.h"
#include "datlookup.h"

using namespace DatAudit;
using DatAuditProfile::GameRollupRow;
using DatAuditProfile::Profile;
using DatAuditProfile::ResultRow;
using DatAuditProfile::RollupRow;

namespace {
constexpr int kHave = 0;      // Status::Have
constexpr int kMissing = 6;   // Status::Missing
constexpr int kWrongName = 1; // Status::WrongName
} // namespace

class TestDatAuditBrowserModels : public QObject {
  Q_OBJECT
private slots:
  void bucketCountsMapStatuses();
  void treeBuildsProfilesAndSources();
  void gameListAggregatesAndStates();
  void gameFilterByStateAndFixes();
  void romFileJoinsCatalogueAndStatus();
  void romFileFallsBackToResultsWithoutCatalogue();
  void groupsResultsByItemFolder();
  void browserPresetsRoundTrip();
  void browserPresetsDefaultUnsavedSlots();
  void reconstructsAuditRowsFromResults();
  void categoryLabelResolutionPrecedence();
  void treeBuildsCategoryLevel();
  void treeGroupedMultiSourceParentChain();
};

void TestDatAuditBrowserModels::bucketCountsMapStatuses() {
  BucketCounts c;
  c.add(static_cast<Status>(kHave), false, 3);
  c.add(static_cast<Status>(kMissing), true, 2);    // MIA
  c.add(static_cast<Status>(kWrongName), false, 1); // present + fixable
  QCOMPARE(c.have, 4);                              // Have + WrongName
  QCOMPARE(c.missing, 2);
  QCOMPARE(c.fixable, 1); // WrongName
  QCOMPARE(c.mia, 2);
  QCOMPARE(c.total, 6);
  QCOMPARE(gameStateOf(c), GameState::Partial); // some present, some missing
}

void TestDatAuditBrowserModels::treeBuildsProfilesAndSources() {
  Profile single;
  single.id = 1;
  single.name = QStringLiteral("SNES");
  DatAuditProfile::DatRef d1;
  d1.path = QStringLiteral("/dats/snes.dat");
  single.dats = {d1};

  Profile multi;
  multi.id = 2;
  multi.name = QStringLiteral("Arcade");
  DatAuditProfile::DatRef a;
  a.path = QStringLiteral("/dats/mame.dat");
  DatAuditProfile::DatRef b;
  b.path = QStringLiteral("/dats/fbneo.dat");
  multi.dats = {a, b};

  Profile neverScanned;
  neverScanned.id = 3;
  neverScanned.name = QStringLiteral("Empty");

  QList<RollupRow> rollups{
      {1, QStringLiteral("snes.dat"), kHave, false, 10},
      {1, QStringLiteral("snes.dat"), kMissing, false, 2},
      {2, QStringLiteral("mame.dat"), kHave, false, 5},
      {2, QStringLiteral("fbneo.dat"), kMissing, true, 3}, // MIA
  };

  AuditTreeModel tree;
  tree.setTree({single, multi, neverScanned}, rollups);

  QCOMPARE(tree.rowCount(QModelIndex()), 3);

  // Single-source profile: a leaf carrying the source path + counts.
  const QModelIndex snes = tree.index(0, 0);
  QCOMPARE(tree.rowCount(snes), 0);
  QCOMPARE(tree.datPathAt(snes), QStringLiteral("/dats/snes.dat"));
  const auto snesCounts = qvariant_cast<BucketCounts>(tree.data(snes, AuditTreeModel::CountsRole));
  QCOMPARE(snesCounts.have, 10);
  QCOMPARE(snesCounts.missing, 2);

  // Multi-source profile: two Source children; counts roll up.
  const QModelIndex arcade = tree.index(1, 0);
  QCOMPARE(tree.rowCount(arcade), 2);
  const auto arcadeCounts =
      qvariant_cast<BucketCounts>(tree.data(arcade, AuditTreeModel::CountsRole));
  QCOMPARE(arcadeCounts.have, 5);
  QCOMPARE(arcadeCounts.missing, 3);
  QCOMPARE(arcadeCounts.mia, 3);
  // A source child knows its DAT path for lazy loading + parent() round-trips.
  const QModelIndex src0 = tree.index(0, 0, arcade);
  QVERIFY(!tree.datPathAt(src0).isEmpty());
  QCOMPARE(tree.parent(src0), arcade);
  // Profile id flows from every node up to its owning profile — the invariant
  // the browser's profile-scoped Re-audit / Fix relies on (Kartend-7iqhl.2): a
  // Source-DAT child reports its PARENT profile's id, not a source id.
  QCOMPARE(tree.profileIdAt(snes), static_cast<qint64>(1));
  QCOMPARE(tree.profileIdAt(arcade), static_cast<qint64>(2));
  QCOMPARE(tree.profileIdAt(src0), static_cast<qint64>(2));

  // Never-audited profile flagged unscanned.
  const QModelIndex empty = tree.index(2, 0);
  QVERIFY(tree.data(empty, AuditTreeModel::UnscannedRole).toBool());
}

void TestDatAuditBrowserModels::gameListAggregatesAndStates() {
  GameListModel m;
  m.setGames({
      {QStringLiteral("Complete Game"), kHave, false, 2},
      {QStringLiteral("Partial Game"), kHave, false, 1},
      {QStringLiteral("Partial Game"), kMissing, false, 1},
      {QStringLiteral("Empty Game"), kMissing, true, 3}, // all missing + MIA
  });
  QCOMPARE(m.rowCount(), 3);

  // Find the row index for each game (order = first-seen).
  auto stateOf = [&](const QString &name) {
    for (int r = 0; r < m.rowCount(); ++r) {
      if (m.gameNameAt(r) == name) {
        return static_cast<GameState>(m.data(m.index(r, 0), GameListModel::GameStateRole).toInt());
      }
    }
    return GameState::Empty;
  };
  QCOMPARE(stateOf(QStringLiteral("Complete Game")), GameState::Complete);
  QCOMPARE(stateOf(QStringLiteral("Partial Game")), GameState::Partial);
  QCOMPARE(stateOf(QStringLiteral("Empty Game")), GameState::Empty);
}

void TestDatAuditBrowserModels::gameFilterByStateAndFixes() {
  auto *m = new GameListModel(this);
  m->setGames({
      {QStringLiteral("Done"), kHave, false, 2},
      {QStringLiteral("Half"), kHave, false, 1},
      {QStringLiteral("Half"), kMissing, false, 1},
      {QStringLiteral("Gone"), kMissing, false, 2},
      {QStringLiteral("Renameable"), kWrongName, false, 1}, // present + fixable
  });
  GameListFilterProxy proxy;
  proxy.setSourceModel(m);

  // Only Empty games.
  proxy.setStateFilter(false, false, true);
  QCOMPARE(proxy.rowCount(), 1); // "Gone"

  // All states, but require Fixes → only the WrongName game.
  proxy.setStateFilter(true, true, true);
  proxy.setRequireFixes(true);
  QCOMPARE(proxy.rowCount(), 1); // "Renameable"
}

void TestDatAuditBrowserModels::romFileJoinsCatalogueAndStatus() {
  QList<DatLookup::DatRecord> recs;
  DatLookup::DatRecord a;
  a.romName = QStringLiteral("a.bin");
  a.crc = QStringLiteral("aaaa1111");
  a.cloneOf = QStringLiteral("parent");
  a.mia = true;
  DatLookup::DatRecord b;
  b.romName = QStringLiteral("b.bin");
  b.crc = QStringLiteral("bbbb2222");
  recs = {a, b};

  QList<ResultRow> results;
  ResultRow ra; // a.bin is present, an archive member at index 2
  ra.detail = QStringLiteral("a.bin");
  ra.status = kHave;
  ra.filePath = QStringLiteral("/roms/set.zip/a.bin");
  ra.zipIndex = 2;
  results = {ra}; // b.bin has no result → Missing

  RomFileModel m;
  m.setGame(recs, results);
  QCOMPARE(m.rowCount(), 2);

  // Row 0 (a.bin): Have, carries clone + mia from the catalogue.
  QCOMPARE(m.data(m.index(0, RomFileModel::RomColumn)).toString(), QStringLiteral("a.bin"));
  QCOMPARE(m.data(m.index(0, 0), RomFileModel::StatusRole).toInt(), kHave);
  QCOMPARE(m.data(m.index(0, RomFileModel::MergeColumn)).toString(), QStringLiteral("parent"));
  QCOMPARE(m.data(m.index(0, RomFileModel::MiaColumn)).toString(), QStringLiteral("yes"));
  QCOMPARE(m.data(m.index(0, RomFileModel::CrcColumn)).toString(), QStringLiteral("AAAA1111"));
  // ZipIndex column shows the member's archive index (Kartend-7iqhl.4).
  QCOMPARE(m.data(m.index(0, RomFileModel::ZipIndexColumn)).toString(), QStringLiteral("2"));

  // Row 1 (b.bin): no result row → Missing; no archive index → blank ZipIndex.
  QCOMPARE(m.data(m.index(1, 0), RomFileModel::StatusRole).toInt(), kMissing);
  QVERIFY(m.data(m.index(1, RomFileModel::ZipIndexColumn)).toString().isEmpty());
}

void TestDatAuditBrowserModels::romFileFallsBackToResultsWithoutCatalogue() {
  // Pre-v22 snapshot / no DAT path: catalogue empty, but result rows still
  // render so status is visible.
  ResultRow r;
  r.detail = QStringLiteral("orphan.bin");
  r.status = kMissing;
  RomFileModel m;
  m.setGame({}, {r});
  QCOMPARE(m.rowCount(), 1);
  QCOMPARE(m.data(m.index(0, RomFileModel::RomColumn)).toString(), QStringLiteral("orphan.bin"));
}

void TestDatAuditBrowserModels::groupsResultsByItemFolder() {
  // SubfolderPerItem (Kartend-m6qsb.30): two item-folders under the scan root,
  // plus a Missing rom attributed to its game (= folder name) so the folder's
  // total includes the absent ROM and reads as "Game A: 2/3 present".
  const QStringList scanRoots{QStringLiteral("/roms")};
  ResultRow a1;
  a1.status = kHave;
  a1.filePath = QStringLiteral("/roms/Game A/disc1.bin");
  ResultRow a2;
  a2.status = kHave;
  a2.filePath = QStringLiteral("/roms/Game A/disc2.bin");
  ResultRow aMissing; // a third rom of Game A is absent (no on-disk file)
  aMissing.status = kMissing;
  aMissing.gameName = QStringLiteral("Game A");
  ResultRow b1;
  b1.status = kWrongName;
  b1.filePath = QStringLiteral("/roms/Game B/rom.bin");

  const auto folders = DatAudit::groupResultsByFolder({a1, a2, aMissing, b1}, scanRoots);
  QCOMPARE(folders.size(), 2);
  // First-seen order: Game A (from a1), then Game B.
  QCOMPARE(folders.at(0).folder, QStringLiteral("Game A"));
  QCOMPARE(folders.at(0).counts.have, 2);
  QCOMPARE(folders.at(0).counts.missing, 1);
  QCOMPARE(folders.at(0).counts.total, 3); // 2 of 3 present
  QCOMPARE(folders.at(1).folder, QStringLiteral("Game B"));
  QCOMPARE(folders.at(1).counts.total, 1);
}

void TestDatAuditBrowserModels::browserPresetsRoundTrip() {
  // A saved slot round-trips every field through a real (temp) QSettings store
  // — no mocking. saveBrowserPreset / loadBrowserPresets manage their own group,
  // so the test just hands them a fresh IniFormat instance (Kartend-7iqhl.1).
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString iniPath = dir.filePath(QStringLiteral("ui-state.ini"));

  BrowserViewPreset p;
  p.name = QStringLiteral("MIA hunt");
  p.complete = false;
  p.partial = true;
  p.empty = false;
  p.fixes = true;
  p.mia = true;
  p.groupByFolder = true;
  p.search = QStringLiteral("zelda");

  {
    QSettings settings(iniPath, QSettings::IniFormat);
    saveBrowserPreset(settings, 2, p); // slot 2
  }

  QSettings settings(iniPath, QSettings::IniFormat);
  const QList<BrowserViewPreset> loaded = loadBrowserPresets(settings);
  QCOMPARE(loaded.size(), kBrowserPresetCount);
  const BrowserViewPreset &got = loaded.at(2);
  QCOMPARE(got.name, p.name);
  QCOMPARE(got.complete, p.complete);
  QCOMPARE(got.partial, p.partial);
  QCOMPARE(got.empty, p.empty);
  QCOMPARE(got.fixes, p.fixes);
  QCOMPARE(got.mia, p.mia);
  QCOMPARE(got.groupByFolder, p.groupByFolder);
  QCOMPARE(got.search, p.search);
}

void TestDatAuditBrowserModels::browserPresetsDefaultUnsavedSlots() {
  // An empty store yields four slots at the page's default filters, each named
  // "Preset N" — so the combo is always fully populated.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QSettings settings(dir.filePath(QStringLiteral("empty.ini")), QSettings::IniFormat);
  const QList<BrowserViewPreset> presets = loadBrowserPresets(settings);
  QCOMPARE(presets.size(), kBrowserPresetCount);
  const BrowserViewPreset defaults; // the documented defaults
  for (int i = 0; i < presets.size(); ++i) {
    const BrowserViewPreset &p = presets.at(i);
    QCOMPARE(p.name, QStringLiteral("Preset %1").arg(i + 1));
    QCOMPARE(p.complete, defaults.complete);
    QCOMPARE(p.partial, defaults.partial);
    QCOMPARE(p.empty, defaults.empty);
    QCOMPARE(p.fixes, defaults.fixes);
    QCOMPARE(p.mia, defaults.mia);
    QCOMPARE(p.groupByFolder, defaults.groupByFolder);
    QVERIFY(p.search.isEmpty());
  }
}

void TestDatAuditBrowserModels::reconstructsAuditRowsFromResults() {
  // The browser's profile-scoped Fix (Kartend-7iqhl.2) rebuilds fix-engine
  // AuditRows from the persisted snapshot. Verify the field mapping the Fix
  // plan relies on: status, filePath, expectedName (= detail), actualName
  // (= basename), gameName/sourceName/mia carried through.
  ResultRow wrong;
  wrong.status = kWrongName;
  wrong.filePath = QStringLiteral("/roms/Game A/badname.bin");
  wrong.detail = QStringLiteral("goodname.bin"); // the canonical rename target
  wrong.gameName = QStringLiteral("Game A");
  wrong.sourceName = QStringLiteral("snes.dat");
  wrong.mia = false;
  ResultRow missing; // entry-only Missing row: no on-disk file
  missing.status = kMissing;
  missing.gameName = QStringLiteral("Game B");
  missing.detail = QStringLiteral("rom.bin");
  missing.mia = true;

  const QList<AuditRow> rows = auditRowsFromResults({wrong, missing});
  QCOMPARE(rows.size(), 2);
  QCOMPARE(rows.at(0).status, static_cast<Status>(kWrongName));
  QCOMPARE(rows.at(0).filePath, QStringLiteral("/roms/Game A/badname.bin"));
  QCOMPARE(rows.at(0).expectedName, QStringLiteral("goodname.bin"));
  QCOMPARE(rows.at(0).actualName, QStringLiteral("badname.bin"));
  QCOMPARE(rows.at(0).gameName, QStringLiteral("Game A"));
  QCOMPARE(rows.at(0).sourceName, QStringLiteral("snes.dat"));
  QVERIFY(!rows.at(0).mia);
  // Entry-only row: no path → no actualName; mia preserved.
  QCOMPARE(rows.at(1).status, static_cast<Status>(kMissing));
  QVERIFY(rows.at(1).filePath.isEmpty());
  QVERIFY(rows.at(1).actualName.isEmpty());
  QCOMPARE(rows.at(1).expectedName, QStringLiteral("rom.bin"));
  QVERIFY(rows.at(1).mia);
}

void TestDatAuditBrowserModels::categoryLabelResolutionPrecedence() {
  // Kartend-7iqhl.5: manual category wins; else the linked collection's name;
  // else "(Ungrouped)".
  const QHash<QString, QString> names{{QStringLiteral("uuid-movies"), QStringLiteral("Movies")}};

  Profile manual;
  manual.category = QStringLiteral("Favourites");
  manual.collectionUuid = QStringLiteral("uuid-movies"); // manual still wins
  QCOMPARE(categoryLabelFor(manual, names), QStringLiteral("Favourites"));

  Profile linked;
  linked.collectionUuid = QStringLiteral("uuid-movies");
  QCOMPARE(categoryLabelFor(linked, names), QStringLiteral("Movies"));

  Profile unlinked;
  QCOMPARE(categoryLabelFor(unlinked, names), QStringLiteral("(Ungrouped)"));

  Profile danglingLink; // linked to a collection not in the map
  danglingLink.collectionUuid = QStringLiteral("uuid-gone");
  QCOMPARE(categoryLabelFor(danglingLink, names), QStringLiteral("(Ungrouped)"));
}

void TestDatAuditBrowserModels::treeBuildsCategoryLevel() {
  // Three profiles across two categories (manual on A, collection-derived on B
  // and C), grouped: Root → Category → Profile. First-seen category order.
  Profile a;
  a.id = 1;
  a.name = QStringLiteral("Alpha");
  a.category = QStringLiteral("Retro");
  Profile b;
  b.id = 2;
  b.name = QStringLiteral("Bravo");
  b.collectionUuid = QStringLiteral("uuid-films");
  Profile c;
  c.id = 3;
  c.name = QStringLiteral("Charlie");
  c.collectionUuid = QStringLiteral("uuid-films"); // same category as Bravo

  const QHash<QString, QString> names{{QStringLiteral("uuid-films"), QStringLiteral("Films")}};
  const QList<RollupRow> rollups{
      {1, QStringLiteral("a.dat"), kHave, false, 4},
      {2, QStringLiteral("b.dat"), kHave, false, 3},
      {2, QStringLiteral("b.dat"), kMissing, false, 1},
      {3, QStringLiteral("c.dat"), kMissing, false, 2},
  };

  AuditTreeModel tree;
  tree.setTree({a, b, c}, rollups, /*groupByCategory=*/true, names);

  // Root → 2 categories in first-seen order: "Retro" (from Alpha), then "Films".
  QCOMPARE(tree.rowCount(QModelIndex()), 2);
  const QModelIndex retro = tree.index(0, 0);
  const QModelIndex films = tree.index(1, 0);
  QCOMPARE(tree.data(retro, Qt::DisplayRole).toString(), QStringLiteral("Retro"));
  QCOMPARE(tree.data(films, Qt::DisplayRole).toString(), QStringLiteral("Films"));
  // Category nodes are not profiles.
  QCOMPARE(tree.profileIdAt(retro), static_cast<qint64>(-1));
  QCOMPARE(
      static_cast<AuditTreeModel::NodeKind>(tree.data(retro, AuditTreeModel::KindRole).toInt()),
      AuditTreeModel::NodeKind::Category);

  // "Retro" has Alpha; "Films" has Bravo + Charlie.
  QCOMPARE(tree.rowCount(retro), 1);
  QCOMPARE(tree.rowCount(films), 2);
  const QModelIndex alpha = tree.index(0, 0, retro);
  QCOMPARE(tree.profileIdAt(alpha), static_cast<qint64>(1));
  QCOMPARE(tree.parent(alpha), retro); // 3-level parent() round-trip

  // Category counts roll up their member profiles (Films: 3 have + 1+2 missing).
  const auto filmsCounts =
      qvariant_cast<BucketCounts>(tree.data(films, AuditTreeModel::CountsRole));
  QCOMPARE(filmsCounts.have, 3);
  QCOMPARE(filmsCounts.missing, 3);
}

void TestDatAuditBrowserModels::treeGroupedMultiSourceParentChain() {
  // A multi-source profile under a category exercises the full
  // Source → Profile → Category → root parent() chain (Kartend-7iqhl.5).
  Profile p;
  p.id = 1;
  p.name = QStringLiteral("Arcade");
  p.category = QStringLiteral("Coin-op");
  const QList<RollupRow> rollups{
      {1, QStringLiteral("mame.dat"), kHave, false, 5},
      {1, QStringLiteral("fbneo.dat"), kHave, false, 3},
  };
  AuditTreeModel tree;
  tree.setTree({p}, rollups, /*groupByCategory=*/true, {});

  QCOMPARE(tree.rowCount(QModelIndex()), 1); // one category
  const QModelIndex cat = tree.index(0, 0);
  QCOMPARE(tree.data(cat, Qt::DisplayRole).toString(), QStringLiteral("Coin-op"));
  QCOMPARE(tree.rowCount(cat), 1); // one profile under it
  const QModelIndex prof = tree.index(0, 0, cat);
  QCOMPARE(tree.profileIdAt(prof), static_cast<qint64>(1));
  QCOMPARE(tree.parent(prof), cat); // Profile → Category
  QCOMPARE(tree.rowCount(prof), 2); // two source children
  const QModelIndex src0 = tree.index(0, 0, prof);
  QCOMPARE(tree.parent(src0), prof);             // Source → Profile
  QCOMPARE(tree.parent(tree.parent(src0)), cat); // …→ Category (full chain)
}

QTEST_GUILESS_MAIN(TestDatAuditBrowserModels)
#include "test_datauditbrowsermodels.moc"
