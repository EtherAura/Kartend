#ifndef TIMERUTILS_H
#define TIMERUTILS_H

#include <QObject>
#include <QList>
#include <QHash>
#include <QString>
#include <functional>

class QTimer;

namespace TimerUtils {

/**
 * @brief A generic debounced timer that coalesces rapid calls into single executions.
 *
 * Usage:
 *   DebouncedTimer timer(100, this);  // 100ms debounce interval
 *   connect(&timer, &DebouncedTimer::triggered, this, &MyClass::handleDebounced);
 *   timer.trigger();  // Call many times, only fires once after 100ms of inactivity
 */
class DebouncedTimer : public QObject {
  Q_OBJECT
public:
  explicit DebouncedTimer(int intervalMs, QObject *parent = nullptr);
  ~DebouncedTimer() override;

  void trigger();           // Schedule a debounced trigger
  void triggerImmediate();  // Trigger immediately, resetting any pending
  void cancel();            // Cancel any pending trigger
  [[nodiscard]] bool isPending() const;
  void setInterval(int intervalMs);

signals:
  void triggered();

private:
  QTimer *m_timer = nullptr;
};

/**
 * @brief Coordinator for layout and viewport update debouncing.
 *
 * Provides centralized debounced timers for common UI update patterns.
 */
class Coordinator : public QObject
{
    Q_OBJECT
public:
    explicit Coordinator(QObject* parent = nullptr);
    ~Coordinator() override;
    void scheduleLayoutUpdate();
    void scheduleViewportUpdate();
    void stopAllTimers();

signals:
    void layoutUpdateRequested();
    void viewportUpdateRequested();

private slots:
    void onLayoutTimerTimeout();
    void onViewportTimerTimeout();

private:
    QTimer* m_layoutTimer;
    QTimer* m_viewportTimer;
};

// Helper functions for timer management
void stopTimers(const QList<QTimer*>& timers);
void disconnectTimers(const QList<QTimer*>& timers);
void stopAndDisconnectTimers(const QList<QTimer*>& timers);
void deleteLaterTimer(QTimer*& timer);

} // namespace TimerUtils

#endif