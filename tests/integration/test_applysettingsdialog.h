/**
 * @file test_applysettingsdialog.h
 * @brief Kartend-iyk: integration tests for the per-category Apply Settings
 *        dialog and the mask-aware copy used by SettingsDialog.
 *
 * Exercises the FieldCategory enum, the dialog's source-combo behaviour in
 * pull mode, and verifies that applyCategoriesToIndices() only mutates the
 * fields whose category bit is set.
 */

#ifndef KARTEND_TESTS_TEST_APPLYSETTINGSDIALOG_H
#define KARTEND_TESTS_TEST_APPLYSETTINGSDIALOG_H

#include <QObject>

class TestApplySettingsDialog : public QObject {
  Q_OBJECT

private slots:
  void testDefaultMaskIsAll();
  void testSelectNoneClearsMask();
  void testSelectAllRestoresMask();
  void testPushModeHasNoSourceCombo();
  void testPullModeExcludesCurrentAndPlaylists();
  void testApplyCategoriesGridLayoutOnly();
  void testApplyCategoriesColorsOnly();
  void testApplyCategoriesAllPreservesPathsAndScanFlags();
  void testApplyCategoriesEmptyMaskIsNoOp();
  void testApplyCategoriesSkipsSourceIndex();
};

#endif // KARTEND_TESTS_TEST_APPLYSETTINGSDIALOG_H
