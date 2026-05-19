#ifndef IINTERACTIONMANAGER_H
#define IINTERACTIONMANAGER_H

#include <QString>

/**
 * @brief Sibling-facing interface to the input-coordination layer.
 *
 * Like INavigationManager and IDetailsPaneManager, this is a deliberate role
 * interface (interface-segregation): the slice sibling managers reach for
 * through ApplicationContext, not InteractionManager's full surface (its
 * many input-event slots, sub-manager accessors, and lifecycle wiring stay
 * on the concrete class, which the owner — ApplicationManager / MainWindow —
 * holds directly).
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
};

#endif // IINTERACTIONMANAGER_H
