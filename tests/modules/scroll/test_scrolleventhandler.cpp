// ScrollEventHandler — scroll-event wiring + user-scroll activity tracking.
//
// The handler owns the connect/disconnect lifecycle between a QScrollArea's
// scrollbars and ScrollManager, and tracks "the user is dragging/stepping the
// scrollbar" state (including the delayed clear of the shared
// InteractionStateHolder flag and the stop of any in-flight arrow-key
// animation). Driven against a real QScrollArea under the offscreen QPA;
// scrollbar signals are raised through the QAbstractSlider API
// (setSliderDown / setSliderPosition / triggerAction), not synthesized mouse
// input, so the cases are deterministic.

#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>

#include "interactionstateholder.h"
#include "scrolleventhandler.h"
#include "timerutils.h"
#include "uiconstants/mouse.h"

class TestScrollEventHandler : public QObject {
  Q_OBJECT

private slots:
  // Connection lifecycle
  void connectEvents_withoutScrollAreaIsNoOp();
  void connectEvents_forwardsVerticalValueChanges();
  void connectEvents_forwardsHorizontalValueChanges();
  void disconnectEvents_stopsValueChangeForwarding();
  void setScrollArea_disconnectsPreviousArea();

  // User-scroll activity tracking
  void sliderPress_marksUserScrollActiveAndEmits();
  void sliderRelease_emitsEndedAndClearsSharedStateAfterDelay();
  void actionTriggered_marksUserScrollActive();
  void sliderPress_firesIdleTimer();

  // Drag-position forwarding (vertical bar only — prefetch optimization)
  void sliderMoved_forwardsVerticalDragPosition();
  void sliderMoved_notConnectedForHorizontalBar();

  // Arrow-key animation interlock
  void sliderPress_stopsRunningArrowKeyAnimation();
};

namespace {

/// A scroll area whose vertical + horizontal bars have a real range, so
/// setValue()/setSliderPosition() produce genuine valueChanged/sliderMoved
/// emissions instead of clamping to 0.
struct AreaWithRange {
  QScrollArea area;
  AreaWithRange() {
    area.verticalScrollBar()->setRange(0, 1000);
    area.horizontalScrollBar()->setRange(0, 1000);
  }
};

} // namespace

void TestScrollEventHandler::connectEvents_withoutScrollAreaIsNoOp() {
  ScrollEventHandler handler;
  // No scroll area wired: connectEvents must return without touching
  // anything, and the activity state stays at its default.
  handler.connectEvents();
  handler.disconnectEvents();
  QVERIFY(!handler.isUserScrollActive());
}

void TestScrollEventHandler::connectEvents_forwardsVerticalValueChanges() {
  AreaWithRange host;
  ScrollEventHandler handler;
  handler.setScrollArea(&host.area);
  handler.connectEvents();

  QSignalSpy changed(&handler, &ScrollEventHandler::scrollChanged);
  host.area.verticalScrollBar()->setValue(10);
  QCOMPARE(changed.count(), 1);
  host.area.verticalScrollBar()->setValue(20);
  QCOMPARE(changed.count(), 2);
  // Same value again — QScrollBar suppresses the no-op, so no forward.
  host.area.verticalScrollBar()->setValue(20);
  QCOMPARE(changed.count(), 2);
}

void TestScrollEventHandler::connectEvents_forwardsHorizontalValueChanges() {
  AreaWithRange host;
  ScrollEventHandler handler;
  handler.setScrollArea(&host.area);
  handler.connectEvents();

  QSignalSpy changed(&handler, &ScrollEventHandler::scrollChanged);
  host.area.horizontalScrollBar()->setValue(15);
  QCOMPARE(changed.count(), 1);
}

void TestScrollEventHandler::disconnectEvents_stopsValueChangeForwarding() {
  AreaWithRange host;
  ScrollEventHandler handler;
  handler.setScrollArea(&host.area);
  handler.connectEvents();

  QSignalSpy changed(&handler, &ScrollEventHandler::scrollChanged);
  host.area.verticalScrollBar()->setValue(10);
  host.area.horizontalScrollBar()->setValue(10);
  QCOMPARE(changed.count(), 2);

  handler.disconnectEvents();
  host.area.verticalScrollBar()->setValue(30);
  host.area.horizontalScrollBar()->setValue(30);
  QCOMPARE(changed.count(), 2); // nothing forwarded after disconnect
}

void TestScrollEventHandler::setScrollArea_disconnectsPreviousArea() {
  AreaWithRange first;
  AreaWithRange second;
  ScrollEventHandler handler;
  handler.setScrollArea(&first.area);
  handler.connectEvents();

  QSignalSpy changed(&handler, &ScrollEventHandler::scrollChanged);
  first.area.verticalScrollBar()->setValue(10);
  QCOMPARE(changed.count(), 1);

  // Re-targeting must sever the old area's forwarding even before
  // connectEvents is called for the new one — otherwise a collection
  // switch would leave the torn-down view still driving scrollChanged.
  handler.setScrollArea(&second.area);
  first.area.verticalScrollBar()->setValue(50);
  QCOMPARE(changed.count(), 1);

  // And the new area only forwards once explicitly connected.
  second.area.verticalScrollBar()->setValue(5);
  QCOMPARE(changed.count(), 1);
  handler.connectEvents();
  second.area.verticalScrollBar()->setValue(25);
  QCOMPARE(changed.count(), 2);
}

void TestScrollEventHandler::sliderPress_marksUserScrollActiveAndEmits() {
  AreaWithRange host;
  InteractionStateHolder state;
  ScrollEventHandler handler;
  handler.setScrollArea(&host.area);
  handler.setInteractionState(&state);
  handler.connectEvents();

  QSignalSpy started(&handler, &ScrollEventHandler::userScrollStarted);
  QVERIFY(!handler.isUserScrollActive());

  host.area.verticalScrollBar()->setSliderDown(true); // emits sliderPressed
  QCOMPARE(started.count(), 1);
  QVERIFY(handler.isUserScrollActive());
  // The shared interaction flag is raised synchronously so consumers
  // (artwork prefetch, selection overlay) see the drag immediately.
  QVERIFY(state.scroll().userScrollActive);
}

void TestScrollEventHandler::sliderRelease_emitsEndedAndClearsSharedStateAfterDelay() {
  AreaWithRange host;
  InteractionStateHolder state;
  ScrollEventHandler handler;
  handler.setScrollArea(&host.area);
  handler.setInteractionState(&state);
  handler.connectEvents();

  host.area.verticalScrollBar()->setSliderDown(true);
  QVERIFY(handler.isUserScrollActive());

  QSignalSpy ended(&handler, &ScrollEventHandler::userScrollEnded);
  host.area.verticalScrollBar()->setSliderDown(false); // emits sliderReleased
  QCOMPARE(ended.count(), 1);
  // The handler's own flag drops immediately...
  QVERIFY(!handler.isUserScrollActive());
  // ...but the shared flag is cleared on a delayed single-shot
  // (USER_SCROLL_ACTIVE_CLEAR_DELAY_MS) so scroll events still in the queue
  // are processed with the flag raised.
  QVERIFY(state.scroll().userScrollActive);
  QTRY_VERIFY_WITH_TIMEOUT(!state.scroll().userScrollActive,
                           UIConstants::Mouse::USER_SCROLL_ACTIVE_CLEAR_DELAY_MS * 20);
}

void TestScrollEventHandler::actionTriggered_marksUserScrollActive() {
  AreaWithRange host;
  InteractionStateHolder state;
  ScrollEventHandler handler;
  handler.setScrollArea(&host.area);
  handler.setInteractionState(&state);
  handler.connectEvents();

  // A step action (track click / arrow button) counts as user scrolling
  // even though the slider is never "down".
  host.area.verticalScrollBar()->triggerAction(QAbstractSlider::SliderSingleStepAdd);
  QVERIFY(handler.isUserScrollActive());
  QVERIFY(state.scroll().userScrollActive);
}

void TestScrollEventHandler::sliderPress_firesIdleTimer() {
  AreaWithRange host;
  TimerUtils::DebouncedTimer idle(0);
  ScrollEventHandler handler;
  handler.setScrollArea(&host.area);
  handler.setIdleTimer(&idle);
  handler.connectEvents();

  QSignalSpy triggered(&idle, &TimerUtils::DebouncedTimer::triggered);
  host.area.verticalScrollBar()->setSliderDown(true);
  // trigger() schedules the debounced fire; a zero interval lands on the
  // next event-loop turn.
  QTRY_VERIFY(triggered.count() >= 1);
}

void TestScrollEventHandler::sliderMoved_forwardsVerticalDragPosition() {
  AreaWithRange host;
  ScrollEventHandler handler;
  handler.setScrollArea(&host.area);
  handler.connectEvents();

  QSignalSpy moved(&handler, &ScrollEventHandler::sliderMoved);
  auto *vBar = host.area.verticalScrollBar();
  vBar->setSliderDown(true);
  vBar->setSliderPosition(42); // emits sliderMoved while the slider is down
  QCOMPARE(moved.count(), 1);
  QCOMPARE(moved.at(0).at(0).toInt(), 42);
  vBar->setSliderDown(false);
}

void TestScrollEventHandler::sliderMoved_notConnectedForHorizontalBar() {
  AreaWithRange host;
  ScrollEventHandler handler;
  handler.setScrollArea(&host.area);
  handler.connectEvents();

  // Only the vertical bar drives the drag-prefetch path; a horizontal drag
  // must not masquerade as a vertical position.
  QSignalSpy moved(&handler, &ScrollEventHandler::sliderMoved);
  auto *hBar = host.area.horizontalScrollBar();
  hBar->setSliderDown(true);
  hBar->setSliderPosition(42);
  QCOMPARE(moved.count(), 0);
  hBar->setSliderDown(false);
}

void TestScrollEventHandler::sliderPress_stopsRunningArrowKeyAnimation() {
  AreaWithRange host;
  ScrollEventHandler handler;
  handler.setScrollArea(&host.area);
  handler.connectEvents();

  // ArrowKeyScrollHelper parks its glide animation on the scrollbar under
  // this well-known object name; grabbing the scrollbar mid-glide must stop
  // it so the drag and the animation don't fight over the value.
  auto *vBar = host.area.verticalScrollBar();
  auto *anim = new QPropertyAnimation(vBar, "value", vBar);
  anim->setObjectName(QStringLiteral("arrowKeyScrollAnim"));
  anim->setStartValue(0);
  anim->setEndValue(500);
  anim->setDuration(5000);
  anim->start();
  QCOMPARE(anim->state(), QAbstractAnimation::Running);

  vBar->setSliderDown(true);
  QCOMPARE(anim->state(), QAbstractAnimation::Stopped);
  vBar->setSliderDown(false);
}

QTEST_MAIN(TestScrollEventHandler)
#include "test_scrolleventhandler.moc"
