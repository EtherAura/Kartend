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
#include <QElapsedTimer>
#include <QFont>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QPair>
#include <QSet>
#include <QStringList>
#include <QTimer>

QT_BEGIN_NAMESPACE
class QStackedWidget;
class QWidget;
class QGridLayout;
class QHBoxLayout;
class QVBoxLayout;
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
class SystemThemeWatcher;
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
class CollectionTreeController;
class DbEventsController;
class DialogController;
class SettingsDialogController;
struct SettingsDialogContext;
struct DialogRunners;
class ScraperController;
class DatAuditController;
class LauncherImportController;
class LibraryToolsController;
class ScrollEventsController;
class TextZoomHud;
class OverlayZOrderRegistry;
namespace ErrorUtils {
struct ErrorContext;
}

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
  QStackedWidget *m_stackedWidget;
  QWidget *m_itemsPage;
  QWidget *m_gridContainer;
  QWidget *m_mainContentWidget;
  QGridLayout *m_itemGrid;
  QHBoxLayout *m_mainHorizontalLayout;
  /// Kartend-auh7u: full-height sidebar substrate built in setupUI — the
  /// outermost row wraps a column holding [itemsTopBar, m_mainContentWidget],
  /// so a Left/Right sidebar with FullHeight justification docks into the
  /// row (spanning the window height, toolbar stopping at its edge) while
  /// BelowToolbar docks keep using m_mainHorizontalLayout as before.
  QWidget *m_sidebarRowWidget = nullptr;
  QHBoxLayout *m_sidebarRowLayout = nullptr;
  QWidget *m_toolbarColumnWidget = nullptr;
  QVBoxLayout *m_toolbarColumnLayout = nullptr;
  QLineEdit *m_searchBar;
  EmptyStateWidget *m_loadingLabel;
  LoadingOverlay *m_loadingOverlay = nullptr;

  int m_currentCollectionIndex;
  QList<CollectionConfig> m_collections;
  CollectionHierarchyCache m_hierarchyCache;
  ApplicationContext m_appContext; // Shared context for manager setup

  void refreshTitleCounts();
  void updateWindowTitleForCollection(int collectionIndex) override;
  void rebuildHierarchyCache();

  // Kartend-ud6q2: hand any collection that just came into existence to the
  // scraper controller's silent entity-art fetch. Driven from
  // rebuildHierarchyCache — every collection-list mutation funnels through
  // there, so this covers the settings dialog, duplicate, the import wizard
  // and launcher import without instrumenting each creation site.
  void autoScrapeNewCollectionArt();

  // Collection identities seen the last time autoScrapeNewCollectionArt ran.
  // A uuid absent from this set is a collection that did not exist before.
  // Playlists are excluded: resyncPlaylistCollections erases and re-appends
  // every playlist row on each resync, so they would read as new forever.
  QSet<QString> m_seenCollectionUuids;
  // Whether m_seenCollectionUuids has been populated at least once. The first
  // pass is the startup baseline — every collection in the config is "new"
  // against an empty set, and scraping the whole library on launch is not
  // what "when a collection is created" means.
  bool m_seenCollectionUuidsSeeded = false;

  // Append a freshly-created collection, persist it, rebuild the hierarchy
  // cache, and (when navigate) switch to it. Single home for the
  // append→save→rebuild→navigate sequence the first-run/new-library wizards and
  // the DAT-library flow share — skipping any step leaves the new collection
  // invisible until restart (Kartend audit D-08).
  void appendCollectionAndPersist(const CollectionConfig &config, bool navigate);

  // IMainWindow — neutral role interface for the data-layer coordinators.
  [[nodiscard]] const QList<CollectionConfig> &collections() const override {
    return m_collections;
  }
  [[nodiscard]] GeneralSettings &generalSettings() override { return m_generalSettings; }
  [[nodiscard]] const GeneralSettings &generalSettings() const override {
    return m_generalSettings;
  }
  void applyGlobalUiFontFromSettings() override { applyGlobalUiFont(m_generalSettings); }
  void markConfigReplacedOnDisk() override { m_configReplacedOnDisk = true; }
  [[nodiscard]] bool isConfigReplacedOnDisk() const override { return m_configReplacedOnDisk; }
  /// Forward interactive per-click collections saves to the
  /// GridWidthDebouncer's save stage; falls back to an immediate
  /// saveCollections when the debouncer isn't constructed yet.
  /// Defined in mainwindow_wiring.cpp.
  void requestDebouncedCollectionsSave() override;
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
  /// Apply the hover-only scrollbar preference to every scrollable
  /// surface (user request 2026-08-18). Idempotent; call after any
  /// settings change.
  void applyHoverOnlyScrollbars();

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
  void openEntityScraperDialog(int collectionIndex) override;

  /// Open the standalone DAT Audit window (File → DAT Audit…, generic). Forwards
  /// to the DatAuditController, which owns the cached dialog.
  void openDatAuditDialog();

  /// Open the DAT Audit window aimed at @p collection (Kartend-4mqkof): selects
  /// the linked profile or seeds an unsaved one. Takes the collection by value
  /// so the settings panel's working copy (unsaved edits) drives it
  /// (Kartend-6wn0p).
  void openDatAuditForCollection(const CollectionConfig &collection) override;

  /// Persisted audit status (last-scan stamp + present/missing counts) for
  /// the profile linked to @p collectionUuid (Kartend-4mqkof,
  /// Kartend-m6qsb.8). Reads the dat_audit_profile store on a short-lived
  /// connection.
  [[nodiscard]] DatAuditStatus datAuditStatusForCollection(const QString &collectionUuid) override;

  /// Create a new collection for @p datPath via CreateCollectionDialog with the
  /// DAT pre-attached; append + save + rebuild like the other add-collection
  /// paths. Returns the new collection's uuid, or empty when the user cancels.
  /// Drives the DAT-library review's "Add to new collection…" (Kartend-m6qsb.18).
  [[nodiscard]] QString createCollectionForDat(const QString &datPath);

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

  /// Debounced persist of m_generalSettings. Continuous-input paths (volume
  /// slider drag, list-header column drag, text-zoom key repeat) mirror
  /// their edit into the live struct immediately and route the full-INI
  /// write through here so a burst costs one disk write, not one per tick.
  /// Same trailing-edge shape as SettingsDialog::scheduleLiveSettingsSave.
  void scheduleGeneralSettingsSave();
  /// Synchronously write a still-pending debounced general-settings save.
  /// Called from closeEvent(): ApplicationManager::shutdown persists
  /// collections but not GeneralSettings, so quitting inside the debounce
  /// window would otherwise drop the last edit.
  void flushPendingGeneralSettingsSave();

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
  // Kartend-h7xnr.1: prompts all destinations first (modal loop), then chains
  // the imports one at a time through KartManager's async worker path.
  void processPendingKartImports();
  /// Phase 1 of the drop drain: peek each queued .kart and run the modal
  /// destination prompt, filling m_kartImportJobs. Drops arriving inside a
  /// prompt's nested loop are consumed by the same pass.
  void promptPendingKartDestinations();
  /// Phase 2: start the next queued job via KartManager::importKartAsync (one
  /// worker at a time), or release m_kartImportInProgress when both queues are
  /// empty. Re-entered from onKartOperationFinished after each import.
  void dispatchNextKartImport();
  /// Continuation for the drop drain — connected to KartManager's terminal
  /// signals (collectionImported / importFailed / kartExported / exportFailed)
  /// in wireKartManager. No-op unless a drop drain is active.
  void onKartOperationFinished();

  bool m_isShuttingDown = false;
  // Set via markConfigReplacedOnDisk() when a settings-dialog profile import
  // replaced kartend.cfg wholesale (the import quits the app so the new file
  // takes effect on restart). closeEvent() honors it by skipping every
  // kartend.cfg write on the way out — the pending general-settings flush and
  // ApplicationManager::shutdown's collections save — because the in-memory
  // state still describes the OLD configuration and persisting it would wipe
  // the just-imported collection sections. Session/viewport/cache persistence
  // is unaffected: those live in separate files, not kartend.cfg.
  bool m_configReplacedOnDisk = false;
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
  // Same one-shot guard for the other two connect tables: several of their
  // edges (subcollectionEntered, virtualFolderEntered,
  // artworkPreviewLaunchRequested, selectionChanged, both DetailsPaneManager
  // sidebar signals) deliberately omit Qt::UniqueConnection, so a second run
  // would double-fire navigation entry, launch-from-preview, and the sidebar
  // refresh handlers.
  bool m_scrollManagerConnected = false;
  bool m_sidebarManagerConnected = false;
  // Kart drag-drop import queue + re-entrancy guard (Kartend-tubnr). dropEvent
  // collects .kart paths here and accepts immediately; processPendingKartImports
  // drains them on the next event-loop turn so the per-file destination prompt
  // and the import run outside the DnD handler. The guard stops a second
  // drop — or one delivered inside a prompt's nested loop — from starting a
  // concurrent drain, and stays up across the async import chain
  // (Kartend-h7xnr.1) until dispatchNextKartImport finds both queues empty.
  bool m_kartImportInProgress = false;
  QStringList m_pendingKartImports;
  /// (kartPath, destDir) jobs with their destination already prompted,
  /// awaiting sequential dispatch through KartManager::importKartAsync.
  QList<QPair<QString, QString>> m_kartImportJobs;
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
  /// Ui-layer settings-dialog orchestrator (Kartend-q8p29). Lazily created by
  /// settingsDialogController() and QObject-parented to this window, so Qt
  /// tears it down with the window (raw pointer, no unique_ptr).
  SettingsDialogController *m_settingsDialogController = nullptr;
  /// Collection tree panel (Kartend-ob1c9) — the hidable, left/right
  /// dockable tree navigator. QObject-parented to this window (raw pointer,
  /// no unique_ptr), same lifetime pattern as the other UI controllers.
  CollectionTreeController *m_collectionTreeController = nullptr;
  /// Central z-order coordinator for every registered overlay. Constructed
  /// before any overlay widget so each overlay's setLayerManager() call
  /// during setupUI() registers against a live instance. Owns no widgets —
  /// only references via QPointer.
  std::unique_ptr<OverlayZOrderRegistry> m_overlayZOrderRegistry;
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
  std::unique_ptr<DatAuditController> m_datAuditController;
  /// Owns the per-collection library tool flows (collection health, variant
  /// grouping, bulk edit, metadata review, artwork wizard) extracted from
  /// mainwindow_dialogs.cpp; the *Interactive methods below are one-line
  /// delegations into it. Context wired in connectDatabaseManager().
  std::unique_ptr<LibraryToolsController> m_libraryToolsController;
  /// Owns the launcher-import flows (Kartend-wuq2c): the "Import from
  /// Launcher…" dialog, the deferred startup re-sync of importSource
  /// collections, and the manual sync action. Context wired in
  /// connectDatabaseManager() beside the other controller contexts.
  std::unique_ptr<LauncherImportController> m_launcherImportController;
  /// Owns dialog construction so MainWindow doesn't need to #include every
  /// dialog header. See dialogcontroller.{h,cpp}.
  std::unique_ptr<DialogController> m_dialogController;
  bool m_startupSplashHandled = false;
  bool m_windowWasInactive = false;
  /// True while attract/gamepad are suspended for a detached (fire-and-forget)
  /// launch — LaunchManager::detachedSessionStarted set it, and either the
  /// balanced detachedSessionEnded or the focus backstop in event() clears it
  /// (Kartend-3232r.1). A detached child gives no process knowledge beyond
  /// spawn/exit, so the window regaining activation is the "user is back"
  /// signal; tracked sessions never touch this flag (they have a real
  /// finished()).
  bool m_detachedSuspendActive = false;
  /// Set when the window deactivates during a detached session — evidence the
  /// launched program actually took focus. The started handler's probe timer
  /// resumes if this never happens: a launcher with no window of its own
  /// (audio player, CLI tool) would otherwise leave the gamepad suspended
  /// under a fully focused frontend until its child exits.
  bool m_detachedSessionSawDeactivate = false;
  /// Started at detachedSessionStarted; the WindowActivate backstop ignores
  /// activations inside kDetachedResumeGraceMs — the launcher-chooser or an
  /// error dialog closing re-activates the window right after the spawn, and
  /// that must not instantly lift the suspension.
  QElapsedTimer m_detachedSuspendSince;
  static constexpr qint64 kDetachedResumeGraceMs = 1500;
  static constexpr int kDetachedFocusProbeMs = 5000;
  /// Coalesces the burst of QEvent::ApplicationPaletteChange / ThemeChange
  /// events KDE fires during one color-scheme rewrite into a single
  /// re-theme on the next event-loop turn. See
  /// reapplyDerivedThemingFromSystemPalette().
  bool m_paletteRetintPending = false;

  // Three-stage debounced pipeline coalescing rapid grid-width adjustments
  // (menu shortcuts) into a single settings save + layout/artwork refresh
  // chain. Owns the QTimers and generation counters internally.
  GridWidthDebouncer *m_gridWidthDebouncer = nullptr;

  /// Trailing-edge coalescer behind scheduleGeneralSettingsSave(). Lazily
  /// constructed on the first schedule; isActive() doubles as the
  /// "save still pending" flag flushPendingGeneralSettingsSave() checks.
  QTimer *m_generalSettingsSaveTimer = nullptr;
  /// Coalesces applyTextZoom()'s virtual-view teardown/rebuild across a
  /// key-repeat burst. The HUD, font push, and sidebar refresh stay
  /// immediate; only the expensive grid rebuild waits for the burst to end.
  QTimer *m_textZoomRebuildTimer = nullptr;

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

  /// Watches the desktop colour config (kdeglobals) for runtime accent /
  /// colour-scheme changes Qt doesn't deliver to us as a palette-change event.
  /// Its themeChanged() signal drives onSystemThemeChanged(). Declared after
  /// m_appManager so it is destroyed first (its slot reaches managers through
  /// m_appManager). Null on non-Linux / when the config dir can't be resolved.
  std::unique_ptr<SystemThemeWatcher> m_systemThemeWatcher;
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
  /// Lazily constructs the ui-layer SettingsDialogController (Kartend-q8p29 —
  /// the dialog-orchestration half that used to live on SettingsManager).
  /// QObject-parented to this window; long-lived because it tracks pending
  /// "Collection Added" scan summaries across dialog sessions. Defined in
  /// mainwindow_dialogs.cpp.
  [[nodiscard]] SettingsDialogController *settingsDialogController();
  /// Builds the generic stock-Qt-modal runner set (Kartend-sqoq0): confirm /
  /// warn / info / getText / file pickers, each parented on this window.
  /// Assigned onto the setup structs of every module that previously
  /// constructed QMessageBox / QInputDialog / QFileDialog directly, so the
  /// dialog construction lives in the UI layer and headless tests can stub
  /// the closures instead of fighting a modal. Defined in
  /// mainwindow_managerwiring.cpp.
  [[nodiscard]] DialogRunners makeDialogRunners();
  /// Wire and refresh the toolbar's filter button. Idempotent — installs
  /// the QMenu::triggered handler the first time and rebuilds the action
  /// list on every call. Delegates to the ToolbarController.
  void connectFilterToolbar();
  /// Rebuild the filter button popup from the current collection set.
  /// Called on startup, on every collection switch, and after settings
  /// edits that may have added or removed type tags. Delegates to the
  /// ToolbarController.
  void refreshFilterToolbar();
  /// Dispatch the per-item state-flags fetch for the current collection to
  /// IDatabaseManager's query worker (Kartend-elte / Kartend-h7xnr.6) so the
  /// grid badges paint without a per-tile DB hop and the collection-switch
  /// path never runs the flags SQL on the main thread. Connected to
  /// DatabaseManager::itemsLoaded; the result lands in
  /// onItemStateFlagsLoaded a beat later.
  void refreshItemStateFlagsRegistry();
  /// Delivery half of refreshItemStateFlagsRegistry(): publish the fetched
  /// flags into ItemWidget's static registry. Drops the reply when the
  /// echoed uuid no longer matches the collection being viewed (the user
  /// switched again while the worker query was in flight). Connected to
  /// DatabaseManager::itemStateFlagsLoaded.
  void onItemStateFlagsLoaded(const QString &collectionUuid, const QStringList &pinnedPaths,
                              const QStringList &hiddenPaths,
                              const QStringList &continueLaterPaths);

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
  // NavigationManager edge — MainWindow owns the ErrorDialog so the input
  // layer stays free of UI-chrome includes.
  void onMediaLibraryErrorRaised(const ErrorUtils::ErrorContext &error);
  // SettingsManager edges (launcherProfileChanged / collectionsModified) —
  // refresh the items-toolbar warning badge via the ToolbarController.
  void refreshCollectionWarningBadge();

  // UI Setup Methods
  void setupUI();
  void setupUIReferences();
  void createMenuBar();
  void adjustGridWidth(int delta);
  void setViewType(ViewType viewType);
  void setupSidebar();
  void setupCollectionTree();
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

  /// Lifts the detached-session attract/gamepad suspension (no-op unless
  /// m_detachedSuspendActive) and releases LaunchManager's detached launch
  /// block. Shared by the detachedSessionEnded handler, the WindowActivate
  /// focus backstop in event(), and the never-took-focus probe — defined in
  /// mainwindow_managerwiring.cpp beside the wiring that arms it
  /// (Kartend-3232r.1). Attract suspension goes through setSuspended only.
  void resumeAfterDetachedSession();

  /// Re-apply every *derived* color (one computed from the system accent /
  /// palette and then cached, baked into a stylesheet/HTML string, or pinned
  /// via an explicit setPalette resolve-mask) after the system color scheme
  /// changes at runtime — e.g. a KDE wallpaper-derived accent shifting when
  /// the user switches Plasma activities. Colors read live inside paintEvent
  /// already track the new palette; this covers everything that froze a copy.
  /// Invoked debounced from event() on QEvent::ApplicationPaletteChange /
  /// ThemeChange.
  void reapplyDerivedThemingFromSystemPalette();

  /// SystemThemeWatcher::themeChanged() handler: KDE updated the system accent
  /// at runtime but Qt didn't dispatch a palette-change event to our widgets,
  /// so synthesize the broadcast Qt skipped (re-assign the application palette
  /// so every widget re-resolves + repaints) and re-apply the derived theming.
  /// const: acts only on the global QApplication; the per-collection re-theme
  /// it triggers runs via the ApplicationPaletteChange event() path.
  void onSystemThemeChanged() const;

  /// Run the first-run wizard modally and persist its outcome.
  ///
  /// Mounts a fresh CollectionConfig + saves when the user finishes with a
  /// folder picked, and always flips firstRunComplete so the auto-launch
  /// never fires twice. Safe to call from both the deferred startup timer
  /// and the Help-menu action.
  void showFirstRunWizard();

  /// Legacy empty-library backstop: modally prompts for a first collection
  /// name (re-prompting on empty/invalid input, closing the window on
  /// Cancel), persists it, then opens the settings dialog so the user can
  /// finish configuring it. Queued by setupInitialTimersEmptyCollections
  /// once the window is shown; defined in mainwindow_dialogs.cpp.
  void promptCreateFirstCollectionInteractive();

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
  /// Opens the collection-health dashboard for the active collection.
  /// Delegates to LibraryToolsController.
  void showCollectionHealthInteractive();
  /// Runs the same-basename variant detector across the active collection's
  /// items table and pops the inspector dialog. Delegates to
  /// LibraryToolsController.
  void showVariantGroupingInteractive();
  /// Switches to the collection that owns `filePath` and selects the
  /// item. No-op when the path can't be resolved to a live collection
  /// (e.g. its row survives from a deleted collection). Driven by the
  /// analytics dialog's double-click handler.
  void navigateToItem(const QString &filePath);
  /// Opens the bulk-edit dialog scoped to all items in the active
  /// collection (BulkEditService thread-pool pipeline behind a cancellable
  /// progress dialog). Delegates to LibraryToolsController.
  void bulkEditInteractive();
  /// Opens the global command palette. The registry comes from
  /// MenuController::buildPaletteCommands, rebuilt each open so live
  /// collections / view-mode / settings entries reflect the current state.
  void openCommandPalette();

  /// Opens the missing-metadata review queue for the active collection.
  /// Delegates to LibraryToolsController.
  void reviewMissingMetadataInteractive();

  /// Opens the artwork-assignment wizard for items in the active collection
  /// that have no artwork match on disk. Delegates to LibraryToolsController.
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
