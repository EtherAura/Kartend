/**
 * @file test_scrolleventscontroller.h
 * @brief Integration tests for ScrollEventsController (src/core/scrolleventscontroller.cpp).
 *
 * The controller reacts to ScrollManager signals through a closure context.
 * The settings-persisting slots (list column widths, sort mode) and their
 * missing-manager guards run standalone against a recording
 * ISettingsManager double; the sort-mode happy path borrows the live
 * NavigationManager from a MockedMainWindowFixture because the slot
 * requires one before it will touch settings at all.
 */

#ifndef KARTEND_TESTS_TEST_SCROLLEVENTSCONTROLLER_H
#define KARTEND_TESTS_TEST_SCROLLEVENTSCONTROLLER_H

#include <QObject>

class TestScrollEventsController : public QObject {
  Q_OBJECT

private slots:
  void listColumnWidths_persistThroughSettingsManager();
  void listColumnWidths_updateSettingsEvenWithoutSettingsManager();
  void listColumnWidths_preferDebouncedSaverOverDirectSave();
  void sortModeChange_withoutNavigationManagerLeavesSettingsUntouched();
  void sortModeChange_updatesAndPersistsSortMode();
  // Kartend-ic4h6
  void selectItemByIndex_cancelsPendingRestoreSoAVerifyCannotRevert();
  void selectItemByIndex_withoutInteractionManagerIsANoOp();
};

#endif // KARTEND_TESTS_TEST_SCROLLEVENTSCONTROLLER_H
