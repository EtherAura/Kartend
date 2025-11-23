#ifndef UIMANAGER_H
#define UIMANAGER_H

#include "collectionconfig.h"
#include <QObject>

class MainWindow;

class UIManager {
public:
  static void setupMainWindow(MainWindow *window);
  static void createMenuBar(MainWindow *window);
  static void setupSidebar(MainWindow *window);
  static void showAbout(QWidget *parent);
  static void
  updateWindowTitleForCollection(QWidget *window, int collectionIndex,
                                 const QList<CollectionConfig> &collections);
  static void handleResizeEvent(MainWindow *window, QResizeEvent *event);

private:
  static void setupManagers(MainWindow *window);
  static void setupUIReferences(MainWindow *window);
  static void setupArtworkManager(MainWindow *window);
  static void setupLastSelectedIndices(MainWindow *window);
  static void setupEventFilters(MainWindow *window);
  static void setupInitialTimers(MainWindow *window);
  static void setupInitialTimersEmptyCollections(MainWindow *window);
  static void setupInitialTimersWithCollections(MainWindow *window);
  static void setupActionExit(MainWindow *window);
  static void setupActionShowSidebar(MainWindow *window);
  static void setupActionSettings(MainWindow *window);
  static void setupActionAbout(MainWindow *window);
  static void setupFullscreenAction(MainWindow *window);
  static void setupFullscreenMenuAction(MainWindow *window,
                                        QAction *fullscreenAction);
};

#endif