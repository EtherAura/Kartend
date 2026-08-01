#ifndef IMAINWINDOW_H
#define IMAINWINDOW_H

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "iappearanceapplier.h"
#include "idataudithost.h"
#include <QList>
#include <QString>

class ApplicationManager;

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
 * it on the main window. Per-domain surfaces are split into narrow role
 * interfaces this class unions (IAppearanceApplier, IDatAuditHost — the way
 * IScrollManager unions its six scroll roles) so a consumer can depend on just
 * the role it uses and those role headers stay free of the CollectionConfig /
 * GeneralSettings god-headers (Kartend-wu2i7).
 */
class IMainWindow : public IAppearanceApplier, public IDatAuditHost {
public:
  ~IMainWindow() override = default;

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

  /// Kartend-ckepd.6/.5: launch an entity (collection/platform artwork) scrape
  /// for @p collectionIndex — one job per non-Game entity type the collection's
  /// provider supports. InteractionManager's right-click "Scrape collection /
  /// platform artwork" entry routes here (sibling of openScraperDialog).
  virtual void openEntityScraperDialog(int collectionIndex) = 0;

  // DAT-audit surface — openDatAuditForCollection / datAuditStatusForCollection
  // and the DatAuditStatus struct — is the IDatAuditHost role (inherited above).

  /// Mutable / const access to the main window's live GeneralSettings.
  /// SettingsDialog panels mirror their working copy onto the main window
  /// through these accessors; ShortcutsDialog reads them to render the
  /// current keybindings. Goes through IMainWindow so the ui/ dialog
  /// translation units don't need to #include the concrete mainwindow.h.
  [[nodiscard]] virtual GeneralSettings &generalSettings() = 0;
  [[nodiscard]] virtual const GeneralSettings &generalSettings() const = 0;

  /// The active kartend.cfg was just replaced wholesale on disk (settings
  /// dialog profile import) and the app is about to quit so the new file
  /// takes effect on restart. The shutdown path honors this by skipping
  /// every kartend.cfg write on the way out — the in-memory state still
  /// describes the OLD configuration, and persisting it would silently
  /// overwrite the just-imported file. Deliberately a no-op default rather
  /// than pure: widget-only fakes in tests never shut the app down and
  /// shouldn't have to stub shutdown-persistence concerns.
  virtual void markConfigReplacedOnDisk() {}

  /// Query counterpart of markConfigReplacedOnDisk(): dialog-side writers
  /// that persist [General] outside the shutdown path (live-save flushes,
  /// reject-path baseline restores) consult this so they don't rewrite the
  /// just-imported file while the app quits. Same default-not-pure rationale.
  [[nodiscard]] virtual bool isConfigReplacedOnDisk() const { return false; }

  /// Coalesce an interactive per-click collections save through the main
  /// window's debounced save stage (the one the grid-width shortcuts and
  /// view-mode toggles already ride). saveCollections is a full INI rewrite
  /// + fsync of the config directory, so per-click callers (toolbar filter
  /// toggles, sidebar drag commits, menu radio switches) route here instead
  /// of saving inline: the mutation has already landed in the live
  /// m_collections, only the disk write is deferred, and a save still inside
  /// the debounce window at quit is covered by the shutdown path's
  /// synchronous persist. Same default-not-pure rationale as
  /// markConfigReplacedOnDisk: widget-only fakes don't persist anyway and
  /// shouldn't have to stub save plumbing. Callers that resolve no host
  /// (headless / test harnesses) keep their inline immediate save.
  virtual void requestDebouncedCollectionsSave() {}

  /// Sole routing point for sibling managers. Callers reach them by going
  /// through the ApplicationManager (e.g.
  /// mainWindow->applicationManager()->getXxx()), or — preferred for ui-layer
  /// dialogs — through an injected ApplicationContext. Forward-declared so
  /// this header stays lean; .cpp callers include the ApplicationManager
  /// header where needed.
  ///
  /// Kartend-qjtz removed the per-manager forwarders (settingsManager() /
  /// interactionManager() / scrollManager()) that used to live here: the
  /// settings dialog now reaches those siblings through its injected
  /// ApplicationContext, so IMainWindow exposes no `*Manager`-returning
  /// accessor other than this one. check-layering.py enforces that.
  [[nodiscard]] virtual ApplicationManager *applicationManager() const = 0;

  // Appearance-apply surface — applyGlobalUiFontFromSettings / applyMarqueeSettings
  // / applyToolbarCustomization / applyPixmapCacheBudget — is the IAppearanceApplier
  // role (inherited above).
};

#endif // IMAINWINDOW_H
