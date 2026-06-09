#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/collectionhierarchycache.h"
#include "collection/generalsettings.h"
#include "imainwindow.h"
#include "isettingsdialog.h" // for SettingsPage on openSettingsDialog signature
#include <functional>
#include <memory>
#include <QFont>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QStringList>
#include <QTimer>

QT_BEGIN_NAMESPACE
class QStackedWidget;
class QWidget;
class QGridLayout;
class QHBoxLayout;
class QScrollArea;
class QLineEdit;
class QPushButton;
class QLabel;
QT_END_NAMESPACE

class Ui_MainWindow;
class GridWidthDebouncer;
class ToolbarController;
class ApplicationManager;
class ArtworkManager;
class CacheManager;
class CollectionFilesystemWatcher;
class InteractionManager;
class IDatabaseManager;
class NavigationManager;
class PlaylistManager;
class SessionManager;
class ISettingsManager;
class ScrapeResultDialog;
namespace Scraper {
class ScraperService;
}
class DetailsPaneManager;
class ScrollManager;
class ItemWidget;
class DetailsPane;
class LoadingOverlay;
class EmptyStateWidget;
class SplashOverlay;
class NowPlayingOverlay;
class DetailPageOverlay;
class DetailPageManager;
class MenuController;
class MarqueeController;
class DbEventsController;
class DialogController;
struct SettingsDialogContext;
class ScraperController;
class ScrollEventsController;
class TextZoomHud;
class OverlayZOrderRegistry;

namespace kart {
class KartManager;
}

namespace TimerUtils {
class DebouncedTimer;
}

class MainWindow : public QMainWindow, public IMainWindow {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(MainWindow)

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

  GeneralSettings m_generalSettings;

  // Kartend-nbfgs: owns the generated Ui struct via unique_ptr instead of a raw
  // pointer + explicit `delete ui` in ~MainWindow. The old `delete ui` ran in
  // the destructor BODY — before any member (manager/controller) destruction —
  // freeing the Ui struct (and dangling ctx.ui) while managers that may read
  // ctx.ui during teardown were still alive. As a member declared above the
  // managers, this unique_ptr destructs AFTER them (reverse declaration order),
  // so the Ui struct outlives its users; the parented widgets themselves are
  // freed later still by ~QObject. Destroyed out-of-line in mainwindow.cpp where
  // Ui_MainWindow is complete.
  std::unique_ptr<Ui_MainWindow> ui;
  QStackedWidget *stackedWidget;
  QWidget *itemsPage;
  QWidget *gridContainer;
  QWidget *m_mainContentWidget;
  QGridLayout *itemGrid;
  QHBoxLayout *m_mainHorizontalLayout;
  QLineEdit *searchBar;
  EmptyStateWidget *loadingLabel;
  LoadingOverlay *m_loadingOverlay = nullptr;

  int currentCollectionIndex;
  QList<CollectionConfig> m_collections;
  CollectionHierarchyCache m_hierarchyCache;
  ApplicationContext m_appContext; // Shared context for manager setup

  void refreshTitleCounts();
  void updateWindowTitleForCollection(int collectionIndex) override;
  void rebuildHierarchyCache();

  // IMainWindow — neutral role interface for the data-layer coordinators.
  [[nodiscard]] const QList<CollectionConfig> &collections() const override {
    return m_collections;
  }
  [[nodiscard]] GeneralSettings &generalSettings() override { return m_generalSettings; }
  [[nodiscard]] const GeneralSettings &generalSettings() const override {
    return m_generalSettings;
  }
  void applyGlobalUiFontFromSettings() override { applyGlobalUiFont(m_generalSettings); }
  [[nodiscard]] const CollectionHierarchyCache &getHierarchyCache() const {
    return m_hierarchyCache;
  }
  [[nodiscard]] const ApplicationContext &getAppContext() const { return m_appContext; }
  [[nodiscard]] bool isShuttingDown() const { return m_isShuttingDown; }

  // Getters for Managers
  [[nodiscard]] ApplicationManager *getApplicationManager() const { return m_appManager.get(); }
  [[nodiscard]] DetailsPane *getMetadataSidebar() const { return m_MetadataSidebar; }

  // Delegated Getters
  // Manager accessors removed (Kartend-pefm). External callers route through
  // mainWindow->applicationManager()->getXxxManager(); internal callers
  // use m_appManager directly. applicationManager() remains because it
  // implements IMainWindow's sole pure-virtual manager accessor — the
  // per-manager settingsManager()/scrollManager()/interactionManager()
  // forwarders were dropped in Kartend-qjtz once the settings dialog moved
  // to ApplicationContext-based sibling access.
  [[nodiscard]] ApplicationManager *applicationManager() const override {
    return m_appManager.get();
  }

  /// Re-runs the playlist synthesis pass: drops any prior
  /// playlist-backed CollectionConfigs from m_collections, queries the live
  /// playlists table, appends a synthesized config per row, and rebuilds the
  /// hierarchy cache. Wired to PlaylistManager::playlistsChanged so adds/
  /// renames/deletes show up in the sidebar without a restart.
  void resyncPlaylistCollections();

  void showStartupSplash();

  /// Apply per-button visibility flags and custom-text overrides from
  /// m_generalSettings to the items-page toolbar. Idempotent — invoked on
  /// startup and again after the user saves the Settings dialog.
  /// Delegates to the ToolbarController.
  void applyToolbarCustomization() override;

  /// Push the pixmap cache budget to Qt's QPixmapCache (per-process
  /// QPainter scratch cache) AND the CacheManager's artworkCache (the
  /// in-process artwork QCache used for fast UI thumbnails). Both used
  /// to be set independently from settings code paths, which let the
  /// CacheManager budget drift permanently to the legacy 50 MB default
  /// — Kartend-10pb. Idempotent.
  void applyPixmapCacheBudget(int megabytes) override;

  /// Open the unified Scraper dialog. Caller is responsible for any
  /// pre-selection (right-click flow passes a collection + item; File
  /// → Scraper passes nothing). The dialog handles the rest:
  /// collection-tree picker, per-item checkboxes, media-type filter,
  /// Auto/Interactive toggle, and per-collection BatchScrapeRunner
  /// orchestration.
  void openScraperDialog(int preCollectionIndex = -1,
                         const QString &preItemPath = QString()) override;

  /// Open the settings dialog with the standard MainWindow-rooted context
  /// (collections list, current index, manager handles, dialog factory).
  /// Used by the menu/palette entries AND by the toolbar warning-badge
  /// click handler so all entry points go through the same wiring. The
  /// @p initialPage hint pre-selects a navigation row (e.g. the badge
  /// click passes SettingsPage::Launchers); SettingsPage::Default leaves
  /// the dialog at its standard landing row.
  void openSettingsDialog(SettingsPage initialPage = SettingsPage::Default);

  // Kartend-hzef step 3: getScraperService() was never used by an external
  // caller; service ownership moved into ScraperController and the accessor
  // is gone with it. Re-add as a forwarder if a future caller needs it.

  /// Check for a pending-scrape state file on startup and surface a
  /// modal resume / discard prompt (unless GeneralSettings has
  /// scrapeAutoResume = true, in which case resume silently). Called
  /// once after the main window has finished its construction so the
  /// modal lands on top of an already-painted UI.
  void promptResumePendingScrapeIfAny();

  /// Sync the secondary-monitor marquee window to the current
  /// m_generalSettings.marquee* fields. Creates the window on first
  /// enable, destroys it on disable, re-pins to a different screen on
  /// screen-name change, and pushes a fresh pixmap for the active mode.
  /// Idempotent — invoked at app startup and again after each Save in
  /// SettingsDialog so the user's edits take effect without a restart.
  void applyMarqueeSettings() override;
  /// Push the artwork relevant to the active marquee mode to the
  /// marquee window. No-op when the marquee is disabled / not created.
  /// Called whenever the selection or active collection changes.
  void updateMarqueeArtwork();
  /// Public escape hatch the toolbar controller calls when the user picks a
  /// new view type from the layout-picker popup. Forwards to the private
  /// setViewType() that does the actual settings save + refresh cascade.
  void setViewTypeFromToolbar(ViewType viewType);

  /// pushes the global UI font (family + point size) from
  /// @p settings to QApplication. Empty family / 0 size means "leave the
  /// platform default in place." Idempotent — invoked on startup after
  /// settings load and again whenever the user changes the font in the
  /// Settings dialog. Honors the runtime text-zoom multiplier from
  /// @p settings.
  void applyGlobalUiFont(const GeneralSettings &settings);

  /// current runtime text-zoom multiplier, expressed as
  /// percent (100 = unscaled). Read by every place that pushes a font size
  /// onto a widget — global UI font, item titles, sidebar labels — so a
  /// single Ctrl++ press scales literally every line of text in the app.
  static int textZoomPercent();
  /// set @p percent (clamped to [50, 300]), persist it to
  /// settings, and trigger a re-render of every widget that draws text:
  ///   • QApplication font (via applyGlobalUiFont)
  ///   • Active sidebar (re-runs applyAppearance for the current collection)
  ///   • Items grid / list / coverflow (rebuilds the virtual scroll widgets)
  /// Safe to call from a shortcut handler — the heavy refresh work is
  /// debounced by the existing scroll-layout pipeline.
  void applyTextZoom(int percent);

  /// helper that returns @p baseSize scaled by the active
  /// text-zoom multiplier. Returns @p baseSize unchanged when zoom is 100
  /// or @p baseSize is non-positive. Centralizes the rounding rule so
  /// every callsite (item widgets, sidebar, coverflow) computes the same
  /// scaled value for a given input.
  static int zoomedFontSize(int baseSize);
  /// lightweight setter used during startup to publish the
  /// persisted multiplier into the static before any widget is built. Does
  /// NOT persist or trigger refreshes — applyTextZoom() is the runtime
  /// path that does both. Clamps @p percent to the same [50, 300] range
  /// applyTextZoom uses so a hand-edited config can't widen it later.
  static void primeTextZoomFromSettings(int percent);

  /// install Ctrl+= / Ctrl+- / Ctrl+0 application shortcuts
  /// for zoom in / out / reset. Called once from setupUI() after the
  /// managers are wired so applyTextZoom() can refresh them. Step size is
  /// 10 percentage points per press — enough to feel responsive without
  /// requiring many keystrokes to traverse the [50, 300] range.
  void setupTextZoomShortcuts();
  /// install Ctrl+K to toggle pause/resume on the sidebar's
  /// preview video. The fullscreen artwork overlay handles its own K key
  /// internally because it grabs keyboard focus when shown.
  void setupVideoPauseShortcut();
  /// bind the toolbar volume slider to
  /// VideoPreviewWidget::setGlobalVolume and persist on change.
  void setupPreviewVolumeSlider();

  /// Reflect @p viewType onto the toolbar layout-picker. Called from
  /// setViewType() and updateWindowTitleForCollection(). Delegates to the
  /// ToolbarController.
  void syncViewModeButton(ViewType viewType);

protected:
  bool event(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  auto eventFilter(QObject *watched, QEvent *event) -> bool override;
  void closeEvent(QCloseEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dropEvent(QDropEvent *event) override;

private:
  // Drains m_pendingKartImports — runs the per-kart destination prompt + import
  // off the drop handler (see dropEvent) so modals don't nest inside the DnD
  // event and a second drop can't re-enter mid-import (Kartend-tubnr).
  void processPendingKartImports();

  bool m_isShuttingDown = false;
  bool m_deferredStartupDone = false;
  // Kartend-3vkjc: first-run startup gate. When the first-run wizard runs, its
  // modal exec() spins a nested event loop; the independent startup
  // singleShots (playlist resync, orphan purge) would otherwise fire inside it
  // and mutate m_collections before setupInitialTimers' post-wizard branch
  // reads it. While this gate is set (only when firstRunComplete was false),
  // those tasks record themselves in the *_pending flags and return without
  // running; runDeferredStartupTasks() releases the gate and runs them once,
  // in order, after the wizard returns. firstRunComplete == true never sets
  // the gate, so the steady-state launch path is unchanged.
  bool m_startupTasksGated = false;
  bool m_pendingResync = false;
  bool m_pendingOrphanPurge = false;
  // Guards connectDatabaseManager() — one-shot wiring whose non-UniqueConnection
  // and lambda edges would double-fire if it ever ran twice (Kartend-x8spn).
  bool m_databaseManagerConnected = false;
  // Kart drag-drop import queue + re-entrancy guard (Kartend-tubnr). dropEvent
  // collects .kart paths here and accepts immediately; processPendingKartImports
  // drains them on the next event-loop turn so the per-file destination prompt
  // and (blocking) import run outside the DnD handler. The guard stops a second
  // drop — or one delivered inside a prompt's nested loop — from starting a
  // concurrent drain.
  bool m_kartImportInProgress = false;
  QStringList m_pendingKartImports;
  // Pristine application font, captured per-instance on the first
  // applyGlobalUiFont call so clearing a font override restores Qt's default.
  // Kartend-r2722: was a process-wide static that captured whatever font the
  // previous MainWindow / test had already left on QApplication.
  bool m_uiFontBaselineCaptured = false;
  QFont m_uiFontBaseline;
  /// Coalesces resize-driven recenter: restarted on every resizeEvent so only
  /// the last resize in a drag re-centers, instead of one singleShot per tick
  /// (Kartend-20utj). Lazily constructed on first resize.
  QTimer *m_resizeRecenterTimer = nullptr;
  std::unique_ptr<MenuController> m_menuController;
  /// Central z-order coordinator for every registered overlay. Constructed
  /// before any overlay widget so each overlay's setLayerManager() call
  /// during setupUI() registers against a live instance. Owns no widgets —
  /// only references via QPointer.
  std::unique_ptr<OverlayZOrderRegistry> m_overlayLayerManager;
  SplashOverlay *m_splashOverlay = nullptr;
  NowPlayingOverlay *m_nowPlayingOverlay = nullptr;
  DetailPageOverlay *m_detailPageOverlay = nullptr;
  TextZoomHud *m_textZoomHud = nullptr;
  /// Declared BEFORE the controllers below (marquee / scrollEvents / dbEvents /
  /// scraper / dialog) and before m_collectionWatcher, so it is destroyed LAST:
  /// members destruct in reverse declaration order, and several of those owners
  /// reach a manager through m_appManager during their own destruction (e.g.
  /// MarqueeController, the filesystem watcher's rescan callback). Keep it first
  /// or a controller's teardown slot/timer can deref a freed manager getter
  /// (Kartend-rqsb).
  std::unique_ptr<ApplicationManager> m_appManager;
  /// Drives the secondary-monitor "marquee" / topper window — owns the
  /// MarqueeWindow (lazily created on first enable) and the trailing-edge
  /// artwork-refresh debounce timer. applyMarqueeSettings() and
  /// updateMarqueeArtwork() delegate straight to it.
  std::unique_ptr<MarqueeController> m_marqueeController;
  /// Owns MainWindow's reactions to ScrollManager view-mode / column-resize
  /// / CoverFlow activation signals. Replaces the mainwindow_scrollevents.cpp
  /// partial (Kartend-hzef).
  std::unique_ptr<ScrollEventsController> m_scrollEventsController;
  std::unique_ptr<DbEventsController> m_dbEventsController;
  std::unique_ptr<ScraperController> m_scraperController;
  /// Owns dialog construction so MainWindow doesn't need to #include every
  /// dialog header. See dialogcontroller.{h,cpp}.
  std::unique_ptr<DialogController> m_dialogController;
  bool m_startupSplashHandled = false;
  bool m_windowWasInactive = false;

  // Three-stage debounced pipeline coalescing rapid grid-width adjustments
  // (menu shortcuts) into a single settings save + layout/artwork refresh
  // chain. Owns the QTimers and generation counters internally.
  GridWidthDebouncer *m_gridWidthDebouncer = nullptr;

  // Owns the items-page toolbar's stateful Qt widgets (layout-picker,
  // search-mode QAction inside the search field, filter button + popup) and
  // the setup / sync / refresh logic that drives them. Parented to this
  // MainWindow.
  ToolbarController *m_toolbarController = nullptr;

  /// Per-collection filesystem watcher driving incremental rescans. Owned by
  /// the MainWindow because its rescan callback closes over NavigationManager
  /// (reached via m_appManager). Declared after m_appManager (above) so it is
  /// destroyed first, while m_appManager is still alive. Null until
  /// setupFilesystemWatcher() runs.
  std::unique_ptr<CollectionFilesystemWatcher> m_collectionWatcher;
  DetailsPane *m_MetadataSidebar = nullptr;

  // Kartend-hzef step 3: ScraperService ownership + the dialog cache moved
  // into m_scraperController. The pending-scrape state file is still
  // persisted to ~/.config/Kartend/pending-scrape.json by the service.

  void setupManagerConnections();

  void updateWindowTitleWithFilter(int visible, int total);
  /// Refreshes the itemPositionLabel in the top bar with the current
  /// selection position and total item count.
  void updateItemPositionLabel();

  void connectDatabaseManager();
  void connectScrollManager();
  void connectSidebarManager();
  // Kartend-8y5z: setupManagerConnections was 250 LOC of inlined wiring;
  // these helpers carry the per-area chunks. Each is called once from
  // setupManagerConnections (in dependency order — InteractionManager's
  // ctx must be populated before NavigationManager's setup), so the
  // public method is now a sequence of named calls instead of a god method.
  void wireInteractionManager();
  void wireNavigationManager();
  void wireDetailPageManager();
  void wireKartManager();
  /// pushes the "sidebar is hidden AND its mode would shrink the
  /// grid (Expand)" predicate to ScrollManager so the layout calculator picks
  /// the alternate per-collection grid sizes when applicable. Called whenever
  /// sidebar visibility changes, when navigating to a collection with a
  /// different sidebar mode, and after the settings dialog live-applies a
  /// sidebar-mode change.
  void updateScrollManagerSidebarShrinking();
  void connectSearchComponents();
  void connectScrollBars();
  /// Builds the SettingsDialogContext shared by every "open settings" entry
  /// point (menu, command palette, first-run timer) so adding a field touches
  /// one place. Does not set initialPage — callers needing a non-default page
  /// set it on the returned context.
  [[nodiscard]] SettingsDialogContext makeSettingsDialogContext();
  /// Wire and refresh the toolbar's filter button. Idempotent — installs
  /// the QMenu::triggered handler the first time and rebuilds the action
  /// list on every call. Delegates to the ToolbarController.
  void connectFilterToolbar();
  /// Rebuild the filter button popup from the current collection set.
  /// Called on startup, on every collection switch, and after settings
  /// edits that may have added or removed type tags. Delegates to the
  /// ToolbarController.
  void refreshFilterToolbar();
  /// Pull the per-item state flags for the current collection from
  /// IDatabaseManager and publish them into ItemWidget's static registry
  /// so the grid badges paint without a per-tile DB hop. Connected to
  /// DatabaseManager::itemsLoaded (Kartend-elte).
  void refreshItemStateFlagsRegistry();

  // Wiring slot handlers — each method below is the named target of one
  // signal/slot connection in mainwindow_wiring.cpp. Extracted from inline
  // lambdas so the manager graph can be read as a flat table of
  // {sender, signal, receiver, slot} edges. Order roughly mirrors the order
  // connections are made in connectDatabaseManager / connectScrollManager /
  // connectSidebarManager.
  //
  // Kartend-hzef step 2: DatabaseManager scan/count slots moved to
  // m_dbEventsController.
  // ScrollManager edges — most are owned by m_scrollEventsController; only
  // the filter-changed slot still has MainWindow-side responsibilities and
  // stays here for now.
  void onScrollFilterChanged(int visible, int total);
  // ArtworkManager TimerCoordinator edges
  void onArtworkViewportUpdateRequested();
  void onArtworkLayoutUpdateRequested();
  // InteractionManager edge
  void onInteractionSelectionChanged(int index);
  // DetailsPaneManager edges
  void onSidebarVisibilityChanged(bool visible);
  void onSidebarLayoutChanged();

  // UI Setup Methods
  void setupUI();
  void setupUIReferences();
  void createMenuBar();
  void adjustGridWidth(int delta);
  void setViewType(ViewType viewType);
  void setupSidebar();
  void setupArtworkManager();
  /// Build / refresh the CollectionFilesystemWatcher's watch set from the
  /// current m_collections. Safe to call repeatedly — used on startup and
  /// after the Settings dialog saves collection changes so the watch set
  /// tracks newly-enabled (or freshly-pointed-at) collections.
  void refreshCollectionFilesystemWatcher();
  void setupLastSelectedIndices();
  void setupEventFilters();
  void setupInitialTimers();
  void setupInitialTimersEmptyCollections();
  void setupInitialTimersWithCollections();
  // Kartend-3vkjc: release the first-run gate and run any startup tasks that
  // deferred themselves while the wizard modal was open, in order.
  void runDeferredStartupTasks();
  // Orphan-collection-data purge with its first-run-gate + shutdown guards;
  // shared by the deferred showEvent task and runDeferredStartupTasks.
  void maybePurgeOrphanCollectionData();

  // Kartend-hzef step 2: scan-counter state moved to DbEventsController.
  // mainwindow_timers.cpp's loadInitialCollection still needs to set the
  // suppression mode on first load — it goes through
  // m_dbEventsController->setSuppressStartupScanOverlays(true).

  void initializeAppContext();
  void showAbout();
  void showFocusReturnSplash();

  /// Run the first-run wizard modally and persist its outcome.
  ///
  /// Mounts a fresh CollectionConfig + saves when the user finishes with a
  /// folder picked, and always flips firstRunComplete so the auto-launch
  /// never fires twice. Safe to call from both the deferred startup timer
  /// and the Help-menu action.
  void showFirstRunWizard();

  /// Pops a file dialog for the user to pick a *.kartend-theme.json,
  /// previews the would-be changes against the active collection, then
  /// applies + saves on confirmation. No-op when no collection is active.
  void importThemeInteractive();
  /// Pops a file dialog and writes the active collection's appearance
  /// fields out as a theme preset.
  void exportThemeInteractive();
  /// Opens the layout-profile registry dialog. Loads the on-disk profile
  /// list, mutates it via the dialog buttons, and persists on close.
  void manageLayoutProfilesInteractive();
  /// Runs @p fn for the active collection: validates the current collection
  /// index, resolves its uuid, null-guards the database manager, and hands the
  /// config + uuid + db to @p fn (which loads the item rows and drives its own
  /// dialog). Shows @p openMessage when no collection is active, and a standard
  /// identity-resolution warning under @p title on uuid failure. Collapses the
  /// shared skeleton of the per-collection *Interactive launchers.
  void withActiveCollectionItems(
      const QString &title, const QString &openMessage,
      const std::function<void(const CollectionConfig &cfg, const QString &uuid,
                               IDatabaseManager *db)> &fn);
  /// Opens the collection-health dashboard for the active collection.
  /// Runs the CollectionHealth analyzer over the current item list +
  /// launcher config and pops a read-only summary dialog.
  void showCollectionHealthInteractive();
  /// Run the same-basename variant detector across the active collection's
  /// items table and pop the inspector dialog. No-op when no collection is
  /// open; falls back to an informational toast when no groups are detected.
  void showVariantGroupingInteractive();
  /// Switches to the collection that owns `filePath` and selects the
  /// item. No-op when the path can't be resolved to a live collection
  /// (e.g. its row survives from a deleted collection). Driven by the
  /// analytics dialog's double-click handler.
  void navigateToItem(const QString &filePath);
  /// Opens the bulk-edit dialog scoped to all items in the active
  /// collection. On confirm, fetches per-item metadata via the batch
  /// loader, applies the chosen action through BulkEdit::applyAction,
  /// persists every row that actually changed, and refreshes the view.
  void bulkEditInteractive();
  /// Opens the global command palette. Builds a fresh command list each
  /// open so live collections / view-mode / settings entries reflect
  /// the current state.
  void openCommandPalette();

  /// Opens the missing-metadata review queue for the active collection.
  /// Items lacking title / description / genre / artwork are presented
  /// one at a time with Edit / Skip / Close controls.
  void reviewMissingMetadataInteractive();

  /// Opens the artwork-assignment wizard for items in the active
  /// collection that have no artwork match on disk. Each item shows a
  /// ranked list of candidate images from the collection's artwork
  /// directory; the user picks one, browses for a file manually, or
  /// skips, and the choice is persisted as a per-item override.
  void artworkWizardInteractive();

  /// Opens the read-only binding visualizer (Help → Binding
  /// Visualizer…). Shows the current keyboard + gamepad mappings and
  /// highlights the matching row when the user presses an input.
  void showBindingVisualizer();

  /// Runs the New Library Wizard. On Finish, appends the wizard's
  /// CollectionConfig to m_collections, persists, rebuilds the
  /// hierarchy cache, and navigates to the new collection.
  void runNewLibraryWizard();

  /// Opens the presentation-profile registry dialog. Loads the on-disk
  /// profile list, mutates it via the dialog buttons (save / apply /
  /// delete), and persists on close.
  void managePresentationProfilesInteractive();

  /// Opens the read-only scraper provider registry dialog. Lists every
  /// built-in metadata provider with its categories, capabilities, and
  /// credential-configured status, plus a Test query line that
  /// renders each provider's search URL on row activation.
  void showScraperProvidersInteractive();

  /// Installs the application-context shortcut (Ctrl+Shift+P) that opens
  /// the command palette. Called once during setupUI alongside the
  /// other application shortcuts.
  void setupCommandPaletteShortcut();
};

#endif
