/**
 * @file test_keyboardmanager.cpp
 * @brief Unit tests for KeyboardManager static selection helpers
 *
 * Covers the pure logic that drives arrow-key navigation:
 *  - calculateHorizontalSelection
 *  - calculateVerticalSelection
 *  - calculateNewSelection (dispatcher)
 *  - hasRowChanged
 *  - deriveDirectionForKey
 *
 * These methods are static and do not depend on KeyboardManager's setup
 * references, making them safe to test in isolation.
 */

#include "keyboardmanager.h"
#include <QTest>
#include <Qt>

class TestKeyboardManager : public QObject {
  Q_OBJECT

private slots:
  // calculateHorizontalSelection
  void testHorizontal_moveRightWithinRow();
  void testHorizontal_moveLeftWithinRow();
  void testHorizontal_clampAtStartNoWrap();
  void testHorizontal_clampAtEndNoWrap();
  void testHorizontal_wrapFromStartToEnd();
  void testHorizontal_wrapFromEndToStart();
  void testHorizontal_singleItem();

  // calculateVerticalSelection
  void testVertical_moveDownWithinGrid();
  void testVertical_moveUpWithinGrid();
  void testVertical_clampAtTopNoWrap();
  void testVertical_clampAtBottomNoWrap();
  void testVertical_wrapUpFromFirstRowFullColumn();
  void testVertical_wrapUpFromFirstRowPartialLastRow();
  void testVertical_wrapDownFromLastRow();
  void testVertical_wrapDownClampsToLastItem();
  void testVertical_zeroGridWidthDoesNotWrap();

  // calculateNewSelection (dispatcher)
  void testDispatcher_routesHorizontal();
  void testDispatcher_routesVertical();
  void testDispatcher_resetsDidWrap();

  // hasRowChanged
  void testHasRowChanged_sameRow();
  void testHasRowChanged_differentRow();
  void testHasRowChanged_zeroGridWidth();

  // deriveDirectionForKey
  void testDerive_leftKey();
  void testDerive_rightKey();
  void testDerive_upKey();
  void testDerive_downKey();
  void testDerive_unhandledKey();
};

// ─────────────────────────────────────────────────────────────────────────────
// calculateHorizontalSelection
// ─────────────────────────────────────────────────────────────────────────────

void TestKeyboardManager::testHorizontal_moveRightWithinRow() {
  bool didWrap = true;
  const int result = KeyboardManager::calculateHorizontalSelection(
      10, 3, 1, /*wrapEnabled=*/false, didWrap);
  QCOMPARE(result, 4);
  QVERIFY(!didWrap);
}

void TestKeyboardManager::testHorizontal_moveLeftWithinRow() {
  bool didWrap = true;
  const int result = KeyboardManager::calculateHorizontalSelection(
      10, 5, -1, /*wrapEnabled=*/false, didWrap);
  QCOMPARE(result, 4);
  QVERIFY(!didWrap);
}

void TestKeyboardManager::testHorizontal_clampAtStartNoWrap() {
  bool didWrap = true;
  const int result = KeyboardManager::calculateHorizontalSelection(
      10, 0, -1, /*wrapEnabled=*/false, didWrap);
  QCOMPARE(result, 0);
  QVERIFY(!didWrap);
}

void TestKeyboardManager::testHorizontal_clampAtEndNoWrap() {
  bool didWrap = true;
  const int result = KeyboardManager::calculateHorizontalSelection(
      10, 9, 1, /*wrapEnabled=*/false, didWrap);
  QCOMPARE(result, 9);
  QVERIFY(!didWrap);
}

void TestKeyboardManager::testHorizontal_wrapFromStartToEnd() {
  bool didWrap = false;
  const int result = KeyboardManager::calculateHorizontalSelection(
      10, 0, -1, /*wrapEnabled=*/true, didWrap);
  QCOMPARE(result, 9);
  QVERIFY(didWrap);
}

void TestKeyboardManager::testHorizontal_wrapFromEndToStart() {
  bool didWrap = false;
  const int result = KeyboardManager::calculateHorizontalSelection(
      10, 9, 1, /*wrapEnabled=*/true, didWrap);
  QCOMPARE(result, 0);
  QVERIFY(didWrap);
}

void TestKeyboardManager::testHorizontal_singleItem() {
  bool didWrap = false;
  // wrap right with single item -> wraps back to 0
  const int result = KeyboardManager::calculateHorizontalSelection(
      1, 0, 1, /*wrapEnabled=*/true, didWrap);
  QCOMPARE(result, 0);
  QVERIFY(didWrap);
}

// ─────────────────────────────────────────────────────────────────────────────
// calculateVerticalSelection
// ─────────────────────────────────────────────────────────────────────────────

void TestKeyboardManager::testVertical_moveDownWithinGrid() {
  bool didWrap = true;
  // 4-wide grid, item 5 (row 1 col 1) moving down by gridWidth=4 -> 9
  const int result = KeyboardManager::calculateVerticalSelection(
      20, 5, 4, /*wrapEnabled=*/false, 4, didWrap);
  QCOMPARE(result, 9);
  QVERIFY(!didWrap);
}

void TestKeyboardManager::testVertical_moveUpWithinGrid() {
  bool didWrap = true;
  // 4-wide grid, item 9 moving up by 4 -> 5
  const int result = KeyboardManager::calculateVerticalSelection(
      20, 9, -4, /*wrapEnabled=*/false, 4, didWrap);
  QCOMPARE(result, 5);
  QVERIFY(!didWrap);
}

void TestKeyboardManager::testVertical_clampAtTopNoWrap() {
  bool didWrap = true;
  // first row, moving up with no wrap -> clamped to 0
  const int result = KeyboardManager::calculateVerticalSelection(
      20, 1, -4, /*wrapEnabled=*/false, 4, didWrap);
  QCOMPARE(result, 0);
  QVERIFY(!didWrap);
}

void TestKeyboardManager::testVertical_clampAtBottomNoWrap() {
  bool didWrap = true;
  // last row of 5x4 grid (20 items), item 18 moving down with no wrap
  const int result = KeyboardManager::calculateVerticalSelection(
      20, 18, 4, /*wrapEnabled=*/false, 4, didWrap);
  QCOMPARE(result, 19);
  QVERIFY(!didWrap);
}

void TestKeyboardManager::testVertical_wrapUpFromFirstRowFullColumn() {
  bool didWrap = false;
  // 4-wide grid, 20 items (5 full rows). Item 2 wrap-up -> last row col 2 = 18
  const int result = KeyboardManager::calculateVerticalSelection(
      20, 2, -4, /*wrapEnabled=*/true, 4, didWrap);
  QCOMPARE(result, 18);
  QVERIFY(didWrap);
}

void TestKeyboardManager::testVertical_wrapUpFromFirstRowPartialLastRow() {
  bool didWrap = false;
  // 4-wide grid, 18 items. Last row = items 16, 17 (cols 0, 1).
  // Item 3 (col 3) wraps up -> last-row-first(16) + col(3) = 19, clamped to 17.
  const int result = KeyboardManager::calculateVerticalSelection(
      18, 3, -4, /*wrapEnabled=*/true, 4, didWrap);
  QCOMPARE(result, 17);
  QVERIFY(didWrap);
}

void TestKeyboardManager::testVertical_wrapDownFromLastRow() {
  bool didWrap = false;
  // 4-wide grid, 20 items. Item 17 (last row col 1) wrap-down -> col 1 = 1
  const int result = KeyboardManager::calculateVerticalSelection(
      20, 17, 4, /*wrapEnabled=*/true, 4, didWrap);
  QCOMPARE(result, 1);
  QVERIFY(didWrap);
}

void TestKeyboardManager::testVertical_wrapDownClampsToLastItem() {
  bool didWrap = false;
  // 4-wide grid, 3 items only. Item 2 (col 2) wrap-down: target col 2,
  // but col 2 < totalItems(3) so result is 2 (wraps to itself).
  const int result = KeyboardManager::calculateVerticalSelection(
      3, 2, 4, /*wrapEnabled=*/true, 4, didWrap);
  QCOMPARE(result, 2);
  QVERIFY(didWrap);
}

void TestKeyboardManager::testVertical_zeroGridWidthDoesNotWrap() {
  bool didWrap = true;
  // gridWidth=0 disables wrap branch; direction 0 means no movement, clamped.
  const int result = KeyboardManager::calculateVerticalSelection(
      10, 5, 0, /*wrapEnabled=*/true, 0, didWrap);
  QCOMPARE(result, 5);
  QVERIFY(!didWrap);
}

// ─────────────────────────────────────────────────────────────────────────────
// calculateNewSelection (dispatcher)
// ─────────────────────────────────────────────────────────────────────────────

void TestKeyboardManager::testDispatcher_routesHorizontal() {
  bool didWrap = false;
  const int result = KeyboardManager::calculateNewSelection(
      10, 3, 1, /*wrapEnabled=*/false, /*vertical=*/false, 4, didWrap);
  QCOMPARE(result, 4);
  QVERIFY(!didWrap);
}

void TestKeyboardManager::testDispatcher_routesVertical() {
  bool didWrap = false;
  const int result = KeyboardManager::calculateNewSelection(
      20, 5, 4, /*wrapEnabled=*/false, /*vertical=*/true, 4, didWrap);
  QCOMPARE(result, 9);
  QVERIFY(!didWrap);
}

void TestKeyboardManager::testDispatcher_resetsDidWrap() {
  bool didWrap = true; // caller-provided stale value
  const int result = KeyboardManager::calculateNewSelection(
      10, 3, 1, /*wrapEnabled=*/false, /*vertical=*/false, 4, didWrap);
  QCOMPARE(result, 4);
  QVERIFY(!didWrap); // must be reset to false
}

// ─────────────────────────────────────────────────────────────────────────────
// hasRowChanged
// ─────────────────────────────────────────────────────────────────────────────

void TestKeyboardManager::testHasRowChanged_sameRow() {
  // 4-wide grid, items 0 and 3 are both in row 0
  QVERIFY(!KeyboardManager::hasRowChanged(4, 0, 3));
  // Items 4 and 7 both in row 1
  QVERIFY(!KeyboardManager::hasRowChanged(4, 4, 7));
}

void TestKeyboardManager::testHasRowChanged_differentRow() {
  // Item 3 (row 0) -> item 4 (row 1)
  QVERIFY(KeyboardManager::hasRowChanged(4, 3, 4));
  // Item 0 (row 0) -> item 8 (row 2)
  QVERIFY(KeyboardManager::hasRowChanged(4, 0, 8));
}

void TestKeyboardManager::testHasRowChanged_zeroGridWidth() {
  QVERIFY(!KeyboardManager::hasRowChanged(0, 3, 7));
  QVERIFY(!KeyboardManager::hasRowChanged(-1, 3, 7));
}

// ─────────────────────────────────────────────────────────────────────────────
// deriveDirectionForKey
// ─────────────────────────────────────────────────────────────────────────────

void TestKeyboardManager::testDerive_leftKey() {
  int direction = 99;
  bool vertical = true;
  const bool handled =
      KeyboardManager::deriveDirectionForKey(Qt::Key_Left, 4, direction, vertical);
  QVERIFY(handled);
  QCOMPARE(direction, -1);
  QVERIFY(!vertical);
}

void TestKeyboardManager::testDerive_rightKey() {
  int direction = 99;
  bool vertical = true;
  const bool handled = KeyboardManager::deriveDirectionForKey(
      Qt::Key_Right, 4, direction, vertical);
  QVERIFY(handled);
  QCOMPARE(direction, 1);
  QVERIFY(!vertical);
}

void TestKeyboardManager::testDerive_upKey() {
  int direction = 99;
  bool vertical = false;
  const bool handled =
      KeyboardManager::deriveDirectionForKey(Qt::Key_Up, 6, direction, vertical);
  QVERIFY(handled);
  QCOMPARE(direction, -6);
  QVERIFY(vertical);
}

void TestKeyboardManager::testDerive_downKey() {
  int direction = 99;
  bool vertical = false;
  const bool handled = KeyboardManager::deriveDirectionForKey(
      Qt::Key_Down, 6, direction, vertical);
  QVERIFY(handled);
  QCOMPARE(direction, 6);
  QVERIFY(vertical);
}

void TestKeyboardManager::testDerive_unhandledKey() {
  int direction = 99;
  bool vertical = true;
  const bool handled = KeyboardManager::deriveDirectionForKey(
      Qt::Key_Space, 4, direction, vertical);
  QVERIFY(!handled);
  // Out-params are reset to defaults even on unhandled keys
  QCOMPARE(direction, 0);
  QVERIFY(!vertical);
}

QTEST_MAIN(TestKeyboardManager)
#include "test_keyboardmanager.moc"
