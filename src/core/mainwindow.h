#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "applicationcontext.h"
#include "collectionutils.h"
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QTimer>
#include <memory>

QT_BEGIN_NAMESPACE
class QStackedWidget;
class QWidget;
class QGridLayout;
class QHBoxLayout;
class QScrollArea;
class QLineEdit;
class QPushButton;
class QLabel;
class QAction;
QT_END_NAMESPACE

class Ui_MainWindow;
class ApplicationManager;
class ArtworkManager;
class CacheManager;
class InteractionManager;
class DatabaseManager;
class NavigationManager;
class SessionManager;
class SettingsManager;
class SidebarManager;
class ScrollManager;
class ItemWidget;
class MetadataSidebar;
class LoadingOverlay;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

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
  QLabel *loadingLabel;
  LoadingOverlay *m_loadingOverlay = nullptr;

  int currentCollectionIndex;
  QList<CollectionConfig> m_collections;
  CollectionHierarchyCache m_hierarchyCache;
  ApplicationContext m_appContext;  // Shared context for manager setup
  
  void refreshTitleCounts();
  void updateWindowTitleForCollection(int collectionIndex);
  void rebuildHierarchyCache();
  [[nodiscard]] const CollectionHierarchyCache &getHierarchyCache() const { return m_hierarchyCache; }
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

protected:
  void resizeEvent(QResizeEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  auto eventFilter(QObject *watched, QEvent *event) -> bool override;
  void closeEvent(QCloseEvent *event) override;

private:
  bool m_isShuttingDown = false;
  QAction *m_fullscreenAction = nullptr;
  QAction *m_shortcutsAction = nullptr;
  QAction *m_gridWidthIncreaseAction = nullptr;
  QAction *m_gridWidthDecreaseAction = nullptr;

  std::unique_ptr<ApplicationManager> m_appManager;
  MetadataSidebar *m_MetadataSidebar = nullptr;

  void setupManagerConnections();
  void updateWindowTitleWithFilter(int visible, int total);

  void connectDatabaseManager();
  void connectScrollManager();
  void connectSidebarManager();
  void connectSearchComponents();
  void connectScrollBars() const;

  // UI Setup Methods
  void setupUI();
  void setupUIReferences();
  void createMenuBar();
  void setupActionExit();
  void setupActionShowSidebar();
  void setupActionSettings();
  void setupActionAbout();
  void setupActionAboutQt();
  void setupActionRefresh();
  void setupFullscreenAction();
  void setupShortcutsAction();
  void setupGridWidthActions();
  void adjustGridWidth(int delta);
  void setupFullscreenMenuAction(QAction *fullscreenAction);
  void setupSidebar();
  void setupArtworkManager();
  void setupLastSelectedIndices();
  void setupEventFilters();
  void setupInitialTimers();
  void setupInitialTimersEmptyCollections();
  void setupInitialTimersWithCollections();
  void initializeAppContext();
  void showAbout();
};

#endif