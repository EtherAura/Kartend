#ifndef IVIEWPORTMANAGER_H
#define IVIEWPORTMANAGER_H

#include "iviewportpositioner.h"
#include "iviewportscrollstate.h"

/**
 * @brief Sibling-facing interface to viewport positioning / centering.
 *
 * Like INavigationManager, this is a deliberate role interface
 * (interface-segregation): the slice sibling managers reach for through
 * ApplicationContext, not ViewportManager's full surface (its
 * signal-driven slots, animation callbacks and private centering helpers
 * stay on the concrete class, which InteractionManager owns directly as a
 * unique_ptr and connects signals against).
 *
 * Two narrower slices live on role interfaces this one unions
 * (Kartend-dl0uz.2) — consumers needing only those take the role accessor:
 *
 *   IViewportScrollState  ctx->viewportScrollState()  scroll bookkeeping flags
 *   IViewportPositioner   ctx->viewportPositioner()   visibility + Y mapping
 *
 * Plain abstract class, not a QObject — ViewportManager derives QObject
 * directly; see its multiple-inheritance declaration. Add a method here
 * only when a sibling genuinely needs to call it via ctx.
 */
class IViewportManager : public IViewportScrollState, public IViewportPositioner {
public:
  ~IViewportManager() override = default;

  // --- Primary Centering Operations ---
  virtual void centerItemVertically(int index, bool immediate) = 0;
  virtual void ensureHorizontallyVisible(int index) = 0;

  // --- State Accessors/Mutators ---
  virtual void setIsWrappingNavigation(bool wrapping) = 0;
  [[nodiscard]] virtual bool isWrappingNavigation() const = 0;

  virtual void setTargetRestoreIndex(int index) = 0;

  virtual void setRepeating(bool repeating) = 0;

  virtual void setPhysicalKeyDown(bool down) = 0;

  // --- Suppression and State Management ---
  virtual void applyImmediateCenterSuppression() = 0;
};

#endif // IVIEWPORTMANAGER_H
