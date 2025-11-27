#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "collectionutils.h"
#include "interactionmanager.h"
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
class SettingsManager;
class SidebarManager;
class ScrollManager;
class MediaItemWidget;
class metadataSidebar;

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

  int currentCollectionIndex;
  QList<CollectionConfig> m_collections;
  CollectionHierarchyCache m_hierarchyCache;
  void refreshTitleCounts();
  void updateWindowTitleForCollection(int collectionIndex);
  void rebuildHierarchyCache();
  [[nodiscard]] const CollectionHierarchyCache &getHierarchyCache() const { return m_hierarchyCache; }
  bool isShuttingDown() const { return m_isShuttingDown; }

  // Getters for Managers
  ApplicationManager *getApplicationManager() const { return m_appManager.get(); }
  metadataSidebar *getMetadataSidebar() const { return m_metadataSidebar; }
  
  // Delegated Getters
  SidebarManager *getSidebarManager() const;
  SettingsManager *getSettingsManager() const;
  DatabaseManager *getDatabaseManager() const;
  ScrollManager *getScrollManager() const;
  NavigationManager *getNavigationManager() const;
  InteractionManager *getInteractionManager() const;
  SessionManager *getSessionManager() const;
  ArtworkManager *getArtworkManager() const;
  CacheManager *getCacheManager() const;

protected:
  void resizeEvent(QResizeEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  auto eventFilter(QObject *watched, QEvent *event) -> bool override;
  void closeEvent(QCloseEvent *event) override;

private:
  bool m_isShuttingDown = false;
  MainScreenConfig m_mainScreenConfig;
  QAction *m_fullscreenAction = nullptr;

  std::unique_ptr<ApplicationManager> m_appManager;
  metadataSidebar *m_metadataSidebar = nullptr;

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
  void setupFullscreenAction();
  void setupFullscreenMenuAction(QAction *fullscreenAction);
  void setupSidebar();
  void setupArtworkManager();
  void setupLastSelectedIndices();
  void setupEventFilters();
  void setupInitialTimers();
  void setupInitialTimersEmptyCollections();
  void setupInitialTimersWithCollections();
  void showAbout();
};

#endif