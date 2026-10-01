#include "scrapercredentialconsent.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QString>
#include <QWidget>

#include "isettingsmanager.h"

namespace ScraperCredentialConsent {

void install(ISettingsManager *settingsManager, QWidget *host) {
  if (!settingsManager || !host) return;
  // Teardown is driven from HERE, not from the host's destructor. Calling back
  // into the manager from ~SettingsDialog was undefined behaviour: by then the
  // manager's derived destructor has already run, so its vptr has decayed to
  // plain QObject and the virtual call lands on an object that is no longer an
  // ISettingsManager (caught by UBSan, Kartend-9t7fe).
  //
  // Binding the cleanup to the host's destroyed() signal WITH THE MANAGER AS
  // THE CONTEXT OBJECT gets both orders right for free:
  //   * host dies first  → the slot runs while the manager is alive, clearing
  //                        the hook;
  //   * manager dies first → Qt drops the connection with its context, so
  //                        nothing ever touches the half-destroyed object.
  // That is why the raw `settingsManager` capture below is safe.
  QObject::connect(host, &QObject::destroyed, settingsManager,
                   [settingsManager]() { settingsManager->setPlaintextCredentialConsent({}); });
  settingsManager->setPlaintextCredentialConsent([guarded = QPointer<QWidget>(host)](
                                                     const QString &reason) -> bool {
    // Host gone without an uninstall(): answer NO. Declining is the
    // conservative outcome — it leaves any securely-stored credential
    // alone and simply does not save the new one, whereas defaulting to
    // yes would write a password to disk in the clear with nobody asked.
    if (!guarded) return false;

    QMessageBox box(guarded);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(
        QCoreApplication::translate("ScraperCredentialConsent", "Store password unencrypted?"));
    box.setText(QCoreApplication::translate(
        "ScraperCredentialConsent",
        "Kartend could not save your scraper password to the system keychain."));
    box.setInformativeText(
        QCoreApplication::translate("ScraperCredentialConsent",
                                    "Reason: %1\n\n"
                                    "It can store the password in Kartend's settings file "
                                    "instead, where it is readable by anything that can read "
                                    "your files. Otherwise the password is not saved — unlock "
                                    "your keyring or wallet and save again to store it securely.")
            .arg(reason));
    QPushButton *store =
        box.addButton(QCoreApplication::translate("ScraperCredentialConsent", "Store Unencrypted"),
                      QMessageBox::DestructiveRole);
    QPushButton *dontSave =
        box.addButton(QCoreApplication::translate("ScraperCredentialConsent", "Don't Save"),
                      QMessageBox::RejectRole);
    // Safe answer is the default, so Return/Escape both decline.
    box.setDefaultButton(dontSave);
    box.setEscapeButton(dontSave);
    box.exec();
    return box.clickedButton() == store;
  });
}

void uninstall(ISettingsManager *settingsManager) {
  if (!settingsManager) return;
  settingsManager->setPlaintextCredentialConsent({});
}

} // namespace ScraperCredentialConsent
