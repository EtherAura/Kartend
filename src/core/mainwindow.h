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
class QComboBox;
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
class SidebarManager;
class ScrollManager;
class ItemWidget;
class MetadataSidebar;
class LoadingOverlay;
class EmptyStateWidget;
class SplashOverlay;
class NowPlayingOverlay;
class MenuController;

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
  QPushButton *m_searchModeButton;
  QPushButton *m_gridViewButton;
  QPushButton *m_listViewButton;
  QPushButton *m_coverFlowViewButton = nullptr;
  // Kartend-dd8: collection categorization toolbar widgets
  QPushButton *m_hideSubcollectionsButton = nullptr;
  QComboBox *m_typeFilterComboBox = nullptr;
  // Kartend-5h6: title-exclusion regex toolbar (per-collection patterns)
  QToolButton *m_titleFilterButton = nullptr;
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
  [[nodiscard]] MetadataSidebar *getMetadataSidebar() const { return m_MetadataSidebar; }

  // Delegated Getters
  [[nodiscard]] SidebarManager *getSidebarManager() const;
  [[nodiscard]] SettingsManager *getSettingsManager() const;
  [[nodiscard]] DatabaseManager *getDatabaseManager() const;
  [[nodiscard]] ScrollManager *getScrollManager() const;
  [[nodiscard]] NavigationManager *getNavigationManager() const;
  [[nodiscard]] InteractionManager *getInteractionManager() const;
  [[nodiscard]] SessionManager *getSessionManager() const;
  [[nodiscard]] ArtworkManager *getArtworkManager() const;
  [[nodiscard]] CacheManager *getCacheManager() const;
  [[nodiscard]] PlaylistManager *getPlaylistManager() const;

  /// Re-runs the playlist synthesis pass (Kartend-vlm7): drops any prior
  /// playlist-backed CollectionConfigs from m_collections, queries the live
  /// playlists table, appends a synthesized config per row, and rebuilds the
  /// hierarchy cache. Wired to PlaylistManager::playlistsChanged so adds/
  /// renames/deletes show up in the sidebar without a restart.
  void resyncPlaylistCollections();

  void showStartupSplash();

  /// Kartend-81o: applies the per-button visibility flags and custom-text
  /// overrides from m_generalSettings to the items-page toolbar. Safe to call
  /// before any toolbar widget is constructed (each ui pointer is null-checked)
  /// and idempotent — invoked on startup and again after the user saves the
  /// Settings dialog.
  void applyToolbarCustomization();

  /// Kartend-9v0o: pushes the global UI font (family + point size) from
  /// @p settings to QApplication. Empty family / 0 size means "leave the
  /// platform default in place." Idempotent — invoked on startup after
  /// settings load and again whenever the user changes the font in the
  /// Settings dialog.
  static void applyGlobalUiFont(const GeneralSettings &settings);

protected:
  bool event(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  auto eventFilter(QObject *watched, QEvent *event) -> bool override;
  void closeEvent(QCloseEvent *event) override;

private:
  bool m_isShuttingDown = false;
  std::unique_ptr<MenuController> m_menuController;
  SplashOverlay *m_splashOverlay = nullptr;
  NowPlayingOverlay *m_nowPlayingOverlay = nullptr;
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
  MetadataSidebar *m_MetadataSidebar = nullptr;

  void setupManagerConnections();
  void updateWindowTitleWithFilter(int visible, int total);
  /// Refreshes the itemPositionLabel in the top bar with the current
  /// selection position and total item count (Kartend-tof).
  void updateItemPositionLabel();

  void connectDatabaseManager();
  void connectScrollManager();
  void connectSidebarManager();
  void connectSearchComponents();
  void connectScrollBars() const;
  /// Kartend-dd8: wires the toolbar type-filter combobox + hide-subcollections
  /// toggle to GeneralSettings persistence and triggers a reload of the
  /// current view when either changes.
  void connectCollectionTypeToolbar();
  /// Kartend-dd8: rebuilds the toolbar type-filter dropdown from the union of
  /// types currently present in m_collections, preserving the active
  /// selection. Called on startup and after settings changes that may have
  /// added/removed type tags.
  void refreshTypeFilterToolbar();

  /// Kartend-5h6: wires the title-exclusion toolbar button. Body click
  /// toggles the per-collection enabled flag; arrow click opens a popup
  /// containing a QPlainTextEdit (one regex per line) with Apply/Cancel.
  void connectTitleFilterToolbar();
  /// Kartend-5h6: refreshes the title-exclusion button's checked state from
  /// the current collection's CollectionConfig::titleExclusionEnabled. Called
  /// after collection switches and after the popup applies edits.
  void refreshTitleFilterToolbar();
  /// Kartend-5h6: opens the popup editor for the current collection's
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
