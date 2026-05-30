// Tests for ArtworkWidgetRegistry::enqueuePending — the same-(widget,path)
// coalesce + kMaxPending FIFO cap that bounds the pending-artwork queue during
// scroll storms. The suppressed-requeue path in ArtworkManager::applyResultsToUi
// was changed to funnel through enqueuePending so it inherits both (Kartend-ghmyu);
// these guard the mechanism it now relies on. The (widget,path) key is exercised
// with a null QPointer widget + distinct paths — enqueuePending treats the widget
// as an opaque value, so no real ItemWidget is needed.
#include "artworkmanager.h" // ArtworkInfo
#include "artworkwidgetregistry.h"

#include <QObject>
#include <QString>
#include <QTest>

class TestArtworkWidgetRegistry : public QObject {
  Q_OBJECT
private slots:
  void enqueuePending_coalescesDuplicateWidgetPath();
  void enqueuePending_distinctPathsAreKept();
  void enqueuePending_capsAtKMaxPendingWithFifoEviction();
};

void TestArtworkWidgetRegistry::enqueuePending_coalescesDuplicateWidgetPath() {
  ArtworkWidgetRegistry registry;
  ArtworkInfo info;
  info.mediaItem = nullptr;
  info.artworkPath = QStringLiteral("/art/a.png");

  registry.enqueuePending(info);
  registry.enqueuePending(info); // identical (widget,path) — coalesced away
  QCOMPARE(registry.pendingCount(), 1);
}

void TestArtworkWidgetRegistry::enqueuePending_distinctPathsAreKept() {
  ArtworkWidgetRegistry registry;
  ArtworkInfo a;
  a.mediaItem = nullptr;
  a.artworkPath = QStringLiteral("/art/a.png");
  ArtworkInfo b = a;
  b.artworkPath = QStringLiteral("/art/b.png");

  registry.enqueuePending(a);
  registry.enqueuePending(b);
  QCOMPARE(registry.pendingCount(), 2);
}

void TestArtworkWidgetRegistry::enqueuePending_capsAtKMaxPendingWithFifoEviction() {
  ArtworkWidgetRegistry registry;
  // Distinct paths (so nothing coalesces) well past the cap; pendingCount must
  // clamp to kMaxPending via FIFO eviction of the oldest entries.
  const int n = ArtworkWidgetRegistry::kMaxPending + 50;
  for (int i = 0; i < n; ++i) {
    ArtworkInfo info;
    info.mediaItem = nullptr;
    info.artworkPath = QStringLiteral("/art/%1.png").arg(i);
    registry.enqueuePending(info);
  }
  QCOMPARE(registry.pendingCount(), ArtworkWidgetRegistry::kMaxPending);
}

QTEST_MAIN(TestArtworkWidgetRegistry)
#include "test_artworkwidgetregistry.moc"
