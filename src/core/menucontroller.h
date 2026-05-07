#ifndef MENUCONTROLLER_H
#define MENUCONTROLLER_H

#include <functional>
#include <QAction>
#include <QActionGroup>
#include <QObject>

#include "collectionutils.h"
#include "usagestatsstore.h"

QT_BEGIN_NAMESPACE
class QMainWindow;
class QMenuBar;
class QMenu;
QT_END_NAMESPACE

class Ui_MainWindow;
class NavigationManager;
class SettingsManager;
class SidebarManager;
class ScrollManager;
class ArtworkManager;
class DatabaseManager;
class InteractionManager;

/// Context struct for menu action callbacks.
/// Contains pointers to managers and state needed by menu actions.
struct MenuControllerContext {
  QMainWindow *mainWindow = nullptr;
  Ui_MainWindow *ui = nullptr;

  // Manager getters (safer than raw pointers - allows null checks)
  std::function<NavigationManager *()> getNavigationManager;
  std::function<SettingsManager *()> getSettingsManager;
  std::function<SidebarManager *()> getSidebarManager;
  std::function<ScrollManager *()> getScrollManager;
  std::function<ArtworkManager *()> getArtworkManager;
  std::function<DatabaseManager *()> getDatabaseManager;
  std::function<InteractionManager *()> getInteractionManager;

  // State accessors
  std::function<int()> getCurrentCollectionIndex;
  std::function<QList<CollectionConfig> *()> getCollections;
  std::function<GeneralSettings *()> getGeneralSettings;
  std::function<const CollectionHierarchyCache *()> getHierarchyCache;
  std::function<ViewType()> getCurrentViewType;

  // Callbacks for actions that need MainWindow involvement
  std::function<void()> onOpenSettings;
  std::function<void()> onShowAbout;
  std::function<void(int delta)> onAdjustGridWidth;
  std::function<void(ViewType)> onSetViewType;
  std::function<void(const QString &filePath, int collectionIndex)> onLaunchItem;
  std::function<void()> onImportKart;
  std::function<void()> onExportKart;
};

/// Handles menu bar setup and action connections.
/// Extracted from MainWindow to reduce its responsibility count.
class MenuController : public QObject {
  Q_OBJECT

public:
  explicit MenuController(QObject *parent = nullptr);
  ~MenuController() override;

  /// Set up the menu controller with required context.
  /// Must be called before setupMenuBar().
  void setContext(const MenuControllerContext &context);

  /// Create and connect all menu bar actions.
  void setupMenuBar();

  /// Get the fullscreen action (for external state sync).
  [[nodiscard]] QAction *fullscreenAction() const { return m_fullscreenAction; }

  /// Sync sort action checked states with current settings.
  void syncSortActions();

  /// Sync layout (view-mode) action checked states with the active view type.
  /// Called by MainWindow whenever the view type changes (toolbar click,
  /// settings dialog change, collection switch).
  void syncLayoutActions(ViewType viewType);

  /// Sync the View → Details Pane Orientation submenu's checked entry with
  /// the active collection's persisted sidebarPosition. Called by MainWindow
  /// on collection switch / settings save so the menu reflects the current
  /// pane edge.
  void syncOrientationActions(SidebarPosition position);

  /// Restore persisted Show Menu Bar / Show Toolbar / Fullscreen states from
  /// GeneralSettings. Called once from setupMenuBar() after every action has
  /// been wired so checked states and widget visibility stay in lockstep on
  /// startup (Kartend-lfu0).
  void applyPersistedViewState();

private:
  MenuControllerContext m_ctx;

  // Owned actions (not in UI file)
  QAction *m_fullscreenAction = nullptr;
  QAction *m_shortcutsAction = nullptr;
  QAction *m_statisticsAction = nullptr;
  QAction *m_gridWidthIncreaseAction = nullptr;
  QAction *m_gridWidthDecreaseAction = nullptr;
  QAction *m_importKartAction = nullptr;
  QAction *m_exportKartAction = nullptr;
  QActionGroup *m_sortActionGroup = nullptr;
  QActionGroup *m_layoutActionGroup = nullptr;
  QActionGroup *m_orientationActionGroup = nullptr;
  QMenu *m_orientationMenu = nullptr;
  QAction *m_orientationActionRight = nullptr;
  QAction *m_orientationActionLeft = nullptr;
  QAction *m_orientationActionTop = nullptr;
  QAction *m_orientationActionBottom = nullptr;

  // Setup methods for each action group
  void setupActionExit();
  void setupActionShowMenuBar();
  void setupActionShowToolbar();
  void setupActionShowSidebar();
  void setupActionSettings();
  void setupActionAbout();
  void setupActionAboutQt();
  void setupActionRefresh();
  void setupSortActions();
  void setupFullscreenAction();
  void setupShortcutsAction();
  void setupStatisticsAction();
  void setupGridWidthActions();
  void setupHamburgerMenu();
  void setupActionOpenRandomItem();
  void setupActionImportKart();
  void setupActionExportKart();
  void setupRecentMenu();
  void setupMostLaunchedMenu();
  void setupLayoutActions();
  /// Build the View → Details Pane Orientation submenu. Each entry mutates
  /// the current collection's `sidebarPosition`, persists via SettingsManager,
  /// and re-applies the layout via SidebarManager so the change takes effect
  /// without requiring a pane toggle. Mirrors the toolbar/settings flow.
  void setupActionDetailsPaneOrientation();
  void insertFullscreenInViewMenu(QAction *fullscreenAction);

  // Repopulate the Recent submenu from items.last_played (Kartend-j5l3);
  // called on QMenu::aboutToShow so the list is fresh each time.
  void rebuildRecentMenu();
  // Repopulate the Most Launched submenu from items.play_count (Kartend-j5l3).
  void rebuildMostLaunchedMenu();
  // Shared row → menu-action wiring used by both Recent and Most Launched.
  // Forward-declares avoid a hard include of usagestatsstore.h here; the
  // real include is in menucontroller.cpp.
  void populateLaunchEntriesIntoMenu(QMenu *menu,
                                     const QList<UsageStatsStore::ItemUsageRow> &rows,
                                     DatabaseManager *db);

  // Mirror menu-bar visibility onto the toolbar hamburger button so the user
  // always has menu access whether the menu bar is hidden by F11 or F10.
  void syncHamburgerVisibility();
};

#endif // MENUCONTROLLER_H
