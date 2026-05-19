#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "applicationcontext.h"
#include "collectionutils.h"
#include "imainwindow.h"
#include <functional>
#include <memory>
#include <QHash>
#include <QList>
#include <QMainWindow>
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
class TextZoomHud;
class ISettingsDialog;

namespace kart {
class KartManager;
}

namespace TimerUtils {
class DebouncedTimer;
}

class MainWindow : public QMainWindow, public IMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

  GeneralSettings m_generalSettings;

  Ui_MainWindow *ui;
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
  [[nodiscard]] const CollectionHierarchyCache &getHierarchyCache() const {
    return m_hierarchyCache;
  }
  [[nodiscard]] const ApplicationContext &getAppContext() const { return m_appContext; }
  [[nodiscard]] bool isShuttingDown() const { return m_isShuttingDown; }

  // Getters for Managers
  [[nodiscard]] ApplicationManager *getApplicationManager() const { return m_appManager.get(); }
  [[nodiscard]] DetailsPane *getMetadataSidebar() const { return m_MetadataSidebar; }

  // Delegated Getters
  [[nodiscard]] DetailsPaneManager *getDetailsPaneManager() const;
  [[nodiscard]] ISettingsManager *getSettingsManager() const;
  [[nodiscard]] IDatabaseManager *getDatabaseManager() const;
  [[nodiscard]] ScrollManager *getScrollManager() const;
  [[nodiscard]] NavigationManager *getNavigationManager() const;
  [[nodiscard]] InteractionManager *getInteractionManager() const;
  [[nodiscard]] SessionManager *getSessionManager() const;
  [[nodiscard]] ArtworkManager *getArtworkManager() const;
  [[nodiscard]] kart::KartManager *getKartManager() const;
  [[nodiscard]] CacheManager *getCacheManager() const;
  [[nodiscard]] PlaylistManager *getPlaylistManager() const;
  [[nodiscard]] DetailPageManager *getDetailPageManager() const;

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
  void applyToolbarCustomization();

  /// Open the unified Scraper dialog. Caller is responsible for any
  /// pre-selection (right-click flow passes a collection + item; File
  /// → Scraper passes nothing). The dialog handles the rest:
  /// collection-tree picker, per-item checkboxes, media-type filter,
  /// Auto/Interactive toggle, and per-collection BatchScrapeRunner
  /// orchestration.
  void openScraperDialog(int preCollectionIndex = -1,
                         const QString &preItemPath = QString()) override;

  /// Accessor for the long-lived ScraperService that survives
  /// dialog close + app restart. Owns the active scrape queue;
  /// persists state to disk after each item completes.
  [[nodiscard]] Scraper::ScraperService *getScraperService() const {
    return m_scraperService.get();
  }

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
  void applyMarqueeSettings();
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
  static void applyGlobalUiFont(const GeneralSettings &settings);

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
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dropEvent(QDropEvent *event) override;

private:
  bool m_isShuttingDown = false;
  std::unique_ptr<MenuController> m_menuController;
  SplashOverlay *m_splashOverlay = nullptr;
  NowPlayingOverlay *m_nowPlayingOverlay = nullptr;
  DetailPageOverlay *m_detailPageOverlay = nullptr;
  TextZoomHud *m_textZoomHud = nullptr;
  /// Drives the secondary-monitor "marquee" / topper window — owns the
  /// MarqueeWindow (lazily created on first enable) and the trailing-edge
  /// artwork-refresh debounce timer. applyMarqueeSettings() and
  /// updateMarqueeArtwork() delegate straight to it.
  std::unique_ptr<MarqueeController> m_marqueeController;
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

  std::unique_ptr<ApplicationManager> m_appManager;
  DetailsPane *m_MetadataSidebar = nullptr;
  /// Long-lived scraper coordinator. Owns the active queue + runner
  /// across dialog lifetime; persists in-flight state to
  /// `~/.config/Kartend/pending-scrape.json` after each item so a
  /// crash or program exit can resume on next launch.
  std::unique_ptr<Scraper::ScraperService> m_scraperService;
  /// Single reused Scraper dialog instance — created on first
  /// openScraperDialog call and kept alive afterwards so closing it
  /// (Close button or X) merely hides; reopening reuses the same
  /// widget tree. Parented to this MainWindow.
  ScrapeResultDialog *m_scraperDialog = nullptr;

  void setupManagerConnections();

  /// Build the factory the data-layer SettingsDialogController uses to
  /// construct + signal-wire the concrete SettingsDialog. Lives on MainWindow
  /// because only the ui/ side may name SettingsDialog and its Qt signals;
  /// the controller drives the result through the neutral ISettingsDialog
  /// interface. Both openSettingsDialog call sites assign the result into
  /// SettingsDialogContext::createSettingsDialog.
  [[nodiscard]] std::function<std::unique_ptr<ISettingsDialog>(
      QWidget *, const QList<CollectionConfig> &, int,
      std::function<void(const QList<CollectionConfig> &)>, std::function<void(int)>)>
  makeSettingsDialogFactory();

  void updateWindowTitleWithFilter(int visible, int total);
  /// Refreshes the itemPositionLabel in the top bar with the current
  /// selection position and total item count.
  void updateItemPositionLabel();

  void connectDatabaseManager();
  void connectScrollManager();
  void connectSidebarManager();
  /// pushes the "sidebar is hidden AND its mode would shrink the
  /// grid (Expand)" predicate to ScrollManager so the layout calculator picks
  /// the alternate per-collection grid sizes when applicable. Called whenever
  /// sidebar visibility changes, when navigating to a collection with a
  /// different sidebar mode, and after the settings dialog live-applies a
  /// sidebar-mode change.
  void updateScrollManagerSidebarShrinking();
  void connectSearchComponents();
  void connectScrollBars() const;
  /// Wire and refresh the toolbar's filter button. Idempotent — installs
  /// the QMenu::triggered handler the first time and rebuilds the action
  /// list on every call. Delegates to the ToolbarController.
  void connectFilterToolbar();
  /// Rebuild the filter button popup from the current collection set.
  /// Called on startup, on every collection switch, and after settings
  /// edits that may have added or removed type tags. Delegates to the
  /// ToolbarController.
  void refreshFilterToolbar();

  // Wiring slot handlers — each method below is the named target of one
  // signal/slot connection in mainwindowwiring.cpp. Extracted from inline
  // lambdas so the manager graph can be read as a flat table of
  // {sender, signal, receiver, slot} edges. Order roughly mirrors the order
  // connections are made in connectDatabaseManager / connectScrollManager /
  // connectSidebarManager.
  //
  // DatabaseManager edges
  void releaseStartupOverlaySuppressionIfIdle(int /*count*/);
  void refreshTitleCountsIfActive();
  void refreshFilterToolbarOnItemsLoaded(const QStringList & /*paths*/,
                                         const QHash<QString, QString> & /*names*/);
  void onScanProgress(int current, int total, const QString &name);
  void onScanStarting(const QString &name, int estimatedItems);
  void onCollectionScanCompletedStartup(const QString &uuid);
  void onCollectionScanCompletedOverlay(const QString &uuid);
  void onScanItemsProgress(int itemsProcessed, int totalItems);
  void refreshCollectionSummaryOnScanCompleted(const QString &uuid);
  // ScrollManager edges
  void onSortModeChangeRequested(SortMode sortMode);
  void onSelectItemByIndex(int index);
  void onCoverFlowActiveChanged(bool active);
  void onArtworkPreviewVisibilityChanged(bool visible);
  void onCoverFlowItemActivated(int index);
  void onListColumnWidthChanged(int width);
  void onListArtworkColumnWidthChanged(int width);
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
  void setupLastSelectedIndices();
  void setupEventFilters();
  void setupInitialTimers();
  void setupInitialTimersEmptyCollections();
  void setupInitialTimersWithCollections();

  // On startup, the initial collection load can trigger a database rescan.
  // Suppress the loading overlay for that first scan so the UI remains
  // immediately navigable while work continues in the background.
  bool m_suppressStartupScanOverlays = false;

  // Number of in-flight startup scan operations (tracked via scanStarting/
  // collectionScanCompleted while m_suppressStartupScanOverlays is true).
  int m_startupActiveScanCount = 0;

  // Counter for active scan operations (e.g., when showAllSubcollectionItems
  // triggers scans of all descendants). Overlay stays visible until all
  // complete.
  int m_activeScanCount = 0;
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
};

#endif
