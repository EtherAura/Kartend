// Per-collection "hide MAME BIOS and device sets" preference (Kartend-tnt0l),
// foundation slice: the default-off flag on ScraperOverrides, its INI
// persistence through the real ScraperOverridesPersistence on temporary
// on-disk files, and its participation in the real scan signature
// (ScanServiceInternal::buildExtSignature), which is what makes toggling it
// trigger the normal rescan.
//
// The off value must leave the signature byte-for-byte what it was before the
// flag existed, so upgrading never forces a rescan of every collection.
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include "collection/collectionconfig.h"
#include "collection/scraperoverrides.h"
#include "collection/scraperoverrides_persistence.h"
#include "scanservice_internal.h"
#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace {

CollectionConfig arcadeCollection() {
  CollectionConfig c;
  c.extensions = QStringList{QStringLiteral("zip"), QStringLiteral("7z")};
  c.scraperOverrides.datFilePaths = QStringList{QStringLiteral("/dats/mame.xml")};
  return c;
}

void saveTo(const QString &iniPath, const QString &group, const ScraperOverrides &overrides) {
  QSettings settings(iniPath, QSettings::IniFormat);
  settings.beginGroup(group);
  ScraperOverridesPersistence::save(settings, overrides);
  settings.endGroup();
  settings.sync();
  QCOMPARE(settings.status(), QSettings::NoError);
}

ScraperOverrides loadFrom(const QString &iniPath, const QString &group, ScraperOverrides into) {
  QSettings settings(iniPath, QSettings::IniFormat);
  settings.beginGroup(group);
  ScraperOverridesPersistence::load(settings, into);
  settings.endGroup();
  return into;
}

} // namespace

class TestCollectionMameBiosSetting : public QObject {
  Q_OBJECT

private slots:
  void defaultConstructedIsOff();
  void absentKeyLoadsOff_evenOverPresetTrue();
  void falseAndTrueSurviveSaveReload();
  void collectionsAreIndependent();
  void signatureUnchangedWhenOff();
  void signatureDistinguishesOn();
  void signatureDistinguishesOnAlongsideMultiDisc();
  void signatureStableAcrossSaveReload();

private:
  QString iniPath(const char *name) const { return m_dir.filePath(QLatin1String(name)); }
  QTemporaryDir m_dir;
};

void TestCollectionMameBiosSetting::defaultConstructedIsOff() {
  QVERIFY(m_dir.isValid());
  QCOMPARE(ScraperOverrides{}.hideMameBiosAndDeviceSets, false);
  QCOMPARE(CollectionConfig{}.scraperOverrides.hideMameBiosAndDeviceSets, false);
}

void TestCollectionMameBiosSetting::absentKeyLoadsOff_evenOverPresetTrue() {
  // A legacy collection group: other overrides present, the new key absent.
  const QString path = iniPath("legacy.ini");
  {
    QSettings settings(path, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Arcade"));
    settings.setValue(QLatin1String(keys::kScraperProviderId), QStringLiteral("screenscraper"));
    settings.setValue(QLatin1String(keys::kDatFilePath), QStringLiteral("/dats/mame.xml"));
    settings.endGroup();
    settings.sync();
    QCOMPARE(settings.status(), QSettings::NoError);
  }
  ScraperOverrides preset;
  preset.hideMameBiosAndDeviceSets = true;
  const ScraperOverrides loaded = loadFrom(path, QStringLiteral("Arcade"), preset);
  QCOMPARE(loaded.hideMameBiosAndDeviceSets, false);
  // The rest of the legacy group still loads.
  QCOMPARE(loaded.scraperProviderId, QStringLiteral("screenscraper"));
  QCOMPARE(loaded.datFilePaths, QStringList{QStringLiteral("/dats/mame.xml")});
}

void TestCollectionMameBiosSetting::falseAndTrueSurviveSaveReload() {
  const QString path = iniPath("roundtrip.ini");
  ScraperOverrides on;
  on.hideMameBiosAndDeviceSets = true;
  saveTo(path, QStringLiteral("Arcade"), on);
  {
    QSettings raw(path, QSettings::IniFormat);
    QCOMPARE(raw.value(QStringLiteral("Arcade/") + QLatin1String(keys::kHideMameBiosAndDeviceSets))
                 .toBool(),
             true);
  }
  QCOMPARE(loadFrom(path, QStringLiteral("Arcade"), ScraperOverrides{}).hideMameBiosAndDeviceSets,
           true);
  QCOMPARE(loadFrom(path, QStringLiteral("Arcade"), ScraperOverrides{}), on);

  // Turning it back off is persisted too, and wins over a preset true.
  const ScraperOverrides off;
  saveTo(path, QStringLiteral("Arcade"), off);
  ScraperOverrides preset;
  preset.hideMameBiosAndDeviceSets = true;
  QCOMPARE(loadFrom(path, QStringLiteral("Arcade"), preset).hideMameBiosAndDeviceSets, false);
}

void TestCollectionMameBiosSetting::collectionsAreIndependent() {
  const QString path = iniPath("two.ini");
  ScraperOverrides on;
  on.hideMameBiosAndDeviceSets = true;
  saveTo(path, QStringLiteral("Arcade"), on);
  saveTo(path, QStringLiteral("NeoGeo"), ScraperOverrides{});
  QCOMPARE(loadFrom(path, QStringLiteral("Arcade"), ScraperOverrides{}).hideMameBiosAndDeviceSets,
           true);
  QCOMPARE(loadFrom(path, QStringLiteral("NeoGeo"), on).hideMameBiosAndDeviceSets, false);
}

void TestCollectionMameBiosSetting::signatureUnchangedWhenOff() {
  // The literal pre-flag signature for this collection: off must not move it.
  const CollectionConfig off = arcadeCollection();
  QCOMPARE(ScanServiceInternal::buildExtSignature(off), QStringLiteral("zip|7z"));
  CollectionConfig multiDisc = arcadeCollection();
  multiDisc.groupMultiDisc = true;
  QCOMPARE(ScanServiceInternal::buildExtSignature(multiDisc), QStringLiteral("zip|7z|multidisc"));
}

void TestCollectionMameBiosSetting::signatureDistinguishesOn() {
  const CollectionConfig off = arcadeCollection();
  CollectionConfig on = arcadeCollection();
  on.scraperOverrides.hideMameBiosAndDeviceSets = true;
  QVERIFY2(ScanServiceInternal::buildExtSignature(on) !=
               ScanServiceInternal::buildExtSignature(off),
           "toggling hideMameBiosAndDeviceSets must change the scan signature");
}

void TestCollectionMameBiosSetting::signatureDistinguishesOnAlongsideMultiDisc() {
  CollectionConfig multiDisc = arcadeCollection();
  multiDisc.groupMultiDisc = true;
  CollectionConfig both = multiDisc;
  both.scraperOverrides.hideMameBiosAndDeviceSets = true;
  CollectionConfig hideOnly = arcadeCollection();
  hideOnly.scraperOverrides.hideMameBiosAndDeviceSets = true;
  const QString sigMulti = ScanServiceInternal::buildExtSignature(multiDisc);
  const QString sigBoth = ScanServiceInternal::buildExtSignature(both);
  const QString sigHide = ScanServiceInternal::buildExtSignature(hideOnly);
  QVERIFY(sigBoth != sigMulti);
  QVERIFY(sigBoth != sigHide);
  QVERIFY(sigHide != sigMulti);
}

void TestCollectionMameBiosSetting::signatureStableAcrossSaveReload() {
  const QString path = iniPath("signature.ini");
  for (const bool value : {false, true}) {
    CollectionConfig before = arcadeCollection();
    before.scraperOverrides.hideMameBiosAndDeviceSets = value;
    saveTo(path, QStringLiteral("Arcade"), before.scraperOverrides);
    CollectionConfig after = arcadeCollection();
    after.scraperOverrides = loadFrom(path, QStringLiteral("Arcade"), ScraperOverrides{});
    QCOMPARE(ScanServiceInternal::buildExtSignature(after),
             ScanServiceInternal::buildExtSignature(before));
  }
}

QTEST_GUILESS_MAIN(TestCollectionMameBiosSetting)
#include "test_collectionmamebiossetting.moc"
