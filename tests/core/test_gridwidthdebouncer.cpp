// Tests for GridWidthDebouncer — the three-stage (save / precalc / finalize)
// debounced pipeline behind the Ctrl++ / Ctrl+- grid-width shortcuts
// (Kartend-tu2hq, first standalone src/core unit test).
//
// The pipeline is timer-driven. Instead of sleeping past the production
// delays with fixed QTest::qWait calls (the flake-prone anti-pattern removed
// tree-wide; this suite was the last holdout — Kartend-e9jmu), each test
// injects 1 ms stage delays via GridWidthDebouncer::StageDelays and converges
// with QTRY_COMPARE. QTEST_GUILESS_MAIN keeps it to a QCoreApplication — no
// display, no widgets.

#include <QTest>

#include "gridwidthdebouncer.h"

namespace {
// 1 ms per stage: fast enough that QTRY_* converges immediately, non-zero so
// the DebouncedTimer still exercises its real timer path.
constexpr GridWidthDebouncer::StageDelays kFastDelays{1, 1, 1};
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
  GridWidthDebouncer deb(nullptr, kFastDelays);
  int saves = 0;
  deb.wire([&saves]() { ++saves; }, []() {}, []() {});

  // A burst of width changes must debounce into a single persistence write.
  deb.triggerSave();
  deb.triggerSave();
  deb.triggerSave();
  QCOMPARE(saves, 0); // nothing fires synchronously

  QTRY_COMPARE(saves, 1);

  // A full second debounce round (trigger → fire) gives the coalesced burst
  // every chance to produce a spurious extra call before we assert it didn't.
  deb.triggerSave();
  QTRY_COMPARE(saves, 2);
}

void TestGridWidthDebouncer::precalcTriggersPrecalcThenFinalize() {
  GridWidthDebouncer deb(nullptr, kFastDelays);
  int precalc = 0;
  int finalize = 0;
  deb.wire([]() {}, [&precalc]() { ++precalc; }, [&finalize]() { ++finalize; });

  deb.triggerPrecalc();
  QCOMPARE(precalc, 0);
  QCOMPARE(finalize, 0);

  // precalc fires first, then chains finalize.
  QTRY_COMPARE(precalc, 1);
  QTRY_COMPARE(finalize, 1);
  QCOMPARE(precalc, 1); // finalize must not re-run precalc
}

void TestGridWidthDebouncer::saveStageDoesNotFirePrecalcOrFinalize() {
  GridWidthDebouncer deb(nullptr, kFastDelays);
  int saves = 0;
  int precalc = 0;
  int finalize = 0;
  deb.wire([&saves]() { ++saves; }, [&precalc]() { ++precalc; }, [&finalize]() { ++finalize; });

  // The save stage is independent — it must not drive the layout stages.
  deb.triggerSave();
  QTRY_COMPARE(saves, 1);

  // Run a second full save round so any precalc/finalize timer mistakenly
  // armed by the first trigger (same 1 ms delay) would have fired by now.
  deb.triggerSave();
  QTRY_COMPARE(saves, 2);
  QCOMPARE(precalc, 0);
  QCOMPARE(finalize, 0);
}

void TestGridWidthDebouncer::triggersAreNoOpWhenUnwired() {
  // No wire() call: triggers must be safe no-ops (no crash on null callbacks).
  GridWidthDebouncer deb(nullptr, kFastDelays);
  deb.triggerSave();
  deb.triggerPrecalc();

  // Use a wired sibling with identical delays as the clock: once its full
  // precalc → finalize chain has run, the unwired instance's timers (armed
  // earlier, same delays) have certainly fired too.
  GridWidthDebouncer clock(nullptr, kFastDelays);
  int finalize = 0;
  clock.wire([]() {}, []() {}, [&finalize]() { ++finalize; });
  clock.triggerSave();
  clock.triggerPrecalc();
  QTRY_COMPARE(finalize, 1);
  // Reaching here without crashing is the assertion.
  QVERIFY(true);
}

QTEST_GUILESS_MAIN(TestGridWidthDebouncer)
#include "test_gridwidthdebouncer.moc"
