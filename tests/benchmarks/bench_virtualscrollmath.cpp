// Walltime benchmark for VirtualScrollMath::neededIndexBand — runs once per
// scroll frame to map the visible row range onto the item band to materialize.
// QBENCHMARK so a regression (e.g. someone turning the O(1) arithmetic into a
// per-item scan) is visible in CI artifact diffs. The suite is labelled
// "benchmark" and never fails CI on an absolute threshold.
#include "virtualscrollmath.h"

#include <QTest>

class BenchVirtualScrollMath : public QObject {
  Q_OBJECT
private slots:
  void neededIndexBand_largeCollection();
};

void BenchVirtualScrollMath::neededIndexBand_largeCollection() {
  // A big library scrolled deep: realistic per-frame inputs.
  constexpr int itemsPerRow = 6;
  constexpr int totalItems = 200000;
  volatile int sink = 0;
  QBENCHMARK {
    int acc = 0;
    // Sweep a range of scroll positions so the band math is exercised across
    // the collection, not just one fixed viewport.
    for (int row = 0; row < 1000; ++row) {
      const auto band =
          VirtualScrollMath::neededIndexBand(row * 10, row * 10 + 12, itemsPerRow, totalItems);
      acc += band.firstIndex + band.lastIndex;
    }
    sink += acc;
  }
  Q_UNUSED(sink);
}

QTEST_MAIN(BenchVirtualScrollMath)
#include "bench_virtualscrollmath.moc"
