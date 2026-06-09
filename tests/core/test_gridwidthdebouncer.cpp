// Tests for GridWidthDebouncer — the three-stage (save / precalc / finalize)
// debounced pipeline behind the Ctrl++ / Ctrl+- grid-width shortcuts
// (Kartend-tu2hq, first standalone src/core unit test).
//
// The pipeline is timer-driven, so these tests drive a real event loop via
// QTest::qWait with generous margins over the production delays
// (SHORT/MEDIUM/LONG = 50/100/200 ms). QTEST_GUILESS_MAIN keeps it to a
// QCoreApplication — no display, no widgets.

#include <QTest>

#include "gridwidthdebouncer.h"
#include "uiconstants/timing.h"

namespace {
// Slack added on top of a stage's nominal delay so a loaded CI host doesn't
// flake. The pipeline's longest single hop is LONG (precalc) then MEDIUM
// (finalize); we wait well past both.
constexpr int kSlackMs = 250;
} // namespace

class TestGridWidthDebouncer : public QObject {
  Q_OBJECT
private slots:
  void saveStageCoalescesBurstIntoOneCall();
  void precalcTriggersPrecalcThenFinalize();
  void saveStageDoesNotFirePrecalcOrFinalize();
  void triggersAreNoOpWhenUnwired();
};

void TestGridWidthDebouncer::saveStageCoalescesBurstIntoOneCall() {
  GridWidthDebouncer deb;
  int saves = 0;
  deb.wire([&saves]() { ++saves; }, []() {}, []() {});

  // A burst of width changes must debounce into a single persistence write.
  deb.triggerSave();
  deb.triggerSave();
  deb.triggerSave();
  QCOMPARE(saves, 0); // nothing fires synchronously

  QTest::qWait(UIConstants::Timing::LONG_DELAY_MS + kSlackMs);
  QCOMPARE(saves, 1);
}

void TestGridWidthDebouncer::precalcTriggersPrecalcThenFinalize() {
  GridWidthDebouncer deb;
  int precalc = 0;
  int finalize = 0;
  deb.wire([]() {}, [&precalc]() { ++precalc; }, [&finalize]() { ++finalize; });

  deb.triggerPrecalc();
  QCOMPARE(precalc, 0);
  QCOMPARE(finalize, 0);

  // precalc fires after LONG, then chains finalize after MEDIUM.
  QTest::qWait(UIConstants::Timing::LONG_DELAY_MS + UIConstants::Timing::MEDIUM_DELAY_MS +
               kSlackMs);
  QCOMPARE(precalc, 1);
  QCOMPARE(finalize, 1);
}

void TestGridWidthDebouncer::saveStageDoesNotFirePrecalcOrFinalize() {
  GridWidthDebouncer deb;
  int saves = 0;
  int precalc = 0;
  int finalize = 0;
  deb.wire([&saves]() { ++saves; }, [&precalc]() { ++precalc; }, [&finalize]() { ++finalize; });

  // The save stage is independent — it must not drive the layout stages.
  deb.triggerSave();
  QTest::qWait(UIConstants::Timing::LONG_DELAY_MS + UIConstants::Timing::MEDIUM_DELAY_MS +
               kSlackMs);
  QCOMPARE(saves, 1);
  QCOMPARE(precalc, 0);
  QCOMPARE(finalize, 0);
}

void TestGridWidthDebouncer::triggersAreNoOpWhenUnwired() {
  // No wire() call: triggers must be safe no-ops (no crash on null callbacks).
  GridWidthDebouncer deb;
  deb.triggerSave();
  deb.triggerPrecalc();
  QTest::qWait(UIConstants::Timing::LONG_DELAY_MS + UIConstants::Timing::MEDIUM_DELAY_MS +
               kSlackMs);
  // Reaching here without crashing is the assertion.
  QVERIFY(true);
}

QTEST_GUILESS_MAIN(TestGridWidthDebouncer)
#include "test_gridwidthdebouncer.moc"
