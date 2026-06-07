#ifndef ARROWKEYSCROLLHELPER_H
#define ARROWKEYSCROLLHELPER_H

#include <QObject>
#include <QTimer>

struct GeneralSettings;

class QScrollArea;
class QScrollBar;
class QPropertyAnimation;
class InteractionStateHolder;
class ScrollEventHandler;

/**
 * @brief Handles arrow key centering animation for virtual scrolling.
 *
 * Encapsulates the logic for smoothly scrolling to center the selected item
 * when navigating with arrow keys. Manages animation timing, suppression
 * checks, and coordination with user scroll state.
 */
class ArrowKeyScrollHelper : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ArrowKeyScrollHelper)
public:
  explicit ArrowKeyScrollHelper(QObject *parent = nullptr);
  ~ArrowKeyScrollHelper() override;

  // Dependencies
  void setScrollArea(QScrollArea *scrollArea) { m_scrollArea = scrollArea; }
  void setInteractionState(InteractionStateHolder *state) { m_state = state; }
  void setScrollEventHandler(ScrollEventHandler *handler) { m_scrollEventHandler = handler; }
  void setGeneralSettings(const GeneralSettings *settings) { m_generalSettings = settings; }

  // Metrics needed for centering calculation
  void setItemMetrics(int itemHeight, int verticalSpacing, int margins);

  /**
   * @brief Schedules an arrow key view update.
   * @param selectedIndex The currently selected item index.
   * @param extendedHold Whether arrow key is being held for rapid navigation.
   */
  void scheduleUpdate(int selectedIndex, bool extendedHold);

  /**
   * @brief Performs the arrow key centering animation.
   * @param selectedIndex The selected item index.
   * @param totalItems Total number of items in the grid.
   * @param itemsPerRow Number of items per row.
   * @param getItemPositionY Callback to get the Y position of an item.
   */
  void performUpdate(int selectedIndex, int totalItems, int itemsPerRow,
                     const std::function<int(int)> &getItemPositionY);

  /**
   * @brief Checks if arrow key update should be skipped.
   * @return true if update should be skipped due to user scroll or suppression.
   */
  [[nodiscard]] bool shouldSkipUpdate() const;

  /**
   * @brief Stops any running arrow key scroll animation.
   */
  void stopAnimation();

  /// Pure centering math behind calculateCenterTarget, exposed as a static for
  /// testing: the scroll-Y that centers an item at @p itemY within a viewport of
  /// height @p viewportHeight, given row @p itemHeight and grid @p margins.
  /// Clamped to [0, scrollMax]; pass scrollMax < 0 for "no scrollbar bound" (the
  /// result is then floored at 0 only).
  [[nodiscard]] static int centerTargetFor(int itemY, int viewportHeight, int itemHeight,
                                           int margins, int scrollMax);

signals:
  /**
   * @brief Emitted when virtual view should be updated after animation.
   */
  void requestViewUpdate();

private:
  [[nodiscard]] int calculateCenterTarget(int itemY, int viewportHeight) const;
  void setupAndStartAnimation(QScrollBar *scrollBar, int current, int target);

  QScrollArea *m_scrollArea = nullptr;
  InteractionStateHolder *m_state = nullptr;
  ScrollEventHandler *m_scrollEventHandler = nullptr;
  const GeneralSettings *m_generalSettings = nullptr;
  QTimer m_updateTimer; // Kartend-a911.5: value member; lifetime tracks owner

  // Cached metrics
  int m_itemHeight = 0;
  int m_verticalSpacing = 0;
  int m_margins = 0;
};

#endif // ARROWKEYSCROLLHELPER_H
