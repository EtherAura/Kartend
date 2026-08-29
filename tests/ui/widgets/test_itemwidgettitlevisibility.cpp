// ItemWidget::shouldPaintTitle() — the single rule deciding whether a widget's
// name is drawn (Kartend-dsjco).
//
// The rule used to be hand-copied into three places: setItemName (populates
// the label), paintEvent (draws the text with the custom painter that bypasses
// the QLabel stylesheet), and applyDimensions' grid arm (sizes the title band).
// They drifted: the regular-item branch gained a list-mode escape hatch and the
// subcollection / virtual-folder branches never did. With subcollection titles
// hidden, a subcollection in LIST layout therefore drew its folder icon and
// nothing else — a row whose only identifying mark was a generic folder glyph,
// while the same subcollection painted fine in Grid.
//
// The hide-titles settings are grid-mode concerns: a grid tile that drops its
// title still shows artwork, but a list row IS its title. These tests pin that
// asymmetry per widget kind so the branches cannot drift apart again.
//
// QTEST_MAIN gives a real QApplication so the widgets construct; none is shown
// and no DB is touched.

#include "itemwidget.h"

#include <QApplication>
#include <QLabel>
#include <QLayout>
#include <QTest>

class TestItemWidgetTitleVisibility : public QObject {
  Q_OBJECT
private slots:
  void gridHonoursEveryHideTitlesSetting();
  void listShowsSubcollectionNameEvenWhenSubcollectionTitlesHidden();
  void listShowsVirtualFolderNameEvenWhenSubfolderTitleHidden();
  void listShowsItemNameEvenWhenTitlesHidden();

  // Kartend-ari1x
  void gridTextReservationIsUnconditional_data();
  void gridTextReservationIsUnconditional();
  void gridContentNeverOverflowsItsCell_data();
  void gridContentNeverOverflowsItsCell();
  void gridArtworkNeverExceedsTilePitch_data();
  void gridArtworkNeverExceedsTilePitch();
};

// Grid is where the settings actually apply — each widget kind reads its own
// hide flag and ignores the other two.
void TestItemWidgetTitleVisibility::gridHonoursEveryHideTitlesSetting() {
  {
    ItemWidget item;
    item.setItemName(QStringLiteral("An Item"));
    QVERIFY(item.shouldPaintTitle());
    item.setHideTitles(true);
    QVERIFY(!item.shouldPaintTitle());
    // A subcollection-only setting must not silence a plain item.
    item.setHideTitles(false);
    item.setHideSubcollectionTitles(true);
    QVERIFY(item.shouldPaintTitle());
  }
  {
    ItemWidget sub;
    sub.setAsSubcollection(0, QStringLiteral("Documentaries"));
    QVERIFY(sub.shouldPaintTitle());
    sub.setHideSubcollectionTitles(true);
    QVERIFY(!sub.shouldPaintTitle());
    // ...and the item-level setting must not silence a subcollection.
    sub.setHideSubcollectionTitles(false);
    sub.setHideTitles(true);
    QVERIFY(sub.shouldPaintTitle());
  }
  {
    ItemWidget folder;
    folder.setAsVirtualFolder(QStringLiteral("/media/Shorts"), QStringLiteral("Shorts"),
                              /*hideTitle=*/false);
    QVERIFY(folder.shouldPaintTitle());

    ItemWidget hidden;
    hidden.setAsVirtualFolder(QStringLiteral("/media/Shorts"), QStringLiteral("Shorts"),
                              /*hideTitle=*/true);
    QVERIFY(!hidden.shouldPaintTitle());
  }
}

// The reported bug: List layout, subcollection titles hidden, row went blank.
void TestItemWidgetTitleVisibility::listShowsSubcollectionNameEvenWhenSubcollectionTitlesHidden() {
  ItemWidget sub;
  sub.setListMode(true);
  sub.setHideSubcollectionTitles(true);
  sub.setAsSubcollection(0, QStringLiteral("Documentaries"));

  QVERIFY2(sub.shouldPaintTitle(),
           "A subcollection row in List layout must still paint its name; the folder "
           "icon alone identifies nothing");

  // Order-independent: the factory configures the base widget (list mode among
  // it) before setAsSubcollection, but a later settings change must not blank
  // the row either.
  ItemWidget reordered;
  reordered.setAsSubcollection(1, QStringLiteral("Documentaries"));
  reordered.setListMode(true);
  reordered.setHideSubcollectionTitles(true);
  QVERIFY(reordered.shouldPaintTitle());
}

void TestItemWidgetTitleVisibility::listShowsVirtualFolderNameEvenWhenSubfolderTitleHidden() {
  ItemWidget folder;
  folder.setListMode(true);
  folder.setAsVirtualFolder(QStringLiteral("/media/Shorts"), QStringLiteral("Shorts"),
                            /*hideTitle=*/true);
  QVERIFY2(folder.shouldPaintTitle(),
           "A virtual-folder row in List layout must still paint its name");
}

// The one branch that already had the list-mode escape — pinned so the shared
// helper did not regress it.
void TestItemWidgetTitleVisibility::listShowsItemNameEvenWhenTitlesHidden() {
  ItemWidget item;
  item.setListMode(true);
  item.setHideTitles(true);
  item.setItemName(QStringLiteral("An Item"));
  QVERIFY(item.shouldPaintTitle());
}

// ─────────────────────────────────────────────────────────────────────────────
// Kartend-ari1x: the grid's text reservation, and what depends on it.
//
// Hiding titles does NOT currently give the reserved three-line band back to
// the artwork. That is deliberate-until-understood, not an oversight: the
// obvious fix shipped on 2026-08-20 and was reverted within the hour after it
// broke a real library with artwork overlapping artwork.
//
// These pin the two facts the next attempt needs, both measured rather than
// assumed, because the issue's original reasoning was wrong in a way that would
// send the next person looking in the wrong place.
// ─────────────────────────────────────────────────────────────────────────────

namespace {
// artworkSize = qMin(availableWidth, availableHeight), where ItemWidget's grid
// arm computes
//   availableWidth  = w - PADDING - 2*ARTWORK_BACKDROP_INSET          = w - 40
//   availableHeight = w's vertical twin - SPACING - reservedTextHeight = h - 48 - R
// so a cell is WIDTH-limited exactly when (w - 40) < (h - 48 - R).
//
// R, the three-line text reservation, is 3 * (ascent + descent) of the
// reference font — a FONT METRIC, not a layout constant. It was hardcoded here
// as 51 (i.e. a 17px line), which is why the 200x260 row was marked
// width-limited: at R=51 that cell is width-limited by exactly one pixel
// (160 < 161). On a machine whose 12pt line is taller the same cell is
// height-limited instead — at the 22px line this suite actually measures,
// R=66 gives 160 vs 146. The cell sits right on the boundary, so a baked-in R
// makes the expectation correct only on the machine it was written on.
//
// R is therefore MEASURED below rather than assumed, and each cell's regime is
// derived from it. What the test pins is unchanged: hiding the title frees the
// band on a height-limited tile and cannot help a width-limited one.
struct GridCell {
  int w;
  int h;
};
const QList<GridCell> kCells = {{200, 260}, {150, 300}, {260, 200}, {200, 200}, {300, 150}};

constexpr int kHorizontalOverhead = 40; // PADDING + 2*ARTWORK_BACKDROP_INSET
constexpr int kVerticalOverhead = 48;   // …plus SPACING

/// The reserved text height, read off the widget instead of recomputed.
/// Calibrated on a deliberately squat cell (300x150): 260px of available width
/// against at most 102px of height, so height binds whether or not the band is
/// reserved, and the difference between the two artwork sizes IS the band.
int measuredTextReservation();

ItemWidget *makeGridItem(ItemWidget &w, int cw, int ch, bool hide) {
  w.setItemName(QStringLiteral("A Reasonably Long Item Title Here"));
  w.setHideTitles(hide);
  w.setItemDimensions(cw, ch);
  w.resize(cw, ch);
  if (QLayout *box = w.layout()) {
    box->activate();
  }
  return &w;
}

int measuredTextReservation() {
  ItemWidget shown;
  ItemWidget hidden;
  makeGridItem(shown, 300, 150, false);
  makeGridItem(hidden, 300, 150, true);
  return hidden.artworkSize() - shown.artworkSize();
}
} // namespace

void TestItemWidgetTitleVisibility::gridTextReservationIsUnconditional_data() {
  QTest::addColumn<int>("cw");
  QTest::addColumn<int>("ch");
  for (const GridCell &c : kCells) {
    QTest::newRow(qPrintable(QStringLiteral("%1x%2").arg(c.w).arg(c.h))) << c.w << c.h;
  }
}

void TestItemWidgetTitleVisibility::gridTextReservationIsUnconditional() {
  QFETCH(int, cw);
  QFETCH(int, ch);

  const int reserved = measuredTextReservation();
  QVERIFY2(reserved > 0, "no band is being reserved for the title at all");

  ItemWidget shown;
  ItemWidget hidden;
  makeGridItem(shown, cw, ch, false);
  makeGridItem(hidden, cw, ch, true);

  // Which axis binds while the title IS drawn — that is the state the band has
  // to be freed from.
  const int availableWidth = cw - kHorizontalOverhead;
  const int availableHeight = ch - kVerticalOverhead - reserved;
  const bool widthLimited = availableWidth < availableHeight;

  // Hiding the title gives its band back to the artwork on a HEIGHT-limited
  // tile, which is the whole point of Kartend-ari1x. On a WIDTH-limited tile
  // the width still binds, so nothing changes — the issue wrongly believed the
  // reporter's tiles were in that second group.
  if (widthLimited) {
    QCOMPARE(hidden.artworkSize(), shown.artworkSize());
  } else {
    QVERIFY2(hidden.artworkSize() > shown.artworkSize(),
             "hiding the title left the artwork the same size on a height-limited tile — the "
             "reserved band is not being given back");
  }
  // Freeing the band can only ever grow the art, and never past the width,
  // which no amount of freed height can relieve.
  QVERIFY(hidden.artworkSize() >= shown.artworkSize());
  QCOMPARE(hidden.artworkSize(), qMin(availableWidth, availableHeight + reserved));
}

// The regression that reverted this once (Kartend-ari1x). Negative grid spacing
// makes the tile PITCH smaller than the cell, so artwork sized only against the
// cell grows straight into the neighbour. These are the reporter's real numbers
// from ~/.config/kartend/kartend.cfg: 325x325 cells, -80 spacing on both axes,
// i.e. a 245px pitch. Before the pitch clamp the freed band took the art to
// 277px — 32px INTO the adjacent tile, on both axes.
void TestItemWidgetTitleVisibility::gridArtworkNeverExceedsTilePitch_data() {
  QTest::addColumn<int>("cell");
  QTest::addColumn<int>("spacing");
  QTest::newRow("reporter 325 cell, -80 spacing") << 325 << -80;
  QTest::newRow("harsher -140 spacing") << 325 << -140;
  QTest::newRow("mild -20 spacing") << 300 << -20;
  QTest::newRow("zero spacing") << 300 << 0;
  QTest::newRow("positive spacing") << 300 << 24;
}

void TestItemWidgetTitleVisibility::gridArtworkNeverExceedsTilePitch() {
  QFETCH(int, cell);
  QFETCH(int, spacing);
  const int pitch = cell + spacing;

  for (bool hide : {false, true}) {
    ItemWidget w;
    w.setItemName(QStringLiteral("A Reasonably Long Item Title Here"));
    w.setHideTitles(hide);
    w.setItemDimensions(cell, cell);
    w.setGridPitch(pitch, pitch);
    w.resize(cell, cell);
    if (QLayout *box = w.layout()) {
      box->activate();
    }
    QVERIFY2(w.artworkSize() <= pitch,
             qPrintable(QStringLiteral("artwork %1 exceeds tile pitch %2 (cell %3, spacing %4, "
                                       "hide=%5) — it will overlap the neighbouring tile")
                            .arg(w.artworkSize())
                            .arg(pitch)
                            .arg(cell)
                            .arg(spacing)
                            .arg(hide)));
  }

  // Positive or zero spacing must be untouched by the clamp: the pitch is then
  // at least the cell, so it can never be the binding constraint and no
  // ordinary configuration changes behaviour.
  if (spacing >= 0) {
    ItemWidget clamped;
    ItemWidget unclamped;
    for (ItemWidget *w : {&clamped, &unclamped}) {
      w->setItemName(QStringLiteral("A Reasonably Long Item Title Here"));
      w->setHideTitles(true);
      w->setItemDimensions(cell, cell);
    }
    clamped.setGridPitch(pitch, pitch);
    QCOMPARE(clamped.artworkSize(), unclamped.artworkSize());
  }
}

void TestItemWidgetTitleVisibility::gridContentNeverOverflowsItsCell_data() {
  gridTextReservationIsUnconditional_data();
}

void TestItemWidgetTitleVisibility::gridContentNeverOverflowsItsCell() {
  QFETCH(int, cw);
  QFETCH(int, ch);

  for (bool hide : {false, true}) {
    ItemWidget w;
    makeGridItem(w, cw, ch, hide);
    QVERIFY(w.imageLabel != nullptr);
    QVERIFY(w.nameLabel != nullptr);
    const int contentBottom =
        qMax(w.imageLabel->geometry().bottom(), w.nameLabel->geometry().bottom()) + 1;
    // The reported failure was "artwork drawn overlapping artwork, rows
    // colliding". Whatever produced that, it is NOT this widget's own layout
    // spilling past its cell — verified across both limiting regimes, titles
    // shown and hidden. Recorded so the next attempt looks at the grid's
    // placement rather than re-deriving the widget's box model.
    QVERIFY2(contentBottom <= ch,
             qPrintable(QStringLiteral("content bottom %1 exceeds cell height %2 (hide=%3)")
                            .arg(contentBottom)
                            .arg(ch)
                            .arg(hide)));
    QVERIFY(w.imageLabel->geometry().right() < cw);
  }
}

QTEST_MAIN(TestItemWidgetTitleVisibility)
#include "test_itemwidgettitlevisibility.moc"
