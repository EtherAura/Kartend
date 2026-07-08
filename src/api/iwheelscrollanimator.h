#ifndef IWHEELSCROLLANIMATOR_H
#define IWHEELSCROLLANIMATOR_H

#include <functional>
#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QScrollBar;
class QPropertyAnimation;
QT_END_NAMESPACE

/**
 * @brief Wheel-scroll animation role of the animation layer (Kartend-dl0uz.2).
 *
 * The slice WheelEventHandler drives: starting the chained wheel-scroll
 * animations (vertical + horizontal-view variants) and querying/stopping an
 * in-flight vertical animation when a wrap teleports the viewport instead.
 * IAnimationManager unions this role; the arrow-key / ensure-visible /
 * horizontal-hold surface stays on IAnimationManager itself.
 *
 * Plain abstract class, not a QObject — AnimationManager derives QObject
 * directly. Reached via ctx->wheelAnimator().
 */
class IWheelScrollAnimator {
public:
  virtual ~IWheelScrollAnimator() = default;

  [[nodiscard]] virtual bool isVerticalAnimRunning() const = 0;
  [[nodiscard]] virtual QPropertyAnimation *verticalAnimation() const = 0;

  virtual void startWheelScrollAnimation(QScrollBar *vScrollBar, int startVal, int endVal,
                                         std::function<void()> onFinished) = 0;
  virtual void startWheelScrollAnimationHorizontal(QScrollBar *hScrollBar, int startVal, int endVal,
                                                   std::function<void()> onFinished) = 0;
};

#endif // IWHEELSCROLLANIMATOR_H
