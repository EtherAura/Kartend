/**
 * @file test_main.cpp
 * @brief Single QApplication entry point for the integration-test binary.
 *
 * All TestXxx slots-only QObjects are instantiated and dispatched through
 * QTest::qExec(). Using one QApplication for the whole binary mirrors how
 * Kartend runs in production and avoids per-test QApplication churn that
 * Qt does not officially support.
 */

#include "test_applicationmanager_lifecycle.h"
#include "test_applysettingsdialog.h"
#include "test_detailspane_coverflow.h"
#include "test_eventmanager_detailspane.h"
#include "test_mainwindow_smoke.h"
#include "test_navigationmanager.h"
#include "test_scrollmanager.h"
#include "test_settingsdialog_changes.h"
#include "test_settingsdialog_scope.h"

#include <QApplication>
#include <QStandardPaths>
#include <QTest>
#include <QThreadPool>
#include <QtPlugin>

namespace {
// QtConcurrent's global thread pool keeps idle worker threads alive between
// tasks. When TSan runs the integration suite, those persistent threads
// retain access history across test boundaries — combined with heap address
// reuse for QArrayData/QHash buffers, that produces phantom data races
// between worker threads of long-since-finished tests. Calling
// waitForDone() between test scopes empties the pool so the next test gets
// fresh thread IDs and a clean shadow-memory view.
void drainGlobalThreadPool() {
  auto *pool = QThreadPool::globalInstance();
  pool->setExpiryTimeout(0);
  pool->waitForDone();
  pool->setExpiryTimeout(30'000);
}
} // namespace

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
  drainGlobalThreadPool();
  {
    TestSettingsDialogScope scope;
    status |= QTest::qExec(&scope, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestApplySettingsDialog applySettings;
    status |= QTest::qExec(&applySettings, argc, argv);
  }
  drainGlobalThreadPool();
  // ApplicationManager lifecycle tests build their own bare ApplicationManager
  // instances (no MainWindow), so they must run after the MainWindow-based
  // tests above to avoid SQL connection-name collisions on
  // "kartend_main" — DatabaseManager removes the connection in its dtor, so
  // sequencing is sufficient.
  {
    TestApplicationManagerLifecycle appLifecycle;
    status |= QTest::qExec(&appLifecycle, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestNavigationManager nav;
    status |= QTest::qExec(&nav, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestEventManagerDetailsPane emDp;
    status |= QTest::qExec(&emDp, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestDetailsPaneCoverflow dpCf;
    status |= QTest::qExec(&dpCf, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestSettingsDialogChanges sdCh;
    status |= QTest::qExec(&sdCh, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestScrollManager sm;
    status |= QTest::qExec(&sm, argc, argv);
  }
  drainGlobalThreadPool();
  return status;
}
