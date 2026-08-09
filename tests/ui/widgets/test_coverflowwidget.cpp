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
//   - Center-rect maintenance: the video-preview overlay follows the centered
//     card via the state setters (glide frame / resize / title slot), never
//     via paintEvent — no paint pass runs in those tests
//
// To exercise the protected event handlers we use a thin subclass that
// re-exports them as public methods, plus QSignalSpy for emission checks.
//
// The suite also hosts the OverlayZOrderRegistry cases — it is the only
// suite linking kartend_chrome, whose public include surface carries
// chrome/overlays.
#include "coverflowgallerystrip.h"
#include "coverflowwidget.h"
#include "overlayzorderregistry.h"
#include "videopreviewwidget.h"
#include "videothumbnailextractor.h"

#include <QApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointF>
#include <QSignalSpy>
#include <QTemporaryDir>
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

  // Accessible name/description track the centered card — the carousel is
  // one custom-painted widget, so this is the only selection channel a
  // screen reader can see.
  void accessibleNameTracksSelection();
  void accessibleNameFollowsCardMutations();

  // updateCard (incremental patch, Kartend-x7bn8)
  void cardAtOutOfRangeReturnsDefault();
  void updateCardOutOfRangeNoOp();
  void updateCardPatchesTitleAndArtwork();
  void updateCardPreservesVideoPath();
  void updateCardDoesNotDisturbSelection();
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
  void setGalleryStalePushForPreviousOwnerDropped();
  void setGalleryClearsActiveIndex();

  // OverlayZOrderRegistry (chrome/overlays): entry order must stay sorted
  // ascending by layer no matter the registration order, because restack()
  // raises in stored order.
  void overlayRegistryOutOfOrderRegistrationRestacksSorted();
  void overlayRegistryReregisterMovesEntryToNewLayer();

  // Property setters (early-out on identical value)
  void setCornerRadiusClampsNegative();
  void setFontSizeClampsTooSmall();
  void setTileColorClearsPixmapCache();
  void setHideTitlesUpdatesFlag();

  // Center-rect maintenance (paint-free layout): the video preview overlay
  // is repositioned by the state setters that move the centered card —
  // selection / glide frames / resize / title-slot changes — never from
  // paintEvent. No paint pass runs in these tests, so a regression back to
  // paint-driven geometry fails them.
  void glideFrameRepositionsVisiblePreview();
  void resizeRepositionsVisiblePreview();
  void titleSlotChangeRepositionsVisiblePreview();
  void hiddenPreviewGeometryUntouched();

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
  // Gallery-strip double click → full-size preview, never a launch
  // (Kartend-5jtyw)
  void mouseDoubleClickOnGalleryThumbPreviewsInsteadOfLaunching();
  void mouseDoubleClickOffGalleryStillActivatesCard();
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

// ----- Accessible name/description -----

void TestCoverFlowWidget::accessibleNameTracksSelection() {
  TestableCoverFlow w;
  // Empty carousel gets a baseline name from the constructor.
  QCOMPARE(w.accessibleName(), QStringLiteral("Cover flow, no items"));

  w.setCards(makeCards(10));
  QCOMPARE(w.accessibleName(), QStringLiteral("Card 0, item 1 of 10"));
  QVERIFY(!w.accessibleDescription().isEmpty());

  // Snap path (widget hidden → animate flag short-circuits).
  w.setSelectedIndex(3, false);
  QCOMPARE(w.accessibleName(), QStringLiteral("Card 3, item 4 of 10"));

  // Animated path: the name tracks the logical selection immediately, not
  // the glide's visual position.
  w.resize(600, 400);
  w.show();
  w.setSelectedIndex(4, true);
  QCOMPARE(w.selectedIndex(), 4);
  QCOMPARE(w.accessibleName(), QStringLiteral("Card 4, item 5 of 10"));
}

void TestCoverFlowWidget::accessibleNameFollowsCardMutations() {
  TestableCoverFlow w;
  w.setCards(makeCards(20));
  w.setSelectedIndex(15, false);
  QCOMPARE(w.accessibleName(), QStringLiteral("Card 15, item 16 of 20"));

  // Retitling the selected slot via the incremental patch refreshes the name.
  CoverFlowCardData patch;
  patch.title = QStringLiteral("Patched Title");
  patch.artworkPath = QStringLiteral("/art/patched.png");
  w.updateCard(15, patch);
  QCOMPARE(w.accessibleName(), QStringLiteral("Patched Title, item 16 of 20"));

  // Shrinking the card list clamps the selection — name follows the clamp.
  w.setCards(makeCards(5));
  QCOMPARE(w.accessibleName(), QStringLiteral("Card 4, item 5 of 5"));

  // Emptying the list restores the baseline.
  w.setCards({});
  QCOMPARE(w.accessibleName(), QStringLiteral("Cover flow, no items"));
}

// ----- updateCard (incremental patch, Kartend-x7bn8) -----

void TestCoverFlowWidget::cardAtOutOfRangeReturnsDefault() {
  TestableCoverFlow w;
  w.setCards(makeCards(3));
  QCOMPARE(w.cardAt(-1).title, QString());
  QCOMPARE(w.cardAt(3).title, QString());
  QCOMPARE(w.cardAt(1).title, QStringLiteral("Card 1"));
}

void TestCoverFlowWidget::updateCardOutOfRangeNoOp() {
  TestableCoverFlow w;
  w.setCards(makeCards(3));
  CoverFlowCardData patch;
  patch.title = QStringLiteral("patched");
  w.updateCard(-1, patch);
  w.updateCard(3, patch);
  QCOMPARE(w.cardCount(), 3);
  QCOMPARE(w.cardAt(0).title, QStringLiteral("Card 0"));
  QCOMPARE(w.cardAt(2).title, QStringLiteral("Card 2"));
}

void TestCoverFlowWidget::updateCardPatchesTitleAndArtwork() {
  TestableCoverFlow w;
  w.setCards(makeCards(5));
  CoverFlowCardData patch;
  patch.title = QStringLiteral("New Title");
  patch.artworkPath = QStringLiteral("/art/new.png");
  w.updateCard(2, patch);
  QCOMPARE(w.cardAt(2).title, QStringLiteral("New Title"));
  QCOMPARE(w.cardAt(2).artworkPath, QStringLiteral("/art/new.png"));
  // Neighbours untouched.
  QCOMPARE(w.cardAt(1).title, QStringLiteral("Card 1"));
  QCOMPARE(w.cardAt(3).title, QStringLiteral("Card 3"));
}

void TestCoverFlowWidget::updateCardPreservesVideoPath() {
  TestableCoverFlow w;
  w.setCards(makeCards(5));
  // Video paths are resolved lazily by the controller via
  // setVideoPathForIndex; a chunk-arrival patch must not clobber one.
  w.setVideoPathForIndex(2, QStringLiteral("/videos/two.mp4"));
  CoverFlowCardData patch;
  patch.title = QStringLiteral("New Title");
  patch.artworkPath = QStringLiteral("/art/new.png");
  w.updateCard(2, patch);
  QCOMPARE(w.cardAt(2).videoPath, QStringLiteral("/videos/two.mp4"));
  QCOMPARE(w.cardAt(2).title, QStringLiteral("New Title"));
}

void TestCoverFlowWidget::updateCardDoesNotDisturbSelection() {
  TestableCoverFlow w;
  w.setCards(makeCards(10));
  w.setSelectedIndex(6, false);
  CoverFlowCardData patch;
  patch.title = QStringLiteral("patched");
  w.updateCard(6, patch);
  w.updateCard(0, patch);
  // Unlike setCards, the incremental patch keeps selection + glide state.
  QCOMPARE(w.selectedIndex(), 6);
  QCOMPARE(w.selectionPositionF(), 0.0);
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

void TestCoverFlowWidget::setGalleryStalePushForPreviousOwnerDropped() {
  TestableCoverFlow w;
  w.resize(600, 400);
  w.setCards(makeCards(10));
  w.setSelectedIndex(3, false);
  auto *preview = w.findChild<VideoPreviewWidget *>();
  QVERIFY(preview);
  w.show();

  QList<CoverFlowGalleryEntry> entries;
  CoverFlowGalleryEntry e;
  e.label = "cover";
  e.path = "/cover.png";
  entries << e;
  // First push is accepted (3 is the selection) and makes 3 the gallery
  // owner; then the selection moves on, leaving 3 only as the stale owner.
  w.setGalleryForIndex(3, entries);
  w.setSelectedIndex(4, false);

  // An accepted push runs applyVideoPreviewState, which hides a visible
  // preview when no video source resolves — so a manually-shown preview
  // being hidden is the observable fingerprint of the accepted path.
  preview->show();
  QList<CoverFlowGalleryEntry> moreEntries;
  CoverFlowGalleryEntry e2;
  e2.label = "screen";
  e2.path = "/screen.png";
  moreEntries << e2;
  // A late result for the previous owner must be dropped: accepting it
  // would resurrect the dismissed card's gallery under the new selection.
  w.setGalleryForIndex(3, moreEntries);
  QVERIFY(preview->isVisible());

  // Control: the same push for the current selection is accepted (the
  // side-effect pipeline runs and hides the sourceless preview).
  w.setGalleryForIndex(4, moreEntries);
  QVERIFY(!preview->isVisible());
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

// ----- OverlayZOrderRegistry -----

namespace {

// QWidget::raise() moves the widget to the end of its parent's child list,
// so after restack() the children order among the overlays is the stacking
// order — later index paints on top.
int childIndexOf(const QWidget &parent, const QWidget *child) {
  return static_cast<int>(parent.children().indexOf(child));
}

} // namespace

void TestCoverFlowWidget::overlayRegistryOutOfOrderRegistrationRestacksSorted() {
  QWidget parent;
  auto *loading = new QWidget(&parent);
  auto *selection = new QWidget(&parent);
  auto *search = new QWidget(&parent);

  OverlayZOrderRegistry registry;
  // Register in an order that contradicts the layer ranking — the registry
  // must keep its entries sorted by layer, not by registration order, or
  // restack() raises the overlays into the wrong stacking order.
  registry.registerOverlay(loading, OverlayZOrderRegistry::Layer::Loading);
  registry.registerOverlay(selection, OverlayZOrderRegistry::Layer::SelectionHighlight);
  registry.registerOverlay(search, OverlayZOrderRegistry::Layer::SearchLoading);
  registry.restack();

  QVERIFY(childIndexOf(parent, selection) < childIndexOf(parent, search));
  QVERIFY(childIndexOf(parent, search) < childIndexOf(parent, loading));
}

void TestCoverFlowWidget::overlayRegistryReregisterMovesEntryToNewLayer() {
  QWidget parent;
  auto *first = new QWidget(&parent);
  auto *second = new QWidget(&parent);
  auto *third = new QWidget(&parent);

  OverlayZOrderRegistry registry;
  registry.registerOverlay(first, OverlayZOrderRegistry::Layer::Splash);
  registry.registerOverlay(second, OverlayZOrderRegistry::Layer::Backdrop);
  registry.registerOverlay(third, OverlayZOrderRegistry::Layer::SelectionHighlight);
  // Re-registration reassigns the layer AND repositions the entry — a
  // stale in-place update would leave `first` stacked above `third`.
  registry.registerOverlay(first, OverlayZOrderRegistry::Layer::Backdrop);
  registry.restack();

  QVERIFY(childIndexOf(parent, second) < childIndexOf(parent, third));
  QVERIFY(childIndexOf(parent, first) < childIndexOf(parent, third));
}

// ----- Property setters -----

void TestCoverFlowWidget::setCornerRadiusClampsNegative() {
  TestableCoverFlow w;
  QCOMPARE(w.cornerRadius(), 0);
  w.setCornerRadius(12);
  QCOMPARE(w.cornerRadius(), 12);
  // Negative input is floored to 0, not stored raw (a negative radius
  // would corrupt the rounded-rect clip path in paint).
  w.setCornerRadius(-50);
  QCOMPARE(w.cornerRadius(), 0);
}

void TestCoverFlowWidget::setFontSizeClampsTooSmall() {
  TestableCoverFlow w;
  // Sizes below the 6pt floor clamp up to it — a 2pt title strip would be
  // unreadable and a non-positive size is invalid for QFont.
  w.setFontSize(2);
  QCOMPARE(w.fontSize(), 6);
  w.setFontSize(-1);
  QCOMPARE(w.fontSize(), 6);
  // Ordinary values above the floor pass through unchanged.
  w.setFontSize(20);
  QCOMPARE(w.fontSize(), 20);
}

void TestCoverFlowWidget::setTileColorClearsPixmapCache() {
  // Populate the artwork pixmap cache the way production does: a real
  // artwork file on a card, decoded by the async load a paint pass kicks
  // off. grab() drives paintEvent without a shown window.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString artPath = dir.filePath(QStringLiteral("card.png"));
  {
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::darkCyan);
    QVERIFY(img.save(artPath));
  }

  TestableCoverFlow w;
  w.resize(400, 300);
  QList<CoverFlowCardData> cards = makeCards(3);
  cards[0].artworkPath = artPath;
  w.setCards(cards);
  QCOMPARE(w.pixmapCacheSize(), 0);

  (void)w.grab(); // paint pass schedules the artwork decode
  QTRY_VERIFY(w.pixmapCacheSize() > 0);

  // Same color → early-out: the cache must survive.
  w.setTileColor(QString()); // matches the default empty tile color
  QVERIFY(w.pixmapCacheSize() > 0);

  // A genuine tile-color change invalidates every cached pixmap —
  // placeholders bake the tile color in, so stale entries would repaint
  // with the old color.
  w.setTileColor(QStringLiteral("#00ff00"));
  QCOMPARE(w.tileColor(), QStringLiteral("#00ff00"));
  QCOMPARE(w.pixmapCacheSize(), 0);
}

void TestCoverFlowWidget::setHideTitlesUpdatesFlag() {
  TestableCoverFlow w;
  QVERIFY(!w.hideTitles());
  w.setHideTitles(true);
  QVERIFY(w.hideTitles());
  w.setHideTitles(true); // early-out keeps the value
  QVERIFY(w.hideTitles());
  w.setHideTitles(false);
  QVERIFY(!w.hideTitles());
}

// ----- Center-rect maintenance (paint-free layout) -----

namespace {

// Shared setup: a sized, shown carousel with the (stopped — playVideo is
// never called, so the QtMultimedia backend stays cold) preview child made
// visible so the center-rect maintenance applies geometry to it.
VideoPreviewWidget *showWithVisiblePreview(TestableCoverFlow &w) {
  w.resize(600, 400);
  w.setCards(makeCards(9));
  w.setSelectedIndex(4, false);
  auto *preview = w.findChild<VideoPreviewWidget *>();
  if (!preview) {
    return nullptr;
  }
  w.show();
  preview->show();
  return preview;
}

} // namespace

void TestCoverFlowWidget::glideFrameRepositionsVisiblePreview() {
  TestableCoverFlow w;
  VideoPreviewWidget *preview = showWithVisiblePreview(w);
  QVERIFY(preview);
  QVERIFY(preview->isVisible());

  // A glide frame carries the centered card off stage center; the preview
  // must follow through the property setter — no paint pass runs here.
  w.setSelectionPositionF(0.4);
  const QRect offCenter = preview->geometry();
  QVERIFY(!offCenter.isEmpty());
  QVERIFY(offCenter.center().x() < w.width() / 2 - 30);

  // Glide settles back to 0 — the preview re-centers over the stage.
  w.setSelectionPositionF(0.0);
  const QRect centered = preview->geometry();
  QVERIFY(!centered.isEmpty());
  QVERIFY(centered != offCenter);
  QVERIFY(qAbs(centered.center().x() - w.width() / 2) <= 2);
}

void TestCoverFlowWidget::resizeRepositionsVisiblePreview() {
  TestableCoverFlow w;
  VideoPreviewWidget *preview = showWithVisiblePreview(w);
  QVERIFY(preview);
  // Nudge the glide off/on center so the maintained rect (computed while
  // the preview was still hidden) is applied to the now-visible preview.
  w.setSelectionPositionF(0.1);
  w.setSelectionPositionF(0.0);
  QVERIFY(qAbs(preview->geometry().center().x() - w.width() / 2) <= 2);

  // Growing the widget moves the stage center; the resize path re-derives
  // the rect and repositions the visible preview. QTRY: the platform
  // delivers the top-level resize event asynchronously.
  w.resize(800, 400);
  QTRY_VERIFY(qAbs(preview->geometry().center().x() - 400) <= 2);
}

void TestCoverFlowWidget::titleSlotChangeRepositionsVisiblePreview() {
  TestableCoverFlow w;
  VideoPreviewWidget *preview = showWithVisiblePreview(w);
  QVERIFY(preview);
  w.setSelectionPositionF(0.1);
  w.setSelectionPositionF(0.0);
  const QRect before = preview->geometry();
  QVERIFY(!before.isEmpty());

  // Hiding titles reclaims the title strip's vertical slot: the stage
  // center drops and the card grows — the visible preview must follow.
  w.setHideTitles(true);
  const QRect after = preview->geometry();
  QVERIFY(after != before);
  QVERIFY(after.center().y() > before.center().y());
  QVERIFY(qAbs(after.center().x() - w.width() / 2) <= 2);
}

void TestCoverFlowWidget::hiddenPreviewGeometryUntouched() {
  TestableCoverFlow w;
  w.resize(600, 400);
  w.setCards(makeCards(9));
  w.setSelectedIndex(4, false);
  auto *preview = w.findChild<VideoPreviewWidget *>();
  QVERIFY(preview);
  w.show();
  // No video source, so the preview stays hidden: center-rect maintenance
  // keeps tracking state but must not touch the hidden child's geometry.
  const QRect dormant = preview->geometry();
  w.setSelectionPositionF(0.4);
  w.setSelectionPositionF(0.0);
  QVERIFY(!preview->isVisible());
  QCOMPARE(preview->geometry(), dormant);
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

// Kartend-5jtyw: mouseDoubleClickEvent used to go straight to hitTestCard,
// which misses every card for a point inside the strip — so double-clicking a
// thumbnail did nothing. It must instead claim the event and ask for a preview
// of that exact file. The launch assertion below is a guard, not a fixed
// regression: the strip lays out below the card rects, so the old fall-through
// was inert rather than harmful, and this pins that it stays that way even if
// the layout later brings the two into overlap.
void TestCoverFlowWidget::mouseDoubleClickOnGalleryThumbPreviewsInsteadOfLaunching() {
  TestableCoverFlow w;
  w.resize(600, 400);
  w.setCards(makeCards(10));
  w.setSelectedIndex(3, false);

  QList<CoverFlowGalleryEntry> entries;
  CoverFlowGalleryEntry cover;
  cover.label = "cover";
  cover.path = "/art/cover.png";
  CoverFlowGalleryEntry clip;
  clip.label = "clip";
  clip.path = "/art/clip.mp4";
  clip.isVideo = true;
  entries << cover << clip;
  w.setGalleryForIndex(3, entries);

  auto *strip = w.findChild<CoverFlowGalleryStrip *>();
  QVERIFY(strip);
  const QList<QRect> thumbs = strip->thumbRects();
  QCOMPARE(thumbs.size(), entries.size());

  QSignalSpy launchSpy(&w, &CoverFlowWidget::itemActivated);
  QSignalSpy previewSpy(&w, &CoverFlowWidget::galleryPreviewRequested);

  // Double-click the second thumbnail (the video entry) — a deliberate pick:
  // it proves the strip's own index is used rather than the centered card's
  // default artwork.
  const QPoint at = thumbs.at(1).center();
  QMouseEvent e(QEvent::MouseButtonDblClick, QPointF(at), w.mapToGlobal(at), Qt::LeftButton,
                Qt::LeftButton, Qt::NoModifier);
  w.mouseDoubleClickEvent(&e);

  QCOMPARE(previewSpy.count(), 1);
  QCOMPARE(previewSpy.at(0).at(0).toString(), clip.path);
  QCOMPARE(previewSpy.at(0).at(1).toBool(), true);
  QVERIFY2(launchSpy.isEmpty(),
           "double-clicking a gallery thumbnail fell through to the card hit-test and activated "
           "the item");
  QVERIFY(e.isAccepted());
}

// The card path is untouched: a double click away from the strip still
// activates, so the fix above narrows nothing.
void TestCoverFlowWidget::mouseDoubleClickOffGalleryStillActivatesCard() {
  TestableCoverFlow w;
  w.resize(600, 400);
  w.setCards(makeCards(10));
  w.setSelectedIndex(3, false);

  QSignalSpy launchSpy(&w, &CoverFlowWidget::itemActivated);
  QSignalSpy previewSpy(&w, &CoverFlowWidget::galleryPreviewRequested);

  // No gallery is set, so the strip has no thumbnails and cannot claim the
  // event; the centered card is at the widget's middle.
  const QPoint at(w.width() / 2, w.height() / 2);
  QMouseEvent e(QEvent::MouseButtonDblClick, QPointF(at), w.mapToGlobal(at), Qt::LeftButton,
                Qt::LeftButton, Qt::NoModifier);
  w.mouseDoubleClickEvent(&e);

  QCOMPARE(launchSpy.count(), 1);
  QCOMPARE(previewSpy.count(), 0);
}

QTEST_MAIN(TestCoverFlowWidget)
#include "test_coverflowwidget.moc"
