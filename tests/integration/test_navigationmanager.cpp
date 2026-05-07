#include "test_navigationmanager.h"

#include "collectionutils.h"
#include "errorutils.h"
#include "mainwindow.h"
#include "mainwindowfixture.h"
#include "navigationmanager.h"
#include "navigationstackmanager.h"

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTest>
#include <QTimer>
#include <QWidget>

namespace {

CollectionConfig makeCollectionStub(const QString &name, int parentIndex = -1) {
  CollectionConfig cfg;
  cfg.name = name;
  cfg.parentCollectionIndex = parentIndex;
  cfg.isSubcollection = (parentIndex >= 0);
  // mediaDirectory left empty intentionally — onSubcollectionEntered's
  // navigation attempt should fail validateCollectionIndex (no media, no
  // descendants), which is exactly the unwind-on-failure path we want to
  // exercise.
  return cfg;
}

// Schedules repeated attempts to dismiss whatever modal QDialog ends up
// active. NavigationManager::onMediaLibraryError calls
// ErrorDialog::showError which spins a nested event loop via dialog.exec();
// the timer fires inside that loop and accepts the dialog so exec() returns.
//
// We poll because exec() may not have brought the dialog up yet when the
// first tick fires. ~50 retries * 5ms gives a 250ms ceiling, well above the
// realistic show latency on the offscreen platform plugin.
void scheduleDismissActiveModal(int retries = 50, int intervalMs = 5) {
  if (retries <= 0) {
    return;
  }
  QTimer::singleShot(intervalMs, [retries, intervalMs]() {
    if (auto *modal = QApplication::activeModalWidget()) {
      if (auto *dlg = qobject_cast<QDialog *>(modal)) {
        dlg->accept();
      } else {
        modal->close();
      }
      return;
    }
    scheduleDismissActiveModal(retries - 1, intervalMs);
  });
}

} // namespace

void TestNavigationManager::testOnCollectionSelectedClearsNavigationStack() {
  KartendTest::MainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getNavigationManager();
  QVERIFY(nav);

  // Pre-load the stack so we can observe the clear() in onCollectionSelected.
  // The contract is that selecting a top-level collection always discards
  // any pending hierarchy traversal — otherwise "Back" from the new view
  // would jump into a stale parent path.
  nav->stackManager()->push(7);
  nav->stackManager()->push(11);
  QVERIFY(!nav->stackManager()->isEmpty());

  // collectionIndex is out of range for the empty m_collections, so
  // showCollectionItems short-circuits via validateCollectionIndex. The
  // contract under test is the clear() that runs *before* the navigation
  // attempt — it must execute regardless of whether the target is valid.
  nav->onCollectionSelected(0);

  QVERIFY(nav->stackManager()->isEmpty());
  QCOMPARE(nav->stackManager()->depth(), 0);
}

void TestNavigationManager::testOnSubcollectionEnteredIgnoresOutOfRangeIndex() {
  KartendTest::MainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getNavigationManager();

  // Seed a single in-range entry so we can assert that the only rejected
  // call paths are the out-of-range ones.
  win->m_collections.append(makeCollectionStub(QStringLiteral("Solo")));
  win->currentCollectionIndex = 0;
  nav->stackManager()->push(42); // sentinel — must remain after the calls

  nav->onSubcollectionEntered(-1);
  nav->onSubcollectionEntered(99);

  // Out-of-range indices fail the outer guard before any push/pop logic
  // runs, so the sentinel must survive untouched. A regression that pushed
  // on every call (or popped unconditionally) would change this.
  QCOMPARE(nav->stackManager()->size(), 1);
  QCOMPARE(nav->stackManager()->top(), 42);
}

void TestNavigationManager::testOnSubcollectionEnteredUnwindsPushOnNavigationFailure() {
  KartendTest::MainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getNavigationManager();

  win->m_collections.append(makeCollectionStub(QStringLiteral("Parent")));
  win->m_collections.append(makeCollectionStub(QStringLiteral("Child"), /*parentIndex=*/0));
  win->currentCollectionIndex = 0;
  win->rebuildHierarchyCache();

  QVERIFY(nav->stackManager()->isEmpty());

  // The child has no media directory and no descendants, so
  // validateCollectionIndex → showCollectionItems returns false. The slot
  // must undo the speculative push it performed before showCollectionItems
  // was called; otherwise the back-stack would accumulate entries for
  // navigations that never actually happened.
  nav->onSubcollectionEntered(1);

  QVERIFY(nav->stackManager()->isEmpty());
}

void TestNavigationManager::testOnSubcollectionEnteredSkipsPushWhenNoCurrentCollection() {
  KartendTest::MainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getNavigationManager();

  win->m_collections.append(makeCollectionStub(QStringLiteral("Solo")));
  win->currentCollectionIndex = -1; // pre-startup: nothing selected yet
  QVERIFY(nav->stackManager()->isEmpty());

  // Even though the target index is in range, the stack push is gated on
  // `*m_currentCollectionIndex >= 0`. With no current collection, there is
  // nothing meaningful to "go back to," so the stack must stay empty even
  // after the navigation attempt fails and triggers the defensive pop.
  nav->onSubcollectionEntered(0);

  QVERIFY(nav->stackManager()->isEmpty());
}

void TestNavigationManager::testOnMediaLibraryErrorRendersErrorWidget() {
  KartendTest::MainWindowFixture fixture;
  MainWindow *win = fixture.window();
  NavigationManager *nav = win->getNavigationManager();
  QWidget *gridContainer = win->gridContainer;
  QVERIFY(gridContainer);

  // Pre-existing noItemsWidget children must be removed by the slot so the
  // new error widget is the unambiguous result of this call. Insert a
  // sentinel and confirm it gets scheduled for deletion.
  auto *stale = new QWidget(gridContainer);
  stale->setObjectName(QStringLiteral("noItemsWidget"));
  QPointer<QWidget> stalePtr(stale);

  const auto error = ErrorUtils::ErrorContext::error(
      ErrorUtils::ErrorCode::DatabaseQueryFailed, QStringLiteral("integration test forced error"),
      QStringLiteral("TestNavigationManager"));

  // ErrorDialog::showError runs dialog.exec(), so we must dismiss the modal
  // from inside that nested loop. The timer is scheduled before the slot
  // call so the dispatcher fires it as soon as exec()'s loop picks it up.
  scheduleDismissActiveModal();
  nav->onMediaLibraryError(error);

  // The stale sentinel was removed via deleteLater(); pump the queue so the
  // assertion observes the post-deletion state.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QVERIFY2(stalePtr.isNull(), "Pre-existing noItemsWidget should have been deleteLater()'d");

  // Exactly one fresh error widget should now exist as a child of the grid
  // container, carrying the error message in a QLabel descendant.
  const auto labels = gridContainer->findChildren<QWidget *>(QStringLiteral("noItemsWidget"));
  QCOMPARE(labels.size(), 1);
  auto *errorLabel = labels.first()->findChild<QLabel *>();
  QVERIFY(errorLabel);
  QCOMPARE(errorLabel->text(), QStringLiteral("integration test forced error"));
}
