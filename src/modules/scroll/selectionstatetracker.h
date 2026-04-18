#ifndef SELECTIONSTATETRACKER_H
#define SELECTIONSTATETRACKER_H

#include "gridutils.h"

/**
 * @brief Tracks selection state for virtual scrolling.
 *
 * Encapsulates the selection tracking variables that were previously
 * scattered across ScrollManager. Provides a cohesive interface for:
 * - Current and committed selection indices
 * - Selection direction (up/down movement)
 * - Row tracking for vertical navigation
 *
 * Thread-safety: Not thread-safe. All access must be from main thread.
 */
class SelectionStateTracker {
public:
  SelectionStateTracker() = default;

  // --- Selection Index ---

  [[nodiscard]] int lastSelectedIndex() const { return m_lastSelectedIndex; }
  [[nodiscard]] int committedSelectedIndex() const {
    return m_committedSelectedIndex;
  }

  void setLastSelectedIndex(int index) { m_lastSelectedIndex = index; }
  void setCommittedSelectedIndex(int index) {
    m_committedSelectedIndex = index;
  }

  // --- Selection Direction ---

  /// Returns -1 (up), 0 (none), or 1 (down)
  [[nodiscard]] int selectionDirection() const { return m_selectionDirection; }

  void setSelectionDirection(int direction) {
    m_selectionDirection = direction;
  }

  // --- Row Tracking ---

  [[nodiscard]] int lastSelectedRow() const { return m_lastSelectedRow; }

  void setLastSelectedRow(int row) { m_lastSelectedRow = row; }

  // --- Convenience Methods ---

  /// Updates direction based on movement from prevIndex to newIndex
  void updateDirection(int newIndex, int prevIndex) {
    if (prevIndex < 0) {
      m_selectionDirection = 0;
      return;
    }
    int delta = newIndex - prevIndex;
    if (delta == 0) {
      m_selectionDirection = 0;
    } else if (delta > 0) {
      m_selectionDirection = 1;
    } else {
      m_selectionDirection = -1;
    }
  }

  /// Updates row based on index and items per row
  void updateRow(int index, int itemsPerRow) {
    m_lastSelectedRow = GridUtils::computeItemRow(index, itemsPerRow);
  }

  /// Full state update: direction, row, and last selected index
  void updateForNewSelection(int newIndex, int prevIndex, int itemsPerRow) {
    updateDirection(newIndex, prevIndex);
    updateRow(newIndex, itemsPerRow);
    m_lastSelectedIndex = newIndex;
  }

  /// Commits the current selection (makes it the "confirmed" selection)
  void commitSelection(int index) { m_committedSelectedIndex = index; }

  /// Resets all state to initial values
  void reset() {
    m_lastSelectedIndex = -1;
    m_lastSelectedRow = -1;
    m_selectionDirection = 0;
    m_committedSelectedIndex = -1;
  }

  /// Returns true if there's a valid last selection
  [[nodiscard]] bool hasSelection() const { return m_lastSelectedIndex >= 0; }

  /// Returns true if the committed selection differs from the given index
  [[nodiscard]] bool needsCommitUpdate(int index) const {
    return m_committedSelectedIndex >= 0 && m_committedSelectedIndex != index;
  }

private:
  int m_lastSelectedIndex = -1;
  int m_lastSelectedRow = -1;
  int m_selectionDirection = 0;
  int m_committedSelectedIndex = -1;
};

#endif // SELECTIONSTATETRACKER_H
