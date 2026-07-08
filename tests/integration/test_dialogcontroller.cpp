#include "test_dialogcontroller.h"

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "dialogcontroller.h"
#include "kartmanager.h"
#include "kartprogressdialog.h"
#include "mainwindow.h"
#include "mainwindowfixture.h"
#include "settingsdialog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QEvent>
#include <QLabel>
#include <QList>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QString>
#include <QTest>
#include <QWidget>

namespace {

// Minimal collection list for SettingsDialog construction — mirrors the
// helper in test_settingsdialog_scope.cpp. The dialog only reads these via
// the public constructor; no filesystem access without a Browse/Save click.
QList<CollectionConfig> makeCollections() {
  CollectionConfig parent;
  parent.name = QStringLiteral("Parent");
  CollectionConfig child;
  child.name = QStringLiteral("Child");
  child.parentCollectionIndex = 0;
  child.isSubcollection = true;
  CollectionConfig sibling;
  sibling.name = QStringLiteral("Sibling");
  return {parent, child, sibling};
}

// WA_DeleteOnClose deletes via deleteLater(); QTest slots run outside an
// event loop, so flush the posted DeferredDelete explicitly instead of
// spinning QTRY_ (which would pump timers queued by unrelated fixtures).
void flushDeferredDeletes() {
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

} // namespace

void TestDialogController::construction_parentsToOwningWidget() {
  // The ctor both QObject-parents the controller to the widget and stores
  // it as the dialog parent. The QObject edge is the observable half — and
  // per house convention parent() doubles as the runtime lifetime guard, so
  // deleting the owning widget must take the controller with it.
  auto *host = new QWidget;
  auto *controller = new DialogController(host);
  QCOMPARE(controller->parent(), host);

  QPointer<DialogController> guard(controller);
  delete host;
  QVERIFY2(guard.isNull(), "deleting the parent widget must destroy the controller (Qt ownership)");
}

void TestDialogController::construction_toleratesNullParentWidget() {
  // A null parent widget is legal (dialogs then open unparented); the
  // controller must construct without a QObject parent rather than crash.
  DialogController controller(nullptr);
  QVERIFY(controller.parent() == nullptr);

  // The static visibility trackers are callable with no instance at all.
  QVERIFY(!DialogController::anyScrapeResultDialogVisible());
  QVERIFY(!DialogController::anySettingsDialogVisible());
}

void TestDialogController::mainWindowConstructsAndParentsController() {
  // MainWindow builds its DialogController in the constructor
  // (mainwindow.cpp) and hands `this` as the parent widget. There is no
  // public accessor by design, so assert through the QObject tree.
  KartendTest::MainWindowFixture fixture;
  MainWindow *win = fixture.window();
  QVERIFY(win);

  auto *controller = win->findChild<DialogController *>();
  QVERIFY2(controller, "MainWindow must own a DialogController child");
  QCOMPARE(controller->parent(), win);
}

void TestDialogController::startKartProgressDialog_buildsWiredDialog() {
  QWidget host;
  DialogController controller(&host);
  kart::KartManager km;

  controller.startKartProgressDialog(&km, QStringLiteral("Importing bundle"));

  auto *dlg = host.findChild<KartProgressDialog *>();
  QVERIFY2(dlg, "startKartProgressDialog must create a KartProgressDialog under the parent widget");
  QPointer<KartProgressDialog> guard(dlg);
  QVERIFY2(dlg->isVisible(), "the progress dialog is shown non-modally via show()");
  QVERIFY2(dlg->testAttribute(Qt::WA_DeleteOnClose),
           "the dialog must delete itself on close — the controller keeps no handle to it");
  QCOMPARE(dlg->windowTitle(), QStringLiteral("Importing bundle"));

  auto *bar = dlg->findChild<QProgressBar *>();
  QVERIFY(bar);
  QCOMPARE(bar->value(), 0);

  // Qt signals are public member functions; emitting them from the test
  // drives exactly the manager→dialog connections the controller wired.
  emit km.kartProgressFraction(0.5);
  QCOMPARE(bar->value(), 500); // KartProgressDialog's bar spans 0..1000

  emit km.kartProgressFraction(2.0);
  QCOMPARE(bar->value(), 1000); // setFraction clamps to [0, 1]

  emit km.kartProgressEntry(QStringLiteral("media/track01.flac"));
  bool entryShown = false;
  const QList<QLabel *> labels = dlg->findChildren<QLabel *>();
  for (const QLabel *label : labels) {
    entryShown = entryShown || label->text() == QStringLiteral("media/track01.flac");
  }
  QVERIFY2(entryShown, "kartProgressEntry must land in the dialog's entry label");

  emit km.kartProgressFinished();
  QCOMPARE(bar->value(), 1000);
  auto *button = dlg->findChild<QPushButton *>();
  QVERIFY(button);
  QCOMPARE(button->text(), QStringLiteral("Close")); // markFinished rebrands Cancel

  // Close must actually destroy the dialog (WA_DeleteOnClose), otherwise a
  // modal ghost would keep blocking input after every kart operation.
  dlg->close();
  flushDeferredDeletes();
  QVERIFY2(guard.isNull(), "closing the finished dialog must delete it");
}

void TestDialogController::startKartProgressDialog_failureClosesAndDeletesDialog() {
  QWidget host;
  DialogController controller(&host);
  kart::KartManager km;

  controller.startKartProgressDialog(&km, QStringLiteral("Exporting bundle"));
  QPointer<KartProgressDialog> dlg(host.findChild<KartProgressDialog *>());
  QVERIFY(dlg);
  QVERIFY(dlg->isVisible());

  // kartProgressFailed is wired straight to QDialog::reject — the dialog
  // must hide immediately and then self-delete via WA_DeleteOnClose.
  emit km.kartProgressFailed();
  QVERIFY2(!dlg->isVisible(), "a failed kart operation must close the progress dialog");
  QCOMPARE(dlg->result(), static_cast<int>(QDialog::Rejected));

  flushDeferredDeletes();
  QVERIFY2(dlg.isNull(), "the rejected dialog must delete itself");
}

void TestDialogController::startKartProgressDialog_nullParentStillShowsDialog() {
  // Null-parent guard: with no parent widget the dialog opens as an
  // unparented top-level window instead of crashing on the null parent.
  DialogController controller(nullptr);
  kart::KartManager km;

  controller.startKartProgressDialog(&km, QStringLiteral("Unparented import"));

  KartProgressDialog *dlg = nullptr;
  const QList<QWidget *> topLevels = QApplication::topLevelWidgets();
  for (QWidget *w : topLevels) {
    if (auto *candidate = qobject_cast<KartProgressDialog *>(w)) {
      dlg = candidate;
      break;
    }
  }
  QVERIFY2(dlg, "the progress dialog must open top-level when the controller has no parent");
  QPointer<KartProgressDialog> guard(dlg);
  QVERIFY(dlg->isVisible());

  // Clean up the unowned modal dialog so it can't leak into later suites.
  dlg->close();
  flushDeferredDeletes();
  QVERIFY(guard.isNull());
}

void TestDialogController::startKartProgressDialog_closesStalePredecessor() {
  QWidget host;
  DialogController controller(&host);
  kart::KartManager km;

  controller.startKartProgressDialog(&km, QStringLiteral("Import A"));
  QPointer<KartProgressDialog> first(host.findChild<KartProgressDialog *>());
  QVERIFY(first);
  // Finish A: markFinished leaves the dialog open in its "Close" state.
  emit km.kartProgressFinished();
  QVERIFY(first->isVisible());

  // The drop chain starts import B while A's finished dialog is still up —
  // the controller must close A instead of stacking a second live dialog
  // that would keep receiving B's progress signals.
  controller.startKartProgressDialog(&km, QStringLiteral("Import B"));
  QVERIFY2(!first->isVisible(), "starting a new kart operation must close the stale dialog");
  flushDeferredDeletes();
  QVERIFY2(first.isNull(), "the stale dialog must delete itself (WA_DeleteOnClose)");

  QPointer<KartProgressDialog> second(host.findChild<KartProgressDialog *>());
  QVERIFY2(second, "the new operation still gets its own progress dialog");
  QVERIFY(second->isVisible());
  QCOMPARE(second->windowTitle(), QStringLiteral("Import B"));

  // Clean up so the modal dialog can't leak into later suites.
  second->close();
  flushDeferredDeletes();
  QVERIFY(second.isNull());
}

void TestDialogController::settingsDialogVisibilityTracker_reflectsShowHide() {
  QVERIFY2(!DialogController::anySettingsDialogVisible(),
           "baseline: no settings dialog is visible (a prior suite leaked one otherwise)");
  QVERIFY(!DialogController::anyScrapeResultDialogVisible());

  SettingsDialog dialog(nullptr, makeCollections(), 0);
  QVERIFY2(!DialogController::anySettingsDialogVisible(),
           "construction alone must not count as visible — only showEvent does");

  dialog.show();
  QVERIFY(DialogController::anySettingsDialogVisible());
  // The two trackers are independent statics; showing settings must not
  // trip the scrape-result gate.
  QVERIFY(!DialogController::anyScrapeResultDialogVisible());

  dialog.hide();
  QVERIFY2(!DialogController::anySettingsDialogVisible(),
           "hiding the dialog must clear the visibility tracker");
}

void TestDialogController::makeSettingsDialogFactory_buildsDialogWithNullCallbacks() {
  ApplicationContext ctx; // all-null members; SettingsDialog null-guards every access
  const auto factory = DialogController::makeSettingsDialogFactory(&ctx);
  QVERIFY(factory);

  // Null callbacks are legal — the factory must skip the connects, not crash.
  const auto dlg = factory(nullptr, makeCollections(), 0, nullptr, nullptr);
  QVERIFY2(dlg != nullptr, "the factory must build a dialog behind ISettingsDialog");
  QCOMPARE(dlg->getCollections().size(), 3);
  QCOMPARE(dlg->getCollections().first().name, QStringLiteral("Parent"));
}

void TestDialogController::makeSettingsDialogFactory_wiresSaveAndRescanCallbacks() {
  ApplicationContext ctx;
  const auto factory = DialogController::makeSettingsDialogFactory(&ctx);
  QVERIFY(factory);

  int savedCalls = 0;
  QList<CollectionConfig> savedSeen;
  int rescanCalls = 0;
  int rescanIndex = -1;
  const auto dlg = factory(
      nullptr, makeCollections(), 0,
      [&savedCalls, &savedSeen](const QList<CollectionConfig> &collections) {
        ++savedCalls;
        savedSeen = collections;
      },
      [&rescanCalls, &rescanIndex](int index) {
        ++rescanCalls;
        rescanIndex = index;
      });
  QVERIFY(dlg != nullptr);

  // The wiring targets the concrete Qt signals, so reach the concrete type.
  auto *concrete = dynamic_cast<SettingsDialog *>(dlg.get());
  QVERIFY2(concrete, "the factory must construct the concrete SettingsDialog");

  QList<CollectionConfig> edited = makeCollections();
  edited.removeLast();
  emit concrete->collectionSaved(edited);
  QCOMPARE(savedCalls, 1);
  QCOMPARE(savedSeen.size(), 2);
  QCOMPARE(savedSeen.first().name, QStringLiteral("Parent"));

  emit concrete->rescanRequired(1);
  QCOMPARE(rescanCalls, 1);
  QCOMPARE(rescanIndex, 1);
}
