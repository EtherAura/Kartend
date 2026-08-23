// Tests for strict round-trip preservation of unknown per-collection INI
// keys. A newer build that writes a key (a future feature flag, an
// experimental setting) followed by an older build that loads + saves the
// same file must not silently drop the unknown key. Coverage protects
// against a version-skew data-loss class.

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QTextStream>

#include "../../support/machomesandbox.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "settingsmanager.h"
#include "settingsutils.h"

class TestSettingsRoundtrip : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();

  void unknownPerCollectionKey_preservedThroughLoadSave();
  void importSourceKey_survivesLoadSave();
  void knownKey_overwritesPreservedDuplicate();
  void legacyBlocklistedKey_droppedNotPreserved();
  void extensions_legacyGlobFormMigratesToBare();
  void globalSections_survivePerCollectionSave();
  void clampedOutOfRangeValue_persistedOnLoad();
  void uuidCollisionCapturedForStartupWarning();
  // Kartend-ob1c9: the collection tree panel's per-collection block.
  void collectionTreeBlock_roundTripsAndClampsPosition();
  void viewSetting_scrollbarsOnHoverOnly_roundTrips();
  void toolbarColorSource_roundTripsAndFallsBackOnGarbage();
  /// v1->v2 stamps the 2026-08-17 sidebar layout defaults onto every
  /// existing collection ONCE — and never again after the version stamp, so
  /// a user's post-migration edit survives the next boot.
  void migration_v1StampsSidebarLayoutOntoCollectionsOnce();

private:
  static void writeConfigIni(const QString &iniContents);
  static QString readConfigIni();
};

void TestSettingsRoundtrip::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
}

void TestSettingsRoundtrip::cleanupTestCase() {
  QStandardPaths::setTestModeEnabled(false);
}

void TestSettingsRoundtrip::init() {
  const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
  QVERIFY2(configRoot.contains(QStringLiteral("qttest")),
           "QStandardPaths test mode should reroute ConfigLocation under qttest");
  QDir(configRoot).removeRecursively();
}

void TestSettingsRoundtrip::writeConfigIni(const QString &iniContents) {
  const QString dest = SettingsUtils::getConfigPath();
  QFile::remove(dest);
  QDir().mkpath(QFileInfo(dest).absolutePath());
  QFile f(dest);
  QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Text),
           qPrintable(QStringLiteral("Failed to open %1 for write").arg(dest)));
  QTextStream out(&f);
  out << iniContents;
  f.close();
}

QString TestSettingsRoundtrip::readConfigIni() {
  QFile f(SettingsUtils::getConfigPath());
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  return QString::fromUtf8(f.readAll());
}

void TestSettingsRoundtrip::unknownPerCollectionKey_preservedThroughLoadSave() {
  // Seed a collection whose section carries a key this build doesn't
  // recognise. Imagine a future Kartend build added it and an older build
  // (this one) is now loading + saving the same file.
  writeConfigIni(QStringLiteral("[TestCol]\n"
                                "name=TestCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "futureFeatureEnabled=true\n"
                                "experimentalScrollMode=trampoline\n"));

  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections;
  mgr.loadCollections(collections);
  QCOMPARE(collections.size(), 1);
  QCOMPARE(collections[0].name, QStringLiteral("TestCol"));
  QVERIFY(collections[0].preservedKeys.contains(QStringLiteral("futureFeatureEnabled")));
  QVERIFY(collections[0].preservedKeys.contains(QStringLiteral("experimentalScrollMode")));

  // Save without modifying the unknown keys; they must survive.
  mgr.saveCollections(collections);

  const QString rewritten = readConfigIni();
  QVERIFY2(
      rewritten.contains(QStringLiteral("futureFeatureEnabled=true")),
      qPrintable(QStringLiteral("Unknown bool key was dropped on round-trip:\n%1").arg(rewritten)));
  QVERIFY2(rewritten.contains(QStringLiteral("experimentalScrollMode=trampoline")),
           qPrintable(
               QStringLiteral("Unknown string key was dropped on round-trip:\n%1").arg(rewritten)));
}

// Kartend-ilkne: importSourceKey decides WHICH slice of a multi-collection
// source a collection holds. If it did not persist, a restart would re-sync
// every ES-DE collection against the whole library and write every system's
// games into each one.
//
// The config is built IN MEMORY rather than seeded from INI, and that is the
// whole point of the case: every key read from a file is echoed back by the
// preserved-unknown-keys mechanism, so a seeded value round-trips even with
// the save wiring deleted. Only a collection that was never loaded — exactly
// what the importer creates — actually exercises it.
void TestSettingsRoundtrip::importSourceKey_survivesLoadSave() {
  CollectionConfig created;
  created.name = QStringLiteral("ES-DE: snes");
  created.mediaDirectory = QStringLiteral("/tmp/media");
  created.importSource = QStringLiteral("esde");
  created.importSourceKey = QStringLiteral("snes");
  QVERIFY(created.preservedKeys.isEmpty()); // nothing to echo it back

  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections{created};
  mgr.saveCollections(collections);

  const QString written = readConfigIni();
  QVERIFY2(written.contains(QStringLiteral("importSourceKey=snes")),
           qPrintable(QStringLiteral("importSourceKey never reached the INI:\n%1").arg(written)));

  // …and comes back on the next load.
  QList<CollectionConfig> reloaded;
  mgr.loadCollections(reloaded);
  QCOMPARE(reloaded.size(), 1);
  QCOMPARE(reloaded[0].importSourceKey, QStringLiteral("snes"));
}

void TestSettingsRoundtrip::viewSetting_scrollbarsOnHoverOnly_roundTrips() {
  writeConfigIni(QStringLiteral("[General]\nschemaVersion=4\n"
                                "scrollbarsOnHoverOnly=true\n"));
  SettingsManager mgr(nullptr, nullptr);
  GeneralSettings loaded;
  mgr.loadGeneralSettings(loaded);
  QVERIFY(loaded.view.scrollbarsOnHoverOnly);

  loaded.view.scrollbarsOnHoverOnly = false;
  mgr.saveGeneralSettings(loaded);
  GeneralSettings reloaded;
  mgr.loadGeneralSettings(reloaded);
  QVERIFY2(!reloaded.view.scrollbarsOnHoverOnly, "the off state must survive a save/load cycle");
}

void TestSettingsRoundtrip::toolbarColorSource_roundTripsAndFallsBackOnGarbage() {
  writeConfigIni(QStringLiteral("[General]\nschemaVersion=4\n\n"
                                "[Accent]\nname=Accent\nmediaDirectory=/tmp/media\n"
                                "toolbarColorSource=accent\n\n"
                                "[Junk]\nname=Junk\nmediaDirectory=/tmp/media\n"
                                "toolbarColorSource=wallpaper-ish\n"));
  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> loaded;
  mgr.loadCollections(loaded);
  QCOMPARE(loaded.size(), 2);
  QHash<QString, ToolbarColorSource> bySection;
  for (const CollectionConfig &cfg : loaded) {
    bySection[cfg.name] = cfg.background.toolbarColorSource;
  }
  QCOMPARE(bySection.value(QStringLiteral("Accent")), ToolbarColorSource::Accent);
  // An unrecognised value must not leave the toolbar unpainted — it lands
  // on the default rather than something undefined.
  QCOMPARE(bySection.value(QStringLiteral("Junk")), ToolbarColorSource::Titlebar);

  mgr.saveCollections(loaded);
  QList<CollectionConfig> reloaded;
  mgr.loadCollections(reloaded);
  QCOMPARE(reloaded.size(), 2);
  for (const CollectionConfig &cfg : reloaded) {
    if (cfg.name == QStringLiteral("Accent")) {
      QCOMPARE(cfg.background.toolbarColorSource, ToolbarColorSource::Accent);
    }
  }
}

void TestSettingsRoundtrip::uuidCollisionCapturedForStartupWarning() {
  // Two collections with the same name + mediaDirectory compute the same
  // canonical UUID — the data-corruption case validateAllCollections flags.
  // loadCollections must CAPTURE it (not just log it) so the GUI startup path
  // can raise a modal warning (Kartend audit cj462).
  writeConfigIni(QStringLiteral("[ColA]\n"
                                "name=Dup\n"
                                "mediaDirectory=/tmp/dup\n"
                                "[ColB]\n"
                                "name=Dup\n"
                                "mediaDirectory=/tmp/dup\n"));

  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections;
  mgr.loadCollections(collections);

  const QStringList errors = mgr.lastCollectionUuidCollisions();
  QVERIFY2(!errors.isEmpty(), "UUID collision must be captured for the startup warning");
  QVERIFY(errors.join(QLatin1Char('\n')).contains(QStringLiteral("UUID collision")));

  // A subsequent clean load clears the captured errors (no stale warning).
  writeConfigIni(QStringLiteral("[Solo]\nname=Solo\nmediaDirectory=/tmp/solo\n"));
  QList<CollectionConfig> clean;
  mgr.loadCollections(clean);
  QVERIFY(mgr.lastCollectionUuidCollisions().isEmpty());
  // (Solo's media dir is missing — an error, but NOT a collision — so the
  // collision channel stays empty, proving the missing-path noise is excluded.)
}

void TestSettingsRoundtrip::knownKey_overwritesPreservedDuplicate() {
  // The capture-then-replay strategy relies on known-field setValue()
  // overwriting the duplicate from preservedKeys. If that ordering ever
  // regresses, modifying a known field via the dialog would silently get
  // rolled back to the on-disk value.
  writeConfigIni(QStringLiteral("[TestCol]\n"
                                "name=TestCol\n"
                                "mediaDirectory=/tmp/old\n"));

  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections;
  mgr.loadCollections(collections);
  QCOMPARE(collections.size(), 1);

  collections[0].mediaDirectory = QStringLiteral("/tmp/new");
  mgr.saveCollections(collections);

  const QString rewritten = readConfigIni();
  QVERIFY2(rewritten.contains(QStringLiteral("mediaDirectory=/tmp/new")),
           qPrintable(QStringLiteral("Known-field write did NOT overwrite preserved duplicate:\n%1")
                          .arg(rewritten)));
  QVERIFY2(!rewritten.contains(QStringLiteral("mediaDirectory=/tmp/old")),
           qPrintable(
               QStringLiteral("Stale value lingered after known-field write:\n%1").arg(rewritten)));
}

void TestSettingsRoundtrip::legacyBlocklistedKey_droppedNotPreserved() {
  // videoDirectory and manualDirectory were intentionally collapsed into
  // the {artwork}/video and {artwork}/manual sublayouts. The strict
  // preservation logic must NOT undo that deliberate removal — the
  // blocklist exists for exactly this case.
  writeConfigIni(QStringLiteral("[TestCol]\n"
                                "name=TestCol\n"
                                "videoDirectory=/tmp/legacy-videos\n"
                                "manualDirectory=/tmp/legacy-manuals\n"));

  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections;
  mgr.loadCollections(collections);
  QCOMPARE(collections.size(), 1);
  QVERIFY(!collections[0].preservedKeys.contains(QStringLiteral("videoDirectory")));
  QVERIFY(!collections[0].preservedKeys.contains(QStringLiteral("manualDirectory")));

  mgr.saveCollections(collections);

  const QString rewritten = readConfigIni();
  QVERIFY2(
      !rewritten.contains(QStringLiteral("videoDirectory=")),
      qPrintable(
          QStringLiteral("Blocklisted videoDirectory leaked on round-trip:\n%1").arg(rewritten)));
  QVERIFY2(
      !rewritten.contains(QStringLiteral("manualDirectory=")),
      qPrintable(
          QStringLiteral("Blocklisted manualDirectory leaked on round-trip:\n%1").arg(rewritten)));
}

void TestSettingsRoundtrip::extensions_legacyGlobFormMigratesToBare() {
  // Kartend-693zb: the pre-fix canonical INI form was glob ("*.mp4"), which
  // ScanService::buildNameFilters double-prefixed into the never-matching
  // "*.*.mp4" — extension-filtered scans found nothing after an INI
  // round-trip. Canonical form is now bare; a legacy glob/dotted INI must
  // load as bare AND be rewritten in place so the migration sticks.
  writeConfigIni(QStringLiteral("[TestCol]\n"
                                "name=TestCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "extensions=*.mp4, .MKV, avi\n"));

  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections;
  mgr.loadCollections(collections);
  QCOMPARE(collections.size(), 1);
  const QStringList expected = {QStringLiteral("mp4"), QStringLiteral("mkv"),
                                QStringLiteral("avi")};
  QCOMPARE(collections[0].extensions, expected);

  // loadCollections rewrites the INI when normalization changed anything.
  const QString rewritten = readConfigIni();
  QVERIFY2(rewritten.contains(QStringLiteral("extensions=mp4, mkv, avi")),
           qPrintable(QStringLiteral("Legacy glob extensions were not migrated to bare form:\n%1")
                          .arg(rewritten)));

  // A second load of the migrated file must be a no-op (idempotence) — the
  // scan filter composed from it must target single-dot filenames.
  QList<CollectionConfig> reloaded;
  mgr.loadCollections(reloaded);
  QCOMPARE(reloaded.size(), 1);
  QCOMPARE(reloaded[0].extensions, expected);
}

void TestSettingsRoundtrip::globalSections_survivePerCollectionSave() {
  // saveCollections() removes every top-level INI group that isn't a
  // collection; the reserved-group set (now built from the same keys::kGroup*
  // constants the writers use) is the single guard that keeps the global
  // sections alive. If a group drops out of that set, a routine collection
  // save silently destroys it. This also exercises the skip side: the global
  // groups must NOT load as blank-named "ghost" collections. Kartend audit S-05.
  writeConfigIni(QStringLiteral("[General]\n"
                                "schemaVersion=1\n"
                                "firstRun=false\n"
                                "[Scrapers]\n"
                                "screenscraperUser=alice\n"
                                "[ScraperOptions]\n"
                                "preset=balanced\n"
                                "[Launchers]\n"
                                "presetCount=0\n"
                                "[TestCol]\n"
                                "name=TestCol\n"
                                "mediaDirectory=/tmp/media\n"));

  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections;
  mgr.loadCollections(collections);
  QCOMPARE(collections.size(), 1); // global groups must not load as ghost collections

  // A routine collection mutation + save must preserve every global section.
  collections[0].mediaDirectory = QStringLiteral("/tmp/media2");
  mgr.saveCollections(collections);

  const QString rewritten = readConfigIni();
  for (const auto &group : {"[General]", "[Scrapers]", "[ScraperOptions]", "[Launchers]"}) {
    QVERIFY2(rewritten.contains(QLatin1String(group)),
             qPrintable(QStringLiteral("%1 was wiped by a collection save:\n%2")
                            .arg(QLatin1String(group), rewritten)));
  }
  QVERIFY2(
      rewritten.contains(QStringLiteral("screenscraperUser=alice")),
      qPrintable(QStringLiteral("[Scrapers] key wiped by collection save:\n%1").arg(rewritten)));
}

void TestSettingsRoundtrip::clampedOutOfRangeValue_persistedOnLoad() {
  // gridWidth=0 is below the MIN_WIDTH floor; clampValues() raises it in
  // memory, and the load must also rewrite the INI so the corrected value
  // sticks instead of re-clamping (and the on-disk file staying out of range)
  // every launch. Kartend audit S-08.
  writeConfigIni(QStringLiteral("[TestCol]\n"
                                "name=TestCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "gridWidth=0\n"));

  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections;
  mgr.loadCollections(collections);
  QCOMPARE(collections.size(), 1);
  QVERIFY2(collections[0].gridLayout.gridWidth >= 1,
           "gridWidth=0 should be clamped up to the MIN_WIDTH floor in memory");

  const QString rewritten = readConfigIni();
  QVERIFY2(!rewritten.contains(QStringLiteral("gridWidth=0")),
           qPrintable(QStringLiteral("Out-of-range gridWidth=0 was not rewritten on load:\n%1")
                          .arg(rewritten)));
}

void TestSettingsRoundtrip::collectionTreeBlock_roundTripsAndClampsPosition() {
  // Kartend-ob1c9: visibility + dock side survive load→save→load, and a
  // position outside the tree's left/right vocabulary (including the
  // details pane's "top"/"bottom") clamps to left instead of importing.
  writeConfigIni(QStringLiteral("[General]\nschemaVersion=4\n\n"
                                "[TreeCol]\n"
                                "name=TreeCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "collectionTreeVisible=false\n"
                                "collectionTreePosition=right\n"));

  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections;
  mgr.loadCollections(collections);
  QCOMPARE(collections.size(), 1);
  QVERIFY(!collections[0].collectionTree.treeVisible);
  QCOMPARE(collections[0].collectionTree.treePosition, DetailsPanePosition::Right);

  collections[0].collectionTree.treeVisible = true;
  collections[0].collectionTree.treePosition = DetailsPanePosition::Left;
  mgr.saveCollections(collections);

  QList<CollectionConfig> reloaded;
  mgr.loadCollections(reloaded);
  QCOMPARE(reloaded.size(), 1);
  QVERIFY(reloaded[0].collectionTree.treeVisible);
  QCOMPARE(reloaded[0].collectionTree.treePosition, DetailsPanePosition::Left);

  writeConfigIni(QStringLiteral("[General]\nschemaVersion=4\n\n"
                                "[TreeCol]\n"
                                "name=TreeCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "collectionTreePosition=top\n"));
  QList<CollectionConfig> clamped;
  mgr.loadCollections(clamped);
  QCOMPARE(clamped.size(), 1);
  QCOMPARE(clamped[0].collectionTree.treePosition, DetailsPanePosition::Left);
  QVERIFY(clamped[0].collectionTree.treeVisible); // default ON
  // Absent justification defaults FULL-HEIGHT for the tree (2026-08-17
  // defaults decision) — unlike the details pane, whose absent-key default
  // stays below-toolbar (asserted further down).
  QCOMPARE(clamped[0].collectionTree.treeJustification, SidebarJustification::FullHeight);

  // Width (grip resize, Kartend-ob1c9.1 follow-on): round-trips, absent key
  // defaults, and out-of-range values clamp to the struct bounds instead of
  // importing a collapsed or runaway panel.
  QCOMPARE(clamped[0].collectionTree.treeWidth, CollectionTreeSettings{}.treeWidth);
  writeConfigIni(QStringLiteral("[General]\nschemaVersion=4\n\n"
                                "[TreeCol]\n"
                                "name=TreeCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "collectionTreeWidth=320\n"));
  QList<CollectionConfig> widthLoaded;
  mgr.loadCollections(widthLoaded);
  QCOMPARE(widthLoaded[0].collectionTree.treeWidth, 320);
  mgr.saveCollections(widthLoaded);
  QList<CollectionConfig> widthReloaded;
  mgr.loadCollections(widthReloaded);
  QCOMPARE(widthReloaded[0].collectionTree.treeWidth, 320);
  writeConfigIni(QStringLiteral("[General]\nschemaVersion=4\n\n"
                                "[TreeCol]\n"
                                "name=TreeCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "collectionTreeWidth=9\n"
                                "collectionTreeIconsOnly=true\n"
                                "collectionTreeShowLines=true\n"
                                "collectionTreeColorizeSelected=true\n"
                                "collectionTreeIconSize=9000\n"));
  QList<CollectionConfig> widthClamped;
  mgr.loadCollections(widthClamped);
  QCOMPARE(widthClamped[0].collectionTree.treeWidth, CollectionTreeSettings::kMinWidth);
  // Icon options (user request 2026-08-17): bools round-trip, size clamps.
  // Tree lines default OFF; absence of the key must read false.
  // Kartend-j1mtg: this fixture writes the LEGACY collectionTreeIconsOnly=true,
  // so it now also pins the migration — true must land on IconOnly, and the
  // save below must re-emit it under the new key so the second load agrees.
  QCOMPARE(widthClamped[0].collectionTree.treeIconDisplay, TreeIconDisplay::IconOnly);
  QVERIFY(widthClamped[0].collectionTree.treeShowLines);
  QVERIFY(widthClamped[0].collectionTree.treeColorizeSelected);
  QCOMPARE(widthClamped[0].collectionTree.treeIconSize, CollectionTreeSettings::kMaxIconSize);
  mgr.saveCollections(widthClamped);
  QList<CollectionConfig> iconReloaded;
  mgr.loadCollections(iconReloaded);
  QCOMPARE(iconReloaded[0].collectionTree.treeIconDisplay, TreeIconDisplay::IconOnly);
  // And the legacy bool is GONE from the file, not just unwritten — leaving it
  // beside the new key would contradict it for anyone reading the config.
  QVERIFY2(!readConfigIni().contains(QStringLiteral("collectionTreeIconsOnly")),
           "migrated config must not keep the legacy collectionTreeIconsOnly key beside "
           "collectionTreeIconDisplay — a reader cannot tell which one wins");
  QVERIFY(iconReloaded[0].collectionTree.treeShowLines);
  QVERIFY(iconReloaded[0].collectionTree.treeColorizeSelected);
  QCOMPARE(iconReloaded[0].collectionTree.treeIconSize, CollectionTreeSettings::kMaxIconSize);
  QVERIFY(!widthLoaded[0].collectionTree.treeShowLines); // key absent -> default off
  QVERIFY(!widthLoaded[0].collectionTree.treeColorizeSelected);

  // Icon style + tint (user request 2026-08-17): round-trip, unknown style
  // clamps to normal, absent tint reads empty (= accent default).
  writeConfigIni(QStringLiteral("[General]\nschemaVersion=4\n\n"
                                "[TreeCol]\n"
                                "name=TreeCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "collectionTreeIconStyle=tinted\n"
                                "collectionTreeIconTint=#e0a030\n"));
  QList<CollectionConfig> styled;
  mgr.loadCollections(styled);
  QCOMPARE(styled[0].collectionTree.treeIconStyle, TreeIconStyle::Tinted);
  QCOMPARE(styled[0].collectionTree.treeIconTintColor, QStringLiteral("#e0a030"));
  mgr.saveCollections(styled);
  QList<CollectionConfig> styledReloaded;
  mgr.loadCollections(styledReloaded);
  QCOMPARE(styledReloaded[0].collectionTree.treeIconStyle, TreeIconStyle::Tinted);
  QCOMPARE(styledReloaded[0].collectionTree.treeIconTintColor, QStringLiteral("#e0a030"));
  writeConfigIni(QStringLiteral("[General]\nschemaVersion=4\n\n"
                                "[TreeCol]\n"
                                "name=TreeCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "collectionTreeIconStyle=sepia\n"));
  QList<CollectionConfig> styleClamped;
  mgr.loadCollections(styleClamped);
  QCOMPARE(styleClamped[0].collectionTree.treeIconStyle, TreeIconStyle::Normal);
  QVERIFY(styleClamped[0].collectionTree.treeIconTintColor.isEmpty());

  // Kartend-1kkk2: the RetroArch system glyph is its own option cluster with
  // its own keys — round-trip, unknown subject clamps to controller, size
  // clamps, and an absent block reads as the (off) defaults.
  writeConfigIni(QStringLiteral("[General]\nschemaVersion=4\n\n"
                                "[TreeCol]\n"
                                "name=TreeCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "systemIconEnabled=true\n"
                                "systemIconName=Nintendo - Game Boy Advance\n"
                                "systemIconSubject=console\n"
                                "systemIconPack=systematic\n"
                                "systemIconPlacement=row-end\n"
                                "systemIconStyle=tinted\n"
                                "systemIconUseCollectionArtwork=true\n"
                                "systemIconSize=24\n"));
  QList<CollectionConfig> glyph;
  mgr.loadCollections(glyph);
  QVERIFY(glyph[0].systemIcon.enabled);
  QCOMPARE(glyph[0].systemIcon.systemName, QStringLiteral("Nintendo - Game Boy Advance"));
  QCOMPARE(glyph[0].systemIcon.subject, SystemIconSubject::Console);
  QCOMPARE(glyph[0].systemIcon.packOverride, QStringLiteral("systematic"));
  QCOMPARE(glyph[0].systemIcon.placement, SystemIconPlacement::RowEnd);
  QCOMPARE(glyph[0].systemIcon.style, TreeIconStyle::Tinted);
  QVERIFY(glyph[0].systemIcon.useCollectionArtwork);
  QCOMPARE(glyph[0].systemIcon.iconSize, 24);
  mgr.saveCollections(glyph);
  QList<CollectionConfig> glyphReloaded;
  mgr.loadCollections(glyphReloaded);
  QCOMPARE(glyphReloaded[0].systemIcon, glyph[0].systemIcon);

  writeConfigIni(QStringLiteral("[General]\nschemaVersion=4\n\n"
                                "[TreeCol]\n"
                                "name=TreeCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "systemIconEnabled=true\n"
                                "systemIconSubject=hologram\n"
                                "systemIconPlacement=diagonally\n"
                                "systemIconStyle=sepia\n"
                                "systemIconSize=9000\n"));
  QList<CollectionConfig> glyphClamped;
  mgr.loadCollections(glyphClamped);
  QCOMPARE(glyphClamped[0].systemIcon.subject, SystemIconSubject::Controller);
  QCOMPARE(glyphClamped[0].systemIcon.placement, SystemIconPlacement::BeforeName);
  QCOMPARE(glyphClamped[0].systemIcon.style, TreeIconStyle::Normal);
  QCOMPARE(glyphClamped[0].systemIcon.iconSize, SystemIconSettings::kMaxIconSize);

  // No block at all — the option is opt-in, so a config that predates it must
  // read as off with nothing selected rather than picking something up.
  writeConfigIni(QStringLiteral("[General]\nschemaVersion=4\n\n"
                                "[TreeCol]\n"
                                "name=TreeCol\n"
                                "mediaDirectory=/tmp/media\n"));
  QList<CollectionConfig> glyphAbsent;
  mgr.loadCollections(glyphAbsent);
  QCOMPARE(glyphAbsent[0].systemIcon, SystemIconSettings{});

  // Kartend-auh7u: justification round-trips for BOTH panels, absent keys
  // default to below-toolbar, and an unknown value clamps instead of
  // importing.
  writeConfigIni(QStringLiteral("[General]\nschemaVersion=4\n\n"
                                "[TreeCol]\n"
                                "name=TreeCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "collectionTreeJustification=full-height\n"
                                "sidebarJustification=full-height\n"));
  QList<CollectionConfig> justified;
  mgr.loadCollections(justified);
  QCOMPARE(justified.size(), 1);
  QCOMPARE(justified[0].collectionTree.treeJustification, SidebarJustification::FullHeight);
  QCOMPARE(justified[0].sidebar.sidebarJustification, SidebarJustification::FullHeight);

  mgr.saveCollections(justified);
  QList<CollectionConfig> justifiedReloaded;
  mgr.loadCollections(justifiedReloaded);
  QCOMPARE(justifiedReloaded[0].collectionTree.treeJustification, SidebarJustification::FullHeight);
  QCOMPARE(justifiedReloaded[0].sidebar.sidebarJustification, SidebarJustification::FullHeight);

  writeConfigIni(QStringLiteral("[General]\nschemaVersion=4\n\n"
                                "[TreeCol]\n"
                                "name=TreeCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "collectionTreeJustification=sideways\n"));
  QList<CollectionConfig> clampedJustification;
  mgr.loadCollections(clampedJustification);
  QCOMPARE(clampedJustification[0].collectionTree.treeJustification,
           SidebarJustification::BelowToolbar);
  QCOMPARE(clampedJustification[0].sidebar.sidebarJustification,
           SidebarJustification::BelowToolbar); // absent key → default

  // Nav sidebar overlap mode (user request 2026-08-20: "i want to allow it to
  // overlap without moving the grid at all. navigation side bar needs to
  // function the same"). Absent MUST read as the docked behaviour every
  // existing collection already has — this setting is opt-in, and defaulting
  // it the other way would silently float every open tree over its grid.
  QCOMPARE(clampedJustification[0].collectionTree.treeMode, DetailsPaneMode::Expand);

  writeConfigIni(QStringLiteral("[General]\nschemaVersion=4\n\n"
                                "[TreeCol]\n"
                                "name=TreeCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "collectionTreeMode=overlay\n"));
  QList<CollectionConfig> overlaid;
  mgr.loadCollections(overlaid);
  QCOMPARE(overlaid[0].collectionTree.treeMode, DetailsPaneMode::Overlay);
  // The two panes' modes are independent — turning the tree into an overlay
  // must not drag the details pane along with it.
  QCOMPARE(overlaid[0].sidebar.sidebarMode, DetailsPaneMode::Overlay); // its own default
  mgr.saveCollections(overlaid);
  QList<CollectionConfig> overlaidReloaded;
  mgr.loadCollections(overlaidReloaded);
  QCOMPARE(overlaidReloaded[0].collectionTree.treeMode, DetailsPaneMode::Overlay);

  writeConfigIni(QStringLiteral("[General]\nschemaVersion=4\n\n"
                                "[TreeCol]\n"
                                "name=TreeCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "collectionTreeMode=nonsense\n"));
  QList<CollectionConfig> badMode;
  mgr.loadCollections(badMode);
  QCOMPARE(badMode[0].collectionTree.treeMode, DetailsPaneMode::Expand);

  // Scrollbar mode (user request 2026-08-19): one tri-state per side pane,
  // round-tripping independently, and — the part that would silently break
  // every existing config — the LEGACY BOOL spelling still reads correctly.
  // These keys were hide-yes/no bools before the Autohide state existed, and
  // the key names were deliberately kept so an old INI migrates in place.
  QCOMPARE(clampedJustification[0].collectionTree.treeScrollbarMode, ScrollbarMode::Show);
  QCOMPARE(clampedJustification[0].sidebar.sidebarScrollbarMode, ScrollbarMode::Show);

  writeConfigIni(QStringLiteral("[General]\nschemaVersion=4\n\n"
                                "[TreeCol]\n"
                                "name=TreeCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "collectionTreeHideScrollbar=true\n"  // legacy bool
                                "sidebarHideScrollbar=false\n"        // legacy bool
                                "hideVerticalScrollbar=autohide\n")); // new spelling
  QList<CollectionConfig> modes;
  mgr.loadCollections(modes);
  QCOMPARE(modes[0].collectionTree.treeScrollbarMode, ScrollbarMode::Hide); // true  -> Hide
  QCOMPARE(modes[0].sidebar.sidebarScrollbarMode, ScrollbarMode::Show);     // false -> Show
  QCOMPARE(modes[0].gridLayout.verticalScrollbarMode, ScrollbarMode::Autohide);

  // Autohide must survive a save/reload — it is the state a writer that fell
  // back to a bool would quietly collapse to Show.
  modes[0].sidebar.sidebarScrollbarMode = ScrollbarMode::Autohide;
  QVERIFY(mgr.saveCollections(modes).isOk());
  QList<CollectionConfig> modesReloaded;
  mgr.loadCollections(modesReloaded);
  QCOMPARE(modesReloaded[0].collectionTree.treeScrollbarMode, ScrollbarMode::Hide);
  QCOMPARE(modesReloaded[0].sidebar.sidebarScrollbarMode, ScrollbarMode::Autohide);
  QCOMPARE(modesReloaded[0].gridLayout.verticalScrollbarMode, ScrollbarMode::Autohide);
}

void TestSettingsRoundtrip::migration_v1StampsSidebarLayoutOntoCollectionsOnce() {
  // A v1 file with the OPPOSITE of every 2026-08-17 default: the migration
  // must flip all four keys on the collection section (that's the "apply to
  // all existing collections" half of the user decision — defaults alone
  // never reach an INI whose save path writes every key explicitly).
  writeConfigIni(QStringLiteral("[General]\n"
                                "schemaVersion=1\n\n"
                                "[MigrateCol]\n"
                                "name=MigrateCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "collectionTreePosition=right\n"
                                "collectionTreeJustification=below-toolbar\n"
                                "sidebarPosition=left\n"
                                "sidebarJustification=full-height\n"));
  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections;
  mgr.loadCollections(collections);
  QCOMPARE(collections.size(), 1);
  QCOMPARE(collections[0].collectionTree.treePosition, DetailsPanePosition::Left);
  QCOMPARE(collections[0].collectionTree.treeJustification, SidebarJustification::FullHeight);
  QCOMPARE(collections[0].sidebar.sidebarPosition, DetailsPanePosition::Right);
  QCOMPARE(collections[0].sidebar.sidebarJustification, SidebarJustification::BelowToolbar);

  // ONCE only: the gate stamped schemaVersion, so an edit the user makes
  // AFTER the migration survives the next load instead of being re-flipped.
  collections[0].collectionTree.treePosition = DetailsPanePosition::Right;
  mgr.saveCollections(collections);
  QList<CollectionConfig> reloaded;
  mgr.loadCollections(reloaded);
  QCOMPARE(reloaded.size(), 1);
  QCOMPARE(reloaded[0].collectionTree.treePosition, DetailsPanePosition::Right);
}

// Expanded QTEST_GUILESS_MAIN so the macOS HOME sandbox is installed before
// QCoreApplication — Foundation and QDir::homePath() must agree before any
// QStandardPaths lookup for the .qttest reroute to fire (Kartend-0ceoe).
int main(int argc, char *argv[]) {
  KartendTest::installMacHomeSandbox();
  QCoreApplication app(argc, argv);
  app.setAttribute(Qt::AA_Use96Dpi, true);
  TestSettingsRoundtrip tc;
  QTEST_SET_MAIN_SOURCE_PATH
  return QTest::qExec(&tc, argc, argv);
}
#include "test_settingsroundtrip.moc"
