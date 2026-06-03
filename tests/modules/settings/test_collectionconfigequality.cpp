// Field-coverage test for CollectionConfig::operator==. Dirty-tracking in the
// settings dialog relies on operator== catching ANY field change; a silently
// omitted field (the historical hideMissingArtwork bug) makes an edit look
// clean and get dropped. This test mutates each compared field in turn and
// asserts the difference is detected, so a future field added to the struct
// but forgotten in operator== fails here. (Kartend-bztw8)
#include <functional>

#include <QHash>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVariant>

#include "collection/collectionconfig.h"
#include "collectiontypes.h"

class TestCollectionConfigEquality : public QObject {
  Q_OBJECT
private slots:
  void twoDefaultsAreEqual();
  void mutatingAnyComparedFieldBreaksEquality();
  void preservedKeysIsExcludedFromEquality();
};

void TestCollectionConfigEquality::twoDefaultsAreEqual() {
  const CollectionConfig a;
  const CollectionConfig b;
  QVERIFY(a == b);
}

void TestCollectionConfigEquality::mutatingAnyComparedFieldBreaksEquality() {
  // Each entry mutates exactly one field on a default config; operator== must
  // report the configs as unequal. If a field is added to the struct and
  // compared here but missing from operator==, the matching row fails with the
  // field name — that's the omission guard.
  const auto detects = [](const char *field,
                          const std::function<void(CollectionConfig &)> &mutate) {
    const CollectionConfig base;
    CollectionConfig changed;
    mutate(changed);
    QVERIFY2(!(base == changed),
             qPrintable(QStringLiteral("operator== does not detect a change to '%1'")
                            .arg(QLatin1String(field))));
  };

  // Strings.
  detects("name", [](CollectionConfig &c) { c.name = QStringLiteral("x"); });
  detects("type", [](CollectionConfig &c) { c.type = QStringLiteral("x"); });
  detects("mediaDirectory", [](CollectionConfig &c) { c.mediaDirectory = QStringLiteral("x"); });
  detects("artworkDirectory",
          [](CollectionConfig &c) { c.artworkDirectory = QStringLiteral("x"); });
  detects("videoDirectory", [](CollectionConfig &c) { c.videoDirectory = QStringLiteral("x"); });
  detects("manualDirectory", [](CollectionConfig &c) { c.manualDirectory = QStringLiteral("x"); });
  detects("placeholderArtwork",
          [](CollectionConfig &c) { c.placeholderArtwork = QStringLiteral("x"); });
  detects("collectionIcon", [](CollectionConfig &c) { c.collectionIcon = QStringLiteral("x"); });
  detects("customFontFamily",
          [](CollectionConfig &c) { c.customFontFamily = QStringLiteral("x"); });
  detects("playlistId", [](CollectionConfig &c) { c.playlistId = QStringLiteral("x"); });
  detects("playlistReservedKind",
          [](CollectionConfig &c) { c.playlistReservedKind = QStringLiteral("favorites"); });

  // String lists.
  detects("extensions", [](CollectionConfig &c) { c.extensions = {QStringLiteral("zip")}; });
  detects("customArtworkTypes",
          [](CollectionConfig &c) { c.customArtworkTypes = {QStringLiteral("box")}; });
  detects("additionalParentNames",
          [](CollectionConfig &c) { c.additionalParentNames = {QStringLiteral("P")}; });

  // Scalars / bools.
  detects("parentCollectionIndex", [](CollectionConfig &c) { c.parentCollectionIndex = 5; });
  detects("isSubcollection", [](CollectionConfig &c) { c.isSubcollection = true; });
  detects("showAllSubcollectionItems",
          [](CollectionConfig &c) { c.showAllSubcollectionItems = true; });
  detects("hideTitles", [](CollectionConfig &c) { c.hideTitles = true; });
  detects("hideSubcollectionTitles", [](CollectionConfig &c) { c.hideSubcollectionTitles = true; });
  detects("hideMissingArtwork", [](CollectionConfig &c) { c.hideMissingArtwork = true; });
  detects("expandMode", [](CollectionConfig &c) { c.expandMode = true; });
  detects("watchFilesystem", [](CollectionConfig &c) { c.watchFilesystem = true; });
  detects("isPlaylist", [](CollectionConfig &c) { c.isPlaylist = true; });
  detects("isSmartPlaylist", [](CollectionConfig &c) { c.isSmartPlaylist = true; });

  // Enums.
  detects("horizontalAlignment",
          [](CollectionConfig &c) { c.horizontalAlignment = HorizontalAlignment::Left; });
  detects("viewType", [](CollectionConfig &c) { c.viewType = ViewType::List; });

  // Nested value structs (mutate one sub-field each).
  detects("launcher", [](CollectionConfig &c) { c.launcher.launcherPath = QStringLiteral("x"); });
  detects("gridLayout", [](CollectionConfig &c) { c.gridLayout.gridWidth = 99; });
  detects("filter", [](CollectionConfig &c) { c.filter.titleExclusionEnabled = false; });
  detects("sidebar", [](CollectionConfig &c) { c.sidebar.sidebarVisible = true; });
  detects("background",
          [](CollectionConfig &c) { c.background.backgroundColor = QStringLiteral("#123456"); });
  detects("archive", [](CollectionConfig &c) { c.archive.extractArchives = true; });
  detects("folderBrowsing",
          [](CollectionConfig &c) { c.folderBrowsing.includeContentSubfolders = true; });
  detects("listView", [](CollectionConfig &c) { c.listView.listFontSize = 99; });
  detects("scraperOverrides",
          [](CollectionConfig &c) { c.scraperOverrides.screenscraperSystemId = 5; });
}

void TestCollectionConfigEquality::preservedKeysIsExcludedFromEquality() {
  // preservedKeys is intentionally NOT part of operator== (it round-trips via
  // its own load/save path). Lock that exclusion in so it isn't "fixed" by
  // mistake — adding it would make every config with passthrough keys look
  // dirty.
  const CollectionConfig base;
  CollectionConfig changed;
  changed.preservedKeys.insert(QStringLiteral("unknownFutureKey"), QVariant(42));
  QVERIFY2(base == changed,
           "preservedKeys must stay excluded from operator== (it round-trips via its own path)");
}

QTEST_APPLESS_MAIN(TestCollectionConfigEquality)
#include "test_collectionconfigequality.moc"
