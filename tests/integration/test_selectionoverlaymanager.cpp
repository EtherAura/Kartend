#include "test_selectionoverlaymanager.h"

#include "selectionoverlaymanager.h"

#include <QPointer>
#include <QRect>
#include <QTest>
#include <QWidget>

void TestSelectionOverlayManager::testConstructionWithoutParentWidget() {
  SelectionOverlayManager mgr;
  // Without a parent widget the manager refuses to materialise the overlay;
  // shouldKeepVisible should default to false and isVisible() must be false.
  QVERIFY(!mgr.isForceVisible());
  QVERIFY(!mgr.shouldKeepVisible());
  QVERIFY(!mgr.isVisible());
  QVERIFY(!mgr.isAnimating());
}

void TestSelectionOverlayManager::testSetParentWidgetReparentsExistingOverlay() {
  // Build two real top-level widgets so the manager has somewhere to root
  // the overlay across the reparent step.
  QWidget firstParent;
  firstParent.resize(400, 300);
  firstParent.show();
  QWidget secondParent;
  secondParent.resize(400, 300);
  secondParent.show();

  SelectionOverlayManager mgr;
  mgr.setParentWidget(&firstParent);
  const QRect target(10, 10, 50, 50);
  mgr.showAtRect(target);
  QVERIFY(mgr.isVisible());
  QCOMPARE(mgr.geometry(), target);

  // setParentWidget calls QWidget::setParent on the existing overlay; Qt
  // hides the widget as a side-effect of reparenting, so isVisible() is
  // expected to flip to false. The overlay survives (geometry is still
  // populated) and a subsequent show would put it back on screen — which
  // is exactly the behaviour MainWindow needs when the items page is
  // re-attached to a different container.
  mgr.setParentWidget(&secondParent);
  QCOMPARE(mgr.geometry(), target);

  mgr.showAtRect(target);
  QVERIFY(mgr.isVisible());
}

void TestSelectionOverlayManager::testDestructionStopsRunningAnimation() {
  QWidget parent;
  parent.resize(400, 300);
  {
    SelectionOverlayManager mgr;
    mgr.setParentWidget(&parent);
    mgr.animateTo(QRect(0, 0, 100, 100), QRect(50, 50, 100, 100));
    // animateTo may have set up the QPropertyAnimation; whether it's
    // running depends on platform paint timing, but tearing down should
    // not crash even if it is.
  }
  // If we got here, the destructor's animation->stop() shut it down safely.
  QVERIFY(true);
}

void TestSelectionOverlayManager::testSetForceVisibleTogglesShouldKeepVisible() {
  SelectionOverlayManager mgr;
  QVERIFY(!mgr.isForceVisible());
  QVERIFY(!mgr.shouldKeepVisible());

  mgr.setForceVisible(true);
  QVERIFY(mgr.isForceVisible());
  QVERIFY(mgr.shouldKeepVisible());

  mgr.setForceVisible(false);
  QVERIFY(!mgr.isForceVisible());
  QVERIFY(!mgr.shouldKeepVisible());
}

void TestSelectionOverlayManager::testSetForceVisibleNoChangeIsIdempotent() {
  SelectionOverlayManager mgr;
  mgr.setForceVisible(true);
  mgr.setForceVisible(true);
  mgr.setForceVisible(true);
  QVERIFY(mgr.isForceVisible());
}

void TestSelectionOverlayManager::testShowAtRectAfterParentWidgetSet() {
  QWidget parent;
  parent.resize(500, 400);
  parent.show();

  SelectionOverlayManager mgr;
  mgr.setParentWidget(&parent);
  QVERIFY(!mgr.isVisible()); // overlay not yet created

  const QRect target(20, 30, 120, 80);
  mgr.showAtRect(target);
  QVERIFY(mgr.isVisible());
  QCOMPARE(mgr.geometry(), target);
}

void TestSelectionOverlayManager::testShowAtRectIgnoresInvalidRect() {
  QWidget parent;
  parent.resize(500, 400);
  parent.show();

  SelectionOverlayManager mgr;
  mgr.setParentWidget(&parent);
  // First a real show so we have an overlay to leave geometry on.
  const QRect first(0, 0, 50, 50);
  mgr.showAtRect(first);
  QCOMPARE(mgr.geometry(), first);

  // Invalid rect (zero size) is rejected — geometry stays as previous.
  mgr.showAtRect(QRect(0, 0, 0, 0));
  QCOMPARE(mgr.geometry(), first);
}

void TestSelectionOverlayManager::testHideHidesOverlay() {
  QWidget parent;
  parent.resize(400, 300);
  parent.show();

  SelectionOverlayManager mgr;
  mgr.setParentWidget(&parent);
  mgr.showAtRect(QRect(5, 5, 40, 40));
  QVERIFY(mgr.isVisible());

  mgr.hide();
  QVERIFY(!mgr.isVisible());
}

void TestSelectionOverlayManager::testIsVisibleFalseBeforeFirstShow() {
  QWidget parent;
  parent.resize(400, 300);

  SelectionOverlayManager mgr;
  mgr.setParentWidget(&parent);
  QVERIFY(!mgr.isVisible());

  // hide() before any show() is a no-op and must not crash
  mgr.hide();
  QVERIFY(!mgr.isVisible());
}

void TestSelectionOverlayManager::testSetGeometryReflectsImmediately() {
  QWidget parent;
  parent.resize(400, 300);
  parent.show();

  SelectionOverlayManager mgr;
  mgr.setParentWidget(&parent);
  mgr.showAtRect(QRect(10, 10, 50, 50));

  const QRect newGeom(100, 50, 200, 150);
  mgr.setGeometry(newGeom);
  QCOMPARE(mgr.geometry(), newGeom);
}

void TestSelectionOverlayManager::testGeometryReturnsEmptyBeforeOverlayCreated() {
  SelectionOverlayManager mgr;
  // No parent widget → no overlay → geometry() must return a null QRect
  // rather than dereferencing a null pointer.
  const QRect g = mgr.geometry();
  QVERIFY(g.isEmpty() || !g.isValid());
}

void TestSelectionOverlayManager::testAnimateToIgnoresInvalidTarget() {
  QWidget parent;
  parent.resize(400, 300);

  SelectionOverlayManager mgr;
  mgr.setParentWidget(&parent);

  mgr.animateTo(QRect());
  // Invalid target → no overlay was made visible.
  QVERIFY(!mgr.isAnimating());
}

void TestSelectionOverlayManager::testStopAnimationIsSafeWhenIdle() {
  SelectionOverlayManager mgr;
  // Called before any animation exists — must be a no-op.
  mgr.stopAnimation();
  QVERIFY(!mgr.isAnimating());
}
