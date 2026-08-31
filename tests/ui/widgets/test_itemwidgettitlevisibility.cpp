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
#include "uiconstants/widget.h"

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
  void hiddenTitleArtworkStaysWithinTilePitch();
  void selectionRingStaysOffTheArtwork();
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

  ItemWidget shown;
  ItemWidget hidden;
  makeGridItem(shown, cw, ch, false);
  makeGridItem(hidden, cw, ch, true);

  // Kartend-hxly2: an untitled tile IS its cell. The layout margin and the
  // artwork-to-label spacing both exist to hold a caption off the cell edge and
  // off the art, so with no caption drawn neither has anything left to
  // separate and the whole cell goes to the artwork. This is what makes the
  // configured item size the size the user actually sees.
  //
  // This supersedes the older expectation that a WIDTH-limited tile could not
  // benefit from hiding its title. That held while the horizontal overhead was
  // charged unconditionally; now that hiding the caption frees the margin on
  // both axes, every tile grows.
  QCOMPARE(hidden.artworkSize(), qMin(cw, ch));
  QVERIFY2(hidden.artworkSize() > shown.artworkSize(),
           "hiding the title did not hand the caption's space back to the artwork");

  // A titled tile still pays for exactly what it draws, and never more than the
  // cell can hold.
  QVERIFY(shown.artworkSize() > 0);
  QVERIFY(shown.artworkSize() <= qMin(cw, ch));
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

  // The clamp holds nothing back, so with spacing >= 0 the pitch is at least
  // the cell and it can never be the binding constraint. Spacing cannot BE
  // negative from the settings any more (Kartend-hxly2 floors it at 0, and
  // clampValues migrates older configs), but the negative rows above are kept:
  // setGridPitch takes a pitch rather than a spacing, so the guard is still
  // reachable and still worth pinning.
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

void TestItemWidgetTitleVisibility::hiddenTitleArtworkStaysWithinTilePitch() {
  // Kartend-rrv5z. gridArtworkNeverExceedsTilePitch above already proves the
  // clamp WORKS once a pitch is set — including on the reporter's exact 325 /
  // -80 config — which is precisely why the field bug was confusing: covers
  // still overlapped on a 52,410-item grid. The clamp was never reached.
  // ItemWidgetFactory built every tile without calling setGridPitch, so tiles
  // were born with pitch 0, and only the scroll engine's reconfigure paths ever
  // armed one. A tile was protected only if some later pass happened to sweep
  // it; a freshly realised one was not.
  //
  // So what is worth pinning here is the other half of the contract — that 0
  // really does mean UNCONSTRAINED, and is therefore never a safe state to
  // leave a grid tile in. Hidden titles are what make the overflow big enough
  // to see, since the reserved caption band goes back to the artwork.
  constexpr int kCell = 325;
  constexpr int kPitch = kCell - 80; // where the neighbouring tile starts

  ItemWidget unarmed;
  unarmed.setHideTitles(true);
  unarmed.setItemDimensions(kCell, kCell);
  QVERIFY2(
      unarmed.artworkSize() > kPitch,
      qPrintable(QStringLiteral("an unarmed tile sized %1 no longer overflows the %2px pitch. If "
                                "the pitch now defaults to something safe, this test's premise is "
                                "stale — revisit it rather than deleting it, because the factory "
                                "arming the pitch is what this is guarding.")
                     .arg(unarmed.artworkSize())
                     .arg(kPitch)));
}

void TestItemWidgetTitleVisibility::selectionRingStaysOffTheArtwork() {
  // Kartend-9gzkl, reversing Kartend-f4hva's clamp. With hidden titles the
  // artwork fills its whole cell (Kartend-hxly2), so a ring clamped inside
  // the tile's paint bounds was painted ON the outer band of the cover art.
  // The ring now lives on a container-hosted SIBLING positioned around the
  // artwork: leaving the tile is the point — the stroke lands in the grid
  // gap. Pin the new contract: the stroke band never intersects the artwork
  // label, and the overlay is parented to the container at exactly that band.
  QWidget host;
  auto *tile = new ItemWidget(&host);
  tile->setHideTitles(true);
  tile->setItemDimensions(325, 325);
  if (QLayout *box = tile->layout()) {
    box->activate();
  }
  tile->setSelected(true);

  const QRect ring = tile->selectionBorderRectInParent();
  QVERIFY2(!ring.isEmpty(), "selection ring rect collapsed to nothing");

  // selectionBorderRectInParent names the OUTER edge of the stroke band; the
  // band's inner hole must clear the artwork label on every side.
  const int pen = UIConstants::Widget::BORDER_WIDTH_SELECTION;
  const QRect hole = ring.adjusted(pen, pen, -pen, -pen);
  const QRect art = tile->imageLabel->geometry().translated(tile->pos());
  QVERIFY2(hole.contains(art),
           qPrintable(QStringLiteral("stroke band %1,%2 %3x%4 overlaps the artwork %5,%6 %7x%8")
                          .arg(ring.x())
                          .arg(ring.y())
                          .arg(ring.width())
                          .arg(ring.height())
                          .arg(art.x())
                          .arg(art.y())
                          .arg(art.width())
                          .arg(art.height())));

  QWidget *overlay = tile->m_selectionBorderOverlay;
  QVERIFY2(overlay, "no ring overlay was created on selection");
  QCOMPARE(overlay->parentWidget(), &host);
  QCOMPARE(overlay->geometry(), ring);
  QVERIFY2(overlay->isVisibleTo(&host), "ring overlay exists but is not visible while selected");

  // Mode flips bypass setSelected (it early-returns on an unchanged selection
  // state), so setListMode must manage the ring itself: down in list mode —
  // where it would float at its stale grid spot — and back up in grid mode.
  tile->setListMode(true);
  QVERIFY2(!overlay->isVisibleTo(&host), "grid ring still visible on a list row");
  tile->setListMode(false);
  QVERIFY2(overlay->isVisibleTo(&host), "ring not restored on returning to grid mode");

  tile->setSelected(false);
  QVERIFY2(!overlay->isVisibleTo(&host), "ring overlay still visible after deselection");

  // A titles-shown tile's art is inset by the layout margin, so its ring
  // still fits inside the tile — the captionless full-bleed case is the only
  // one that needs the gap.
  auto *titled = new ItemWidget(&host);
  titled->setItemName(QStringLiteral("A Title"));
  titled->setItemDimensions(325, 325);
  if (QLayout *box = titled->layout()) {
    box->activate();
  }
  titled->setSelected(true);
  QVERIFY(QRect(titled->pos(), QSize(325, 325)).contains(titled->selectionBorderRectInParent()));

  // A parentless tile has nowhere to host the ring — selection must not
  // create a stray top-level window (or crash); the ring appears once the
  // tile is adopted and re-selected.
  ItemWidget orphan;
  orphan.setHideTitles(true);
  orphan.setItemDimensions(100, 100);
  orphan.setSelected(true);
  QVERIFY2(!orphan.m_selectionBorderOverlay, "a parentless tile must not create a top-level ring");
}

QTEST_MAIN(TestItemWidgetTitleVisibility)
#include "test_itemwidgettitlevisibility.moc"
