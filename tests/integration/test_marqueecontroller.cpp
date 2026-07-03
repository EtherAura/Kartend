#include "test_marqueecontroller.h"

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "marqueecontroller.h"
#include "marqueewindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QList>
#include <QTest>

namespace {

/// MarqueeWindow is a parentless top-level pinned to a QScreen — the only
/// way to observe the controller's window lifecycle from outside.
QList<MarqueeWindow *> marqueeWindows() {
  QList<MarqueeWindow *> out;
  const auto tops = QApplication::topLevelWidgets();
  for (QWidget *w : tops) {
    if (auto *marquee = qobject_cast<MarqueeWindow *>(w)) out.append(marquee);
  }
  return out;
}

/// The disable path retires the window via deleteLater(); flush those so
/// the top-level scan reflects the teardown.
void flushDeferredDeletes() {
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

struct MarqueeHarness {
  GeneralSettings settings;
  QList<CollectionConfig> collections;
  int currentIndex = 0;
  MarqueeController controller;

  MarqueeHarness() {
    CollectionConfig videos;
    videos.name = QStringLiteral("Videos");
    // No collectionIcon on purpose: collection-icon mode (mode 1) then takes
    // the clear-to-background path, which needs no artwork on disk.
    collections = {videos};
    settings.marquee.marqueeMode = 1;

    MarqueeControllerSetup setup;
    setup.ctx = nullptr; // item mode unused; ctx only feeds selectedFilePath
    setup.generalSettings = &settings;
    setup.currentCollectionIndex = &currentIndex;
    setup.collections = &collections;
    setup.isShuttingDown = []() { return false; };
    controller.setupReferences(setup);
  }
};

} // namespace

void TestMarqueeController::applyMarqueeSettings_disabledCreatesNoWindow() {
  MarqueeHarness h;
  h.settings.marquee.marqueeEnabled = false;

  h.controller.applyMarqueeSettings();
  flushDeferredDeletes();
  QVERIFY2(marqueeWindows().isEmpty(), "a disabled marquee must not construct the topper window");
}

void TestMarqueeController::applyMarqueeSettings_enableReuseDisableLifecycle() {
  MarqueeHarness h;

  // Enable: exactly one shown top-level MarqueeWindow appears.
  h.settings.marquee.marqueeEnabled = true;
  h.controller.applyMarqueeSettings();
  auto windows = marqueeWindows();
  QCOMPARE(windows.size(), 1);
  QVERIFY(windows.first()->isVisible());

  // Re-apply (settings Save with no change): idempotent — the window is
  // reused, not duplicated.
  h.controller.applyMarqueeSettings();
  windows = marqueeWindows();
  QCOMPARE(windows.size(), 1);

  // Disable: the window is torn down (deleteLater) so the user reclaims
  // the secondary monitor.
  h.settings.marquee.marqueeEnabled = false;
  h.controller.applyMarqueeSettings();
  flushDeferredDeletes();
  QVERIFY2(marqueeWindows().isEmpty(), "disabling the marquee must tear the window down");
}

void TestMarqueeController::updateMarqueeArtwork_invalidCollectionIndexLeavesWindowAlone() {
  MarqueeHarness h;
  h.settings.marquee.marqueeEnabled = true;
  h.controller.applyMarqueeSettings();
  QCOMPARE(marqueeWindows().size(), 1);

  // A collection switch can transiently leave the index out of range; the
  // artwork push must guard rather than dereference.
  h.currentIndex = -1;
  h.controller.updateMarqueeArtwork();
  QCOMPARE(marqueeWindows().size(), 1);
  h.currentIndex = h.collections.size();
  h.controller.updateMarqueeArtwork();
  QCOMPARE(marqueeWindows().size(), 1);

  // Clean up the top-level window for later suites.
  h.settings.marquee.marqueeEnabled = false;
  h.controller.applyMarqueeSettings();
  flushDeferredDeletes();
  QVERIFY(marqueeWindows().isEmpty());
}
