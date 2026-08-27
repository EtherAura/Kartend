#include "test_marqueecontroller.h"

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "marqueecontroller.h"
#include "marqueewindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QList>
#include <QScreen>
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

// Kartend-599xq: resolveMarqueeScreen was reached ONLY from
// applyMarqueeSettings — startup and Settings-save — so unplugging the marquee
// monitor mid-session left the window pinned to a screen that no longer
// existed until the next save or restart.
//
// QGuiApplication's screen signals cannot be emitted from outside Qt, so the
// wiring and the reaction are checked separately: this case proves the
// connections exist, and the two below drive the handler directly through the
// meta-object. The physical unplug itself stays a manual check.
void TestMarqueeController::screenChanges_areWiredAtConstruction() {
  MarqueeHarness h;
  // disconnect() returns true only if a matching connection was there to
  // remove, which is the assertion — the teardown is incidental.
  QVERIFY2(QObject::disconnect(qApp, &QGuiApplication::screenRemoved, &h.controller, nullptr),
           "screenRemoved must be wired, or an unplugged marquee monitor goes unnoticed");
  QVERIFY2(QObject::disconnect(qApp, &QGuiApplication::screenAdded, &h.controller, nullptr),
           "screenAdded must be wired, or replugging the monitor never brings the marquee home");
}

void TestMarqueeController::screenChange_repinsWithoutDuplicatingTheWindow() {
  MarqueeHarness h;
  h.settings.marquee.marqueeEnabled = true;
  h.controller.applyMarqueeSettings();
  QCOMPARE(marqueeWindows().size(), 1);

  // A dock or undock emits several of these at once; they must collapse into
  // one re-pin and must reuse the existing window rather than stacking toppers.
  for (int i = 0; i < 3; ++i) {
    QVERIFY(QMetaObject::invokeMethod(&h.controller, "handleScreenConfigurationChanged",
                                      Qt::DirectConnection, Q_ARG(QScreen *, nullptr)));
  }
  // The re-pin is deferred one event-loop turn on purpose (the removed QScreen
  // is still listed at signal time), so let that turn run.
  QTest::qWait(20);

  const auto windows = marqueeWindows();
  QCOMPARE(windows.size(), 1);
  QVERIFY2(windows.first()->isVisible(), "the marquee must still be shown after a re-pin");

  h.settings.marquee.marqueeEnabled = false;
  h.controller.applyMarqueeSettings();
  flushDeferredDeletes();
  QVERIFY(marqueeWindows().isEmpty());
}

void TestMarqueeController::screenChange_isInertWhileTheMarqueeIsDisabled() {
  MarqueeHarness h;
  h.settings.marquee.marqueeEnabled = false;

  QVERIFY(QMetaObject::invokeMethod(&h.controller, "handleScreenConfigurationChanged",
                                    Qt::DirectConnection, Q_ARG(QScreen *, nullptr)));
  QTest::qWait(20);
  flushDeferredDeletes();
  QVERIFY2(marqueeWindows().isEmpty(),
           "a screen change must not conjure a topper the user has turned off");
}
