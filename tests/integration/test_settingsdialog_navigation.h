#ifndef TEST_SETTINGSDIALOG_NAVIGATION_H
#define TEST_SETTINGSDIALOG_NAVIGATION_H

#include <QObject>

// Integration tests for SettingsDialog::setInitialPage (Kartend-colt) — the
// caller page-hint that lets the toolbar warning badge open the dialog
// directly on the global Launchers tab. Drives a real SettingsDialog instance
// and asserts the navigation rail + page stack land where the public
// SettingsPage hint says they should.
class TestSettingsDialogNavigation : public QObject {
  Q_OBJECT
private slots:
  void defaultHint_landsOnFirstCollectionRow();
  void launchersHint_selectsGlobalLaunchersRow();
  void defaultHintAfterConstruction_leavesRowUnchanged();
};

#endif // TEST_SETTINGSDIALOG_NAVIGATION_H
