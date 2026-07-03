/**
 * @file test_interactionstateholder.cpp
 * @brief Unit tests for InteractionStateHolder
 *
 * Tests the centralized interaction state holder that replaces
 * scattered Qt dynamic properties.
 */

#include "interactionstateholder.h"
#include <QSignalSpy>
#include <QTest>

class TestInteractionStateHolder : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanup();

  // Selection suppression tests
  void testSelectionSuppression_initialState();
  void testSelectionSuppression_beginEnd();
  void testSelectionSuppression_multipleCycles();

  // Arrow center suppression tests
  void testArrowCenterSuppression_initialState();
  void testArrowCenterSuppression_suppressFor();
  void testArrowCenterSuppression_clear();

  // Glide animation state tests
  void testGlideAnimating_toggle();

  // Horiz animation state tests
  void testHorizAnimGen_increment();

  // Click state tests
  void testClickState_reset();

  // Scroll state tests
  void testScrollState_programmatic();

  // Search state tests
  void testSearchState_clearedByEscape();

  // Full reset tests
  void testResetAll();

private:
  InteractionStateHolder *m_state = nullptr;
};

void TestInteractionStateHolder::initTestCase() {
  m_state = new InteractionStateHolder(this);
}

void TestInteractionStateHolder::cleanup() {
  m_state->resetAll();
}

// ─────────────────────────────────────────────────────────────────────────────
// Selection suppression tests
// ─────────────────────────────────────────────────────────────────────────────

void TestInteractionStateHolder::testSelectionSuppression_initialState() {
  QVERIFY(!m_state->isSelectionSuppressed());
  QCOMPARE(m_state->pendingSelectionIndex(), -1);
}

void TestInteractionStateHolder::testSelectionSuppression_beginEnd() {
  m_state->beginSelectionSuppression(42);

  QVERIFY(m_state->isSelectionSuppressed());
  QCOMPARE(m_state->pendingSelectionIndex(), 42);

  int pending = m_state->endSelectionSuppression();

  QCOMPARE(pending, 42);
  QVERIFY(!m_state->isSelectionSuppressed());
  QCOMPARE(m_state->pendingSelectionIndex(), -1);
}

void TestInteractionStateHolder::testSelectionSuppression_multipleCycles() {
  // First cycle
  m_state->beginSelectionSuppression(10);
  QCOMPARE(m_state->pendingSelectionIndex(), 10);
  m_state->endSelectionSuppression();

  // Second cycle with different index
  m_state->beginSelectionSuppression(20);
  QCOMPARE(m_state->pendingSelectionIndex(), 20);
  int pending = m_state->endSelectionSuppression();
  QCOMPARE(pending, 20);
}

// ─────────────────────────────────────────────────────────────────────────────
// Arrow center suppression tests
// ─────────────────────────────────────────────────────────────────────────────

void TestInteractionStateHolder::testArrowCenterSuppression_initialState() {
  QVERIFY(!m_state->isArrowCenterSuppressed());
  QVERIFY(!m_state->arrow().suppressArrowCenter);
  QCOMPARE(m_state->arrow().suppressArrowCenterUntilMs, 0);
}

void TestInteractionStateHolder::testArrowCenterSuppression_suppressFor() {
  m_state->suppressArrowCenterFor(1000); // 1 second

  QVERIFY(m_state->arrow().suppressArrowCenter);
  QVERIFY(m_state->arrow().suppressArrowCenterUntilMs > 0);
  QVERIFY(m_state->isArrowCenterSuppressed());
}

void TestInteractionStateHolder::testArrowCenterSuppression_clear() {
  m_state->suppressArrowCenterFor(1000);
  QVERIFY(m_state->isArrowCenterSuppressed());

  m_state->clearArrowCenterSuppression();

  QVERIFY(!m_state->arrow().suppressArrowCenter);
  QCOMPARE(m_state->arrow().suppressArrowCenterUntilMs, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Glide animation state tests
// ─────────────────────────────────────────────────────────────────────────────

void TestInteractionStateHolder::testGlideAnimating_toggle() {
  QVERIFY(!m_state->glideAnimating());

  m_state->setGlideAnimating(true);
  QVERIFY(m_state->glideAnimating());

  m_state->setGlideAnimating(false);
  QVERIFY(!m_state->glideAnimating());
}

// ─────────────────────────────────────────────────────────────────────────────
// Horiz animation state tests
// ─────────────────────────────────────────────────────────────────────────────

void TestInteractionStateHolder::testHorizAnimGen_increment() {
  QCOMPARE(m_state->horizAnimGen(), 0);

  int gen1 = m_state->nextHorizAnimGen();
  QCOMPARE(gen1, 1);
  QCOMPARE(m_state->horizAnimGen(), 1);

  int gen2 = m_state->nextHorizAnimGen();
  QCOMPARE(gen2, 2);
  QCOMPARE(m_state->horizAnimGen(), 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Click state tests
// ─────────────────────────────────────────────────────────────────────────────

void TestInteractionStateHolder::testClickState_reset() {
  // Set some state
  m_state->click().rowChangeFirstClickIndex = 5;
  m_state->click().doubleClickPending = true;
  m_state->click().deferCenterOnClick = true;

  // Reset just click state
  m_state->click().reset();

  QCOMPARE(m_state->click().rowChangeFirstClickIndex, -1);
  QVERIFY(!m_state->click().doubleClickPending);
  QVERIFY(!m_state->click().deferCenterOnClick);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scroll state tests
// ─────────────────────────────────────────────────────────────────────────────

void TestInteractionStateHolder::testScrollState_programmatic() {
  QVERIFY(!m_state->scroll().programmaticScroll);
  QVERIFY(!m_state->scroll().userScrollActive);

  m_state->beginProgrammaticScroll();

  QVERIFY(m_state->scroll().programmaticScroll);
  QVERIFY(!m_state->scroll().userScrollActive);

  m_state->endProgrammaticScroll();

  QVERIFY(!m_state->scroll().programmaticScroll);
}

// ─────────────────────────────────────────────────────────────────────────────
// Search state tests
// ─────────────────────────────────────────────────────────────────────────────

void TestInteractionStateHolder::testSearchState_clearedByEscape() {
  // Initial state should be false
  QVERIFY(!m_state->search().clearedByEscape);

  // Set the flag
  m_state->search().clearedByEscape = true;
  QVERIFY(m_state->search().clearedByEscape);

  // Reset should clear it
  m_state->search().reset();
  QVERIFY(!m_state->search().clearedByEscape);
}

// ─────────────────────────────────────────────────────────────────────────────
// Full reset tests
// ─────────────────────────────────────────────────────────────────────────────

void TestInteractionStateHolder::testResetAll() {
  // Set various state
  m_state->beginSelectionSuppression(10);
  m_state->setGlideAnimating(true);
  m_state->scroll().programmaticScroll = true;
  m_state->click().deferCenterOnClick = true;
  m_state->search().clearedByEscape = true;

  // Reset all
  m_state->resetAll();

  // Verify all reset
  QVERIFY(!m_state->isSelectionSuppressed());
  QVERIFY(!m_state->glideAnimating());
  QVERIFY(!m_state->scroll().programmaticScroll);
  QVERIFY(!m_state->click().deferCenterOnClick);
  QVERIFY(!m_state->search().clearedByEscape);
  QCOMPARE(m_state->horizAnimGen(), 0);
}

QTEST_MAIN(TestInteractionStateHolder)
#include "test_interactionstateholder.moc"
