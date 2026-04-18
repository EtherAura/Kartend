#ifndef PRESEARCHSTATEMANAGER_H
#define PRESEARCHSTATEMANAGER_H

#include <QHash>
#include <QObject>
#include <QPointer>

class QScrollArea;
class QScrollBar;
class QWidget;
class ItemWidget;
class WidgetPoolManager;
class ArtworkManager;

/**
 * @brief Manages pre-search widget state for fast search result restoration.
 *
 * When a user begins a search, this manager saves the current widget state
 * (positions, scroll position, loaded artwork) so that exiting search can
 * instantly restore the view without reloading artwork from disk.
 */
class PreSearchStateManager : public QObject {
  Q_OBJECT
public:
  explicit PreSearchStateManager(QObject *parent = nullptr);
  ~PreSearchStateManager() override = default;

  /**
   * @brief Set external dependencies.
   * @param scrollArea The scroll area containing the view
   * @param gridContainer The grid container widget (for reparenting)
   */
  void setReferences(QScrollArea *scrollArea, QWidget *gridContainer);

  // ─────────────────────────────────────────────────────────────────────────
  // State Management
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * @brief Check if there is saved pre-search state.
   */
  [[nodiscard]] bool hasSavedState() const { return !m_savedWidgets.isEmpty(); }

  /**
   * @brief Get saved scroll position.
   */
  [[nodiscard]] int savedScrollPosition() const {
    return m_savedScrollPosition;
  }

  /**
   * @brief Save current widget state before search.
   *
   * Moves active widgets to saved state and hides them, reparenting to
   * gridContainer so they survive virtual container cleanup.
   *
   * @param activeWidgets Current active widget map (will be cleared)
   * @return true if state was saved, false if already had saved state or no
   * widgets
   */
  bool saveState(QHash<int, ItemWidget *> &activeWidgets);

  /**
   * @brief Restore saved widget state after search exit.
   *
   * Deletes current search result widgets and restores saved widgets.
   *
   * @param activeWidgets Current search result widgets (will be cleared and
   * replaced)
   * @param virtualContainer Container to reparent widgets to
   * @param widgetPool Widget pool to clear (prevents double-delete)
   * @param artworkManager Artwork manager to clear references
   * @param getPositionFunc Function to get item position: (int index) -> QPoint
   * @param itemWidth Widget width
   * @param itemHeight Widget height
   * @return true if state was restored
   */
  bool restoreState(QHash<int, ItemWidget *> &activeWidgets,
                    QWidget *virtualContainer, WidgetPoolManager *widgetPool,
                    ArtworkManager *artworkManager,
                    std::function<QPoint(int)> getPositionFunc, int itemWidth,
                    int itemHeight);

  /**
   * @brief Discard saved state without restoring.
   */
  void discardSavedState();

private:
  QPointer<QScrollArea> m_scrollArea;
  QWidget *m_gridContainer = nullptr;

  QHash<int, ItemWidget *> m_savedWidgets;
  int m_savedScrollPosition = 0;
};

#endif // PRESEARCHSTATEMANAGER_H
