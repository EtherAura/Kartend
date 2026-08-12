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
