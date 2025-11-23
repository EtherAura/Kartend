#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "collectionconfig.h"
#include "interactionmanager.h"
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
class QAction;
QT_END_NAMESPACE

class Ui_MainWindow;
class ArtworkManager;
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
  void refreshTitleCounts();
  void updateWindowTitleForCollection(int collectionIndex);

  // Getters for Managers
  SidebarManager *getSidebarManager() const { return m_sidebarManager; }
  metadataSidebar *getMetadataSidebar() const { return m_metadataSidebar; }
  SettingsManager *getSettingsManager() const { return m_settingsManager; }
  DatabaseManager *getDatabaseManager() const { return m_databaseManager; }
  ScrollManager *getScrollManager() const { return m_scrollManager; }
  NavigationManager *getNavigationManager() const { return m_navigationManager; }
  InteractionManager *getInteractionManager() const { return m_interactionManager; }

protected:
  void resizeEvent(QResizeEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  auto eventFilter(QObject *watched, QEvent *event) -> bool override;
  void closeEvent(QCloseEvent *event) override;
  bool m_isShuttingDown = false;

private:
  MainScreenConfig m_mainScreenConfig;
  QAction *m_fullscreenAction = nullptr;

  friend class InteractionManager;
  friend class NavigationManager;
  friend class ScrollManager;
  friend class SidebarManager;
  friend class SettingsDialog;

  SidebarManager *m_sidebarManager = nullptr;
  metadataSidebar *m_metadataSidebar = nullptr;
  SettingsManager *m_settingsManager = nullptr;
  DatabaseManager *m_databaseManager = nullptr;
  ScrollManager *m_scrollManager = nullptr;
  NavigationManager *m_navigationManager = nullptr;
  InteractionManager *m_interactionManager = nullptr;

  void setupManagerConnections();
  void updateWindowTitleWithFilter(int visible, int total);

  void connectDatabaseManager();
  void connectScrollManager();
  void connectSidebarManager();
  void connectSearchComponents();
  void connectScrollBars() const;

  // UI Setup Methods
  void setupUI();
  void setupManagers();
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