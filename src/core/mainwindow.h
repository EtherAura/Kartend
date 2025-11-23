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

  SidebarManager *m_sidebarManager;
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
  metadataSidebar *m_metadataSidebar;
  SettingsManager *m_settingsManager;
  DatabaseManager *m_databaseManager;
  ScrollManager *m_scrollManager;
  NavigationManager *m_navigationManager;
  InteractionManager *m_interactionManager;

  int currentCollectionIndex;
  QList<CollectionConfig> m_collections;
  void refreshTitleCounts();
  void updateWindowTitleForCollection(int collectionIndex);

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