// Tests for CollectionConfig::operator==. The struct accretes fields over time
// and each scalar field must be wired into operator==, or dirty-tracking on a
// settings save compares two configs as equal and silently skips persisting a
// changed field (Kartend-bdm5: hideMissingArtwork was omitted).
#include "collection/collectionconfig.h"

#include <functional>
#include <vector>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

class TestCollectionConfig : public QObject {
  Q_OBJECT
private slots:
  void negativeSpacingMigratesWithoutMovingTheGrid();
  void defaultConfigsCompareEqual();
  void hideMissingArtworkDifferenceDetected();
  void scalarFieldDifferencesDetected();
};

void TestCollectionConfig::defaultConfigsCompareEqual() {
  CollectionConfig a;
  CollectionConfig b;
  QVERIFY(a == b);
}

void TestCollectionConfig::hideMissingArtworkDifferenceDetected() {
  // The specific omission this issue fixed: two configs differing only in the
  // hideMissingArtwork toggle must compare unequal (Kartend-bdm5).
  CollectionConfig base;
  CollectionConfig flipped;
  flipped.hideMissingArtwork = true;
  QVERIFY(!(base == flipped));
}

void TestCollectionConfig::scalarFieldDifferencesDetected() {
  // Field-enumerating guard: flipping any single scalar field away from its
  // default must break equality. Catches a new scalar field being added to the
  // struct but forgotten in operator== — the bug class behind Kartend-bdm5.
  // (Sub-struct members — launcher, gridLayout, background, … — carry their own
  // operator== and are exercised by their own tests.)
  struct Case {
    const char *field;
    std::function<void(CollectionConfig &)> mutate;
  };
  const std::vector<Case> cases = {
      {"name", [](CollectionConfig &c) { c.name = QStringLiteral("x"); }},
      {"type", [](CollectionConfig &c) { c.type = QStringLiteral("x"); }},
      {"mediaDirectory", [](CollectionConfig &c) { c.mediaDirectory = QStringLiteral("x"); }},
      {"artworkDirectory", [](CollectionConfig &c) { c.artworkDirectory = QStringLiteral("x"); }},
      {"videoDirectory", [](CollectionConfig &c) { c.videoDirectory = QStringLiteral("x"); }},
      {"manualDirectory", [](CollectionConfig &c) { c.manualDirectory = QStringLiteral("x"); }},
      {"placeholderArtwork",
       [](CollectionConfig &c) { c.placeholderArtwork = QStringLiteral("x"); }},
      {"collectionIcon", [](CollectionConfig &c) { c.collectionIcon = QStringLiteral("x"); }},
      {"extensions", [](CollectionConfig &c) { c.extensions = {QStringLiteral("x")}; }},
      {"customArtworkTypes",
       [](CollectionConfig &c) { c.customArtworkTypes = {QStringLiteral("x")}; }},
      {"parentCollectionIndex", [](CollectionConfig &c) { c.parentCollectionIndex = 7; }},
      {"isSubcollection", [](CollectionConfig &c) { c.isSubcollection = true; }},
      {"showAllSubcollectionItems",
       [](CollectionConfig &c) { c.showAllSubcollectionItems = true; }},
      {"hideTitles", [](CollectionConfig &c) { c.hideTitles = true; }},
      {"hideSubcollectionTitles", [](CollectionConfig &c) { c.hideSubcollectionTitles = true; }},
      {"horizontalAlignment",
       [](CollectionConfig &c) { c.horizontalAlignment = HorizontalAlignment::Left; }},
      {"viewType", [](CollectionConfig &c) { c.viewType = ViewType::List; }},
      {"hideMissingArtwork", [](CollectionConfig &c) { c.hideMissingArtwork = true; }},
      {"expandMode", [](CollectionConfig &c) { c.expandMode = true; }},
      {"watchFilesystem", [](CollectionConfig &c) { c.watchFilesystem = true; }},
      {"importSource", [](CollectionConfig &c) { c.importSource = QStringLiteral("steam"); }},
      {"customFontFamily", [](CollectionConfig &c) { c.customFontFamily = QStringLiteral("x"); }},
      {"isPlaylist", [](CollectionConfig &c) { c.isPlaylist = true; }},
      {"playlistId", [](CollectionConfig &c) { c.playlistId = QStringLiteral("x"); }},
      {"isSmartPlaylist", [](CollectionConfig &c) { c.isSmartPlaylist = true; }},
      {"playlistReservedKind",
       [](CollectionConfig &c) { c.playlistReservedKind = QStringLiteral("x"); }},
      {"additionalParentNames",
       [](CollectionConfig &c) { c.additionalParentNames = {QStringLiteral("x")}; }},
  };
  for (const auto &cse : cases) {
    CollectionConfig base;
    CollectionConfig mutated;
    cse.mutate(mutated);
    QVERIFY2(
        !(base == mutated),
        qPrintable(QStringLiteral("operator== ignores field: %1").arg(QLatin1String(cse.field))));
  }
}

void TestCollectionConfig::negativeSpacingMigratesWithoutMovingTheGrid() {
  // Kartend-hxly2. Spacing is the gap between tile edges and cannot be
  // negative. Configs predating that stored negative values meaning something
  // else entirely — cells placed closer than their own size, with the tile then
  // shrunk to clear its neighbour. The migration has to keep such a grid
  // LOOKING the same, because these were tuned by eye to fit a window: pitch is
  // what decides how many columns fit, so pitch is what must be preserved.
  //
  // Real reported config: 325px cells at -80, which rendered 237px tiles on a
  // 245px pitch.
  CollectionConfig cfg;
  cfg.gridLayout.itemWidth = 325;
  cfg.gridLayout.itemHeight = 325;
  cfg.gridLayout.horizontalSpacing = -80;
  cfg.gridLayout.verticalSpacing = -80;
  const int pitchBeforeX = cfg.gridLayout.itemWidth + cfg.gridLayout.horizontalSpacing;
  const int pitchBeforeY = cfg.gridLayout.itemHeight + cfg.gridLayout.verticalSpacing;

  cfg.clampValues();

  QVERIFY2(cfg.gridLayout.horizontalSpacing >= 0, "horizontal spacing left negative");
  QVERIFY2(cfg.gridLayout.verticalSpacing >= 0, "vertical spacing left negative");
  QCOMPARE(cfg.gridLayout.itemWidth + cfg.gridLayout.horizontalSpacing, pitchBeforeX);
  QCOMPARE(cfg.gridLayout.itemHeight + cfg.gridLayout.verticalSpacing, pitchBeforeY);
  QCOMPARE(cfg.gridLayout.itemWidth, 237);
  QCOMPARE(cfg.gridLayout.horizontalSpacing, 8);

  // Idempotent — clampValues runs on every load, import and preset apply, so a
  // second pass must not shrink the grid again.
  const GridLayoutPreferences once = cfg.gridLayout;
  cfg.clampValues();
  QCOMPARE(cfg.gridLayout.itemWidth, once.itemWidth);
  QCOMPARE(cfg.gridLayout.itemHeight, once.itemHeight);
  QCOMPARE(cfg.gridLayout.horizontalSpacing, once.horizontalSpacing);
  QCOMPARE(cfg.gridLayout.verticalSpacing, once.verticalSpacing);

  // A config that was already honest is untouched.
  CollectionConfig fine;
  fine.gridLayout.itemWidth = 300;
  fine.gridLayout.itemHeight = 300;
  fine.gridLayout.horizontalSpacing = 20;
  fine.gridLayout.verticalSpacing = 20;
  fine.clampValues();
  QCOMPARE(fine.gridLayout.itemWidth, 300);
  QCOMPARE(fine.gridLayout.horizontalSpacing, 20);
}

QTEST_MAIN(TestCollectionConfig)
#include "test_collectionconfig.moc"
