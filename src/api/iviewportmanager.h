#ifndef IVIEWPORTMANAGER_H
#define IVIEWPORTMANAGER_H

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
 * Plain abstract class, not a QObject — ViewportManager derives QObject
 * directly; see its multiple-inheritance declaration. Add a method here
 * only when a sibling genuinely needs to call it via ctx.
 */
class IViewportManager {
public:
  virtual ~IViewportManager() = default;

  // --- Primary Centering Operations ---
  virtual void centerItemVertically(int index, bool immediate) = 0;
  virtual void ensureItemVisible(int index, bool allowHorizontalScroll) = 0;
  virtual void ensureHorizontallyVisible(int index) = 0;
  virtual void applyImmediateViewportPositioningForSelection(int targetIndex) = 0;

  [[nodiscard]] virtual double getScrollScale() const = 0;
  [[nodiscard]] virtual int toWidgetScrollY(int logicalScrollY) const = 0;

  // --- State Accessors/Mutators ---
  virtual void setForceImmediateCenter(bool force) = 0;

  virtual void setIsWrappingNavigation(bool wrapping) = 0;
  [[nodiscard]] virtual bool isWrappingNavigation() const = 0;

  virtual void setTargetRestoreIndex(int index) = 0;

  virtual void setWrapSequenceActive(bool active) = 0;

  virtual void setContinuousScrollActive(bool active) = 0;
  [[nodiscard]] virtual bool continuousScrollActive() const = 0;

  virtual void setRepeating(bool repeating) = 0;

  virtual void setPhysicalKeyDown(bool down) = 0;

  // --- Suppression and State Management ---
  virtual void applyImmediateCenterSuppression() = 0;
};

#endif // IVIEWPORTMANAGER_H
