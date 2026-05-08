/**
 * @file test_settingsdialog_scope.h
 * @brief: integration tests for the Settings Mode scope selector
 *        in SettingsDialog.
 *
 * Builds a real SettingsDialog with a small synthetic collection list, then
 * exercises the scope getter, change signal, and silent appearance/layout
 * propagation triggered by handleSaveCollection() under each scope.
 */

#ifndef KARTEND_TESTS_TEST_SETTINGSDIALOG_SCOPE_H
#define KARTEND_TESTS_TEST_SETTINGSDIALOG_SCOPE_H

#include <QObject>

class TestSettingsDialogScope : public QObject {
  Q_OBJECT

private slots:
  void testDefaultScopeIsCurrent();
  void testScopeChangeEmitsSignal();
  void testScopeChangeSignalDoesNotFireForSameValue();
  void testNonPropagatableFieldsDisabledInWiderScope();
  void testFieldsReEnabledWhenScopeReturnsToCurrent();
};

#endif // KARTEND_TESTS_TEST_SETTINGSDIALOG_SCOPE_H
