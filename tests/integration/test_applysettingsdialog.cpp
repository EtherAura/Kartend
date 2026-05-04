#include "test_applysettingsdialog.h"

#include "applysettingsdialog.h"
#include "collectionutils.h"
#include "settingsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QList>
#include <QTest>

namespace {

QList<CollectionConfig> makeCollections() {
  CollectionConfig src;
  src.name = QStringLiteral("Source");
  src.gridWidth = 8;
  src.itemWidth = 220;
  src.itemHeight = 330;
  src.cornerRadius = 12;
  src.fontSize = 18;
  src.customFontFamily = QStringLiteral("Inter");
  src.backgroundColor = QStringLiteral("#112233");
  src.primaryColor = QStringLiteral("#445566");
  src.tileColor = QStringLiteral("#778899");
  src.selectionColor = QStringLiteral("#aabbcc");
  src.hideTitles = true;
  src.sidebarMode = SidebarMode::Expand;
  src.listFontSize = 22;
  src.listRowHeight = 64;
  // Identity / paths / scan flags — must NEVER be copied regardless of mask.
  src.mediaDirectory = QStringLiteral("/src/media");
  src.artworkDirectory = QStringLiteral("/src/artwork");
  src.extensions = QStringList{QStringLiteral(".sfc")};
  src.includeContentSubfolders = true;
  src.showAllSubcollectionItems = true;

  CollectionConfig dst;
  dst.name = QStringLiteral("Destination");
  dst.gridWidth = 4;
  dst.itemWidth = 100;
  dst.itemHeight = 150;
  dst.cornerRadius = 4;
  dst.fontSize = 11;
  dst.customFontFamily = QStringLiteral("Sans");
  dst.backgroundColor = QStringLiteral("#000000");
  dst.primaryColor = QStringLiteral("#111111");
  dst.tileColor = QStringLiteral("#222222");
  dst.selectionColor = QStringLiteral("#333333");
  dst.hideTitles = false;
  dst.sidebarMode = SidebarMode::Overlay;
  dst.listFontSize = 12;
  dst.listRowHeight = 28;
  dst.mediaDirectory = QStringLiteral("/dst/media");
  dst.artworkDirectory = QStringLiteral("/dst/artwork");
  dst.extensions = QStringList{QStringLiteral(".gba")};
  dst.includeContentSubfolders = false;
  dst.showAllSubcollectionItems = false;

  CollectionConfig playlist;
  playlist.name = QStringLiteral("Favorites");
  playlist.isPlaylist = true;

  return {src, dst, playlist};
}

} // namespace

void TestApplySettingsDialog::testDefaultMaskIsAll() {
  ApplySettingsDialog dialog(ApplySettingsDialog::Mode::Push, makeCollections(), 0,
                             ApplySettingsDialog::All);
  QCOMPARE(dialog.selectedCategories(), ApplySettingsDialog::FieldCategories(ApplySettingsDialog::All));
}

void TestApplySettingsDialog::testSelectNoneClearsMask() {
  ApplySettingsDialog dialog(ApplySettingsDialog::Mode::Push, makeCollections(), 0,
                             ApplySettingsDialog::All);
  for (auto *cb : dialog.findChildren<QCheckBox *>()) {
    cb->setChecked(false);
  }
  QCOMPARE(dialog.selectedCategories(),
           ApplySettingsDialog::FieldCategories(ApplySettingsDialog::None));
}

void TestApplySettingsDialog::testSelectAllRestoresMask() {
  ApplySettingsDialog dialog(ApplySettingsDialog::Mode::Push, makeCollections(), 0,
                             ApplySettingsDialog::None);
  QCOMPARE(dialog.selectedCategories(),
           ApplySettingsDialog::FieldCategories(ApplySettingsDialog::None));
  for (auto *cb : dialog.findChildren<QCheckBox *>()) {
    cb->setChecked(true);
  }
  QCOMPARE(dialog.selectedCategories(),
           ApplySettingsDialog::FieldCategories(ApplySettingsDialog::All));
}

void TestApplySettingsDialog::testPushModeHasNoSourceCombo() {
  ApplySettingsDialog dialog(ApplySettingsDialog::Mode::Push, makeCollections(), 0);
  QCOMPARE(dialog.selectedSourceIndex(), -1);
  QVERIFY(dialog.findChildren<QComboBox *>().isEmpty());
}

void TestApplySettingsDialog::testPullModeExcludesCurrentAndPlaylists() {
  // currentIndex 1 = "Destination". The combo should list "Source" but skip
  // both the current collection (index 1) and the playlist (index 2).
  ApplySettingsDialog dialog(ApplySettingsDialog::Mode::Pull, makeCollections(), 1);
  auto combos = dialog.findChildren<QComboBox *>();
  QCOMPARE(combos.size(), 1);
  auto *combo = combos.first();
  QCOMPARE(combo->count(), 1);
  QCOMPARE(combo->itemText(0), QStringLiteral("Source"));
  QCOMPARE(dialog.selectedSourceIndex(), 0);
}

void TestApplySettingsDialog::testApplyCategoriesGridLayoutOnly() {
  // Build a real SettingsDialog and use its mask-aware applyCategoriesToIndices.
  // currentCollectionIndex defaults to whatever the dialog selects on open;
  // pass 0 so "Source" is the active collection and we can target index 1.
  SettingsDialog dialog(nullptr, makeCollections(), 0);
  const int applied = dialog.applyCategoriesToIndices({1}, ApplySettingsDialog::GridLayout, 0);
  QCOMPARE(applied, 1);

  const auto &collections = dialog.getCollections();
  QCOMPARE(collections[1].gridWidth, 8);     // copied
  QCOMPARE(collections[1].itemWidth, 220);   // copied
  QCOMPARE(collections[1].cornerRadius, 12); // copied
  // Untouched by GridLayout-only:
  QCOMPARE(collections[1].fontSize, 11);
  QCOMPARE(collections[1].backgroundColor, QStringLiteral("#000000"));
  QCOMPARE(collections[1].listRowHeight, 28);
  QCOMPARE(collections[1].hideTitles, false);
  // Identity / paths / scan flags untouched (regression guard):
  QCOMPARE(collections[1].name, QStringLiteral("Destination"));
  QCOMPARE(collections[1].mediaDirectory, QStringLiteral("/dst/media"));
  QCOMPARE(collections[1].includeContentSubfolders, false);
}

void TestApplySettingsDialog::testApplyCategoriesColorsOnly() {
  SettingsDialog dialog(nullptr, makeCollections(), 0);
  const int applied = dialog.applyCategoriesToIndices({1}, ApplySettingsDialog::Colors, 0);
  QCOMPARE(applied, 1);

  const auto &collections = dialog.getCollections();
  QCOMPARE(collections[1].backgroundColor, QStringLiteral("#112233"));
  QCOMPARE(collections[1].primaryColor, QStringLiteral("#445566"));
  QCOMPARE(collections[1].tileColor, QStringLiteral("#778899"));
  QCOMPARE(collections[1].selectionColor, QStringLiteral("#aabbcc"));
  // Untouched:
  QCOMPARE(collections[1].gridWidth, 4);
  QCOMPARE(collections[1].itemWidth, 100);
  QCOMPARE(collections[1].fontSize, 11);
  QCOMPARE(collections[1].listRowHeight, 28);
}

void TestApplySettingsDialog::testApplyCategoriesAllPreservesPathsAndScanFlags() {
  SettingsDialog dialog(nullptr, makeCollections(), 0);
  const int applied = dialog.applyCategoriesToIndices({1}, ApplySettingsDialog::All, 0);
  QCOMPARE(applied, 1);

  const auto &collections = dialog.getCollections();
  // All curated fields copied:
  QCOMPARE(collections[1].gridWidth, 8);
  QCOMPARE(collections[1].fontSize, 18);
  QCOMPARE(collections[1].backgroundColor, QStringLiteral("#112233"));
  QCOMPARE(collections[1].listRowHeight, 64);
  QCOMPARE(collections[1].hideTitles, true);
  QCOMPARE(collections[1].sidebarMode, SidebarMode::Expand);
  // Identity, paths, extensions, scan flags must be untouched even with the
  // full mask — that's the safety guarantee Kartend-iyk preserves.
  QCOMPARE(collections[1].name, QStringLiteral("Destination"));
  QCOMPARE(collections[1].mediaDirectory, QStringLiteral("/dst/media"));
  QCOMPARE(collections[1].artworkDirectory, QStringLiteral("/dst/artwork"));
  QCOMPARE(collections[1].extensions, QStringList{QStringLiteral(".gba")});
  QCOMPARE(collections[1].includeContentSubfolders, false);
  QCOMPARE(collections[1].showAllSubcollectionItems, false);
}

void TestApplySettingsDialog::testApplyCategoriesEmptyMaskIsNoOp() {
  SettingsDialog dialog(nullptr, makeCollections(), 0);
  const int applied = dialog.applyCategoriesToIndices({1}, ApplySettingsDialog::None, 0);
  QCOMPARE(applied, 0);

  const auto &collections = dialog.getCollections();
  // Destination unchanged.
  QCOMPARE(collections[1].gridWidth, 4);
  QCOMPARE(collections[1].backgroundColor, QStringLiteral("#000000"));
}

void TestApplySettingsDialog::testApplyCategoriesSkipsSourceIndex() {
  SettingsDialog dialog(nullptr, makeCollections(), 0);
  // Including the source itself in the target list must be a silent skip;
  // applied count counts only real mutations.
  const int applied = dialog.applyCategoriesToIndices({0, 1}, ApplySettingsDialog::All, 0);
  QCOMPARE(applied, 1);
  const auto &collections = dialog.getCollections();
  QCOMPARE(collections[0].name, QStringLiteral("Source"));
  QCOMPARE(collections[0].gridWidth, 8); // unchanged
}
