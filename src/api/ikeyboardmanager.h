#ifndef IKEYBOARDMANAGER_H
#define IKEYBOARDMANAGER_H

#include <Qt>

QT_BEGIN_NAMESPACE
class QKeyEvent;
QT_END_NAMESPACE

/**
 * @brief Sibling-facing interface to keyboard input / key-repeat state.
 *
 * Like INavigationManager, this is a deliberate role interface
 * (interface-segregation): the slice sibling managers reach for through
 * ApplicationContext, not KeyboardManager's full surface (its many
 * signal-driven slots, timer wiring and static selection-calculation
 * helpers stay on the concrete class, which InteractionManager owns
 * directly as a unique_ptr and connects signals against).
 *
 * Plain abstract class, not a QObject — KeyboardManager derives QObject
 * directly; see its multiple-inheritance declaration. Add a method here
 * only when a sibling genuinely needs to call it via ctx.
 */
class IKeyboardManager {
public:
  virtual ~IKeyboardManager() = default;

  // Key event handling
  [[nodiscard]] virtual bool handleKeyPress(QKeyEvent *event, bool searchBarFocused) = 0;
  [[nodiscard]] virtual bool handleKeyRelease(QKeyEvent *event) = 0;

  // Key repeat management
  virtual void stopRepeat(bool suppressRecentering = false) = 0;
  [[nodiscard]] virtual bool isRepeating() const = 0;
  virtual void finalizeKeyRepeatForKey(Qt::Key key, int direction, bool vertical) = 0;
  [[nodiscard]] virtual bool consumePendingNavigationKey(Qt::Key &outKey) = 0;

  // Navigation state
  [[nodiscard]] virtual bool isPhysicalKeyDown() const = 0;
  virtual void setPhysicalKeyDown(bool down) = 0;
  [[nodiscard]] virtual int repeatDelta() const = 0;
  [[nodiscard]] virtual bool repeatVertical() const = 0;

  // Wrap navigation state
  [[nodiscard]] virtual bool isWrapSequenceActive() const = 0;
  virtual void setWrapSequenceActive(bool active) = 0;

  // Continuous scroll state
  [[nodiscard]] virtual bool isContinuousScrollActive() const = 0;
  virtual void setContinuousScrollActive(bool active) = 0;

  // Prepare for key navigation step
  virtual void prepareKeyNavigationState() = 0;

  // Finalize key repeat state
  virtual void finalizeKeyRepeat(QKeyEvent *event, int direction, bool vertical) = 0;
};

#endif // IKEYBOARDMANAGER_H
