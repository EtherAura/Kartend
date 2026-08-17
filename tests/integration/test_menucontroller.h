/**
 * @file test_menucontroller.h
 * @brief Integration tests for MenuController's menu-state sync logic.
 *
 * MenuController (src/core/menucontroller*.cpp) wires the menu bar and keeps
 * action checked-states in sync with the active configuration. It directly
 * references Ui_MainWindow, so it can't be unit-tested in isolation; these
 * tests build a real Ui_MainWindow on a throwaway QMainWindow, drive a
 * MenuController via a stub context, and assert the sync methods set the
 * correct QAction::checked states. Covered: syncSortActions (incl. the
 * list-view-only modes that intentionally leave the prior selection) and
 * syncLayoutActions (exactly the active ViewType's entry checked).
 */

#ifndef KARTEND_TESTS_TEST_MENUCONTROLLER_H
#define KARTEND_TESTS_TEST_MENUCONTROLLER_H

#include <QObject>

class TestMenuController : public QObject {
  Q_OBJECT

private slots:
  void syncSortActions_checksMatchingEntry_data();
  void syncSortActions_checksMatchingEntry();
  void syncSortActions_listViewOnlyModeLeavesPriorSelection();
  void syncSortActions_reflectsExcludeSubfoldersToggle();
  void syncLayoutActions_checksExactlyActiveViewType_data();
  void syncLayoutActions_checksExactlyActiveViewType();
  void setupMenuBar_keepsExitLastInFileMenu();
  void setupMenuBar_keepsAboutAtTailOfHelpMenu();
  void setupMenuBar_routesLibraryToolsToToolsMenu();
  void setupMenuBar_groupsImportAndExportIntoSubmenus();
  void setupMenuBar_bindsImportFromLauncherShortcut();
};

#endif // KARTEND_TESTS_TEST_MENUCONTROLLER_H
