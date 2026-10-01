// Kartend-9t7fe: the ui half of the plaintext-credential decision. The prompt
// itself is modal and needs a person, so what is pinned here is the part that
// runs WITHOUT one — install/uninstall lifetime and the answer given when the
// host window is gone. That is the crash-and-security-relevant half: the
// settings manager outlives every dialog, so a callback holding a dangling host
// would either fault or silently answer for a window nobody is looking at.
#include "scrapercredentialconsent.h"

#include <memory>
#include <QString>
#include <QTest>
#include <QWidget>

#include "../../integration/mocks/mocksettingsmanager.h"
#include "isettingsmanager.h"

namespace {

/// Captures the consent hook the ui installs so the test can invoke it the way
/// SettingsManager::saveScraperSection would.
class CapturingSettingsManager : public KartendTest::MockSettingsManager {
public:
  void setPlaintextCredentialConsent(const PlaintextCredentialConsent &consent) override {
    installed = consent;
  }
  PlaintextCredentialConsent installed;
};

} // namespace

class TestScraperCredentialConsent : public QObject {
  Q_OBJECT
private slots:
  void installStoresAHookAndUninstallClearsIt();
  void destroyedHostClearsTheHook();
  void hookDeclinesIfSomehowCalledWithNoHost();
  void nullSettingsManagerIsANoOp();
};

void TestScraperCredentialConsent::installStoresAHookAndUninstallClearsIt() {
  CapturingSettingsManager sm;
  QWidget host;
  QVERIFY(!sm.installed); // nothing installed until asked

  ScraperCredentialConsent::install(&sm, &host);
  QVERIFY2(static_cast<bool>(sm.installed), "install() must leave a callable hook behind");

  // Uninstall restores the "no callback" state, which is what makes the data
  // layer fall back to its pre-existing demote-and-report-after behaviour.
  ScraperCredentialConsent::uninstall(&sm);
  QVERIFY2(!sm.installed, "uninstall() must clear the hook, not merely replace it");
}

void TestScraperCredentialConsent::destroyedHostClearsTheHook() {
  // Teardown is driven by the host's destroyed() signal, NOT by a dialog
  // destructor — doing it from ~SettingsDialog called a virtual on an
  // ISettingsManager whose derived part had already been destroyed, which UBSan
  // flagged as a member call on an object that is no longer of that type.
  //
  // Clearing (rather than leaving a dead hook installed) is what keeps
  // behaviour consistent: "no credential dialog open" always means the
  // pre-existing demote-and-warn path, whether or not a dialog was ever opened
  // in this session.
  CapturingSettingsManager sm;
  auto host = std::make_unique<QWidget>();
  ScraperCredentialConsent::install(&sm, host.get());
  QVERIFY(static_cast<bool>(sm.installed));

  host.reset();
  QVERIFY2(!sm.installed, "destroying the host must clear the consent hook");
}

void TestScraperCredentialConsent::hookDeclinesIfSomehowCalledWithNoHost() {
  // Defence in depth for the same hazard: even if a hook outlives its window
  // (a future edit, a queued invocation racing teardown), invoking it must not
  // dereference the dead widget — and must answer with the conservative
  // outcome, since returning true would write a password to disk in the clear
  // with nobody asked. Captured before the host dies so the cleared-hook path
  // above cannot mask this one.
  CapturingSettingsManager sm;
  auto host = std::make_unique<QWidget>();
  ScraperCredentialConsent::install(&sm, host.get());
  const auto captured = sm.installed;
  QVERIFY(static_cast<bool>(captured));

  host.reset();

  // No QMessageBox is constructed on this branch, so it is headless-safe —
  // which is exactly the branch being asserted.
  QVERIFY2(!captured(QStringLiteral("keychain unavailable")),
           "a hook with no host must decline the plaintext fallback, not permit it");
}

void TestScraperCredentialConsent::nullSettingsManagerIsANoOp() {
  // Both dialogs reach the settings manager through an optional accessor, so
  // null is an ordinary state rather than a programming error. A null host is
  // likewise tolerated — there would be nothing to parent a prompt to.
  QWidget host;
  ScraperCredentialConsent::install(nullptr, &host);
  ScraperCredentialConsent::uninstall(nullptr);

  CapturingSettingsManager sm;
  ScraperCredentialConsent::install(&sm, nullptr);
  QVERIFY2(!sm.installed, "a null host must not install a prompt with nothing to parent to");
}

QTEST_MAIN(TestScraperCredentialConsent)
#include "test_scrapercredentialconsent.moc"
