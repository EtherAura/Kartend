// Kartend-0eeuk: FlowLayout wrap math. The layout reflows fixed-size chip
// widgets left-to-right and wraps at the container edge; these tests pin the
// deterministic geometry contract with 50x20 fixed-size items and known
// spacings: heightForWidth row counts (including the no-wrap guarantee for
// the first item on a too-narrow row), contents-margin inclusion, the
// per-item positions stamped by setGeometry, the count/itemAt/takeAt list
// contract, and the minimumSize/sizeHint coupling. Driven headlessly under
// the offscreen QPA — the host widget is never shown.

#include "flowlayout.h"

#include <QTest>
#include <QWidget>

namespace {

constexpr int kItemW = 50;
constexpr int kItemH = 20;
constexpr int kHSpace = 8;
constexpr int kVSpace = 6;

QWidget *addChip(QWidget *host, FlowLayout *layout) {
  auto *w = new QWidget(host);
  w->setFixedSize(kItemW, kItemH); // pins QWidgetItem::sizeHint to 50x20
  layout->addWidget(w);
  return w;
}

} // namespace

class TestFlowLayout : public QObject {
  Q_OBJECT

private slots:
  void heightForWidthWrapsAtRightEdge();
  void firstItemOnRowNeverWrapsOnNarrowWidth();
  void heightForWidthIncludesContentsMargins();
  void setGeometryPositionsItemsInWrapOrder();
  void itemListContract();
  void minimumSizeIsLargestItemPlusMargins();

private:
  // Expected height for n full rows of 20px items with 6px row gaps.
  static int rows(int n) { return n * kItemH + (n - 1) * kVSpace; }
};

void TestFlowLayout::heightForWidthWrapsAtRightEdge() {
  QWidget host;
  auto *layout = new FlowLayout(&host, /*margin=*/0, kHSpace, kVSpace);
  for (int i = 0; i < 5; ++i) {
    addChip(&host, layout);
  }
  QVERIFY(layout->hasHeightForWidth());

  // Width 200 fits 3 chips per row (3*50 + 2*8 = 166; a 4th would need
  // 224): 5 chips -> rows of 3 + 2.
  QCOMPARE(layout->heightForWidth(200), rows(2));
  // One pixel of slack still fits 3 per row.
  QCOMPARE(layout->heightForWidth(167), rows(2));
  // The exact 3-per-row fit (166) keeps the flush chip on the row: the
  // wrap check compares against x + width, not QRect::right() (= width
  // - 1, the canonical Qt example's one-chip-early off-by-one, fixed in
  // Kartend-29vam).
  QCOMPARE(layout->heightForWidth(166), rows(2));
  QCOMPARE(layout->heightForWidth(165), rows(3));
  // Ultrawide: single row.
  QCOMPARE(layout->heightForWidth(1000), rows(1));
}

void TestFlowLayout::firstItemOnRowNeverWrapsOnNarrowWidth() {
  QWidget host;
  auto *layout = new FlowLayout(&host, 0, kHSpace, kVSpace);
  for (int i = 0; i < 3; ++i) {
    addChip(&host, layout);
  }
  // Narrower than a single chip: each row still takes one chip (the
  // lineH > 0 guard), so 3 chips -> 3 rows, never an infinite wrap.
  QCOMPARE(layout->heightForWidth(10), rows(3));
}

void TestFlowLayout::heightForWidthIncludesContentsMargins() {
  QWidget host;
  auto *layout = new FlowLayout(&host, /*margin=*/4, kHSpace, kVSpace);
  addChip(&host, layout);
  // top margin + one 20px row + bottom margin.
  QCOMPARE(layout->heightForWidth(500), 4 + kItemH + 4);
}

void TestFlowLayout::setGeometryPositionsItemsInWrapOrder() {
  QWidget host;
  auto *layout = new FlowLayout(&host, 0, kHSpace, kVSpace);
  QWidget *a = addChip(&host, layout);
  QWidget *b = addChip(&host, layout);
  QWidget *c = addChip(&host, layout);
  QWidget *d = addChip(&host, layout);

  layout->setGeometry(QRect(0, 0, 200, 100)); // 3 per row (see above)

  QCOMPARE(a->geometry(), QRect(0, 0, kItemW, kItemH));
  QCOMPARE(b->geometry(), QRect(kItemW + kHSpace, 0, kItemW, kItemH));
  QCOMPARE(c->geometry(), QRect(2 * (kItemW + kHSpace), 0, kItemW, kItemH));
  // Fourth chip wraps to a fresh row at x=0, y = rowHeight + vSpace.
  QCOMPARE(d->geometry(), QRect(0, kItemH + kVSpace, kItemW, kItemH));
}

void TestFlowLayout::itemListContract() {
  QWidget host;
  auto *layout = new FlowLayout(&host, 0, kHSpace, kVSpace);
  QWidget *a = addChip(&host, layout);
  QWidget *b = addChip(&host, layout);

  QCOMPARE(layout->count(), 2);
  QCOMPARE(layout->itemAt(0)->widget(), a);
  QCOMPARE(layout->itemAt(1)->widget(), b);
  QCOMPARE(layout->itemAt(2), static_cast<QLayoutItem *>(nullptr));

  // Out-of-range takeAt is a null no-op, not an assert.
  QCOMPARE(layout->takeAt(-1), static_cast<QLayoutItem *>(nullptr));
  QCOMPARE(layout->takeAt(5), static_cast<QLayoutItem *>(nullptr));

  QLayoutItem *taken = layout->takeAt(0);
  QVERIFY(taken);
  QCOMPARE(taken->widget(), a);
  delete taken; // takeAt transfers ownership of the item to the caller
  QCOMPARE(layout->count(), 1);
  QCOMPARE(layout->itemAt(0)->widget(), b);
}

void TestFlowLayout::minimumSizeIsLargestItemPlusMargins() {
  QWidget host;
  auto *layout = new FlowLayout(&host, /*margin=*/4, kHSpace, kVSpace);
  addChip(&host, layout);
  auto *tall = new QWidget(&host);
  tall->setFixedSize(30, 44); // taller but narrower than the chip
  layout->addWidget(tall);

  // Per-axis max across items (50 wide from the chip, 44 tall from this
  // one), plus 4px margins on every side.
  QCOMPARE(layout->minimumSize(), QSize(kItemW + 8, 44 + 8));
  // The wrap layout advertises its minimum as the hint.
  QCOMPARE(layout->sizeHint(), layout->minimumSize());
  QCOMPARE(layout->expandingDirections(), Qt::Orientations());
}

QTEST_MAIN(TestFlowLayout)
#include "test_flowlayout.moc"
