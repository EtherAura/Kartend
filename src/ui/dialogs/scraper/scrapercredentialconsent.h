#ifndef SCRAPERCREDENTIALCONSENT_H
#define SCRAPERCREDENTIALCONSENT_H

class ISettingsManager;
class QWidget;

/// Kartend-9t7fe: the user-facing half of the plaintext-credential decision.
///
/// SettingsManager cannot ask the question itself — it lives in the data layer
/// and a save can happen with no window to parent a dialog to — so it exposes a
/// consent hook and this installs the prompt into it. Both dialogs that edit
/// scraper credentials call install() while they are open, which is exactly the
/// window in which a keychain write can fail with a user present to answer.
///
/// The hook is a plain std::function on ISettingsManager, so this stays a ui →
/// api dependency and adds no manager pointer anywhere.
namespace ScraperCredentialConsent {

/// Install the prompt on @p settingsManager, parented to @p host. Safe to call
/// with a null manager or host.
///
/// Teardown is automatic and needs NO destructor call from the host: the hook
/// is cleared when @p host is destroyed, via a connection whose context object
/// is the manager, so whichever of the two dies first the other is never
/// touched after the fact. Uninstalling from a dialog destructor instead was
/// undefined behaviour — see the note in the .cpp.
///
/// Because a cleared hook means "no prompt", a save with no credential dialog
/// open behaves exactly as it did before this existed: demote to plaintext and
/// report it in the banner afterwards. Prompting requires a window to ask from.
///
/// KNOWN WART: the hook is a single slot, so with BOTH credential dialogs open
/// at once the second install wins and the first one's close clears it. The
/// degradation is back to the pre-existing demote-and-warn behaviour, never a
/// crash, and the two dialogs are not reachable simultaneously in practice.
void install(ISettingsManager *settingsManager, QWidget *host);

/// Clear the prompt explicitly, restoring the "demote and report afterwards"
/// default. Not needed for ordinary teardown — install() handles that — but
/// kept for callers that want to revoke consent-prompting early, and used by
/// the tests to assert the cleared state.
void uninstall(ISettingsManager *settingsManager);

} // namespace ScraperCredentialConsent

#endif // SCRAPERCREDENTIALCONSENT_H
