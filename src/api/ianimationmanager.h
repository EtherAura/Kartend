#ifndef IANIMATIONMANAGER_H
#define IANIMATIONMANAGER_H

#include <functional>
#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QScrollBar;
class QPropertyAnimation;
QT_END_NAMESPACE

/**
 * @brief Sibling-facing interface to scroll-animation orchestration.
 *
 * Like INavigationManager, this is a deliberate role interface
 * (interface-segregation): the slice sibling managers reach for through
 * ApplicationContext, not AnimationManager's full surface (its
 * signal-driven slots, lifecycle wiring and static duration/target
 * calculators stay on the concrete class, which InteractionManager owns
 * directly as a unique_ptr and connects signals against).
 *
 * Plain abstract class, not a QObject — AnimationManager derives QObject
 * directly; see its multiple-inheritance declaration. Add a method here
 * only when a sibling genuinely needs to call it via ctx.
 */
class IAnimationManager {
public:
  virtual ~IAnimationManager() = default;

  // --- Vertical Animation ---
  virtual void ensureVAnimCreated(QScrollBar *vScrollBar) = 0;
  virtual void configureAndStartVerticalAnimation(QScrollBar *vScrollBar, int curY, int targetY,
                                                  int duration, bool clickScroll,
                                                  bool clickHoldAdv) = 0;
  virtual void stopActiveVerticalAnims(QScrollBar *verticalScrollBar) = 0;
  [[nodiscard]] virtual bool isVerticalAnimRunning() const = 0;
  [[nodiscard]] virtual bool handleExistingVerticalAnimIfRunning(QScrollBar *verticalScrollBar,
                                                                 int targetY, bool clickScroll,
                                                                 bool clickHoldAdv, int &curY,
                                                                 int &distance) = 0;
  [[nodiscard]] virtual QPropertyAnimation *verticalAnimation() const = 0;

  // --- Horizontal Animation ---
  virtual void initHorizontalAnimIfNeeded(QScrollBar *hScrollBar) = 0;
  virtual void animateHorizontalHold(QScrollBar *hScrollBar, int startX, int targetX) = 0;
  virtual void animateHorizontalSmooth(QScrollBar *hScrollBar, int startX, int targetX) = 0;
  [[nodiscard]] virtual bool isHorizontalAnimRunning() const = 0;
  [[nodiscard]] virtual QPropertyAnimation *horizontalAnimation() const = 0;

  // --- Ensure Visible Animation ---
  virtual void startEnsureVisibleVAnim(QScrollBar *vScrollBar, int startVal, int endVal,
                                       int itemHeight, int verticalSpacing, bool isRepeating) = 0;

  // --- Wheel Scroll Animation ---
  virtual void startWheelScrollAnimation(QScrollBar *vScrollBar, int startVal, int endVal,
                                         std::function<void()> onFinished) = 0;
  virtual void startWheelScrollAnimationHorizontal(QScrollBar *hScrollBar, int startVal, int endVal,
                                                   std::function<void()> onFinished) = 0;
};

#endif // IANIMATIONMANAGER_H
