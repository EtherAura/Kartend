// Tests for SelectionRestoreHelpers — pure helpers extracted from
// SelectionRestoreCoordinator. The manager itself owns timers + ApplicationContext
// + lambda dispatch; these are the predicates and the clamp it leans on.

#include <QTest>

#include "selectionrestorehelpers.h"

using SelectionRestoreHelpers::clampRestoreIndex;
using SelectionRestoreHelpers::restoreStillValid;
using SelectionRestoreHelpers::searchTextIsActive;
using SelectionRestoreHelpers::shouldRestoreSelection;

class TestSelectionRestoreHelpers : public QObject {
  Q_OBJECT
private slots:
  // searchTextIsActive
  void searchTextIsActive_emptyIsInactive();
  void searchTextIsActive_whitespaceOnlyIsInactive();
  void searchTextIsActive_nonWhitespaceIsActive();
  void searchTextIsActive_trimmedSurroundingWhitespaceCountsContent();

  // shouldRestoreSelection
  void shouldRestoreSelection_allConditionsTrueReturnsTrue();
  void shouldRestoreSelection_rememberDisabledReturnsFalse();
  void shouldRestoreSelection_searchActiveSuppresses();
  void shouldRestoreSelection_missingScrollManagerReturnsFalse();
  void shouldRestoreSelection_missingInteractionManagerReturnsFalse();

  // clampRestoreIndex
  void clampRestoreIndex_zeroTotalAlwaysReturnsMinusOne();
  void clampRestoreIndex_negativeTotalReturnsMinusOne();
  void clampRestoreIndex_negativeSelIdxReturnsMinusOne();
  void clampRestoreIndex_inRangeValuePassesThrough();
  void clampRestoreIndex_indexAtTotalClampsToLast();
  void clampRestoreIndex_indexFarPastTotalClampsToLast();
  void clampRestoreIndex_indexAtZeroIsKept();

  // restoreStillValid
  void restoreStillValid_matchingCollectionAndTokenReturnsTrue();
  void restoreStillValid_collectionMismatchReturnsFalse();
  void restoreStillValid_tokenMismatchReturnsFalse();
  void restoreStillValid_bothMismatchReturnsFalse();
  void restoreStillValid_zeroTokensMatchEachOther();
};

// --------------------------- searchTextIsActive -----------------------------

void TestSelectionRestoreHelpers::searchTextIsActive_emptyIsInactive() {
  QVERIFY(!searchTextIsActive(QString()));
  QVERIFY(!searchTextIsActive(QStringLiteral("")));
}

void TestSelectionRestoreHelpers::searchTextIsActive_whitespaceOnlyIsInactive() {
  // Surrounding-only whitespace is treated as "user hasn't typed anything
  // meaningful yet" — matches the manager's pre-helper behavior.
  QVERIFY(!searchTextIsActive(QStringLiteral("   ")));
  QVERIFY(!searchTextIsActive(QStringLiteral("\t\n")));
}

void TestSelectionRestoreHelpers::searchTextIsActive_nonWhitespaceIsActive() {
  QVERIFY(searchTextIsActive(QStringLiteral("mario")));
}

void TestSelectionRestoreHelpers::searchTextIsActive_trimmedSurroundingWhitespaceCountsContent() {
  QVERIFY(searchTextIsActive(QStringLiteral("  mario  ")));
}

// --------------------------- shouldRestoreSelection -------------------------

void TestSelectionRestoreHelpers::shouldRestoreSelection_allConditionsTrueReturnsTrue() {
  QVERIFY(shouldRestoreSelection(/*remember=*/true, /*searchActive=*/false, /*hasScroll=*/true,
                                 /*hasInteraction=*/true));
}

void TestSelectionRestoreHelpers::shouldRestoreSelection_rememberDisabledReturnsFalse() {
  QVERIFY(!shouldRestoreSelection(false, false, true, true));
}

void TestSelectionRestoreHelpers::shouldRestoreSelection_searchActiveSuppresses() {
  // Restore overriding what the user is actively searching would be very
  // disruptive — once the search bar has content, don't fight the user.
  QVERIFY(!shouldRestoreSelection(true, /*searchActive=*/true, true, true));
}

void TestSelectionRestoreHelpers::shouldRestoreSelection_missingScrollManagerReturnsFalse() {
  QVERIFY(!shouldRestoreSelection(true, false, /*hasScroll=*/false, true));
}

void TestSelectionRestoreHelpers::shouldRestoreSelection_missingInteractionManagerReturnsFalse() {
  QVERIFY(!shouldRestoreSelection(true, false, true, /*hasInteraction=*/false));
}

// --------------------------- clampRestoreIndex ------------------------------

void TestSelectionRestoreHelpers::clampRestoreIndex_zeroTotalAlwaysReturnsMinusOne() {
  QCOMPARE(clampRestoreIndex(0, 0), -1);
  QCOMPARE(clampRestoreIndex(5, 0), -1);
  QCOMPARE(clampRestoreIndex(-1, 0), -1);
}

void TestSelectionRestoreHelpers::clampRestoreIndex_negativeTotalReturnsMinusOne() {
  // Total should never be negative in practice, but the helper degrades
  // gracefully rather than producing a clamp value below -1.
  QCOMPARE(clampRestoreIndex(3, -1), -1);
}

void TestSelectionRestoreHelpers::clampRestoreIndex_negativeSelIdxReturnsMinusOne() {
  QCOMPARE(clampRestoreIndex(-1, 10), -1);
  QCOMPARE(clampRestoreIndex(-99, 10), -1);
}

void TestSelectionRestoreHelpers::clampRestoreIndex_inRangeValuePassesThrough() {
  QCOMPARE(clampRestoreIndex(0, 10), 0);
  QCOMPARE(clampRestoreIndex(5, 10), 5);
  QCOMPARE(clampRestoreIndex(9, 10), 9);
}

void TestSelectionRestoreHelpers::clampRestoreIndex_indexAtTotalClampsToLast() {
  // Off-by-one stored from a session before a delete: clamp to the new last.
  QCOMPARE(clampRestoreIndex(10, 10), 9);
}

void TestSelectionRestoreHelpers::clampRestoreIndex_indexFarPastTotalClampsToLast() {
  QCOMPARE(clampRestoreIndex(9999, 10), 9);
}

void TestSelectionRestoreHelpers::clampRestoreIndex_indexAtZeroIsKept() {
  QCOMPARE(clampRestoreIndex(0, 1), 0);
}

// --------------------------- restoreStillValid ------------------------------

void TestSelectionRestoreHelpers::restoreStillValid_matchingCollectionAndTokenReturnsTrue() {
  QVERIFY(restoreStillValid(/*currentColl=*/3, /*scheduledColl=*/3, /*currentTok=*/7,
                            /*expectedTok=*/7));
}

void TestSelectionRestoreHelpers::restoreStillValid_collectionMismatchReturnsFalse() {
  // User navigated away — any pending restore is stale.
  QVERIFY(!restoreStillValid(/*currentColl=*/4, /*scheduledColl=*/3, /*currentTok=*/7,
                             /*expectedTok=*/7));
}

void TestSelectionRestoreHelpers::restoreStillValid_tokenMismatchReturnsFalse() {
  // A newer restore was scheduled; this one is stale even though the
  // collection still matches.
  QVERIFY(!restoreStillValid(/*currentColl=*/3, /*scheduledColl=*/3, /*currentTok=*/8,
                             /*expectedTok=*/7));
}

void TestSelectionRestoreHelpers::restoreStillValid_bothMismatchReturnsFalse() {
  QVERIFY(!restoreStillValid(/*currentColl=*/4, /*scheduledColl=*/3, /*currentTok=*/8,
                             /*expectedTok=*/7));
}

void TestSelectionRestoreHelpers::restoreStillValid_zeroTokensMatchEachOther() {
  // Initial state: state token starts at 0, first restore captures token 0
  // before ++ bumps it. The predicate must accept the equal-zero case so
  // initial restores aren't all rejected as stale.
  QVERIFY(restoreStillValid(0, 0, 0, 0));
}

QTEST_APPLESS_MAIN(TestSelectionRestoreHelpers)
#include "test_selectionrestorehelpers.moc"
