#ifndef ANIMATIONMANAGER_H
#define ANIMATIONMANAGER_H

#include "setuputils.h"
#include <QObject>
#include <QPointer>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>

#include <functional>

class ScrollManager;
class ArtworkManager;
class InteractionStateHolder;
struct ApplicationContext;

/// Setup struct for AnimationManager dependencies
struct AnimationManagerSetup {
  const ApplicationContext *ctx = nullptr;
  const GeneralSettings *generalSettings = nullptr;

  QScrollArea *itemScrollArea = nullptr;

  SETUP_GETTER_DECL(QScrollArea *, ItemScrollArea)
};

/// Handles scroll animation creation, configuration, and execution for
/// vertical and horizontal centering/visibility operations.
class AnimationManager : public QObject {
  Q_OBJECT
public:
  explicit AnimationManager(QObject *parent = nullptr);
  ~AnimationManager() override;

  void setupReferences(const AnimationManagerSetup &setup);

  // --- Vertical Animation ---
  /// Creates vertical animation if not already created
  void ensureVAnimCreated(QScrollBar *vScrollBar);

  /// Starts vertical animation with the given parameters
  void configureAndStartVerticalAnimation(QScrollBar *vScrollBar, int curY, int targetY,
                                          int duration, bool clickScroll, bool clickHoldAdv);

  /// Stops active vertical animations on the scrollbar
  void stopActiveVerticalAnims(QScrollBar *verticalScrollBar);

  /// Returns true if vertical animation is currently running
  [[nodiscard]] bool isVerticalAnimRunning() const;

  /// If running, updates or stops animation based on click mode.
  /// Returns true if animation was handled (caller should return early).
  /// If not click-scroll mode, updates curY and distance from scrollbar.
  [[nodiscard]] bool handleExistingVerticalAnimIfRunning(QScrollBar *verticalScrollBar, int targetY,
                                                         bool clickScroll, bool clickHoldAdv,
                                                         int &curY, int &distance);

  /// Returns the current vertical animation (may be nullptr)
  [[nodiscard]] QPropertyAnimation *verticalAnimation() const { return m_vScrollAnim; }

  /// Sets programmatic scroll property with guarded deferred clearing
  void setProgrammaticScrollGuarded(bool enable);

  /// Updates virtual view and selection during vertical animation
  void updateVirtualViewAndSelectionDuringVAnim(bool clickScroll, bool clickHoldAdv);

  // --- Horizontal Animation ---
  /// Creates horizontal animation if not already created
  void initHorizontalAnimIfNeeded(QScrollBar *hScrollBar);

  /// Animates horizontal scrolling for hold mode
  void animateHorizontalHold(QScrollBar *hScrollBar, int startX, int targetX);

  /// Animates horizontal scrolling with smooth easing
  void animateHorizontalSmooth(QScrollBar *hScrollBar, int startX, int targetX);

  /// Returns true if horizontal animation is currently running
  [[nodiscard]] bool isHorizontalAnimRunning() const;

  /// Returns the current horizontal animation (may be nullptr)
  [[nodiscard]] QPropertyAnimation *horizontalAnimation() const { return m_hScrollAnim; }

  /// Stops any arrow key scroll animation on the given scrollbar
  static void stopArrowKeyAnimationIfRunning(QScrollBar *scrollBar);

  // --- Duration Calculation ---
  /// Computes animation duration based on distance, item dimensions, repeat
  /// state, and base duration
  [[nodiscard]] static int computeVerticalCenterDuration(int distance, int itemHeight,
                                                         int verticalSpacing, bool repeatActive,
                                                         int durationMs = 1500);

  // --- Target Calculation ---
  /// Computes target Y position for centering an item
  /// @param totalHeight Clamped container height for large grids
  /// @param logicalHeight True logical height (may exceed Qt limits)
  /// @param headerOffset Offset for list header (list view mode)
  [[nodiscard]] static int computeTargetYForIndex(int index, int gridWidth, int itemHeight,
                                                  int verticalSpacing, int viewportHeight,
                                                  int scrollbarMax, int totalHeight = 0,
                                                  int logicalHeight = 0, int headerOffset = 0);

  /// Computes horizontal target X for visibility
  [[nodiscard]] static int computeHorizontalTargetX(int itemX, int collectionItemWidth, int curX,
                                                    int viewportWidth, int margins, int scrollMax);

  /// Computes desired Y for visibility (returns true if vertical scroll needed)
  [[nodiscard]] static int computeDesiredYForVisibility(int itemY, int itemHeight, int curY,
                                                        int viewportHeight, int margins,
                                                        bool &needVertical);

  // --- Ensure Visible Animation ---
  /// Starts animation for ensuring an item is visible
  void startEnsureVisibleVAnim(QScrollBar *vScrollBar, int startVal, int endVal, int itemHeight,
                               int verticalSpacing, bool isRepeating);

  // --- Wheel Scroll Animation ---
  /// Gets the current end value of vertical animation (for chaining wheel
  /// scrolls)
  [[nodiscard]] int getVerticalAnimEndValue() const;

  /// Starts a wheel scroll animation with custom finish callback
  void startWheelScrollAnimation(QScrollBar *vScrollBar, int startVal, int endVal,
                                 std::function<void()> onFinished);

  /// horizontal-axis sibling of startWheelScrollAnimation. Used
  /// by Horizontal view mode where the wheel drives the X scrollbar. Mirrors
  /// the vertical version exactly — chains from currentValue when an anim is
  /// already running so consecutive wheel notches blend instead of restart,
  /// and uses the configurable scrollAnimationDurationMs / OutCubic for the
  /// same momentum feel as vertical wheel scrolling.
  void startWheelScrollAnimationHorizontal(QScrollBar *hScrollBar, int startVal, int endVal,
                                           std::function<void()> onFinished);

signals:
  /// Emitted when vertical scroll animation finishes
  void verticalAnimationFinished();

  /// Emitted when horizontal scroll animation finishes
  void horizontalAnimationFinished();

  /// Request to update virtual view during animation
  void requestVirtualViewUpdate();

  /// Request to update selection overlay for current selection
  void requestSelectionUpdate();

  /// Request to refresh selection overlay state
  void requestSelectionOverlayRefresh();

  /// Request to start glide animation (set GlideAnimating property)
  void requestGlideAnimationStart();

private slots:
  void onVScrollAnimationFinished();
  void onHScrollAnimationFinished();

private:
  // ctx is the single source of truth for sibling managers + state.
  const ApplicationContext *m_ctx = nullptr;
  const GeneralSettings *m_generalSettings = nullptr;
  QPointer<QScrollArea> m_itemScrollArea = nullptr;

  QPropertyAnimation *m_vScrollAnim = nullptr;
  QPropertyAnimation *m_hScrollAnim = nullptr;
};

#endif // ANIMATIONMANAGER_H
