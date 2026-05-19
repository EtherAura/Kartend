#ifndef IMOUSEMANAGER_H
#define IMOUSEMANAGER_H

/**
 * @brief Sibling-facing interface to mouse hold / wheel scrolling.
 *
 * Like INavigationManager, this is a deliberate role interface
 * (interface-segregation): the slice sibling managers reach for through
 * ApplicationContext, not MouseManager's full surface (its many
 * signal-driven slots, click-hold-timer wiring and static widget-finding
 * helpers stay on the concrete class, which InteractionManager owns
 * directly as a unique_ptr and connects signals against).
 *
 * Plain abstract class, not a QObject — MouseManager derives QObject
 * directly; see its multiple-inheritance declaration. Add a method here
 * only when a sibling genuinely needs to call it via ctx.
 */
class IMouseManager {
public:
  virtual ~IMouseManager() = default;

  [[nodiscard]] virtual bool isMouseHoldScrolling() const = 0;

  virtual void setWheelScrolling(bool scrolling) = 0;
  virtual void setLeftMouseDown(bool down) = 0;
  virtual void stopClickHoldTimer() = 0;
  virtual void clearHorizontalCandidate() = 0;
  virtual void stopMouseHoldScrolling() = 0;
};

#endif // IMOUSEMANAGER_H
