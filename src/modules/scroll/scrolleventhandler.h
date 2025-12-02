#ifndef SCROLLEVENTHANDLER_H
#define SCROLLEVENTHANDLER_H

#include <QMetaObject>
#include <QObject>

class QScrollArea;
class QScrollBar;
class InteractionStateHolder;

namespace TimerUtils {
class DebouncedTimer;
}

/**
 * @brief Handles scroll event wiring and user scroll activity tracking.
 * 
 * Encapsulates the connection/disconnection of scroll events and the
 * tracking of user scroll activity state. Emits signals when scroll
 * events occur for the parent manager to handle.
 */
class ScrollEventHandler : public QObject {
  Q_OBJECT
public:
  explicit ScrollEventHandler(QObject *parent = nullptr);
  ~ScrollEventHandler() override;

  // Setup
  void setScrollArea(QScrollArea *scrollArea);
  void setIdleTimer(TimerUtils::DebouncedTimer *timer) { m_idleTimer = timer; }
  void setInteractionState(InteractionStateHolder *state) { m_state = state; }

  // Connection management
  void connectEvents();
  void disconnectEvents();

  // State queries
  [[nodiscard]] bool isUserScrollActive() const { return m_userScrollActive; }
  void setUserScrollActive(bool active) { m_userScrollActive = active; }

signals:
  // Emitted when any scroll value changes
  void scrollChanged();
  
  // Emitted when user scroll activity starts
  void userScrollStarted();
  
  // Emitted when user scroll activity ends
  void userScrollEnded();

private:
  void connectVerticalEvents(QScrollBar *scrollBar);
  void connectHorizontalEvents(QScrollBar *scrollBar);
  void onSliderPressed(QScrollBar *scrollBar);
  void onSliderReleased();
  void onActionTriggered(QScrollBar *scrollBar);

  QScrollArea *m_scrollArea = nullptr;
  InteractionStateHolder *m_state = nullptr;
  TimerUtils::DebouncedTimer *m_idleTimer = nullptr;
  QMetaObject::Connection m_vScrollConn;
  QMetaObject::Connection m_hScrollConn;
  bool m_userScrollActive = false;
};

#endif // SCROLLEVENTHANDLER_H
