// Sibling TU: pure static selection-calculation helpers for KeyboardManager.
//
// Extracted into a standalone TU so they can be linked into unit tests
// without pulling in the full KeyboardManager dependency graph (ScrollManager,
// InteractionStateHolder, ApplicationContext, etc.).
#include "keyboardmanager.h"

#include <QtGlobal>
#include <Qt>
#include <algorithm>

int KeyboardManager::calculateNewSelection(int totalItems, int currentSelection,
                                           int direction, bool wrapEnabled,
                                           bool vertical, int gridWidth,
                                           bool &didWrap) {
  didWrap = false;
  if (vertical) {
    return calculateVerticalSelection(totalItems, currentSelection, direction,
                                      wrapEnabled, gridWidth, didWrap);
  }
  return calculateHorizontalSelection(totalItems, currentSelection, direction,
                                      wrapEnabled, didWrap);
}

int KeyboardManager::calculateHorizontalSelection(int totalItems,
                                                  int currentSelection,
                                                  int direction,
                                                  bool wrapEnabled,
                                                  bool &didWrap) {
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

int KeyboardManager::calculateVerticalSelection(int totalItems,
                                                int currentSelection,
                                                int direction, bool wrapEnabled,
                                                int gridWidth, bool &didWrap) {
  didWrap = false;
  int newSelection = currentSelection + direction;
  if (wrapEnabled && gridWidth > 0) {
    if (direction == -gridWidth && currentSelection < gridWidth) {
      const int lastRowFirst = ((totalItems - 1) / gridWidth) * gridWidth;
      const int targetColumn = currentSelection % gridWidth;
      const int candidate = lastRowFirst + targetColumn;
      newSelection = qMin(candidate, totalItems - 1);
      didWrap = true;
    } else if (direction == gridWidth &&
               currentSelection + gridWidth >= totalItems) {
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

bool KeyboardManager::hasRowChanged(int gridWidth, int currentSelection,
                                    int newSelection) {
  if (gridWidth <= 0) {
    return false;
  }
  return (currentSelection / gridWidth) != (newSelection / gridWidth);
}

bool KeyboardManager::deriveDirectionForKey(int key, int gridWidth,
                                            int &direction, bool &vertical) {
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
