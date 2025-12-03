// Handles alphabetic navigation via PageUp/PageDown keys.
#include "alphabeticnavigationhandler.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "filtermanager.h"

#include <QFileInfo>

AlphabeticNavigationHandler::AlphabeticNavigationHandler(QObject *parent)
    : QObject(parent) {}

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

auto AlphabeticNavigationHandler::getLetterForIndex(int visualIndex) const
    -> QChar {
  const QString displayName = getDisplayNameForIndex(visualIndex);
  if (displayName.isEmpty()) {
    return QChar();
  }

  // Find the first letter character, skipping leading non-letters
  for (const QChar &ch : displayName) {
    if (ch.isLetter()) {
      return ch.toUpper();
    }
  }

  // No letter found - return first character for non-alphabetic items
  return displayName.at(0).toUpper();
}

auto AlphabeticNavigationHandler::getDisplayNameForIndex(int visualIndex) const
    -> QString {
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
    const QString folderPath =
        m_scrollManager->virtualFolderPathForVisualIndex(visualIndex);
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

auto AlphabeticNavigationHandler::findNextLetterIndex(int currentIndex,
                                                      bool forward,
                                                      int totalItems) const
    -> int {
  const QChar currentLetter = getLetterForIndex(currentIndex);
  if (currentLetter.isNull()) {
    return -1;
  }

  // Target letter is the next/previous letter in the alphabet
  QChar targetLetter = getAdjacentLetter(currentLetter, forward);
  const QChar startLetter = targetLetter;

  // Search for an item starting with the target letter
  // If not found, try subsequent letters until we wrap around
  int attempts = 0;
  constexpr int MAX_LETTER_ATTEMPTS = 27; // 26 letters + 1 for non-alpha

  while (attempts < MAX_LETTER_ATTEMPTS) {
    // Search through all items for one starting with targetLetter
    if (forward) {
      // Search forward from current position first, then wrap
      for (int i = currentIndex + 1; i < totalItems; ++i) {
        const QChar letter = getLetterForIndex(i);
        if (letter == targetLetter) {
          return i;
        }
      }
      // Wrap to beginning
      for (int i = 0; i < currentIndex; ++i) {
        const QChar letter = getLetterForIndex(i);
        if (letter == targetLetter) {
          return i;
        }
      }
    } else {
      // Search backward from current position first, then wrap
      for (int i = currentIndex - 1; i >= 0; --i) {
        const QChar letter = getLetterForIndex(i);
        if (letter == targetLetter) {
          return i;
        }
      }
      // Wrap to end
      for (int i = totalItems - 1; i > currentIndex; --i) {
        const QChar letter = getLetterForIndex(i);
        if (letter == targetLetter) {
          return i;
        }
      }
    }

    // Target letter not found, try the next adjacent letter
    targetLetter = getAdjacentLetter(targetLetter, forward);
    ++attempts;

    // If we've wrapped back to start, no different letter exists
    if (targetLetter == startLetter) {
      break;
    }
  }

  return -1;
}

auto AlphabeticNavigationHandler::getAdjacentLetter(QChar current,
                                                    bool forward) -> QChar {
  if (!current.isLetter()) {
    // For non-letters, treat as before 'A' when going forward
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
