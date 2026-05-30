#ifndef IINTERACTIONMANAGER_H
#define IINTERACTIONMANAGER_H

#include <QString>

class AttractManager;
class GamepadManager;

/**
 * @brief Sibling-facing interface to the input-coordination layer.
 *
 * Like INavigationManager and IDetailsPaneManager, this is a deliberate role
 * interface (interface-segregation): the slice sibling managers reach for
 * through ApplicationContext, not InteractionManager's full surface (its
 * many input-event slots and lifecycle wiring stay on the concrete class,
 * which the owner — ApplicationManager / MainWindow — holds directly).
 *
 * Most owned sub-manager accessors stay concrete-only, but attractManager()
 * and gamepadManager() are promoted here (Kartend-qjtz) so ctx callers — e.g.
 * the settings dialog's gamepad-capture controller — reach them through
 * IInteractionManager instead of an IMainWindow forwarder to the concrete type.
 *
 * Plain abstract class, not a QObject — InteractionManager derives QObject
 * directly; see its multiple-inheritance declaration. Add a method here only
 * when a sibling genuinely needs to call it via ctx.
 */
class IInteractionManager {
public:
  virtual ~IInteractionManager() = default;

  [[nodiscard]] virtual int currentSelectedIndex() const = 0;
  virtual void clearSelectionAndFocus() = 0;
  virtual void initializeSearchModeForCurrentCollection() = 0;
  virtual void beginSelectionRestore(int targetIndex) = 0;
  virtual void cancelPendingSelectionRestore() = 0;
  virtual void resetSelectionRestoreState() = 0;
  virtual void stopRepeat(bool suppressRecentering = false) = 0;
  virtual void stopScrollAnimations() = 0;
  [[nodiscard]] virtual bool isWheelScrolling() const = 0;
  virtual void setNavigationInProgress(bool inProgress) = 0;
  [[nodiscard]] virtual QString selectedFilePath() const = 0;

  virtual void saveCurrentSelection() = 0;

  // Owned sub-managers reachable via ctx (Kartend-qjtz). Only these two are
  // promoted to the interface; the rest stay concrete-only.
  [[nodiscard]] virtual AttractManager *attractManager() const = 0;
  [[nodiscard]] virtual GamepadManager *gamepadManager() const = 0;
};

#endif // IINTERACTIONMANAGER_H
