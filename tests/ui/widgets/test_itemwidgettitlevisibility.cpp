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
#include <QTest>

class TestItemWidgetTitleVisibility : public QObject {
  Q_OBJECT
private slots:
  void gridHonoursEveryHideTitlesSetting();
  void listShowsSubcollectionNameEvenWhenSubcollectionTitlesHidden();
  void listShowsVirtualFolderNameEvenWhenSubfolderTitleHidden();
  void listShowsItemNameEvenWhenTitlesHidden();
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

QTEST_MAIN(TestItemWidgetTitleVisibility)
#include "test_itemwidgettitlevisibility.moc"
