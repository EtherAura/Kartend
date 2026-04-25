#ifndef MENUCONTROLLER_H
#define MENUCONTROLLER_H

#include <functional>
#include <QAction>
#include <QActionGroup>
#include <QObject>

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

  // State accessors
  std::function<int()> getCurrentCollectionIndex;
  std::function<QList<struct CollectionConfig> *()> getCollections;
  std::function<struct GeneralSettings *()> getGeneralSettings;

  // Callbacks for actions that need MainWindow involvement
  std::function<void()> onOpenSettings;
  std::function<void()> onShowAbout;
  std::function<void(int delta)> onAdjustGridWidth;
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

private:
  MenuControllerContext m_ctx;

  // Owned actions (not in UI file)
  QAction *m_fullscreenAction = nullptr;
  QAction *m_shortcutsAction = nullptr;
  QAction *m_gridWidthIncreaseAction = nullptr;
  QAction *m_gridWidthDecreaseAction = nullptr;
  QActionGroup *m_sortActionGroup = nullptr;

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
  void setupGridWidthActions();
  void insertFullscreenInViewMenu(QAction *fullscreenAction);
};

#endif // MENUCONTROLLER_H
