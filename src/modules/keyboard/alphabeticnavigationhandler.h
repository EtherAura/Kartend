#ifndef ALPHABETICNAVIGATIONHANDLER_H
#define ALPHABETICNAVIGATIONHANDLER_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class ScrollManager;
class SelectionManager;
class FilterManager;

/**
 * @brief Handles alphabetic navigation via PageUp/PageDown keys.
 *
 * Allows users to skip through a sorted list by jumping to items starting
 * with the next or previous letter. PageDown moves forward through the
 * alphabet (A→B→C...), PageUp moves backward (C→B→A...).
 *
 * The handler works with both filtered and unfiltered views, using the
 * FilterManager to map visual indices to actual item indices.
 */
class AlphabeticNavigationHandler : public QObject {
  Q_OBJECT

public:
  explicit AlphabeticNavigationHandler(QObject *parent = nullptr);
  ~AlphabeticNavigationHandler() override = default;

  // Dependencies
  void setScrollManager(ScrollManager *manager) { m_scrollManager = manager; }
  void setSelectionManager(SelectionManager *manager) {
    m_selectionManager = manager;
  }

  /**
   * @brief Navigate to the next letter (PageDown) or previous letter (PageUp).
   * @param forward true for next letter (PageDown), false for previous
   * (PageUp).
   * @return The new selection index, or -1 if no navigation occurred.
   */
  [[nodiscard]] int navigateToNextLetter(bool forward);

  /**
   * @brief Get the starting character of an item at the given visual index.
   * @param visualIndex The visual index in the current view.
   * @return The uppercase first character, or QChar() if invalid.
   */
  [[nodiscard]] QChar getFirstCharForIndex(int visualIndex) const;

signals:
  /**
   * @brief Emitted when alphabetic navigation should select a new index.
   * @param newIndex The visual index to select.
   */
  void requestSelection(int newIndex);

private:
  /**
   * @brief Get the display name for an item at the given visual index.
   * @param visualIndex The visual index in the current view.
   * @return The display name, or empty string if invalid.
   */
  [[nodiscard]] QString getDisplayNameForIndex(int visualIndex) const;

  /**
   * @brief Find the first item starting with the next/previous letter.
   * @param currentIndex Current selection index.
   * @param forward true to search forward, false for backward.
   * @param totalItems Total number of items in the view.
   * @return The index of the found item, or -1 if none found.
   */
  [[nodiscard]] int findNextLetterIndex(int currentIndex, bool forward,
                                        int totalItems) const;

  /**
   * @brief Get the next letter in alphabetic order.
   * @param current The current letter.
   * @param forward true for next letter, false for previous.
   * @return The next/previous letter, wrapping A↔Z.
   */
  [[nodiscard]] static QChar getAdjacentLetter(QChar current, bool forward);

  ScrollManager *m_scrollManager = nullptr;
  SelectionManager *m_selectionManager = nullptr;
};

#endif // ALPHABETICNAVIGATIONHANDLER_H
