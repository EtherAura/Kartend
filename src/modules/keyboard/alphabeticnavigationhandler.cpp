// Handles alphabetic navigation via PageUp/PageDown keys.
#include "alphabeticnavigationhandler.h"
#include "filtermanager.h"
#include "scrollmanager.h"
#include "selectionmanager.h"

#include <QFileInfo>

AlphabeticNavigationHandler::AlphabeticNavigationHandler(QObject *parent) : QObject(parent) {}

auto AlphabeticNavigationHandler::navigateToNextLetter(bool forward) -> int {
  if (!m_scrollManager || !m_selectionManager) {
    return -1;
  }

  const int totalItems = m_scrollManager->getTotalItems();
  if (totalItems <= 0) {
    return -1;
  }

  const int currentIndex = m_selectionManager->currentSelectedIndex();
  if (currentIndex < 0 || currentIndex >= totalItems) {
    // If no valid selection, start from beginning or end
    const int startIndex = forward ? 0 : (totalItems - 1);
    emit requestSelection(startIndex);
    return startIndex;
  }

  const int newIndex = findNextLetterIndex(currentIndex, forward, totalItems);
  if (newIndex >= 0 && newIndex != currentIndex) {
    emit requestSelection(newIndex);
    return newIndex;
  }

  return -1;
}

auto AlphabeticNavigationHandler::getFirstCharForIndex(int visualIndex) const -> QChar {
  const QString displayName = getDisplayNameForIndex(visualIndex);
  if (displayName.isEmpty()) {
    return QChar();
  }

  // Return the first character (uppercased for consistent comparison)
  return displayName.at(0).toUpper();
}

auto AlphabeticNavigationHandler::getDisplayNameForIndex(int visualIndex) const -> QString {
  if (!m_scrollManager) {
    return QString();
  }

  const int totalItems = m_scrollManager->getTotalItems();
  if (visualIndex < 0 || visualIndex >= totalItems) {
    return QString();
  }

  // Get the subcollection count to determine if this is a subcollection or file
  const int subcollectionCount = m_scrollManager->getSubcollectionCount();
  const int virtualFolderCount = m_scrollManager->getVirtualFolderCount();
  const int prefixCount = subcollectionCount + virtualFolderCount;

  if (visualIndex < subcollectionCount) {
    // This is a subcollection - get its name
    return m_scrollManager->getSubcollectionName(visualIndex);
  } else if (visualIndex < prefixCount) {
    // This is a virtual folder
    const QString folderPath = m_scrollManager->virtualFolderPathForVisualIndex(visualIndex);
    return QFileInfo(folderPath).fileName();
  }

  // This is a file - get from file paths/names
  const QString filePath = m_scrollManager->filePathForVisualIndex(visualIndex);
  if (filePath.isEmpty()) {
    return QString();
  }

  // Try to get display name from file names hash
  const auto &fileNames = m_scrollManager->getFileNames();
  const QString displayName = fileNames.value(filePath);
  if (!displayName.isEmpty()) {
    return displayName;
  }

  // Fall back to base name without extension
  return QFileInfo(filePath).completeBaseName();
}

auto AlphabeticNavigationHandler::findNextLetterIndex(int currentIndex, bool forward,
                                                      int totalItems) const -> int {
  const QChar currentChar = getFirstCharForIndex(currentIndex);
  if (currentChar.isNull()) {
    return -1;
  }

  // Simple approach: find the next item that starts with a different character
  // This handles numbers, special chars, and letters uniformly
  if (forward) {
    // Search forward from current position, then wrap to beginning
    for (int i = currentIndex + 1; i < totalItems; ++i) {
      const QChar itemChar = getFirstCharForIndex(i);
      if (!itemChar.isNull() && itemChar != currentChar) {
        return i;
      }
    }
    // Wrap to beginning
    for (int i = 0; i < currentIndex; ++i) {
      const QChar itemChar = getFirstCharForIndex(i);
      if (!itemChar.isNull() && itemChar != currentChar) {
        return i;
      }
    }
  } else {
    // Search backward: find the FIRST item of the previous contiguous group
    // First, find what character the previous group starts with
    QChar prevGroupChar;
    int prevGroupFoundAt = -1;
    for (int i = currentIndex - 1; i >= 0; --i) {
      const QChar itemChar = getFirstCharForIndex(i);
      if (!itemChar.isNull() && itemChar != currentChar) {
        prevGroupChar = itemChar;
        prevGroupFoundAt = i;
        break;
      }
    }

    // If not found before current, wrap and search from end
    if (prevGroupChar.isNull()) {
      for (int i = totalItems - 1; i > currentIndex; --i) {
        const QChar itemChar = getFirstCharForIndex(i);
        if (!itemChar.isNull() && itemChar != currentChar) {
          prevGroupChar = itemChar;
          prevGroupFoundAt = i;
          break;
        }
      }
    }

    if (prevGroupChar.isNull()) {
      return -1; // All items have the same first character
    }

    // Find the start of the contiguous group containing prevGroupFoundAt
    // Walk backward from prevGroupFoundAt until we find a different character
    int groupStart = prevGroupFoundAt;
    for (int i = prevGroupFoundAt - 1; i >= 0; --i) {
      const QChar itemChar = getFirstCharForIndex(i);
      if (itemChar == prevGroupChar) {
        groupStart = i;
      } else {
        // Found a different character - the group starts at groupStart
        break;
      }
    }

    return groupStart;
  }

  return -1;
}

auto AlphabeticNavigationHandler::getAdjacentLetter(QChar current, bool forward) -> QChar {
  // This function is no longer used but kept for potential future use
  if (!current.isLetter()) {
    return forward ? QChar('A') : QChar('Z');
  }

  const QChar upper = current.toUpper();
  if (forward) {
    if (upper == 'Z') {
      return QChar('A'); // Wrap Z → A
    }
    return QChar(upper.unicode() + 1);
  } else {
    if (upper == 'A') {
      return QChar('Z'); // Wrap A → Z
    }
    return QChar(upper.unicode() - 1);
  }
}
