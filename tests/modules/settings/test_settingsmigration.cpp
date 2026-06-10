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

#include "../../support/machomesandbox.h"
#include "collection/generalsettings.h"
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
  void legacyIni_invokesV0ToV1Migration();
  void v1Ini_doesNotInvokeAnyMigration();
  void misfiledSchemaIni_recognisedViaScraperOptionsFallback();

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

  QCOMPARE(settings.input.rememberSelection, false);
  QCOMPARE(settings.media.pixmapCacheSizeMB, 128);
  QCOMPARE(settings.appearance.titleTintSaturation, 120);
}

void TestSettingsMigration::v1Ini_loadsCleanlyWithCurrentSchemaVersion() {
  installFixture(QStringLiteral("v1.ini"));

  SettingsManager mgr(nullptr, nullptr);
  GeneralSettings settings;
  mgr.loadGeneralSettings(settings);

  QCOMPARE(settings.input.rememberSelection, true);
  QCOMPARE(settings.media.pixmapCacheSizeMB, 64);
  QCOMPARE(settings.appearance.titleTintSaturation, 200);
}

void TestSettingsMigration::futureIni_warnsButStillLoadsAllKnownKeys() {
  installFixture(QStringLiteral("future.ini"));

  // Expect exactly one schemaVersion warning from the future-versioned INI.
  // The category-prefixed message routes through QtMsgType::QtWarningMsg
  // for ignoreMessage matching purposes.
  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("schemaVersion 99")));

  SettingsManager mgr(nullptr, nullptr);
  GeneralSettings settings;
  mgr.loadGeneralSettings(settings);

  // All known keys still decode normally — unknown future keys are dropped
  // silently by QSettings on read.
  QCOMPARE(settings.input.rememberSelection, false);
  QCOMPARE(settings.media.pixmapCacheSizeMB, 200);
  QCOMPARE(settings.appearance.titleTintSaturation, 50);
}

void TestSettingsMigration::legacyIni_invokesV0ToV1Migration() {
  // Kartend-h16z: with the v0->v1 no-op step registered, loading a pre-
  // versioning INI must trigger the dispatcher and emit the "applying
  // settings migration 0 -> 1" qCInfo message. The message is the
  // observable side-effect that proves the registration table is wired —
  // the step body is intentionally empty, so this is the only assertion
  // available short of a side-channel observer.
  installFixture(QStringLiteral("legacy.ini"));

  QTest::ignoreMessage(QtInfoMsg,
                       QRegularExpression(QStringLiteral("applying settings migration 0 -> 1")));

  SettingsManager mgr(nullptr, nullptr);
  GeneralSettings settings;
  mgr.loadGeneralSettings(settings);

  // Reading still produces the same values as legacyIni_loadsAllKnownKeysWithoutWarning;
  // the migration is a no-op so the load behavior is unchanged.
  QCOMPARE(settings.input.rememberSelection, false);
  QCOMPARE(settings.media.pixmapCacheSizeMB, 128);
}

void TestSettingsMigration::v1Ini_doesNotInvokeAnyMigration() {
  // The dispatcher's short-circuit must fire when loadedVersion ==
  // currentVersion. Verify by failing the test if the v0->v1 step's
  // qCInfo line appears. QTest treats unexpected qInfo via failOnWarning,
  // which we drive by tee-ing the migration category through a temporary
  // message handler that records its invocations.
  installFixture(QStringLiteral("v1.ini"));

  static QStringList capturedMigrationMessages;
  capturedMigrationMessages.clear();
  auto previousHandler =
      qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
        Q_UNUSED(type);
        if (ctx.category && QByteArray(ctx.category) == "kartend.settingsmigrations") {
          capturedMigrationMessages.append(msg);
        }
      });

  SettingsManager mgr(nullptr, nullptr);
  GeneralSettings settings;
  mgr.loadGeneralSettings(settings);

  qInstallMessageHandler(previousHandler);

  QVERIFY2(capturedMigrationMessages.isEmpty(),
           qPrintable(QStringLiteral("Expected no migration log for v1 INI but got: %1")
                          .arg(capturedMigrationMessages.join(QStringLiteral(" | ")))));
  QCOMPARE(settings.input.rememberSelection, true);
  QCOMPARE(settings.media.pixmapCacheSizeMB, 64);
}

void TestSettingsMigration::misfiledSchemaIni_recognisedViaScraperOptionsFallback() {
  // An early build stamped the file-wide schemaVersion sentinel into
  // [ScraperOptions] while load read it from [General], so a versioned file
  // looked unversioned (v0) on every load and would re-run the v0->v1 step
  // forever. The tolerant dual-location read falls back to the
  // [ScraperOptions] copy when [General] lacks the key, so such a file must be
  // recognised as its true version (1) and must NOT trigger the migration.
  // Mirror v1Ini_doesNotInvokeAnyMigration: capture the migration category and
  // assert it stayed silent.
  installFixture(QStringLiteral("misfiled_schema.ini"));

  static QStringList capturedMigrationMessages;
  capturedMigrationMessages.clear();
  auto previousHandler =
      qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
        Q_UNUSED(type);
        if (ctx.category && QByteArray(ctx.category) == "kartend.settingsmigrations") {
          capturedMigrationMessages.append(msg);
        }
      });

  SettingsManager mgr(nullptr, nullptr);
  GeneralSettings settings;
  mgr.loadGeneralSettings(settings);

  qInstallMessageHandler(previousHandler);

  QVERIFY2(capturedMigrationMessages.isEmpty(),
           qPrintable(QStringLiteral("Expected no migration log for a file whose schemaVersion "
                                     "lives in [ScraperOptions] but got: %1")
                          .arg(capturedMigrationMessages.join(QStringLiteral(" | ")))));
  // The [General] keys still decode normally alongside the fallback read.
  QCOMPARE(settings.input.rememberSelection, true);
  QCOMPARE(settings.media.pixmapCacheSizeMB, 64);
}

// Expanded QTEST_GUILESS_MAIN so the macOS HOME sandbox is installed before
// QCoreApplication — Foundation and QDir::homePath() must agree before any
// QStandardPaths lookup for the .qttest reroute to fire (Kartend-0ceoe).
int main(int argc, char *argv[]) {
  KartendTest::installMacHomeSandbox();
  QCoreApplication app(argc, argv);
  app.setAttribute(Qt::AA_Use96Dpi, true);
  TestSettingsMigration tc;
  QTEST_SET_MAIN_SOURCE_PATH
  return QTest::qExec(&tc, argc, argv);
}
#include "test_settingsmigration.moc"
