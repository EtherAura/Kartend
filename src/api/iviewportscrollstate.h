#ifndef IVIEWPORTSCROLLSTATE_H
#define IVIEWPORTSCROLLSTATE_H

/**
 * @brief Scroll-state-flag role of the viewport layer (Kartend-dl0uz.2).
 *
 * The bookkeeping flags scroll gestures toggle so ViewportManager can tell
 * user-driven motion (wheel ticks, scrollbar drags) from programmatic
 * centering: continuous-scroll active, force-immediate-center, and the
 * wrap-sequence marker. IViewportManager unions this role with
 * IViewportPositioner.
 *
 * Plain abstract class, not a QObject — ViewportManager derives QObject
 * directly. Reached via ctx->viewportScrollState().
 */
class IViewportScrollState {
public:
  virtual ~IViewportScrollState() = default;

  virtual void setContinuousScrollActive(bool active) = 0;
  [[nodiscard]] virtual bool continuousScrollActive() const = 0;

  virtual void setForceImmediateCenter(bool force) = 0;

  virtual void setWrapSequenceActive(bool active) = 0;
};

#endif // IVIEWPORTSCROLLSTATE_H
