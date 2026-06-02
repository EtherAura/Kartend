/**
 * @file test_main.cpp
 * @brief Single QApplication entry point for the integration-test binary.
 *
 * All TestXxx slots-only QObjects are instantiated and dispatched through
 * QTest::qExec(). Using one QApplication for the whole binary mirrors how
 * Kartend runs in production and avoids per-test QApplication churn that
 * Qt does not officially support.
 *
 * An optional leading positional argument selects a single suite by class name
 * (e.g. `test_integration TestScanService`). ctest registers one entry per
 * class via that selector (Kartend-40hxp) so `ctest -j` parallelizes across
 * classes and a failure names the class instead of one opaque result. With no
 * selector the binary runs every suite in registry order.
 */

#include "errorpresentation.h"
#include "test_applicationmanager_lifecycle.h"
#include "test_applysettingsdialog.h"
#include "test_attractmanager.h"
#include "test_dbeventscontroller.h"
#include "test_detailpagemanager.h"
#include "test_detailspane_coverflow.h"
#include "test_detailspanemanager.h"
#include "test_eventmanager_detailspane.h"
#include "test_filtermanager.h"
#include "test_kartmanager.h"
#include "test_mainwindow_smoke.h"
#include "test_navigationmanager.h"
#include "test_scanservice.h"
#include "test_scrapedialog_perf.h"
#include "test_scrollmanager.h"
#include "test_searchmanager.h"
#include "test_selectiondisplaymanager.h"
#include "test_selectionoverlaymanager.h"
#include "test_settingsdialog_changes.h"
#include "test_settingsdialog_navigation.h"
#include "test_settingsdialog_perf.h"
#include "test_settingsdialog_scope.h"
#include "test_virtualcontainermanager.h"

#include <QApplication>
#include <QStandardPaths>
#include <QTest>
#include <QThreadPool>
#include <QtPlugin>

#include <cstring>
#include <vector>

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

struct Suite {
  const char *name;
  int (*run)(int, char **);
};

// Suite registry. Each captureless lambda constructs its test object in a fresh
// scope and runs it through QTest::qExec. The names are the ctest entry
// suffixes registered in tests/integration/CMakeLists.txt — keep the two lists
// in sync. Order matches the historical run order: the lighter smoke test runs
// first and the heavier full-ApplicationManager kart/scan suites run last
// (Kartend-r9u0) so an "all suites" run keeps early-failure feedback fast.
const Suite kSuites[] = {
    {"TestMainWindowSmoke",
     [](int c, char **v) {
       TestMainWindowSmoke t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestSettingsDialogScope",
     [](int c, char **v) {
       TestSettingsDialogScope t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestApplySettingsDialog",
     [](int c, char **v) {
       TestApplySettingsDialog t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestApplicationManagerLifecycle",
     [](int c, char **v) {
       TestApplicationManagerLifecycle t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestNavigationManager",
     [](int c, char **v) {
       TestNavigationManager t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestDbEventsController",
     [](int c, char **v) {
       TestDbEventsController t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestEventManagerDetailsPane",
     [](int c, char **v) {
       TestEventManagerDetailsPane t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestDetailsPaneCoverflow",
     [](int c, char **v) {
       TestDetailsPaneCoverflow t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestSettingsDialogChanges",
     [](int c, char **v) {
       TestSettingsDialogChanges t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestSettingsDialogNavigation",
     [](int c, char **v) {
       TestSettingsDialogNavigation t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestSettingsDialogPerf",
     [](int c, char **v) {
       TestSettingsDialogPerf t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestScrapeDialogPerf",
     [](int c, char **v) {
       TestScrapeDialogPerf t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestScrollManager",
     [](int c, char **v) {
       TestScrollManager t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestSelectionOverlayManager",
     [](int c, char **v) {
       TestSelectionOverlayManager t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestVirtualContainerManager",
     [](int c, char **v) {
       TestVirtualContainerManager t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestFilterManager",
     [](int c, char **v) {
       TestFilterManager t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestSearchManager",
     [](int c, char **v) {
       TestSearchManager t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestSelectionDisplayManager",
     [](int c, char **v) {
       TestSelectionDisplayManager t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestDetailsPaneManager",
     [](int c, char **v) {
       TestDetailsPaneManager t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestAttractManager",
     [](int c, char **v) {
       TestAttractManager t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestDetailPageManager",
     [](int c, char **v) {
       TestDetailPageManager t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestKartManager",
     [](int c, char **v) {
       TestKartManager t;
       return QTest::qExec(&t, c, v);
     }},
    {"TestScanService",
     [](int c, char **v) {
       TestScanService t;
       return QTest::qExec(&t, c, v);
     }},
};
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

  // Replace the modal ErrorDialog with a no-op presenter for every test in
  // this binary (Kartend-hlnl). The wiring at mainwindow_wiring.cpp routes
  // NavigationManager::mediaLibraryErrorRaised through
  // ErrorPresentation::showError; tests that exercise that signal path
  // (e.g. TestNavigationManager::testOnMediaLibraryErrorRendersErrorWidget)
  // would otherwise spin a nested QDialog::exec() loop they then have to
  // race-dismiss inside a tight 250 ms window. The stub captures nothing —
  // tests assert on the slot's UI side-effects (the noItemsWidget tree the
  // NavigationManager builds) rather than on the dialog itself.
  ErrorPresentation::setShowErrorOverride(
      [](QWidget * /*parent*/, const ErrorUtils::ErrorContext & /*ctx*/) {});
  ErrorPresentation::setShowCriticalErrorOverride([](QWidget * /*parent*/,
                                                     const ErrorUtils::ErrorContext & /*ctx*/,
                                                     bool /*allowContinue*/) { return true; });

  // An optional leading positional argument selects a single suite so the
  // per-class ctest entries fan out across cores under `ctest -j`
  // (Kartend-40hxp). A leading '-' marks a QTest option (e.g. -o,
  // -maxwarnings), not a class name, so fall through to running every suite.
  // The selector token is stripped before the args reach QTest::qExec, which
  // would otherwise read it as a test-function filter.
  const char *only = nullptr;
  int execArgc = argc;
  char **execArgv = argv;
  std::vector<char *> execArgs;
  if (argc > 1 && argv[1][0] != '-') {
    only = argv[1];
    execArgs.push_back(argv[0]);
    for (int i = 2; i < argc; ++i) {
      execArgs.push_back(argv[i]);
    }
    execArgc = static_cast<int>(execArgs.size());
    execArgv = execArgs.data();
  }

  int status = 0;
  bool matched = false;
  for (const auto &suite : kSuites) {
    if (only && std::strcmp(only, suite.name) != 0) {
      continue;
    }
    matched = true;
    status |= suite.run(execArgc, execArgv);
    drainGlobalThreadPool();
  }

  if (only && !matched) {
    qCritical("Unknown integration suite: %s", only);
    return 2;
  }
  return status;
}
