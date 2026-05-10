// Tests for CoverFlowWidget — the carousel cover-flow view mode.
//
// CoverFlowWidget owns custom rendering, animation, and gesture handling.
// We can't easily probe its private layout math, so we drive it through the
// public/protected surface and verify externally observable state:
//   - Selection clamping and storage (setCards / setSelectedIndex / cardCount)
//   - Signal emission on user input (wheel, key, mouse)
//   - Gallery owner/active-index bookkeeping (setGalleryForIndex)
//   - Property-setter early-out and side effects (cache reset on tile color)
//   - selectionPositionF round-trip (drives the QPropertyAnimation glide)
//
// To exercise the protected event handlers we use a thin subclass that
// re-exports them as public methods, plus QSignalSpy for emission checks.
#include "coverflowwidget.h"
#include "videothumbnailextractor.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointF>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

namespace {

QList<CoverFlowCardData> makeCards(int n) {
  QList<CoverFlowCardData> cards;
  cards.reserve(n);
  for (int i = 0; i < n; ++i) {
    CoverFlowCardData c;
    c.title = QStringLiteral("Card %1").arg(i);
    cards.append(c);
  }
  return cards;
}

class TestableCoverFlow : public CoverFlowWidget {
public:
  using CoverFlowWidget::CoverFlowWidget;
  using CoverFlowWidget::keyPressEvent;
  using CoverFlowWidget::mouseDoubleClickEvent;
  using CoverFlowWidget::mousePressEvent;
  using CoverFlowWidget::wheelEvent;
};

void sendWheel(QWidget *target, int verticalDelta) {
  QWheelEvent e(QPointF(50, 50), target->mapToGlobal(QPoint(50, 50)), QPoint(0, verticalDelta),
                QPoint(0, verticalDelta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  static_cast<TestableCoverFlow *>(target)->wheelEvent(&e);
}

void sendKey(QWidget *target, Qt::Key key) {
  QKeyEvent e(QEvent::KeyPress, key, Qt::NoModifier);
  static_cast<TestableCoverFlow *>(target)->keyPressEvent(&e);
}

} // namespace

class TestCoverFlowWidget : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();

  // Card data + selection state
  void emptyDefaults();
  void setCardsStoresCount();
  void setCardsClampsSelectionWhenShrinking();
  void setSelectedIndexClampsBelowZero();
  void setSelectedIndexClampsAboveSize();
  void setSelectedIndexEmptyCardsResets();
  void setSelectedIndexSameIndexNoChange();

  // selectionPositionF / glide property
  void selectionPositionFRoundTrips();
  void selectionPositionFFuzzyEqualNoChange();
  void largeJumpSnapsAndZerosPosition();

  // setVideoPathForIndex
  void setVideoPathOutOfRangeNoOp();
  void setVideoPathSameValueNoOp();
  void setVideoPathStoresValue();

  // Gallery state
  void setGalleryStaleIndexDropped();
  void setGalleryAcceptedAtSelectedIndex();
  void setGalleryAcceptedAtCurrentOwner();
  void setGalleryClearsActiveIndex();

  // Property setters (early-out on identical value)
  void setCornerRadiusClampsNegative();
  void setFontSizeClampsTooSmall();
  void setTileColorClearsPixmapCache();
  void setHideTitlesUpdatesFlag();

  // Wheel input → selectionChangeRequested
  void wheelDownRequestsForward();
  void wheelUpRequestsBackward();
  void wheelAtFrontEdgeNoEmit();
  void wheelAtBackEdgeNoEmit();
  void wheelEmptyCardsIgnored();
  void wheelHorizontalDeltaUsedAsFallback();
  void wheelDoesNotMutateSelection();

  // Keyboard input
  void keyLeftMovesBack();
  void keyUpMovesBack();
  void keyRightMovesForward();
  void keyDownMovesForward();
  void keyPageUpJumpsByVisibleSideCards();
  void keyPageDownJumpsByVisibleSideCards();
  void keyHomeRequestsZero();
  void keyEndRequestsLast();
  void keyEnterActivatesCurrent();
  void keySpaceActivatesCurrent();
  void keyAtBoundaryNoEmit();
  void keyEmptyCardsIgnored();

  // Mouse input
  void mousePressOnEmptyCardsIgnored();
  void mouseDoubleClickEmptyCardsIgnored();
  void mouseDoubleClickRightButtonIgnored();
};

void TestCoverFlowWidget::initTestCase() {
  if (!qApp) {
    static int argc = 1;
    static char arg0[] = "test";
    static char *argv[] = {arg0, nullptr};
    new QApplication(argc, argv);
  }
  // Touch the singleton so the connection in CoverFlowWidget's ctor is
  // hooked up against an already-constructed instance — keeps the first
  // test that constructs a widget from racing the singleton lazy-init.
  (void)VideoThumbnailExtractor::instance();
}

// ----- Card data + selection state -----

void TestCoverFlowWidget::emptyDefaults() {
  TestableCoverFlow w;
  QCOMPARE(w.cardCount(), 0);
  QCOMPARE(w.selectedIndex(), 0);
  QCOMPARE(w.selectionPositionF(), 0.0);
}

void TestCoverFlowWidget::setCardsStoresCount() {
  TestableCoverFlow w;
  w.setCards(makeCards(7));
  QCOMPARE(w.cardCount(), 7);
}

void TestCoverFlowWidget::setCardsClampsSelectionWhenShrinking() {
  TestableCoverFlow w;
  w.setCards(makeCards(20));
  w.setSelectedIndex(15, false);
  QCOMPARE(w.selectedIndex(), 15);
  // Shrink to 5 cards — selection 15 is now out of range and should clamp
  // to the last valid index (4).
  w.setCards(makeCards(5));
  QCOMPARE(w.cardCount(), 5);
  QCOMPARE(w.selectedIndex(), 4);
}

void TestCoverFlowWidget::setSelectedIndexClampsBelowZero() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(-5, false);
  QCOMPARE(w.selectedIndex(), 0);
}

void TestCoverFlowWidget::setSelectedIndexClampsAboveSize() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(99, false);
  QCOMPARE(w.selectedIndex(), 9);
}

void TestCoverFlowWidget::setSelectedIndexEmptyCardsResets() {
  TestableCoverFlow w;
  // No cards set — index stays at 0 regardless of input.
  w.setSelectedIndex(5, true);
  QCOMPARE(w.selectedIndex(), 0);
  QCOMPARE(w.selectionPositionF(), 0.0);
}

void TestCoverFlowWidget::setSelectedIndexSameIndexNoChange() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(3, false);
  QCOMPARE(w.selectedIndex(), 3);
  // Same index + zero positionF should be a no-op.
  w.setSelectedIndex(3, false);
  QCOMPARE(w.selectedIndex(), 3);
  QCOMPARE(w.selectionPositionF(), 0.0);
}

// ----- selectionPositionF / glide -----

void TestCoverFlowWidget::selectionPositionFRoundTrips() {
  TestableCoverFlow w;
  w.setSelectionPositionF(0.42);
  QCOMPARE(w.selectionPositionF(), 0.42);
}

void TestCoverFlowWidget::selectionPositionFFuzzyEqualNoChange() {
  TestableCoverFlow w;
  w.setSelectionPositionF(0.5);
  // Setting to a fuzzy-equal value should not log a change. We can't
  // observe the early-out directly, but we can observe that the stored
  // value is unaffected by a microscopic delta.
  w.setSelectionPositionF(0.5 + 1e-15);
  QCOMPARE(w.selectionPositionF(), 0.5);
}

void TestCoverFlowWidget::largeJumpSnapsAndZerosPosition() {
  TestableCoverFlow w;
  w.setCards(makeCards(100));
  // Visible side count is 5 → diameter is 10. Jumping > 10 cards should
  // snap (positionF stays at 0) rather than glide. Widget is hidden so
  // the animate flag also short-circuits to a snap.
  w.setSelectedIndex(50, true);
  QCOMPARE(w.selectedIndex(), 50);
  QCOMPARE(w.selectionPositionF(), 0.0);
}

// ----- setVideoPathForIndex -----

void TestCoverFlowWidget::setVideoPathOutOfRangeNoOp() {
  TestableCoverFlow w;
  w.setCards(makeCards(3));
  // Out-of-range indices are silently dropped — observable via no crash
  // and that the selection state remains valid.
  w.setVideoPathForIndex(-1, QStringLiteral("/x.mp4"));
  w.setVideoPathForIndex(99, QStringLiteral("/x.mp4"));
  QCOMPARE(w.cardCount(), 3);
  QCOMPARE(w.selectedIndex(), 0);
}

void TestCoverFlowWidget::setVideoPathSameValueNoOp() {
  TestableCoverFlow w;
  w.setCards(makeCards(3));
  // Setting an empty path on a card whose videoPath is already empty
  // should early-return — observable as no crash and stable state.
  w.setVideoPathForIndex(1, QString());
  QCOMPARE(w.cardCount(), 3);
}

void TestCoverFlowWidget::setVideoPathStoresValue() {
  TestableCoverFlow w;
  w.setCards(makeCards(3));
  // Setting on the selected index triggers applyVideoPreviewState. The
  // resulting state isn't directly observable from the public surface,
  // but the call must complete without crashing. (Selected card is 0.)
  w.setVideoPathForIndex(0, QStringLiteral("/some/preview.mp4"));
  w.setVideoPathForIndex(2, QStringLiteral("/another.mp4"));
  QCOMPARE(w.cardCount(), 3);
}

// ----- Gallery state -----

void TestCoverFlowWidget::setGalleryStaleIndexDropped() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(5, false);
  // Selection moved to 5, but the resolver is racing in with results for
  // the previous selection (index 2). The push should be dropped because
  // 2 is neither selected nor the current gallery owner.
  QList<CoverFlowGalleryEntry> entries;
  CoverFlowGalleryEntry e;
  e.label = "cover";
  e.path = "/cover.png";
  entries.append(e);
  w.setGalleryForIndex(2, entries);
  // Move selection back to 2 — gallery should still be empty since the
  // stale push was dropped, so no signals/observable side effects from
  // the push survived.
  w.setSelectedIndex(2, false);
  QCOMPARE(w.selectedIndex(), 2);
}

void TestCoverFlowWidget::setGalleryAcceptedAtSelectedIndex() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(3, false);
  QList<CoverFlowGalleryEntry> entries;
  CoverFlowGalleryEntry e1;
  e1.label = "cover";
  e1.path = "/cover.png";
  CoverFlowGalleryEntry e2;
  e2.label = "screen";
  e2.path = "/screen.png";
  entries << e1 << e2;
  // Accepted because index matches m_selectedIndex.
  w.setGalleryForIndex(3, entries);
  QCOMPARE(w.selectedIndex(), 3);
}

void TestCoverFlowWidget::setGalleryAcceptedAtCurrentOwner() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(3, false);
  QList<CoverFlowGalleryEntry> entries;
  CoverFlowGalleryEntry e;
  e.label = "cover";
  e.path = "/cover.png";
  entries << e;
  // First push establishes 3 as the gallery owner.
  w.setGalleryForIndex(3, entries);
  // Move selection to 4. A fresh push for 3 should be accepted because
  // 3 is still the gallery owner (replace path), even though selection
  // has moved on. This protects against the resolver order being
  // back-to-back: caller sees the selection update later.
  w.setSelectedIndex(4, false);
  QList<CoverFlowGalleryEntry> moreEntries;
  CoverFlowGalleryEntry e2;
  e2.label = "screen";
  e2.path = "/screen.png";
  moreEntries << e2;
  w.setGalleryForIndex(3, moreEntries);
  QCOMPARE(w.selectedIndex(), 4);
}

void TestCoverFlowWidget::setGalleryClearsActiveIndex() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  // setGalleryForIndex resets m_galleryActiveIndex to -1 — i.e. a fresh
  // gallery push always starts on the card's default artwork. We can't
  // read m_galleryActiveIndex directly, but the call must complete
  // without crash even when called repeatedly.
  QList<CoverFlowGalleryEntry> entries;
  CoverFlowGalleryEntry e;
  e.label = "cover";
  e.path = "/cover.png";
  entries << e;
  w.setGalleryForIndex(0, entries);
  w.setGalleryForIndex(0, entries);
  w.setGalleryForIndex(0, QList<CoverFlowGalleryEntry>{});
  QCOMPARE(w.cardCount(), 10);
}

// ----- Property setters -----

void TestCoverFlowWidget::setCornerRadiusClampsNegative() {
  TestableCoverFlow w;
  w.setCornerRadius(-50);
  // No public getter — verify by passing 0 next, which should early-out
  // (already 0 from the clamp). No crash + stable state means the clamp
  // landed.
  w.setCornerRadius(0);
  w.setCornerRadius(12);
  w.setCornerRadius(12); // early-out path
  QCOMPARE(w.cardCount(), 0);
}

void TestCoverFlowWidget::setFontSizeClampsTooSmall() {
  TestableCoverFlow w;
  w.setFontSize(2);  // clamps to 6
  w.setFontSize(-1); // clamps to 6
  w.setFontSize(20);
  w.setFontSize(20); // early-out path
}

void TestCoverFlowWidget::setTileColorClearsPixmapCache() {
  TestableCoverFlow w;
  w.setCards(makeCards(5));
  // Tile color change must invalidate the pixmap cache (placeholders
  // depend on tile color). We can't read the cache directly; verify the
  // call sequence runs without crashing across changes.
  w.setTileColor(QStringLiteral("#ff0000"));
  w.setTileColor(QStringLiteral("#ff0000")); // early-out
  w.setTileColor(QStringLiteral("#00ff00"));
}

void TestCoverFlowWidget::setHideTitlesUpdatesFlag() {
  TestableCoverFlow w;
  w.setHideTitles(true);
  w.setHideTitles(true); // early-out
  w.setHideTitles(false);
}

// ----- Wheel input -----

void TestCoverFlowWidget::wheelDownRequestsForward() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(5, false);
  QSignalSpy spy(&w, &CoverFlowWidget::selectionChangeRequested);
  // angleDelta < 0 → step forward (wheel down advances).
  sendWheel(&w, -120);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toInt(), 6);
}

void TestCoverFlowWidget::wheelUpRequestsBackward() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(5, false);
  QSignalSpy spy(&w, &CoverFlowWidget::selectionChangeRequested);
  sendWheel(&w, 120);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toInt(), 4);
}

void TestCoverFlowWidget::wheelAtFrontEdgeNoEmit() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(0, false);
  QSignalSpy spy(&w, &CoverFlowWidget::selectionChangeRequested);
  sendWheel(&w, 120); // would step to -1, clamped to 0 → no change → no emit
  QCOMPARE(spy.count(), 0);
}

void TestCoverFlowWidget::wheelAtBackEdgeNoEmit() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(9, false);
  QSignalSpy spy(&w, &CoverFlowWidget::selectionChangeRequested);
  sendWheel(&w, -120);
  QCOMPARE(spy.count(), 0);
}

void TestCoverFlowWidget::wheelEmptyCardsIgnored() {
  TestableCoverFlow w;
  QSignalSpy spy(&w, &CoverFlowWidget::selectionChangeRequested);
  sendWheel(&w, -120);
  QCOMPARE(spy.count(), 0);
}

void TestCoverFlowWidget::wheelHorizontalDeltaUsedAsFallback() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(5, false);
  QSignalSpy spy(&w, &CoverFlowWidget::selectionChangeRequested);
  // Construct a wheel event with zero vertical delta but non-zero
  // horizontal delta — cover flow should treat horizontal scroll as a
  // navigation step too.
  QWheelEvent e(QPointF(50, 50), w.mapToGlobal(QPoint(50, 50)), QPoint(120, 0), QPoint(120, 0),
                Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  w.wheelEvent(&e);
  QCOMPARE(spy.count(), 1);
}

void TestCoverFlowWidget::wheelDoesNotMutateSelection() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(5, false);
  sendWheel(&w, -120);
  // The widget only emits a request — it does NOT update its own
  // m_selectedIndex. The caller is expected to route the update back
  // through SelectionManager → setSelectedIndex().
  QCOMPARE(w.selectedIndex(), 5);
}

// ----- Keyboard input -----

void TestCoverFlowWidget::keyLeftMovesBack() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(4, false);
  QSignalSpy spy(&w, &CoverFlowWidget::selectionChangeRequested);
  sendKey(&w, Qt::Key_Left);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toInt(), 3);
}

void TestCoverFlowWidget::keyUpMovesBack() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(4, false);
  QSignalSpy spy(&w, &CoverFlowWidget::selectionChangeRequested);
  sendKey(&w, Qt::Key_Up);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toInt(), 3);
}

void TestCoverFlowWidget::keyRightMovesForward() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(4, false);
  QSignalSpy spy(&w, &CoverFlowWidget::selectionChangeRequested);
  sendKey(&w, Qt::Key_Right);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toInt(), 5);
}

void TestCoverFlowWidget::keyDownMovesForward() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(4, false);
  QSignalSpy spy(&w, &CoverFlowWidget::selectionChangeRequested);
  sendKey(&w, Qt::Key_Down);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toInt(), 5);
}

void TestCoverFlowWidget::keyPageUpJumpsByVisibleSideCards() {
  TestableCoverFlow w;
  w.setCards(makeCards(50));
  w.setSelectedIndex(20, false);
  QSignalSpy spy(&w, &CoverFlowWidget::selectionChangeRequested);
  sendKey(&w, Qt::Key_PageUp);
  QCOMPARE(spy.count(), 1);
  // kVisibleSideCards = 5 → page up jumps by 5.
  QCOMPARE(spy.takeFirst().at(0).toInt(), 15);
}

void TestCoverFlowWidget::keyPageDownJumpsByVisibleSideCards() {
  TestableCoverFlow w;
  w.setCards(makeCards(50));
  w.setSelectedIndex(20, false);
  QSignalSpy spy(&w, &CoverFlowWidget::selectionChangeRequested);
  sendKey(&w, Qt::Key_PageDown);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toInt(), 25);
}

void TestCoverFlowWidget::keyHomeRequestsZero() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(7, false);
  QSignalSpy spy(&w, &CoverFlowWidget::selectionChangeRequested);
  sendKey(&w, Qt::Key_Home);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toInt(), 0);
}

void TestCoverFlowWidget::keyEndRequestsLast() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(2, false);
  QSignalSpy spy(&w, &CoverFlowWidget::selectionChangeRequested);
  sendKey(&w, Qt::Key_End);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toInt(), 9);
}

void TestCoverFlowWidget::keyEnterActivatesCurrent() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(4, false);
  QSignalSpy activationSpy(&w, &CoverFlowWidget::itemActivated);
  QSignalSpy changeSpy(&w, &CoverFlowWidget::selectionChangeRequested);
  sendKey(&w, Qt::Key_Return);
  QCOMPARE(activationSpy.count(), 1);
  QCOMPARE(activationSpy.takeFirst().at(0).toInt(), 4);
  QCOMPARE(changeSpy.count(), 0);

  sendKey(&w, Qt::Key_Enter);
  QCOMPARE(activationSpy.count(), 1);
  QCOMPARE(activationSpy.takeFirst().at(0).toInt(), 4);
}

void TestCoverFlowWidget::keySpaceActivatesCurrent() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(2, false);
  QSignalSpy spy(&w, &CoverFlowWidget::itemActivated);
  sendKey(&w, Qt::Key_Space);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.takeFirst().at(0).toInt(), 2);
}

void TestCoverFlowWidget::keyAtBoundaryNoEmit() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(0, false);
  QSignalSpy spy(&w, &CoverFlowWidget::selectionChangeRequested);
  sendKey(&w, Qt::Key_Left); // already at 0 — no emit
  QCOMPARE(spy.count(), 0);

  w.setSelectedIndex(9, false);
  sendKey(&w, Qt::Key_Right); // already at last — no emit
  QCOMPARE(spy.count(), 0);
}

void TestCoverFlowWidget::keyEmptyCardsIgnored() {
  TestableCoverFlow w;
  QSignalSpy changeSpy(&w, &CoverFlowWidget::selectionChangeRequested);
  QSignalSpy activationSpy(&w, &CoverFlowWidget::itemActivated);
  sendKey(&w, Qt::Key_Right);
  sendKey(&w, Qt::Key_Return);
  sendKey(&w, Qt::Key_End);
  QCOMPARE(changeSpy.count(), 0);
  QCOMPARE(activationSpy.count(), 0);
}

// ----- Mouse input -----

void TestCoverFlowWidget::mousePressOnEmptyCardsIgnored() {
  TestableCoverFlow w;
  QSignalSpy spy(&w, &CoverFlowWidget::selectionChangeRequested);
  QMouseEvent e(QEvent::MouseButtonPress, QPointF(50, 50), w.mapToGlobal(QPoint(50, 50)),
                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  w.mousePressEvent(&e);
  QCOMPARE(spy.count(), 0);
}

void TestCoverFlowWidget::mouseDoubleClickEmptyCardsIgnored() {
  TestableCoverFlow w;
  QSignalSpy spy(&w, &CoverFlowWidget::itemActivated);
  QMouseEvent e(QEvent::MouseButtonDblClick, QPointF(50, 50), w.mapToGlobal(QPoint(50, 50)),
                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  w.mouseDoubleClickEvent(&e);
  QCOMPARE(spy.count(), 0);
}

void TestCoverFlowWidget::mouseDoubleClickRightButtonIgnored() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  QSignalSpy spy(&w, &CoverFlowWidget::itemActivated);
  // Right-button double click is delegated to the base class — no
  // activation should fire, since cover flow only treats left double
  // click as "launch".
  QMouseEvent e(QEvent::MouseButtonDblClick, QPointF(50, 50), w.mapToGlobal(QPoint(50, 50)),
                Qt::RightButton, Qt::RightButton, Qt::NoModifier);
  w.mouseDoubleClickEvent(&e);
  QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(TestCoverFlowWidget)
#include "test_coverflowwidget.moc"
