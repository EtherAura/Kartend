/**
 * @file test_mainwindow_smoke.h
 * @brief Smoke test that proves the integration harness builds and links a
 *        MainWindow whose top-level managers are all wired up.
 */

#ifndef KARTEND_TESTS_TEST_MAINWINDOW_SMOKE_H
#define KARTEND_TESTS_TEST_MAINWINDOW_SMOKE_H

#include <QObject>

class TestMainWindowSmoke : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void testFixtureBuildsWithoutTouchingRealConfig();
  void testTopLevelManagersAreWired();
  void testGridWidthAndZoomShortcutsAreDisambiguated();
};

#endif // KARTEND_TESTS_TEST_MAINWINDOW_SMOKE_H
