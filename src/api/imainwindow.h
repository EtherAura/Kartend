#ifndef IMAINWINDOW_H
#define IMAINWINDOW_H

#include "collectionutils.h"
#include <QList>
#include <QString>

class ISettingsManager;
class InteractionManager;
class ScrollManager;

/**
 * @brief Neutral role interface to the application's main window.
 *
 * Lets lower-layer coordinators (data-layer SettingsManager /
 * SettingsDialogController, input-layer InteractionManager) reach the
 * handful of MainWindow operations they need without #including the
 * concrete src/core/mainwindow.h — which would pull a data->core /
 * input->core edge and break the layered DAG.
 *
 * Plain abstract class, not a QObject: MainWindow already derives QMainWindow
 * (its single QObject base), so it picks this up as a second, non-QObject
 * base. Cross-cast to it with dynamic_cast, not qobject_cast.
 *
 * Add a method here only when a lower-layer module genuinely needs to call
 * it on the main window.
 */
class IMainWindow {
public:
  virtual ~IMainWindow() = default;

  /// The live collection list the main window owns. SettingsManager reads
  /// it to resolve a collection's persistent last-selected index;
  /// ui-layer settings dialogs read it to populate the startup-collection
  /// picker without #including the concrete MainWindow.
  [[nodiscard]] virtual const QList<CollectionConfig> &collections() const = 0;

  /// Refresh the window title for the given collection after a settings save.
  virtual void updateWindowTitleForCollection(int collectionIndex) = 0;

  /// Open the unified scraper dialog, optionally pre-targeted at one
  /// collection / item. InteractionManager's right-click "Scraper…" entry
  /// routes here so it need not #include the concrete MainWindow.
  virtual void openScraperDialog(int preCollectionIndex = -1,
                                 const QString &preItemPath = QString()) = 0;

  /// Mutable / const access to the main window's live GeneralSettings.
  /// SettingsDialog panels mirror their working copy onto the main window
  /// through these accessors; ShortcutsDialog reads them to render the
  /// current keybindings. Goes through IMainWindow so the ui/ dialog
  /// translation units don't need to #include the concrete mainwindow.h.
  [[nodiscard]] virtual GeneralSettings &generalSettings() = 0;
  [[nodiscard]] virtual const GeneralSettings &generalSettings() const = 0;

  /// Sibling manager accessors the ui-layer settings dialogs reach for to
  /// persist edits, push live-apply side effects, and capture gamepad
  /// bindings. Promoted to the interface to break the ui->core mainwindow.h
  /// include cycle — concrete return types are forward-declared above, so
  /// callers bring their own concrete header when they actually deref.
  [[nodiscard]] virtual ISettingsManager *getSettingsManager() const = 0;
  [[nodiscard]] virtual InteractionManager *getInteractionManager() const = 0;
  [[nodiscard]] virtual ScrollManager *getScrollManager() const = 0;

  /// Apply this window's current GeneralSettings to the global QApplication
  /// font. Thin instance shim over MainWindow's static applyGlobalUiFont so
  /// settings-dialog callers don't have to name MainWindow at all.
  virtual void applyGlobalUiFontFromSettings() = 0;

  /// Sync the secondary-monitor marquee window to the current
  /// GeneralSettings.marquee* fields after a settings save. Idempotent.
  virtual void applyMarqueeSettings() = 0;

  /// Push per-button visibility flags and custom-text overrides from
  /// GeneralSettings to the items-page toolbar after a settings save.
  /// Idempotent.
  virtual void applyToolbarCustomization() = 0;
};

#endif // IMAINWINDOW_H
