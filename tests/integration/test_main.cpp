/**
 * @file test_main.cpp
 * @brief Single QApplication entry point for the integration-test binary.
 *
 * All TestXxx slots-only QObjects are instantiated and dispatched through
 * QTest::qExec(). Using one QApplication for the whole binary mirrors how
 * Kartend runs in production and avoids per-test QApplication churn that
 * Qt does not officially support.
 */

#include "test_mainwindow_smoke.h"
#include "test_settingsdialog_scope.h"

#include <QApplication>
#include <QStandardPaths>
#include <QTest>
#include <QtPlugin>

int main(int argc, char *argv[]) {
  // setTestModeEnabled BEFORE QApplication so any path lookups during Qt's
  // own startup are sandboxed too. Each MainWindowFixture re-asserts this
  // in case a test toggles it off.
  QStandardPaths::setTestModeEnabled(true);

  // The offscreen platform plugin lets the binary run on headless CI without
  // a display server. Tests that need real rendering can override this by
  // setting QT_QPA_PLATFORM before launching.
  if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }

  QApplication app(argc, argv);

  int status = 0;
  {
    TestMainWindowSmoke smoke;
    status |= QTest::qExec(&smoke, argc, argv);
  }
  {
    TestSettingsDialogScope scope;
    status |= QTest::qExec(&scope, argc, argv);
  }
  return status;
}
