#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "applicationcontext.h"
#include "collectionutils.h"
#include <memory>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QTimer>

namespace TimerUtils {
class DebouncedTimer;
}

QT_BEGIN_NAMESPACE
class QStackedWidget;
class QWidget;
class QGridLayout;
class QHBoxLayout;
class QScrollArea;
class QLineEdit;
class QPushButton;
class QToolButton;
class QLabel;
class QAction;
class QActionGroup;
QT_END_NAMESPACE

class Ui_MainWindow;
class ApplicationManager;
class ArtworkManager;
class CacheManager;
class InteractionManager;
class DatabaseManager;
class NavigationManager;
class PlaylistManager;
class SessionManager;
class SettingsManager;
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
class TextZoomHud;

namespace kart {
class KartManager;
}

class MainWindow : public QMainWindow {
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
  /// Search-mode toggle exposed as an action embedded inside the QLineEdit
  /// (QLineEdit::addAction at LeadingPosition). Replaces the standalone
  /// searchModeButton.
  QAction *m_searchModeAction = nullptr;
  /// Single layout-picker QToolButton with InstantPopup menu listing the
  /// view types as text entries. Replaces the four legacy view buttons
  /// (grid/list/cover-flow/horizontal).
  QToolButton *m_viewModeButton = nullptr;
  QAction *m_viewActionGrid = nullptr;
  QAction *m_viewActionList = nullptr;
  QAction *m_viewActionCoverFlow = nullptr;
  QAction *m_viewActionHorizontal = nullptr;
  /// Single toolbar filter entry-point:
  /// hosts an InstantPopup menu containing the type-filter radio group and
  /// the per-collection title-pattern toggle + editor entry. Replaces the
  /// former separate typeFilterButton, titleFilterButton, and the
  /// hideSubcollectionsButton.
  QToolButton *m_filterButton = nullptr;
  /// Checkable menu entry mirroring CollectionConfig::titleExclusionEnabled
  /// for the active collection. Lives inside m_filterButton's popup.
  QAction *m_titleFilterEnabledAction = nullptr;
  EmptyStateWidget *loadingLabel;
  LoadingOverlay *m_loadingOverlay = nullptr;

  int currentCollectionIndex;
  QList<CollectionConfig> m_collections;
  CollectionHierarchyCache m_hierarchyCache;
  ApplicationContext m_appContext; // Shared context for manager setup

  void refreshTitleCounts();
  void updateWindowTitleForCollection(int collectionIndex);
  void rebuildHierarchyCache();
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
  [[nodiscard]] SettingsManager *getSettingsManager() const;
  [[nodiscard]] DatabaseManager *getDatabaseManager() const;
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

  /// applies the per-button visibility flags and custom-text
  /// overrides from m_generalSettings to the items-page toolbar. Safe to call
  /// before any toolbar widget is constructed (each ui pointer is null-checked)
  /// and idempotent — invoked on startup and again after the user saves the
  /// Settings dialog.
  void applyToolbarCustomization();

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

  /// Build the layout-picker popup menu attached to m_viewModeButton. The menu
  /// holds checkable text entries (Grid / List / Cover Flow / Horizontal) wired
  /// to setViewType. Replaces the four standalone view buttons.
  void setupViewModeButton();
  /// Reflects @p viewType onto m_viewModeButton: ticks the matching menu
  /// action and refreshes the button's tooltip + theme icon. Called from
  /// setViewType() and updateWindowTitleForCollection().
  void syncViewModeButton(ViewType viewType);
  /// Creates the search-mode QAction with the kde-breeze "search" icon and
  /// adds it to the searchBar QLineEdit at LeadingPosition. Wires the
  /// triggered() signal to InteractionManager::toggleSearchMode (deferred
  /// until the manager exists). Replaces the standalone searchModeButton.
  void setupSearchModeAction();

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
  bool m_startupSplashHandled = false;
  bool m_windowWasInactive = false;

  // Coalesce rapid grid-width adjustments (menu shortcuts) into a single
  // settings save + layout/artwork refresh chain.
  TimerUtils::DebouncedTimer *m_gridWidthSaveDebouncer = nullptr;
  TimerUtils::DebouncedTimer *m_gridWidthPrecalcDebouncer = nullptr;
  TimerUtils::DebouncedTimer *m_gridWidthFinalizeDebouncer = nullptr;
  int m_gridWidthPendingGeneration = 0;
  int m_gridWidthActiveGeneration = 0;

  std::unique_ptr<ApplicationManager> m_appManager;
  DetailsPane *m_MetadataSidebar = nullptr;

  void setupManagerConnections();
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
  /// Wires the consolidated m_filterButton: builds its popup once, hooks
  /// settings persistence, and triggers a reload of the current view when
  /// either the type filter or the title-exclusion toggle
  /// changes.
  void connectFilterToolbar();
  /// Rebuilds m_filterButton's popup from scratch — the type list comes from
  /// the live collection types (so deleted/retagged types vanish) and the
  /// title-pattern toggle reflects the active collection's state. Called on
  /// startup, on every collection switch, and after settings edits that may
  /// have added/removed type tags.
  void refreshFilterToolbar();
  /// opens the popup editor for the current collection's
  /// title-exclusion patterns. Returns immediately when no collection is
  /// active or when the user cancels.
  void showTitleFilterEditor();

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
};

#endif
