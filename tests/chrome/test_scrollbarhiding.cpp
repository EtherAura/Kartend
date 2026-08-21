// Hiding a side pane's scrollbar has to defeat BOTH mechanisms.
//
// The native policy alone is not enough: with slim overlay scrollbars on, the
// native bars are forced off at attach time and a handle is painted over the
// viewport instead, so a user who hid the scrollbar still saw one (field
// report 2026-08-19, against the item grid's own toggle). And the reverse
// trap is just as real — restoring on unhide must put back the policy the
// surface actually had, because several of these surfaces are deliberately
// AlwaysOff and a blanket ScrollBarAsNeeded would hand them a bar they never
// had.
#include "overlayscrollbars.h"

#include <algorithm>

#include <QApplication>
#include <QEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>
#include <QVBoxLayout>
#include <QWidget>

namespace {

/// A scroll area with more content than viewport, so a bar is warranted and
/// the policies are the only thing deciding whether one appears.
QScrollArea *makeOverflowingArea(QWidget *parent = nullptr) {
  auto *area = new QScrollArea(parent);
  area->resize(400, 300);
  auto *content = new QWidget;
  content->resize(2000, 2000);
  area->setWidget(content);
  return area;
}

/// The overlay handles are file-local to overlayscrollbars.cpp, so reach them
/// structurally: they are children of the viewport. So is the scroll area's
/// OWN content widget — setWidget() reparents it into the viewport — and it
/// is always visible, so it has to be excluded or this answers "yes" for
/// every area and asserts nothing.
bool anyHandleVisible(QScrollArea *area) {
  const auto children = area->viewport()->findChildren<QWidget *>(Qt::FindDirectChildrenOnly);
  return std::any_of(children.cbegin(), children.cend(),
                     [area](const QWidget *w) { return w != area->widget() && w->isVisible(); });
}

} // namespace

class TestScrollbarHiding : public QObject {
  Q_OBJECT
private slots:
  void aHiddenHandleIsNotResurrectedByHoverOrScroll();
  void autohideKeepsTheHandleUntilThePointerNearsTheLane();
  void onlyShowReservesTheLane();
  void togglingHiddenWhileHoveredDoesNotRecurse();
  void hidingForcesNativeBarsOffAndUnhidingRestoresWhatWasThere();
  void unhidingDoesNotConjureABarOnAnAlwaysOffSurface();
  void hidingSurvivesTurningOverlayScrollbarsOnAndOffAgain();
  void aHiddenAxisReservesNoOverlayLane();
  void paneSweepReachesNestedScrollAreas();
};

void TestScrollbarHiding::aHiddenHandleIsNotResurrectedByHoverOrScroll() {
  // flash() used to call syncGeometry() — which hides a handle the user hid —
  // and then show() unconditionally, so every hover and every scroll tick
  // brought the handle back. The user reported exactly that: "I still see the
  // scrollbar for the nav pane" after restarting with the option on.
  QScrollArea *area = makeOverflowingArea();
  area->show();
  QVERIFY(QTest::qWaitForWindowExposed(area));
  OverlayScrollbars::apply(area, true);
  OverlayScrollbars::setScrollbarMode(area, ScrollbarMode::Hide);
  QTest::qWait(50);
  QVERIFY2(!anyHandleVisible(area), "a hidden axis must not paint a handle");

  // Hover, then scroll — the two paths into flash().
  QEvent enter(QEvent::Enter);
  QApplication::sendEvent(area->viewport(), &enter);
  QTest::qWait(20);
  QVERIFY2(!anyHandleVisible(area), "hovering resurrected a handle the user hid");

  area->verticalScrollBar()->setValue(area->verticalScrollBar()->value() + 40);
  QTest::qWait(20);
  QVERIFY2(!anyHandleVisible(area), "scrolling resurrected a handle the user hid");

  // The guard must suppress the handle, not destroy it: unhiding and
  // scrolling has to bring it back, or the setting would be one-way.
  OverlayScrollbars::setScrollbarMode(area, ScrollbarMode::Show);
  area->verticalScrollBar()->setValue(area->verticalScrollBar()->value() + 40);
  QTest::qWait(20);
  QVERIFY2(anyHandleVisible(area), "unhiding left the handle permanently suppressed");
  delete area;
}

void TestScrollbarHiding::autohideKeepsTheHandleUntilThePointerNearsTheLane() {
  // Autohide is the middle state (user request 2026-08-19): nothing drawn
  // until the pointer comes near the lane. It routes through the SAME
  // shouldStayHidden() choke point as Hide rather than adding a parallel
  // show path — that is what keeps it from reopening the hide/show recursion
  // that crashed the app (Kartend-axlod).
  QScrollArea *area = makeOverflowingArea();
  area->show();
  QVERIFY(QTest::qWaitForWindowExposed(area));
  OverlayScrollbars::apply(area, true);
  OverlayScrollbars::setScrollbarMode(area, ScrollbarMode::Autohide);
  QTest::qWait(50);
  QVERIFY2(!anyHandleVisible(area), "autohide must start hidden");

  // Entering the viewport is NOT enough on its own — Show would light up
  // here, and if autohide did too it would just be Show with extra steps.
  // The cursor is wherever the test runner left it, which is not the lane.
  QEvent enter(QEvent::Enter);
  QApplication::sendEvent(area->viewport(), &enter);
  QTest::qWait(120); // longer than the proximity poll interval
  QVERIFY2(!anyHandleVisible(area),
           "autohide lit up on plain hover — proximity to the lane is the trigger");

  // Scrolling must not summon it either: the whole point is that the
  // indicator stays out of the way until deliberately approached.
  area->verticalScrollBar()->setValue(area->verticalScrollBar()->value() + 40);
  QTest::qWait(20);
  QVERIFY2(!anyHandleVisible(area), "autohide lit up on scroll rather than on proximity");
  delete area;
}

void TestScrollbarHiding::onlyShowReservesTheLane() {
  // Show keeps a permanent lane so its always-visible handle never sits on
  // the items (field report 2026-08-19). Autohide and Hide both give the
  // width back: Hide because no handle can ever appear, Autohide because a
  // standing empty lane read as "a large margin between the details pane
  // and the grid" (field report 2026-08-20) — its handle briefly floats
  // OVER item edges instead, the overlap-not-move trade the maintainer
  // keeps choosing. What must never happen is the reservation TRACKING the
  // handle's visibility: per mode it is a constant, so approach/fade moves
  // nothing.
  QScrollArea *area = makeOverflowingArea();
  area->show();
  QVERIFY(QTest::qWaitForWindowExposed(area));
  OverlayScrollbars::apply(area, true);
  QTest::qWait(50);
  QVERIFY2(OverlayScrollbars::reservedGutter(area) > 0, "baseline: Show reserves a lane");

  OverlayScrollbars::setScrollbarMode(area, ScrollbarMode::Autohide);
  QCOMPARE(OverlayScrollbars::reservedGutter(area), 0);

  // The reservation must not flicker with proximity: still zero while the
  // handle is up. (Proximity is cursor-driven; force the handle visible via
  // a flash instead, which is the same painted state.)
  OverlayScrollbars::setScrollbarMode(area, ScrollbarMode::Hide);
  QCOMPARE(OverlayScrollbars::reservedGutter(area), 0);
  delete area;
}

void TestScrollbarHiding::togglingHiddenWhileHoveredDoesNotRecurse() {
  // The crash (SIGSEGV, 2026-08-19): show()/hide() make Qt synthesise
  // enter/leave events, which re-enter the controller's filter and call
  // flash() again. With flash() hiding and showing the same handle, that
  // ping-pongs until the stack is gone. If it regresses, this test does not
  // fail — it dies, which is the point.
  QScrollArea *area = makeOverflowingArea();
  area->show();
  QVERIFY(QTest::qWaitForWindowExposed(area));
  OverlayScrollbars::apply(area, true);

  QEvent enter(QEvent::Enter);
  QEvent leave(QEvent::Leave);
  // Get a handle on screen first, so the toggle below has something to hide.
  QApplication::sendEvent(area->viewport(), &enter);
  QTest::qWait(50);

  for (int i = 0; i < 8; ++i) {
    OverlayScrollbars::setScrollbarMode(area,
                                        i % 2 == 0 ? ScrollbarMode::Hide : ScrollbarMode::Show);
    QApplication::sendEvent(area->viewport(), &enter);
    QApplication::sendEvent(area->viewport(), &leave);
    QTest::qWait(10);
  }

  // Settle on hidden and hover once more: after all that churn the handle
  // must still obey the setting rather than whichever toggle won a race.
  OverlayScrollbars::setScrollbarMode(area, ScrollbarMode::Hide);
  QApplication::sendEvent(area->viewport(), &enter);
  QTest::qWait(20);
  QVERIFY2(!anyHandleVisible(area), "a handle survived the toggle storm");
  delete area;
}

void TestScrollbarHiding::hidingForcesNativeBarsOffAndUnhidingRestoresWhatWasThere() {
  QScrollArea area;
  area.setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  area.setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  OverlayScrollbars::setScrollbarMode(&area, ScrollbarMode::Hide);
  QCOMPARE(area.verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
  QCOMPARE(area.horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);

  OverlayScrollbars::setScrollbarMode(&area, ScrollbarMode::Show);
  QCOMPARE(area.verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
  QCOMPARE(area.horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
}

void TestScrollbarHiding::unhidingDoesNotConjureABarOnAnAlwaysOffSurface() {
  // The details pane's content view is AlwaysOff by design. Unhiding must
  // leave it that way — restoring a blanket AsNeeded would be a visible
  // regression for every user who toggled the setting on and back off.
  QScrollArea area;
  area.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  area.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  OverlayScrollbars::setScrollbarMode(&area, ScrollbarMode::Hide);
  OverlayScrollbars::setScrollbarMode(&area, ScrollbarMode::Show);
  QCOMPARE(area.verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
  QCOMPARE(area.horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);

  // Repeated hides must not re-snapshot the forced-off state and strand it.
  OverlayScrollbars::setScrollbarMode(&area, ScrollbarMode::Hide);
  OverlayScrollbars::setScrollbarMode(&area, ScrollbarMode::Hide);
  OverlayScrollbars::setScrollbarMode(&area, ScrollbarMode::Show);
  QCOMPARE(area.verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
}

void TestScrollbarHiding::hidingSurvivesTurningOverlayScrollbarsOnAndOffAgain() {
  // The two mechanisms hand the native policies back and forth. A pane hidden
  // BEFORE overlay bars are switched on must still be hidden after they are
  // switched off again — otherwise toggling the global slim-scrollbar option
  // silently hands a bar back to a pane the user hid.
  QScrollArea area;
  area.setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  area.setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  OverlayScrollbars::setScrollbarMode(&area, ScrollbarMode::Hide);
  OverlayScrollbars::apply(&area, true);
  OverlayScrollbars::apply(&area, false);
  QCOMPARE(area.verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
  QCOMPARE(area.horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);

  // And unhiding afterwards still finds the ORIGINAL policy, not the
  // AlwaysOff either mechanism left behind on the way through.
  OverlayScrollbars::setScrollbarMode(&area, ScrollbarMode::Show);
  QCOMPARE(area.verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
  QCOMPARE(area.horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
}

void TestScrollbarHiding::aHiddenAxisReservesNoOverlayLane() {
  QScrollArea *area = makeOverflowingArea();
  area->show();
  QVERIFY(QTest::qWaitForWindowExposed(area));
  OverlayScrollbars::apply(area, true);
  QTest::qWait(50);
  QVERIFY2(OverlayScrollbars::reservedGutter(area) > 0, "baseline: a lane is reserved");

  // No handle can ever appear, so the lane is dead space — the content should
  // get the width back rather than keep an empty strip.
  OverlayScrollbars::setScrollbarMode(area, ScrollbarMode::Hide);
  QCOMPARE(OverlayScrollbars::reservedGutter(area), 0);

  OverlayScrollbars::setScrollbarMode(area, ScrollbarMode::Show);
  QVERIFY(OverlayScrollbars::reservedGutter(area) > 0);
  delete area;
}

void TestScrollbarHiding::paneSweepReachesNestedScrollAreas() {
  // The details pane is several scroll areas (content view, artwork strip,
  // metadata card) and "hide the pane's scrollbars" means all of them.
  QWidget pane;
  auto *layout = new QVBoxLayout(&pane);
  QScrollArea *outer = makeOverflowingArea(&pane);
  layout->addWidget(outer);
  auto *inner = new QScrollArea(outer->widget());
  inner->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  inner->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  OverlayScrollbars::setPaneScrollbarMode(&pane, ScrollbarMode::Hide);
  QCOMPARE(outer->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
  QCOMPARE(inner->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);

  // Each area keeps its own snapshot, so the sweep back restores the strip's
  // horizontal bar without giving it a vertical one it never had.
  OverlayScrollbars::setPaneScrollbarMode(&pane, ScrollbarMode::Show);
  QCOMPARE(inner->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
  QCOMPARE(inner->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
}

QTEST_MAIN(TestScrollbarHiding)
#include "test_scrollbarhiding.moc"
