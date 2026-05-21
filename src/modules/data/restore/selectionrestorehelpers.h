#ifndef SELECTIONRESTOREHELPERS_H
#define SELECTIONRESTOREHELPERS_H

#include <QString>

// Pure helpers extracted from SelectionRestoreCoordinator. The manager owns the
// QTimer + ApplicationContext + lambda dispatch wiring; these are the
// decisions in between, so the validity / clamping / "should I restore?"
// rules can be unit-tested without the full UI graph.
namespace SelectionRestoreHelpers {

// True when the user-supplied search field, after trimming, has content.
// A null/empty pointer is treated as "not active". Whitespace-only counts
// as inactive — the search bar is logically idle until the user types a
// non-whitespace character.
[[nodiscard]] bool searchTextIsActive(const QString &rawText);

// Decision for whether selection restore should run at all. All four
// preconditions must hold: the user has opted into rememberSelection, no
// search is active, and the two sibling managers exist. Returning false
// is the safe default — the manager bails out silently.
[[nodiscard]] bool shouldRestoreSelection(bool rememberSelection, bool searchActive,
                                          bool hasScrollManager, bool hasInteractionManager);

// Clamps a candidate selection index against the current total.
// Contract:
//   total <= 0           -> -1   (no items, nothing to restore)
//   selIdx < 0           -> -1   (no stored selection)
//   selIdx >= total      -> total - 1  (stored index outdated by a delete)
//   otherwise            -> selIdx
[[nodiscard]] int clampRestoreIndex(int selIdx, int total);

// Predicate used by every restore-validation lambda. The restore should
// only proceed when the user is still looking at the same collection
// the restore was scheduled for AND no newer restore has bumped the
// token. The manager wraps this with the additional "manager destroyed"
// + "app shutting down" checks, which can't be expressed as pure data.
[[nodiscard]] bool restoreStillValid(int currentCollectionIndex, int scheduledCollectionIndex,
                                     int currentToken, int expectedToken);

} // namespace SelectionRestoreHelpers

#endif // SELECTIONRESTOREHELPERS_H
