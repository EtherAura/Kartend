#ifndef IMOUSEWHEELSTATE_H
#define IMOUSEWHEELSTATE_H

/**
 * @brief Wheel-scrolling-flag role of the mouse layer (Kartend-dl0uz.2).
 *
 * The one bit WheelEventHandler toggles on MouseManager: "a wheel-driven
 * scroll is in flight", which the hover/click paths consult to suppress
 * hover selection during wheel motion. IMouseManager unions this role with
 * IMouseHoldControl.
 *
 * Plain abstract class, not a QObject — MouseManager derives QObject
 * directly. Reached via ctx->mouseWheelState().
 */
class IMouseWheelState {
public:
  virtual ~IMouseWheelState() = default;

  virtual void setWheelScrolling(bool scrolling) = 0;
};

#endif // IMOUSEWHEELSTATE_H
