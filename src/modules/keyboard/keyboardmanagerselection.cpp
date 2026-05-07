// Sibling TU: pure static selection-calculation helpers for KeyboardManager.
//
// Extracted into a standalone TU so they can be linked into unit tests
// without pulling in the full KeyboardManager dependency graph (ScrollManager,
// InteractionStateHolder, ApplicationContext, etc.).
#include "keyboardmanager.h"

#include <algorithm>
#include <Qt>
#include <QtGlobal>

int KeyboardManager::calculateNewSelection(int totalItems, int currentSelection, int direction,
                                           bool wrapEnabled, bool vertical, int gridWidth,
                                           bool &didWrap, bool horizontalLayout) {
  didWrap = false;
  if (horizontalLayout) {
    // In Horizontal view, "vertical" is the *fixed* (within-column) axis;
    // "horizontal" is the *long* (between-column) axis. The column-major
    // selection helper handles wrap on the right axis for each.
    return calculateHorizontalLayoutSelection(totalItems, currentSelection, direction, wrapEnabled,
                                              gridWidth, /*downAxis=*/!vertical, didWrap);
  }
  if (vertical) {
    return calculateVerticalSelection(totalItems, currentSelection, direction, wrapEnabled,
                                      gridWidth, didWrap);
  }
  return calculateHorizontalSelection(totalItems, currentSelection, direction, wrapEnabled,
                                      didWrap);
}

int KeyboardManager::calculateHorizontalLayoutSelection(int totalItems, int currentSelection,
                                                        int direction, bool wrapEnabled,
                                                        int itemsPerCol, bool downAxis,
                                                        bool &didWrap) {
  didWrap = false;
  if (itemsPerCol <= 0 || totalItems <= 0) {
    return currentSelection;
  }
  int col = currentSelection / itemsPerCol;
  int row = currentSelection % itemsPerCol;
  int totalCols = (totalItems + itemsPerCol - 1) / itemsPerCol;
  auto rowsIn = [&](int c) {
    if (c < totalCols - 1) return itemsPerCol;
    return totalItems - c * itemsPerCol;
  };

  if (downAxis) {
    // Up/Down: step ±1 within the current column, wrapping at column edges.
    int step = (direction != 0) ? (direction > 0 ? 1 : -1) : 0;
    if (step == 0) return currentSelection;
    int newRow = row + step;
    int rowsInCol = rowsIn(col);
    if (newRow < 0) {
      if (!wrapEnabled) return currentSelection;
      newRow = rowsInCol - 1;
      didWrap = true;
    } else if (newRow >= rowsInCol) {
      if (!wrapEnabled) return currentSelection;
      newRow = 0;
      didWrap = true;
    }
    return col * itemsPerCol + newRow;
  }

  // Left/Right: step ±1 column, preserve row when possible. The wrap target
  // is the first or last column; the in-column row is clamped because
  // partial last columns may not have the same row count.
  int colStep = (direction != 0) ? (direction > 0 ? 1 : -1) : 0;
  if (colStep == 0) return currentSelection;
  int newCol = col + colStep;
  if (newCol < 0) {
    if (!wrapEnabled) return currentSelection;
    newCol = totalCols - 1;
    didWrap = true;
  } else if (newCol >= totalCols) {
    if (!wrapEnabled) return currentSelection;
    newCol = 0;
    didWrap = true;
  }
  int rowsInNewCol = rowsIn(newCol);
  int newRow = qMin(row, rowsInNewCol - 1);
  return newCol * itemsPerCol + newRow;
}

int KeyboardManager::calculateHorizontalSelection(int totalItems, int currentSelection,
                                                  int direction, bool wrapEnabled, bool &didWrap) {
  didWrap = false;
  int newSelection = currentSelection + direction;
  if (wrapEnabled) {
    if (direction == -1 && currentSelection == 0) {
      newSelection = totalItems - 1;
      didWrap = true;
    } else if (direction == 1 && currentSelection == totalItems - 1) {
      newSelection = 0;
      didWrap = true;
    }
  }
  if (!didWrap) {
    newSelection = std::max(newSelection, 0);
    if (newSelection >= totalItems) {
      newSelection = totalItems - 1;
    }
  }
  return newSelection;
}

int KeyboardManager::calculateVerticalSelection(int totalItems, int currentSelection, int direction,
                                                bool wrapEnabled, int gridWidth, bool &didWrap) {
  didWrap = false;
  int newSelection = currentSelection + direction;
  if (wrapEnabled && gridWidth > 0) {
    if (direction == -gridWidth && currentSelection < gridWidth) {
      const int lastRowFirst = ((totalItems - 1) / gridWidth) * gridWidth;
      const int targetColumn = currentSelection % gridWidth;
      const int candidate = lastRowFirst + targetColumn;
      newSelection = qMin(candidate, totalItems - 1);
      didWrap = true;
    } else if (direction == gridWidth && currentSelection + gridWidth >= totalItems) {
      newSelection = currentSelection % gridWidth;
      if (newSelection >= totalItems) {
        newSelection = totalItems - 1;
      }
      didWrap = true;
    }
  }
  if (!didWrap) {
    newSelection = std::max(newSelection, 0);
    if (newSelection >= totalItems) {
      newSelection = totalItems - 1;
    }
  }
  return newSelection;
}

bool KeyboardManager::hasRowChanged(int gridWidth, int currentSelection, int newSelection) {
  if (gridWidth <= 0) {
    return false;
  }
  return (currentSelection / gridWidth) != (newSelection / gridWidth);
}

bool KeyboardManager::deriveDirectionForKey(int key, int gridWidth, int &direction,
                                            bool &vertical) {
  direction = 0;
  vertical = false;

  switch (key) {
  case Qt::Key_Left:
    direction = -1;
    vertical = false;
    return true;
  case Qt::Key_Right:
    direction = 1;
    vertical = false;
    return true;
  case Qt::Key_Up:
    direction = -gridWidth;
    vertical = true;
    return true;
  case Qt::Key_Down:
    direction = gridWidth;
    vertical = true;
    return true;
  default:
    return false;
  }
}
