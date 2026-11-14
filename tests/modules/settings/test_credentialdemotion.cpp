// Kartend-ztc64: credential-storage demotion flag round trip. When a
// keychain write fails, saveScraperSection falls back to plaintext INI and
// persists a [Scrapers]/credentialDemotionReason meta key so the settings
// dialog can show a one-time non-modal banner across restarts. These tests
// drive the flag through a real INI: load surfaces it via
// credentialDemotionReason(), a subsequent save with no failing keychain
// write clears it (the self-heal contract) and emits
// credentialStorageDemotionChanged exactly once.
//
// Apart from the declined*, lockedAtLoad*, readableThenCleared* and secureUnchanged* tests, no test
// here seeds a <provider>/<field> credential key AND call save with the platform
// keychain: on a developer machine with a live keyring that would issue real
// keychain writes/deletes under the production service name. Those tests
// (Kartend-f4nf2) do save credentials, but only through FakeKeychain, an in-memory backend
// installed with SettingsManager::setCredentialBackendForTesting before the first load, so no real
// wallet is ever read, written, locked or unlocked.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTextStream>

#include "../../support/machomesandbox.h"
#include "collection/generalsettings.h"
#include "settingskeys.h"
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
  void noKeychainBuild_alwaysFlagsPlaintext();
  void declinedChange_keepsOldPlaintextAndWarning_thenRecovers();
  void declinedChange_otherPlaintextKeepsWarning();
  void declinedChange_oldSecureRow_keepsKeychainEntry();
  void declinedNewCredential_noOldRow_noWarning();
  void lockedAtLoad_hooklessSaves_keepKeychainRow_thenRecovers();
  void readableThenCleared_removesRowAndKeychainEntry();
  void lockedAtLoad_thenEdited_isNotIgnored();
  void secureUnchanged_savesSkipKeychain();
  void declinedThenReverted_loadedBaseline_skipsAgain();
  void declinedThenReverted_savedBaseline_skipsAgain();
  void declinedStillChanged_retriesThenEditsAndClearWork();

private:
  static void writeConfigIni(const QString &iniContents);
  static QString readConfigIni();
  static QVariant iniValue(const QString &key);
};

namespace {
const QString kProvider = QStringLiteral("screenscraper");
const QString kPasswordField = QStringLiteral("user_password");
const QString kNameField = QStringLiteral("user_name");
const QString kPasswordKey = QStringLiteral("screenscraper/user_password");
const QString kNameKey = QStringLiteral("screenscraper/user_name");

// Isolated, controllable stand-in for the platform keychain. Writes fail for
// every key while failAll is set, and for the keys in failKeys always. Reads
// fail while failReads is set (a wallet locked at load).
struct FakeKeychain {
  QHash<QString, QString> store;
  bool failAll = false;
  bool failReads = false;
  QSet<QString> failKeys;
  QStringList readAttempts;
  QStringList writeAttempts; // "<key>=<value>" per attempt
  QStringList removed;

  SettingsManager::CredentialBackend backend() {
    SettingsManager::CredentialBackend b;
    b.read = [this](const QString &key, bool *ok) {
      readAttempts << key;
      if (failReads) {
        if (ok) *ok = false;
        return QString();
      }
      if (ok) *ok = store.contains(key);
      return store.value(key);
    };
    b.write = [this](const QString &key, const QString &value, QString *errorOut) {
      writeAttempts << key + QLatin1Char('=') + value;
      if (failAll || failKeys.contains(key)) {
        if (errorOut) *errorOut = QStringLiteral("fake keychain locked");
        return false;
      }
      store[key] = value;
      return true;
    };
    b.remove = [this](const QString &key) {
      removed << key;
      store.remove(key);
      return true;
    };
    return b;
  }
};

QString credential(const GeneralSettings &gs, const QString &field) {
  return gs.scraper.credentials.value(kProvider).value(field);
}

QString readIniFile() {
  QFile f(SettingsUtils::getConfigPath());
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  return QString::fromUtf8(f.readAll());
}

// [Scrapers]/<key> on disk now (a fresh QSettings per call), read through
// SettingsUtils::getFormat(), the format the app writes the file with. Not
// QSettings::IniFormat: Qt shares one parsed copy of a file between every
// QSettings on it, whatever its format, and only on Windows does IniFormat
// look keys up case-insensitively — so there every read after a save came
// back empty, which also made the absent-row checks pass vacuously
// (Kartend-fg8v7).
QVariant iniScrapersValue(const QString &key) {
  QSettings s(SettingsUtils::getConfigPath(), SettingsUtils::getFormat());
  return s.value(QStringLiteral("Scrapers/") + key);
}

QString iniPassword() {
  return iniScrapersValue(kPasswordKey).toString();
}

// Kartend-f4nf2: shared body of the declinedThenReverted_* tests. The manager
// holds `oldSecret` stored securely (loaded or saved) and keychain writes now
// fail. A changed value is declined, then the user reverts to the old value:
// hook-less saves must skip it again, with no keychain write or delete, no
// plaintext, no prompt and no warning, and a fresh load must recover it.
void declineThenRevert(SettingsManager &mgr, GeneralSettings &gs, FakeKeychain &fake,
                       const QString &oldSecret) {
  fake.failAll = true;
  int asks = 0;
  mgr.setPlaintextCredentialConsent([&asks](const QString &) {
    ++asks;
    return false;
  });
  gs.scraper.credentials[kProvider][kPasswordField] = QStringLiteral("declined");
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QCOMPARE(asks, 1);
  QCOMPARE(fake.writeAttempts.last(), kPasswordKey + QStringLiteral("=declined"));
  QCOMPARE(iniPassword(), QStringLiteral("@keychain"));
  QCOMPARE(fake.store.value(kPasswordKey), oldSecret);

  gs.scraper.credentials[kProvider][kPasswordField] = oldSecret;
  mgr.setPlaintextCredentialConsent({});
  const qsizetype writesBefore = fake.writeAttempts.size();
  const qsizetype removesBefore = fake.removed.size();
  QSignalSpy demotionSpy(&mgr, &ISettingsManager::credentialStorageDemotionChanged);
  for (int round = 0; round < 3; ++round) {
    QVERIFY(!mgr.saveGeneralSettings(gs).isError());
    QCOMPARE(fake.writeAttempts.size(), writesBefore);
    QCOMPARE(fake.removed.size(), removesBefore);
    QCOMPARE(iniPassword(), QStringLiteral("@keychain"));
    QCOMPARE(fake.store.value(kPasswordKey), oldSecret);
    QVERIFY(!readIniFile().contains(oldSecret));
    QVERIFY(!readIniFile().contains(QStringLiteral("declined")));
    QCOMPARE(mgr.credentialDemotionReason(), QString());
    QVERIFY(!readIniFile().contains(QStringLiteral("credentialDemotionReason")));
  }
  QCOMPARE(asks, 1);
  QCOMPARE(demotionSpy.count(), 0);

  fake.failAll = false;
  SettingsManager reloaded(nullptr, nullptr);
  reloaded.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs2;
  reloaded.loadGeneralSettings(gs2);
  QCOMPARE(credential(gs2, kPasswordField), oldSecret);
  QCOMPARE(reloaded.credentialDemotionReason(), QString());
}
} // namespace

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

void TestCredentialDemotion::noKeychainBuild_alwaysFlagsPlaintext() {
  // Kartend-4ahok. In a build compiled WITHOUT keychain support every
  // credential is plaintext, always — and that was the one configuration whose
  // warning banner never appeared, because the #else arm of the save path set
  // no demotion reason and the persist of that reason was itself inside
  // #ifdef KARTEND_HAVE_QTKEYCHAIN. The weakest storage was the only silent one.
  //
  // The file comment above explains why no other test here seeds a credential
  // and saves: on a developer machine with a live keyring that issues real
  // keychain writes under the production service name. That objection does not
  // apply in this configuration — there is no keychain to write to — which is
  // exactly why this case can finally be asserted rather than reasoned about.
  if (SettingsManager::keychainBackendCompiledIn()) {
    QSKIP("Keychain backend compiled in; this asserts the no-keychain build. "
          "Configure with -DCMAKE_DISABLE_FIND_PACKAGE_Qt6Keychain=ON to run it.");
  }

  writeConfigIni(QStringLiteral("[Scrapers]\n"));

  SettingsManager mgr(nullptr, nullptr);
  GeneralSettings gs;
  mgr.loadGeneralSettings(gs);
  QCOMPARE(mgr.credentialDemotionReason(), QString());

  gs.scraper.credentials[QStringLiteral("screenscraper")][QStringLiteral("password")] =
      QStringLiteral("hunter2");
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());

  // The running app now warns...
  QCOMPARE(mgr.credentialDemotionReason(),
           QLatin1String(kartend::settings::keys::kCredentialDemotionNoKeychainBuild));
  // ...and the marker survives a restart, like the runtime-failure one does.
  const QString ini = readConfigIni();
  QVERIFY2(ini.contains(QLatin1String(kartend::settings::keys::kCredentialDemotionNoKeychainBuild)),
           "The demotion marker must be persisted in a no-keychain build too");

  SettingsManager reloaded(nullptr, nullptr);
  GeneralSettings gs2;
  reloaded.loadGeneralSettings(gs2);
  QCOMPARE(reloaded.credentialDemotionReason(),
           QLatin1String(kartend::settings::keys::kCredentialDemotionNoKeychainBuild));
  // The credential itself is genuinely in the clear — that IS the thing being
  // warned about, so assert it rather than assume it.
  QCOMPARE(gs2.scraper.credentials.value(QStringLiteral("screenscraper"))
               .value(QStringLiteral("password")),
           QStringLiteral("hunter2"));
}

QVariant TestCredentialDemotion::iniValue(const QString &key) {
  return iniScrapersValue(key);
}

void TestCredentialDemotion::declinedChange_keepsOldPlaintextAndWarning_thenRecovers() {
  // Kartend-f4nf2: a changed password whose secure write fails and whose
  // plaintext fallback is declined must leave the OLD stored row and the
  // warning exactly as they were, also across hook-less retries, until the
  // keychain recovers.
  if (!SettingsManager::keychainBackendCompiledIn()) {
    QSKIP("Needs the keychain build; the declined fallback does not exist without it.");
  }
  writeConfigIni(QStringLiteral("[Scrapers]\n"
                                "credentialDemotionReason=Could not open wallet\n"
                                "screenscraper/user_password=oldpass\n"));
  FakeKeychain fake;
  fake.failAll = true;
  SettingsManager mgr(nullptr, nullptr);
  mgr.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs;
  mgr.loadGeneralSettings(gs);
  QCOMPARE(credential(gs, kPasswordField), QStringLiteral("oldpass"));
  QCOMPARE(mgr.credentialDemotionReason(), QStringLiteral("Could not open wallet"));

  int asks = 0;
  mgr.setPlaintextCredentialConsent([&asks](const QString &) {
    ++asks;
    return false;
  });
  gs.scraper.credentials[kProvider][kPasswordField] = QStringLiteral("newpass");
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());

  QCOMPARE(asks, 1);
  QVERIFY(fake.writeAttempts.contains(kPasswordKey + QStringLiteral("=newpass")));
  QCOMPARE(iniValue(kPasswordKey).toString(), QStringLiteral("oldpass"));
  QVERIFY2(!readConfigIni().contains(QStringLiteral("newpass")),
           "A declined value must not reach the INI");
  QVERIFY(!mgr.credentialDemotionReason().isEmpty());
  QVERIFY(!iniValue(QStringLiteral("credentialDemotionReason")).toString().isEmpty());
  {
    SettingsManager reloaded(nullptr, nullptr);
    reloaded.setCredentialBackendForTesting(fake.backend());
    GeneralSettings gs2;
    reloaded.loadGeneralSettings(gs2);
    QCOMPARE(credential(gs2, kPasswordField), QStringLiteral("oldpass"));
    QCOMPARE(reloaded.credentialDemotionReason(), QStringLiteral("Could not open wallet"));
  }

  // Hook-less saves (toolbar toggles and the like) retry the keychain first,
  // but must not write the refused value in the clear when it still fails.
  mgr.setPlaintextCredentialConsent({});
  for (int round = 0; round < 2; ++round) {
    const qsizetype attemptsBefore = fake.writeAttempts.size();
    QVERIFY(!mgr.saveGeneralSettings(gs).isError());
    QCOMPARE(fake.writeAttempts.size(), attemptsBefore + 1);
    QCOMPARE(fake.writeAttempts.last(), kPasswordKey + QStringLiteral("=newpass"));
    QCOMPARE(iniValue(kPasswordKey).toString(), QStringLiteral("oldpass"));
    QVERIFY(!readConfigIni().contains(QStringLiteral("newpass")));
    QCOMPARE(mgr.credentialDemotionReason(), QStringLiteral("Could not open wallet"));
    QCOMPARE(iniValue(QStringLiteral("credentialDemotionReason")).toString(),
             QStringLiteral("Could not open wallet"));
  }
  QCOMPARE(asks, 1);

  // Recovery: the next hook-less save stores the new value securely, and
  // with no plaintext credential left the warning clears.
  fake.failAll = false;
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QCOMPARE(fake.store.value(kPasswordKey), QStringLiteral("newpass"));
  QCOMPARE(iniValue(kPasswordKey).toString(), QStringLiteral("@keychain"));
  QVERIFY(!readConfigIni().contains(QStringLiteral("oldpass")));
  QVERIFY(!readConfigIni().contains(QStringLiteral("newpass")));
  QCOMPARE(mgr.credentialDemotionReason(), QString());
  QVERIFY(!readConfigIni().contains(QStringLiteral("credentialDemotionReason")));

  SettingsManager reloaded(nullptr, nullptr);
  reloaded.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs3;
  reloaded.loadGeneralSettings(gs3);
  QCOMPARE(credential(gs3, kPasswordField), QStringLiteral("newpass"));
  QCOMPARE(reloaded.credentialDemotionReason(), QString());
}

void TestCredentialDemotion::declinedChange_otherPlaintextKeepsWarning() {
  // After the declined password recovers, another credential still stored in
  // the clear keeps the warning on.
  if (!SettingsManager::keychainBackendCompiledIn()) {
    QSKIP("Needs the keychain build; the declined fallback does not exist without it.");
  }
  writeConfigIni(QStringLiteral("[Scrapers]\n"
                                "credentialDemotionReason=Could not open wallet\n"
                                "screenscraper/user_name=alice\n"
                                "screenscraper/user_password=oldpass\n"));
  FakeKeychain fake;
  fake.failAll = true;
  SettingsManager mgr(nullptr, nullptr);
  mgr.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs;
  mgr.loadGeneralSettings(gs);

  int asks = 0;
  mgr.setPlaintextCredentialConsent([&asks](const QString &) {
    ++asks;
    return false;
  });
  gs.scraper.credentials[kProvider][kPasswordField] = QStringLiteral("newpass");
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QCOMPARE(asks, 1); // once per save, not once per credential
  QCOMPARE(iniValue(kNameKey).toString(), QStringLiteral("alice"));
  QCOMPARE(iniValue(kPasswordKey).toString(), QStringLiteral("oldpass"));

  mgr.setPlaintextCredentialConsent({});
  fake.failAll = false;
  fake.failKeys.insert(kNameKey);
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QCOMPARE(iniValue(kPasswordKey).toString(), QStringLiteral("@keychain"));
  QCOMPARE(fake.store.value(kPasswordKey), QStringLiteral("newpass"));
  QCOMPARE(iniValue(kNameKey).toString(), QStringLiteral("alice"));
  QVERIFY(!mgr.credentialDemotionReason().isEmpty());
  QVERIFY(!iniValue(QStringLiteral("credentialDemotionReason")).toString().isEmpty());
}

void TestCredentialDemotion::declinedChange_oldSecureRow_keepsKeychainEntry() {
  // Declining must not delete a password that is already stored securely,
  // and must not raise a plaintext warning when nothing is in the clear.
  if (!SettingsManager::keychainBackendCompiledIn()) {
    QSKIP("Needs the keychain build; the declined fallback does not exist without it.");
  }
  writeConfigIni(QStringLiteral("[Scrapers]\n"
                                "screenscraper/user_password=@keychain\n"));
  FakeKeychain fake;
  fake.store[kPasswordKey] = QStringLiteral("secret");
  SettingsManager mgr(nullptr, nullptr);
  mgr.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs;
  mgr.loadGeneralSettings(gs);
  QCOMPARE(credential(gs, kPasswordField), QStringLiteral("secret"));

  fake.failAll = true;
  mgr.setPlaintextCredentialConsent([](const QString &) { return false; });
  gs.scraper.credentials[kProvider][kPasswordField] = QStringLiteral("newpass");
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  mgr.setPlaintextCredentialConsent({});
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());

  QCOMPARE(iniValue(kPasswordKey).toString(), QStringLiteral("@keychain"));
  QCOMPARE(fake.store.value(kPasswordKey), QStringLiteral("secret"));
  QVERIFY(fake.removed.isEmpty());
  QVERIFY(!readConfigIni().contains(QStringLiteral("newpass")));
  QCOMPARE(mgr.credentialDemotionReason(), QString());

  fake.failAll = false;
  SettingsManager reloaded(nullptr, nullptr);
  reloaded.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs2;
  reloaded.loadGeneralSettings(gs2);
  QCOMPARE(credential(gs2, kPasswordField), QStringLiteral("secret"));
  QCOMPARE(reloaded.credentialDemotionReason(), QString());
}

void TestCredentialDemotion::declinedNewCredential_noOldRow_noWarning() {
  // A brand-new credential that is declined is simply not saved: no
  // plaintext now, none on a later hook-less save, and no warning.
  if (!SettingsManager::keychainBackendCompiledIn()) {
    QSKIP("Needs the keychain build; the declined fallback does not exist without it.");
  }
  writeConfigIni(QStringLiteral("[Scrapers]\n"));
  FakeKeychain fake;
  fake.failAll = true;
  SettingsManager mgr(nullptr, nullptr);
  mgr.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs;
  mgr.loadGeneralSettings(gs);

  mgr.setPlaintextCredentialConsent([](const QString &) { return false; });
  gs.scraper.credentials[kProvider][kPasswordField] = QStringLiteral("newpass");
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QCOMPARE(mgr.credentialDemotionReason(), QString());
  QVERIFY(!readConfigIni().contains(QStringLiteral("newpass")));

  mgr.setPlaintextCredentialConsent({});
  const qsizetype attemptsBefore = fake.writeAttempts.size();
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QCOMPARE(fake.writeAttempts.size(), attemptsBefore + 1);
  QVERIFY2(!readConfigIni().contains(QStringLiteral("newpass")),
           "A hook-less retry must not write the refused value in the clear");
  QCOMPARE(mgr.credentialDemotionReason(), QString());
  QVERIFY(!readConfigIni().contains(QStringLiteral("credentialDemotionReason")));

  SettingsManager reloaded(nullptr, nullptr);
  reloaded.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs2;
  reloaded.loadGeneralSettings(gs2);
  QCOMPARE(credential(gs2, kPasswordField), QString());
}

void TestCredentialDemotion::lockedAtLoad_hooklessSaves_keepKeychainRow_thenRecovers() {
  // Kartend-f4nf2: a wallet locked at startup loads an @keychain password as
  // empty. Hook-less saves (toolbar toggles and the like) must keep the stored
  // row and keychain entry exactly as they were: no write, no delete, no
  // plaintext, no warning. An unlocked fresh load gets the password back.
  if (!SettingsManager::keychainBackendCompiledIn()) {
    QSKIP("Needs the keychain build; the @keychain sentinel does not exist without it.");
  }
  writeConfigIni(QStringLiteral("[Scrapers]\n"
                                "screenscraper/user_password=@keychain\n"));
  FakeKeychain fake;
  fake.store[kPasswordKey] = QStringLiteral("secret");
  fake.failReads = true;
  fake.failAll = true;
  SettingsManager mgr(nullptr, nullptr);
  mgr.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs;
  mgr.loadGeneralSettings(gs);
  QCOMPARE(fake.readAttempts, QStringList{kPasswordKey});
  QCOMPARE(credential(gs, kPasswordField), QString());

  for (int round = 0; round < 3; ++round) {
    QVERIFY(!mgr.saveGeneralSettings(gs).isError());
    QCOMPARE(iniValue(kPasswordKey).toString(), QStringLiteral("@keychain"));
    QVERIFY(fake.writeAttempts.isEmpty());
    QVERIFY(fake.removed.isEmpty());
    QCOMPARE(fake.store.value(kPasswordKey), QStringLiteral("secret"));
    QVERIFY(!readConfigIni().contains(QStringLiteral("secret")));
    QCOMPARE(mgr.credentialDemotionReason(), QString());
    QVERIFY(!readConfigIni().contains(QStringLiteral("credentialDemotionReason")));
  }
  QCOMPARE(fake.readAttempts.size(), 1); // saves never read

  fake.failReads = false;
  fake.failAll = false;
  SettingsManager reloaded(nullptr, nullptr);
  reloaded.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs2;
  reloaded.loadGeneralSettings(gs2);
  QCOMPARE(credential(gs2, kPasswordField), QStringLiteral("secret"));
  QCOMPARE(reloaded.credentialDemotionReason(), QString());
}

void TestCredentialDemotion::readableThenCleared_removesRowAndKeychainEntry() {
  // The guard above is for unreadable values only: a password that was read
  // and then cleared is removed from the INI and the keychain, and stays gone.
  if (!SettingsManager::keychainBackendCompiledIn()) {
    QSKIP("Needs the keychain build; the @keychain sentinel does not exist without it.");
  }
  writeConfigIni(QStringLiteral("[Scrapers]\n"
                                "screenscraper/user_password=@keychain\n"));
  FakeKeychain fake;
  fake.store[kPasswordKey] = QStringLiteral("secret");
  SettingsManager mgr(nullptr, nullptr);
  mgr.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs;
  mgr.loadGeneralSettings(gs);
  QCOMPARE(credential(gs, kPasswordField), QStringLiteral("secret"));

  int asks = 0;
  mgr.setPlaintextCredentialConsent([&asks](const QString &) {
    ++asks;
    return false;
  });
  gs.scraper.credentials[kProvider][kPasswordField] = QString();
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  mgr.setPlaintextCredentialConsent({});
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());

  QCOMPARE(asks, 0);
  QVERIFY(!iniValue(kPasswordKey).isValid());
  QVERIFY(!fake.store.contains(kPasswordKey));
  QVERIFY(fake.removed.contains(kPasswordKey));

  SettingsManager reloaded(nullptr, nullptr);
  reloaded.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs2;
  reloaded.loadGeneralSettings(gs2);
  QCOMPARE(credential(gs2, kPasswordField), QString());
}

void TestCredentialDemotion::lockedAtLoad_thenEdited_isNotIgnored() {
  // A password unreadable at load that the user then changes goes through the
  // normal keychain-first path. Declined while locked, the old secure entry is
  // kept; once stored, a later clear removes it and nothing comes back.
  if (!SettingsManager::keychainBackendCompiledIn()) {
    QSKIP("Needs the keychain build; the @keychain sentinel does not exist without it.");
  }
  writeConfigIni(QStringLiteral("[Scrapers]\n"
                                "screenscraper/user_password=@keychain\n"));
  FakeKeychain fake;
  fake.store[kPasswordKey] = QStringLiteral("secret");
  fake.failReads = true;
  fake.failAll = true;
  SettingsManager mgr(nullptr, nullptr);
  mgr.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs;
  mgr.loadGeneralSettings(gs);
  QCOMPARE(credential(gs, kPasswordField), QString());

  int asks = 0;
  mgr.setPlaintextCredentialConsent([&asks](const QString &) {
    ++asks;
    return false;
  });
  gs.scraper.credentials[kProvider][kPasswordField] = QStringLiteral("newpass");
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QCOMPARE(asks, 1);
  QVERIFY(fake.writeAttempts.contains(kPasswordKey + QStringLiteral("=newpass")));
  QCOMPARE(iniValue(kPasswordKey).toString(), QStringLiteral("@keychain"));
  QCOMPARE(fake.store.value(kPasswordKey), QStringLiteral("secret"));
  QVERIFY(fake.removed.isEmpty());
  QVERIFY(!readConfigIni().contains(QStringLiteral("newpass")));

  mgr.setPlaintextCredentialConsent({});
  fake.failAll = false;
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QCOMPARE(fake.store.value(kPasswordKey), QStringLiteral("newpass"));
  QCOMPARE(iniValue(kPasswordKey).toString(), QStringLiteral("@keychain"));

  // The unreadable-at-load marker is gone: clearing now removes the row and
  // the keychain entry instead of preserving them.
  gs.scraper.credentials[kProvider][kPasswordField] = QString();
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QVERIFY(!iniValue(kPasswordKey).isValid());
  QVERIFY(!fake.store.contains(kPasswordKey));
  QVERIFY(fake.removed.contains(kPasswordKey));
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QVERIFY(!iniValue(kPasswordKey).isValid());

  fake.failReads = false;
  SettingsManager reloaded(nullptr, nullptr);
  reloaded.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs2;
  reloaded.loadGeneralSettings(gs2);
  QCOMPARE(credential(gs2, kPasswordField), QString());
}

void TestCredentialDemotion::secureUnchanged_savesSkipKeychain() {
  // Kartend-f4nf2: a password read from the keychain and not changed since is
  // left alone by every later save, with or without a consent hook, even while
  // keychain writes fail: no write, no delete, no prompt, no plaintext, no
  // warning.
  if (!SettingsManager::keychainBackendCompiledIn()) {
    QSKIP("Needs the keychain build; the @keychain sentinel does not exist without it.");
  }
  writeConfigIni(QStringLiteral("[Scrapers]\n"
                                "screenscraper/user_password=@keychain\n"));
  FakeKeychain fake;
  fake.store[kPasswordKey] = QStringLiteral("secret");
  SettingsManager mgr(nullptr, nullptr);
  mgr.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs;
  mgr.loadGeneralSettings(gs);
  QCOMPARE(credential(gs, kPasswordField), QStringLiteral("secret"));

  fake.failAll = true;
  QSignalSpy demotionSpy(&mgr, &ISettingsManager::credentialStorageDemotionChanged);
  int asks = 0;
  for (int round = 0; round < 4; ++round) {
    if (round == 2) {
      mgr.setPlaintextCredentialConsent([&asks](const QString &) {
        ++asks;
        return true;
      });
    }
    QVERIFY(!mgr.saveGeneralSettings(gs).isError());
    QVERIFY(fake.writeAttempts.isEmpty());
    QVERIFY(fake.removed.isEmpty());
    QCOMPARE(iniValue(kPasswordKey).toString(), QStringLiteral("@keychain"));
    QCOMPARE(fake.store.value(kPasswordKey), QStringLiteral("secret"));
    QVERIFY(!readConfigIni().contains(QStringLiteral("secret")));
    QCOMPARE(mgr.credentialDemotionReason(), QString());
    QVERIFY(!readConfigIni().contains(QStringLiteral("credentialDemotionReason")));
  }
  QCOMPARE(asks, 0);
  QCOMPARE(demotionSpy.count(), 0);
  QCOMPARE(fake.readAttempts.size(), 1); // saves never read

  fake.failAll = false;
  SettingsManager reloaded(nullptr, nullptr);
  reloaded.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs2;
  reloaded.loadGeneralSettings(gs2);
  QCOMPARE(credential(gs2, kPasswordField), QStringLiteral("secret"));
}

void TestCredentialDemotion::declinedThenReverted_loadedBaseline_skipsAgain() {
  // The baseline is the value read at load. Declining an edit must keep that
  // baseline, not replace it with the refused value.
  if (!SettingsManager::keychainBackendCompiledIn()) {
    QSKIP("Needs the keychain build; the @keychain sentinel does not exist without it.");
  }
  writeConfigIni(QStringLiteral("[Scrapers]\n"
                                "screenscraper/user_password=@keychain\n"));
  FakeKeychain fake;
  fake.store[kPasswordKey] = QStringLiteral("secret");
  SettingsManager mgr(nullptr, nullptr);
  mgr.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs;
  mgr.loadGeneralSettings(gs);
  QCOMPARE(credential(gs, kPasswordField), QStringLiteral("secret"));
  declineThenRevert(mgr, gs, fake, QStringLiteral("secret"));
}

void TestCredentialDemotion::declinedThenReverted_savedBaseline_skipsAgain() {
  // Same, with a baseline set by a later successful secure save.
  if (!SettingsManager::keychainBackendCompiledIn()) {
    QSKIP("Needs the keychain build; the @keychain sentinel does not exist without it.");
  }
  writeConfigIni(QStringLiteral("[Scrapers]\n"
                                "screenscraper/user_password=@keychain\n"));
  FakeKeychain fake;
  fake.store[kPasswordKey] = QStringLiteral("secret");
  SettingsManager mgr(nullptr, nullptr);
  mgr.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs;
  mgr.loadGeneralSettings(gs);

  gs.scraper.credentials[kProvider][kPasswordField] = QStringLiteral("second");
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QCOMPARE(fake.writeAttempts.last(), kPasswordKey + QStringLiteral("=second"));
  QCOMPARE(fake.store.value(kPasswordKey), QStringLiteral("second"));
  QCOMPARE(iniValue(kPasswordKey).toString(), QStringLiteral("@keychain"));
  declineThenRevert(mgr, gs, fake, QStringLiteral("second"));
}

void TestCredentialDemotion::declinedStillChanged_retriesThenEditsAndClearWork() {
  // A declined value that is still the current one keeps retrying the
  // keychain on hook-less saves and is stored once it recovers; a later edit
  // is written, not skipped; clearing removes the row and the entry for good.
  if (!SettingsManager::keychainBackendCompiledIn()) {
    QSKIP("Needs the keychain build; the @keychain sentinel does not exist without it.");
  }
  writeConfigIni(QStringLiteral("[Scrapers]\n"
                                "screenscraper/user_password=@keychain\n"));
  FakeKeychain fake;
  fake.store[kPasswordKey] = QStringLiteral("secret");
  SettingsManager mgr(nullptr, nullptr);
  mgr.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs;
  mgr.loadGeneralSettings(gs);

  fake.failAll = true;
  mgr.setPlaintextCredentialConsent([](const QString &) { return false; });
  gs.scraper.credentials[kProvider][kPasswordField] = QStringLiteral("newpass");
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  mgr.setPlaintextCredentialConsent({});
  for (int round = 0; round < 2; ++round) {
    const qsizetype writesBefore = fake.writeAttempts.size();
    QVERIFY(!mgr.saveGeneralSettings(gs).isError());
    QCOMPARE(fake.writeAttempts.size(), writesBefore + 1);
    QCOMPARE(fake.writeAttempts.last(), kPasswordKey + QStringLiteral("=newpass"));
    QCOMPARE(iniValue(kPasswordKey).toString(), QStringLiteral("@keychain"));
    QCOMPARE(fake.store.value(kPasswordKey), QStringLiteral("secret"));
    QVERIFY(!readConfigIni().contains(QStringLiteral("newpass")));
    QVERIFY(fake.removed.isEmpty());
  }

  fake.failAll = false;
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QCOMPARE(fake.store.value(kPasswordKey), QStringLiteral("newpass"));
  QCOMPARE(iniValue(kPasswordKey).toString(), QStringLiteral("@keychain"));
  // Now the baseline: the next save does not write it again.
  qsizetype writesBefore = fake.writeAttempts.size();
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QCOMPARE(fake.writeAttempts.size(), writesBefore);

  // Going back to the original value is an edit now, not the baseline.
  gs.scraper.credentials[kProvider][kPasswordField] = QStringLiteral("secret");
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QCOMPARE(fake.writeAttempts.size(), writesBefore + 1);
  QCOMPARE(fake.store.value(kPasswordKey), QStringLiteral("secret"));
  writesBefore = fake.writeAttempts.size();
  gs.scraper.credentials[kProvider][kPasswordField] = QStringLiteral("third");
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QCOMPARE(fake.writeAttempts.size(), writesBefore + 1);
  QCOMPARE(fake.store.value(kPasswordKey), QStringLiteral("third"));

  gs.scraper.credentials[kProvider][kPasswordField] = QString();
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QVERIFY(!iniValue(kPasswordKey).isValid());
  QVERIFY(!fake.store.contains(kPasswordKey));
  QVERIFY(fake.removed.contains(kPasswordKey));
  // Typing the cleared value again is a new credential and is written.
  gs.scraper.credentials[kProvider][kPasswordField] = QStringLiteral("third");
  fake.failAll = true;
  mgr.setPlaintextCredentialConsent([](const QString &) { return false; });
  writesBefore = fake.writeAttempts.size();
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QCOMPARE(fake.writeAttempts.size(), writesBefore + 1);
  QVERIFY(!iniValue(kPasswordKey).isValid());
  mgr.setPlaintextCredentialConsent({});
  gs.scraper.credentials[kProvider][kPasswordField] = QString();
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QVERIFY(!mgr.saveGeneralSettings(gs).isError());
  QVERIFY(!iniValue(kPasswordKey).isValid());
  QVERIFY(!fake.store.contains(kPasswordKey));

  fake.failAll = false;
  SettingsManager reloaded(nullptr, nullptr);
  reloaded.setCredentialBackendForTesting(fake.backend());
  GeneralSettings gs2;
  reloaded.loadGeneralSettings(gs2);
  QCOMPARE(credential(gs2, kPasswordField), QString());
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
