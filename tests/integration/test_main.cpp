/**
 * @file test_main.cpp
 * @brief Single QApplication entry point for the integration-test binary.
 *
 * All TestXxx slots-only QObjects are instantiated and dispatched through
 * QTest::qExec(). Using one QApplication for the whole binary mirrors how
 * Kartend runs in production and avoids per-test QApplication churn that
 * Qt does not officially support.
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
  // instances (no MainWindow). DatabaseManager now suffixes its Qt SQL
  // connection names per-instance, so this suite no longer needs to be
  // ordered relative to the MainWindow-based tests above.
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
    TestDbEventsController dbEvents;
    status |= QTest::qExec(&dbEvents, argc, argv);
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
    TestSettingsDialogNavigation sdNav;
    status |= QTest::qExec(&sdNav, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestSettingsDialogPerf sdPerf;
    status |= QTest::qExec(&sdPerf, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestScrapeDialogPerf scrapePerf;
    status |= QTest::qExec(&scrapePerf, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestScrollManager sm;
    status |= QTest::qExec(&sm, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestSelectionOverlayManager som;
    status |= QTest::qExec(&som, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestVirtualContainerManager vcm;
    status |= QTest::qExec(&vcm, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestFilterManager fm;
    status |= QTest::qExec(&fm, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestSearchManager search;
    status |= QTest::qExec(&search, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestSelectionDisplayManager selectionDisplay;
    status |= QTest::qExec(&selectionDisplay, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestDetailsPaneManager detailsPane;
    status |= QTest::qExec(&detailsPane, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestAttractManager attract;
    status |= QTest::qExec(&attract, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestDetailPageManager detailPage;
    status |= QTest::qExec(&detailPage, argc, argv);
  }
  drainGlobalThreadPool();
  // Kartend-r9u0: KartManager + ScanService integration coverage.
  // Kept last in the chain so the (heavier, full-ApplicationManager)
  // kart tests don't slow the early-failure feedback for the lighter
  // tests above.
  {
    TestKartManager kart;
    status |= QTest::qExec(&kart, argc, argv);
  }
  drainGlobalThreadPool();
  {
    TestScanService scanService;
    status |= QTest::qExec(&scanService, argc, argv);
  }
  drainGlobalThreadPool();
  return status;
}
