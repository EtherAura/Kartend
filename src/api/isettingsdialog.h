#ifndef ISETTINGSDIALOG_H
#define ISETTINGSDIALOG_H

#include "collection/collectionconfig.h"
#include <QList>

/// Caller-side hint for which navigation row the settings dialog should
/// pre-select. The dialog defaults to its standard first per-collection
/// row when this is left at Default. A caller that opens the dialog with
/// a specific intent (toolbar warning badge → Launchers; future hints →
/// Scrapers, etc.) sets the matching value; the mapping from this enum
/// to the QStackedWidget page indices lives inside the concrete dialog
/// so the page-order detail stays out of the public ABI.
enum class SettingsPage {
  Default = 0,
  // Application (app-wide) tabs surfaced through caller hints today.
  Launchers,
};

/**
 * @brief Neutral role interface to the modal Settings dialog.
 *
 * Lets the data-layer SettingsDialogController drive the settings dialog
 * (run it modally, read back the edited collection list) without #including
 * the concrete src/ui/dialogs/settings/settingsdialog.h — which would pull a
 * data->ui edge and break the layered DAG.
 *
 * Plain abstract class, not a QObject: SettingsDialog already derives QDialog
 * (its single QObject base) plus CollectionRemoverHost, so it picks this up
 * as a further non-QObject base. exec() is satisfied by QDialog::exec().
 *
 * The dialog cannot be *constructed* through this interface — construction
 * (and the collectionSaved / rescanRequired signal wiring, which needs the
 * concrete Qt signal symbols) is owned by MainWindow and handed to the data
 * layer as the SettingsDialogContext::createSettingsDialog factory. This
 * interface covers only what the controller invokes on an already-built,
 * already-wired dialog instance.
 */
class ISettingsDialog {
public:
  virtual ~ISettingsDialog() = default;

  /// Run the dialog modally. Returns a QDialog::DialogCode
  /// (QDialog::Accepted / QDialog::Rejected). Satisfied by QDialog::exec().
  virtual int exec() = 0;

  /// The collection list as edited in the dialog, valid after exec() returns.
  [[nodiscard]] virtual const QList<CollectionConfig> &getCollections() const = 0;

  /// Pre-select the navigation row matching @p page before the dialog runs.
  /// Called by the controller between the factory hand-off and exec(), so
  /// implementations can apply the selection in whichever phase suits
  /// (immediately or queued post-show). A no-op when @p page is Default.
  virtual void setInitialPage(SettingsPage page) = 0;
};

#endif // ISETTINGSDIALOG_H
