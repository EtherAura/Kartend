// Kartend-ztc64: credential-storage demotion flag round trip. When a
// keychain write fails, saveScraperSection falls back to plaintext INI and
// persists a [Scrapers]/credentialDemotionReason meta key so the settings
// dialog can show a one-time non-modal banner across restarts. These tests
// drive the flag through a real INI: load surfaces it via
// credentialDemotionReason(), a subsequent save with no failing keychain
// write clears it (the self-heal contract) and emits
// credentialStorageDemotionChanged exactly once.
//
// Deliberately NOT covered headlessly: the set-on-failed-write path needs a
// keychain backend that errors on demand (locked KWallet, stopped secret
// service) — that's a manual-verify scenario. No test here seeds a
// <provider>/<field> credential key AND calls save: on a developer machine
// with a live keyring that would issue real keychain writes/deletes under
// the production service name.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTextStream>

#include "../../support/machomesandbox.h"
#include "collection/generalsettings.h"
#include "settingsmanager.h"
#include "settingsutils.h"

class TestCredentialDemotion : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();

  void demotionFlagInIni_surfacesThroughGetter();
  void metaKey_doesNotPoisonCredentialMap();
  void plaintextCredential_loadsAsValue_noFlag();
  void cleanSave_clearsFlag_emitsSignalOnce();

private:
  static void writeConfigIni(const QString &iniContents);
  static QString readConfigIni();
};

void TestCredentialDemotion::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
}

void TestCredentialDemotion::cleanupTestCase() {
  QStandardPaths::setTestModeEnabled(false);
}

void TestCredentialDemotion::init() {
  const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
  QVERIFY2(configRoot.contains(QStringLiteral("qttest")),
           "QStandardPaths test mode should reroute ConfigLocation under qttest");
  QDir(configRoot).removeRecursively();
}

void TestCredentialDemotion::writeConfigIni(const QString &iniContents) {
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

QString TestCredentialDemotion::readConfigIni() {
  QFile f(SettingsUtils::getConfigPath());
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  return QString::fromUtf8(f.readAll());
}

void TestCredentialDemotion::demotionFlagInIni_surfacesThroughGetter() {
  // A previous session's failed keychain write left the persisted marker.
  writeConfigIni(QStringLiteral("[Scrapers]\n"
                                "credentialDemotionReason=Could not open wallet\n"));

  SettingsManager mgr(nullptr, nullptr);
  QCOMPARE(mgr.credentialDemotionReason(), QString());

  GeneralSettings gs;
  mgr.loadGeneralSettings(gs);
  QCOMPARE(mgr.credentialDemotionReason(), QStringLiteral("Could not open wallet"));
}

void TestCredentialDemotion::metaKey_doesNotPoisonCredentialMap() {
  // The meta key has no provider/field slash, so the credential walk must
  // skip it instead of turning it into a bogus provider entry.
  writeConfigIni(QStringLiteral("[Scrapers]\n"
                                "credentialDemotionReason=Could not open wallet\n"));

  SettingsManager mgr(nullptr, nullptr);
  GeneralSettings gs;
  mgr.loadGeneralSettings(gs);
  QVERIFY(gs.scraper.credentials.isEmpty());
}

void TestCredentialDemotion::plaintextCredential_loadsAsValue_noFlag() {
  // The demoted (or legacy) plaintext form: no @keychain sentinel. The read
  // path must hand the value through untouched — this is the precondition
  // for the save-path re-migration self-heal (the next save retries the
  // keychain write with exactly this value). Load-only on purpose; see the
  // file header for why no save happens with a credential key present.
  writeConfigIni(QStringLiteral("[Scrapers]\n"
                                "screenscraper/user_password=hunter2\n"));

  SettingsManager mgr(nullptr, nullptr);
  GeneralSettings gs;
  mgr.loadGeneralSettings(gs);
  QCOMPARE(gs.scraper.credentials.value(QStringLiteral("screenscraper"))
               .value(QStringLiteral("user_password")),
           QStringLiteral("hunter2"));
  // A plaintext value alone doesn't imply a recorded demotion (it may be a
  // legacy INI from a pre-keychain build) — only the persisted marker does.
  QCOMPARE(mgr.credentialDemotionReason(), QString());
}

void TestCredentialDemotion::cleanSave_clearsFlag_emitsSignalOnce() {
  writeConfigIni(QStringLiteral("[Scrapers]\n"
                                "credentialDemotionReason=Could not open wallet\n"));

  SettingsManager mgr(nullptr, nullptr);
  GeneralSettings gs;
  mgr.loadGeneralSettings(gs);
  QCOMPARE(mgr.credentialDemotionReason(), QStringLiteral("Could not open wallet"));
  // Flag-only INI loads no credentials, so the save below performs zero
  // keychain operations: nothing to write, and the pre-wipe sweep skips
  // slash-less meta keys.
  QVERIFY(gs.scraper.credentials.isEmpty());

  QSignalSpy demotionSpy(&mgr, &ISettingsManager::credentialStorageDemotionChanged);
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());

  // Cleared in memory, cleared on disk, signalled exactly once with the
  // empty "resolved" reason.
  QCOMPARE(mgr.credentialDemotionReason(), QString());
  QCOMPARE(demotionSpy.count(), 1);
  QCOMPARE(demotionSpy.takeFirst().at(0).toString(), QString());
  QVERIFY(!readConfigIni().contains(QStringLiteral("credentialDemotionReason")));

  // A second clean save must not re-emit — the state didn't change.
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QCOMPARE(demotionSpy.count(), 0);
}

int main(int argc, char *argv[]) {
  KartendTest::installMacHomeSandbox();
  QCoreApplication app(argc, argv);
  app.setAttribute(Qt::AA_Use96Dpi, true);
  TestCredentialDemotion tc;
  QTEST_SET_MAIN_SOURCE_PATH
  return QTest::qExec(&tc, argc, argv);
}
#include "test_credentialdemotion.moc"
