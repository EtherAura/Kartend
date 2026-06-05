// Tests for CoverFlowController's view-type gating + null-safe no-op contract
// (Kartend-kv0wa). The controller borrows every dependency and is designed to
// no-op cleanly when they are unwired (headless smoke / early shutdown), so
// most of its public surface is exercisable without standing up the GUI stack.
#include <QObject>
#include <QTest>

#include "collection/collectioncontext.h"
#include "collectiontypes.h"
#include "coverflowcontroller.h"

class TestCoverFlowController : public QObject {
  Q_OBJECT
private slots:
  void isActive_falseWithoutContext();
  void isActive_followsViewType();
  void publicMethods_noopWithoutSetup();
  void ensureWidget_noopWithoutScrollArea();
};

void TestCoverFlowController::isActive_falseWithoutContext() {
  CoverFlowController controller;
  // No CollectionContext wired -> the carousel is never the active view.
  QVERIFY(!controller.isActive());
  QCOMPARE(controller.widget(), nullptr);
}

void TestCoverFlowController::isActive_followsViewType() {
  CoverFlowController controller;
  CollectionContext ctx;
  CoverFlowControllerSetup setup;
  setup.context = &ctx;
  controller.setupReferences(setup);

  ctx.config.viewType = ViewType::CoverFlow;
  QVERIFY(controller.isActive());

  ctx.config.viewType = ViewType::Grid;
  QVERIFY(!controller.isActive());

  ctx.config.viewType = ViewType::List;
  QVERIFY(!controller.isActive());
}

void TestCoverFlowController::publicMethods_noopWithoutSetup() {
  CoverFlowController controller; // nothing wired
  // The null-safe contract: exercising the public surface unwired must not
  // crash, and no carousel widget is created.
  controller.applyConfig();
  controller.applyVisibility();
  controller.rebuildCards();
  controller.rebuildCardsIfActive();
  controller.refreshForViewTypeChange();
  controller.onSelectionChanged(3);
  QCOMPARE(controller.widget(), nullptr);
  QVERIFY(!controller.isActive());
}

void TestCoverFlowController::ensureWidget_noopWithoutScrollArea() {
  CoverFlowController controller;
  CollectionContext ctx;
  ctx.config.viewType = ViewType::CoverFlow; // active...
  CoverFlowControllerSetup setup;
  setup.context = &ctx; // ...but no media scroll area to host the widget
  controller.setupReferences(setup);
  controller.ensureWidget();
  QCOMPARE(controller.widget(), nullptr);
}

QTEST_MAIN(TestCoverFlowController)
#include "test_coverflowcontroller.moc"
