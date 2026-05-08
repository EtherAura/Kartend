#ifndef TEST_SETTINGSDIALOG_CHANGES_H
#define TEST_SETTINGSDIALOG_CHANGES_H

#include <QObject>

// Integration tests for SettingsDialog dirty-detection.
// The 13 settingsdialog* TUs were previously covered only by the scope
// combobox test. These exercise the check*Changes path through the
// public hasUnsavedChanges() observer, validating that mutations to
// representative widget categories (line edit, checkbox, spinbox,
// combobox-as-enum) flip the dirty state — and that a no-mutation
// dialog reports clean.
class TestSettingsDialogChanges : public QObject {
  Q_OBJECT
private slots:
  void cleanDialogReportsNoChanges();
  void editingLineEditMarksDirty();
  void togglingCheckBoxMarksDirty();
  void spinBoxValueChangeMarksDirty();
  void comboIndexChangeMarksDirty();
  void revertingMutationReportsClean();
};

#endif // TEST_SETTINGSDIALOG_CHANGES_H
