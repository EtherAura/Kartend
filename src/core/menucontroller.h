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
class ISettingsManager;
class DetailsPaneManager;
class ScrollManager;
class ArtworkManager;
class IDatabaseManager;
class InteractionManager;

/// Context struct for menu action callbacks.
/// Contains pointers to managers and state needed by menu actions.
struct MenuControllerContext {
  QMainWindow *mainWindow = nullptr;
  Ui_MainWindow *ui = nullptr;

  // Manager getters (safer than raw pointers - allows null checks)
  std::function<NavigationManager *()> getNavigationManager;
  std::function<ISettingsManager *()> getSettingsManager;
  std::function<DetailsPaneManager *()> getDetailsPaneManager;
  std::function<ScrollManager *()> getScrollManager;
  std::function<ArtworkManager *()> getArtworkManager;
  std::function<IDatabaseManager *()> getDatabaseManager;
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
  std::function<void()> onShowFirstRunWizard;
  std::function<void()> onShowScraperCredentials;
  std::function<void()> onRunBatchScrape;
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
  void syncOrientationActions(DetailsPanePosition position);

  /// Restore persisted Show Menu Bar / Show Toolbar / Fullscreen states from
  /// GeneralSettings. Called once from setupMenuBar() after every action has
  /// been wired so checked states and widget visibility stay in lockstep on
  /// startup.
  void applyPersistedViewState();

private:
  MenuControllerContext m_ctx;

  // Owned actions (not in UI file)
  QAction *m_fullscreenAction = nullptr;
  QAction *m_shortcutsAction = nullptr;
  QAction *m_statisticsAction = nullptr;
  QAction *m_firstRunWizardAction = nullptr;
  QAction *m_scraperCredentialsAction = nullptr;
  QAction *m_batchScrapeAction = nullptr;
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

  // ─── Setup helpers ────────────────────────────────────────────────────
  //
  // These collapse the repeated "null-check ctx.ui+mainWindow, check action
  // exists, connect, setShortcutContext, addAction" cargo from each
  // setupActionX() into a couple of one-liners. Returns true when the
  // action existed and was wired (so callers can short-circuit decoration
  // logic) — specifically allowing the body of each setupAction* method
  // to read as a single helper call plus its lambda.

  /// Connects @p action to @p handler, marks it as ApplicationShortcut,
  /// and registers it on the main window so the shortcut is reachable
  /// from any focused widget. No-op (returns false) when @p action or the
  /// main window is null.
  bool connectGlobalAction(QAction *action, std::function<void()> handler);

  /// Connects @p action to @p handler. Used for menu-only items that don't
  /// participate in the global shortcut chain (Help → About / About Qt).
  /// No-op (returns false) when @p action is null.
  bool connectMenuAction(QAction *action, std::function<void()> handler);

  /// Toggle action that mirrors a bool field on GeneralSettings and
  /// persists via SettingsManager. @p applyVisual receives the new
  /// checked state so the caller can flip the corresponding widget's
  /// visibility. Used by setupActionShowMenuBar / ShowToolbar /
  /// FullscreenAction.
  bool connectVisibilityToggle(QAction *action, std::function<void(bool)> applyVisual,
                               bool GeneralSettings::*field);

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
  void setupFirstRunWizardAction();
  void setupScraperCredentialsAction();
  void setupBatchScrapeAction();
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
  /// and re-applies the layout via DetailsPaneManager so the change takes effect
  /// without requiring a pane toggle. Mirrors the toolbar/settings flow.
  void setupActionDetailsPaneOrientation();
  void insertFullscreenInViewMenu(QAction *fullscreenAction);

  // Repopulate the Recent submenu from items.last_played;
  // called on QMenu::aboutToShow so the list is fresh each time.
  void rebuildRecentMenu();
  // Repopulate the Most Launched submenu from items.play_count.
  void rebuildMostLaunchedMenu();
  // Shared row → menu-action wiring used by both Recent and Most Launched.
  // Forward-declares avoid a hard include of usagestatsstore.h here; the
  // real include is in menucontroller.cpp.
  void populateLaunchEntriesIntoMenu(QMenu *menu, const QList<UsageStatsStore::ItemUsageRow> &rows,
                                     IDatabaseManager *db);

  // Mirror menu-bar visibility onto the toolbar hamburger button so the user
  // always has menu access whether the menu bar is hidden by F11 or F10.
  void syncHamburgerVisibility();
};

#endif // MENUCONTROLLER_H
