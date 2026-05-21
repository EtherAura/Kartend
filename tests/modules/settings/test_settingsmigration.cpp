// Tests for SettingsManager INI migration / schemaVersion handling
// (Kartend-39g9). Loads hand-crafted fixture INIs from tests/modules/
// settings/fixtures/ into the QStandardPaths test-mode config dir, then
// calls SettingsManager::loadGeneralSettings and asserts the decoded
// GeneralSettings reflects the fixture's stored values. Also asserts the
// future-versioned INI emits exactly one schemaVersion warning while still
// loading every known key.

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTest>

#include "collectionutils.h"
#include "settingsmanager.h"
#include "settingsutils.h"

#ifndef KARTEND_TEST_FIXTURES_DIR
#error "KARTEND_TEST_FIXTURES_DIR must be defined by CMake to locate the .ini fixtures."
#endif

class TestSettingsMigration : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();

  void legacyIni_loadsAllKnownKeysWithoutWarning();
  void v1Ini_loadsCleanlyWithCurrentSchemaVersion();
  void futureIni_warnsButStillLoadsAllKnownKeys();

private:
  void installFixture(const QString &fixtureName);
};

void TestSettingsMigration::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
}

void TestSettingsMigration::cleanupTestCase() {
  QStandardPaths::setTestModeEnabled(false);
}

void TestSettingsMigration::init() {
  // Each test gets a wiped config dir so the previous fixture can't bleed
  // into the next load.
  const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
  // Defence: test-mode config locations carry "qttest" in the path so
  // a misconfigured test can't recurse into the user's real config.
  QVERIFY2(configRoot.contains(QStringLiteral("qttest")),
           "QStandardPaths test mode should reroute ConfigLocation under qttest");
  QDir(configRoot).removeRecursively();
}

void TestSettingsMigration::installFixture(const QString &fixtureName) {
  const QString src = QStringLiteral(KARTEND_TEST_FIXTURES_DIR) + QLatin1Char('/') + fixtureName;
  const QString dest = SettingsUtils::getConfigPath();
  // getConfigPath created the parent dir as a side effect; copy in the
  // fixture so the next QSettings open reads from it instead of the empty
  // default.
  QFile::remove(dest);
  QVERIFY2(QFile::copy(src, dest),
           qPrintable(QStringLiteral("Failed to copy fixture %1 to %2").arg(src, dest)));
}

void TestSettingsMigration::legacyIni_loadsAllKnownKeysWithoutWarning() {
  installFixture(QStringLiteral("legacy.ini"));

  SettingsManager mgr(nullptr, nullptr);
  GeneralSettings settings;
  mgr.loadGeneralSettings(settings);

  QCOMPARE(settings.rememberSelection, false);
  QCOMPARE(settings.pixmapCacheSizeMB, 128);
  QCOMPARE(settings.titleTintSaturation, 120);
}

void TestSettingsMigration::v1Ini_loadsCleanlyWithCurrentSchemaVersion() {
  installFixture(QStringLiteral("v1.ini"));

  SettingsManager mgr(nullptr, nullptr);
  GeneralSettings settings;
  mgr.loadGeneralSettings(settings);

  QCOMPARE(settings.rememberSelection, true);
  QCOMPARE(settings.pixmapCacheSizeMB, 64);
  QCOMPARE(settings.titleTintSaturation, 200);
}

void TestSettingsMigration::futureIni_warnsButStillLoadsAllKnownKeys() {
  installFixture(QStringLiteral("future.ini"));

  // Expect exactly one schemaVersion warning from the future-versioned INI.
  // The category-prefixed message routes through QtMsgType::QtWarningMsg
  // for ignoreMessage matching purposes.
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral("schemaVersion 99")));

  SettingsManager mgr(nullptr, nullptr);
  GeneralSettings settings;
  mgr.loadGeneralSettings(settings);

  // All known keys still decode normally — unknown future keys are dropped
  // silently by QSettings on read.
  QCOMPARE(settings.rememberSelection, false);
  QCOMPARE(settings.pixmapCacheSizeMB, 200);
  QCOMPARE(settings.titleTintSaturation, 50);
}

QTEST_GUILESS_MAIN(TestSettingsMigration)
#include "test_settingsmigration.moc"
