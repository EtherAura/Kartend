#ifndef IMOUSEMANAGER_H
#define IMOUSEMANAGER_H

#include "imouseholdcontrol.h"
#include "imousewheelstate.h"

/**
 * @brief Sibling-facing interface to mouse hold / wheel scrolling.
 *
 * Kartend-dl0uz.2: pure union of two role interfaces
 * (interface-segregation), carrying no methods of its own:
 *
 *   IMouseWheelState   ctx->mouseWheelState()  wheel-scroll-in-flight flag
 *   IMouseHoldControl  ctx->mouseHold()        button + click-hold control
 *
 * Consumers should reach for the narrowest role accessor; the facade stays
 * for consumers spanning both roles. MouseManager's many signal-driven
 * slots, click-hold-timer wiring and static widget-finding helpers stay on
 * the concrete class, which InteractionManager owns directly as a
 * unique_ptr and connects signals against.
 *
 * Plain abstract class, not a QObject — MouseManager derives QObject
 * directly; see its multiple-inheritance declaration. Add a method to one
 * of the roles only when a sibling genuinely needs to call it via ctx.
 */
class IMouseManager : public IMouseWheelState, public IMouseHoldControl {
public:
  ~IMouseManager() override = default;
};

#endif // IMOUSEMANAGER_H
