#include "selectioncoordinator.h"
#include "gridutils.h"
#include "selectionoverlaymanager.h"
#include <QWidget>
#include <cmath>

SelectionCoordinator::SelectionCoordinator(QObject *parent) : QObject(parent) {}

void SelectionCoordinator::setOverlayManager(SelectionOverlayManager *overlay) {
  m_overlayManager = overlay;
}

void SelectionCoordinator::setSelectedIndex(int index) {
  if (m_selectedIndex == index) {
    return;
  }
  int prevIndex = m_selectedIndex;
  m_selectedIndex = index;

  // Calculate direction
  if (prevIndex >= 0 && index >= 0) {
    int delta = index - prevIndex;
    m_direction = (delta > 0) ? 1 : (delta < 0) ? -1 : 0;
  } else {
    m_direction = 0;
  }

  emit selectionChanged(index, prevIndex);
}

auto SelectionCoordinator::analyzeMovement(int newIndex, int prevIndex,
                                           int itemsPerRow) const
    -> MovementInfo {
  MovementInfo info;

  if (prevIndex < 0 || newIndex < 0 || itemsPerRow <= 0) {
    return info;
  }

  int delta = newIndex - prevIndex;
  info.direction = (delta > 0) ? 1 : (delta < 0) ? -1 : 0;

  int absDelta = std::abs(delta);

  // Horizontal move: same row, adjacent column
  // Also treat small movements (1-2 items) as horizontal for smooth animation
  if (absDelta == 1) {
    // Single step - definitely horizontal within row or row wrap
    int prevRow = GridUtils::computeItemRow(prevIndex, itemsPerRow);
    int newRow = GridUtils::computeItemRow(newIndex, itemsPerRow);
    // Only horizontal if same row (not a row wrap)
    info.isHorizontal = (prevRow == newRow);
  } else if (absDelta == itemsPerRow) {
    // Exact row jump - vertical movement
    info.isHorizontal = false;
  } else if (absDelta < itemsPerRow) {
    // Within-row jump (e.g., from click) - treat as horizontal
    int prevRow = GridUtils::computeItemRow(prevIndex, itemsPerRow);
    int newRow = GridUtils::computeItemRow(newIndex, itemsPerRow);
    info.isHorizontal = (prevRow == newRow);
  } else {
    // Multi-row jump - vertical
    info.isHorizontal = false;
  }

  return info;
}

auto SelectionCoordinator::rectForIndex(int visualIndex, int totalItems) const
    -> QRect {
  if (visualIndex < 0 || visualIndex >= totalItems) {
    return {};
  }

  if (!m_getPosition || !m_getMetrics) {
    return {};
  }

  QPoint pos = m_getPosition(visualIndex);
  auto [width, height] = m_getMetrics();

  return SelectionOverlayManager::overlayRectForPosition(pos, width, height);
}

void SelectionCoordinator::reset() {
  m_selectedIndex = -1;
  m_committedIndex = -1;
  m_selectedRow = -1;
  m_direction = 0;
}
