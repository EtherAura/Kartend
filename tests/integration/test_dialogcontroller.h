/**
 * @file test_dialogcontroller.h
 * @brief Integration tests for DialogController's non-modal surface.
 *
 * DialogController (src/core/dialogcontroller.cpp) centralizes dialog
 * construction for MainWindow. Most of its methods run modal exec() loops
 * and are out of headless reach, but the non-modal surface is testable:
 * QObject parenting/ownership (including MainWindow constructing its own
 * instance), the KartProgressDialog lifecycle wiring against a real
 * KartManager's progress signals, the static dialog-visibility trackers,
 * and the SettingsDialog factory returned by makeSettingsDialogFactory
 * (construction + collectionSaved / rescanRequired callback wiring).
 */

#ifndef KARTEND_TESTS_TEST_DIALOGCONTROLLER_H
#define KARTEND_TESTS_TEST_DIALOGCONTROLLER_H

#include <QObject>

class TestDialogController : public QObject {
  Q_OBJECT

private slots:
  void construction_parentsToOwningWidget();
  void construction_toleratesNullParentWidget();
  void mainWindowConstructsAndParentsController();
  void startKartProgressDialog_buildsWiredDialog();
  void startKartProgressDialog_failureClosesAndDeletesDialog();
  void startKartProgressDialog_nullParentStillShowsDialog();
  // Kartend-h7xnr.1: the sequential drop-import chain starts the next
  // operation while the previous dialog may still sit in its finished
  // "Close" state. A new startKartProgressDialog must close (and thereby
  // delete) the stale predecessor so dialogs don't stack per dropped file,
  // each still tracking the new operation's signals.
  void startKartProgressDialog_closesStalePredecessor();
  void settingsDialogVisibilityTracker_reflectsShowHide();
  void makeSettingsDialogFactory_buildsDialogWithNullCallbacks();
  void makeSettingsDialogFactory_wiresSaveAndRescanCallbacks();
};

#endif // KARTEND_TESTS_TEST_DIALOGCONTROLLER_H
