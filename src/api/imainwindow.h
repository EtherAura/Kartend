#ifndef IMAINWINDOW_H
#define IMAINWINDOW_H

#include "collectionutils.h"
#include <QList>
#include <QString>

class ApplicationManager;
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

  /// New-style routing point. Callers reach sibling managers by going through
  /// the ApplicationManager (e.g. mainWindow->applicationManager()->getXxx()).
  /// Forward-declared so this header stays lean; .cpp callers include the
  /// ApplicationManager header where needed.
  [[nodiscard]] virtual ApplicationManager *applicationManager() const = 0;

  /// Sibling manager accessors used by ui-layer settings dialogs to persist
  /// edits, push live-apply side effects, and capture gamepad bindings.
  /// Promoted to the interface to break the ui->core mainwindow.h include
  /// cycle — concrete return types are forward-declared above, so callers
  /// bring their own concrete header when they actually deref.
  ///
  /// Kartend-5wuk.1: renamed from getXxxManager() so a grep for
  /// 'mainWindow->get.*Manager' catches only legacy per-manager call sites
  /// inside src/core/mainwindow*.cpp (which the audit exempts). External
  /// callers route through these without dragging in applicationmanager.h.
  [[nodiscard]] virtual ISettingsManager *settingsManager() const = 0;
  [[nodiscard]] virtual InteractionManager *interactionManager() const = 0;
  [[nodiscard]] virtual ScrollManager *scrollManager() const = 0;

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

  /// Apply the user-configured pixmap cache budget (MB) to BOTH Qt's
  /// process-global QPixmapCache and the CacheManager artworkCache.
  /// Settings dialogs and startup wiring should call this single entry
  /// point rather than touching the two caches independently — historic
  /// drift between them was Kartend-10pb. Idempotent.
  virtual void applyPixmapCacheBudget(int megabytes) = 0;
};

#endif // IMAINWINDOW_H
