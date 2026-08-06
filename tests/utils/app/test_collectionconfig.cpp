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

QTEST_MAIN(TestCollectionConfig)
#include "test_collectionconfig.moc"
