#ifndef TIMERUTILS_H
#define TIMERUTILS_H

#include <QObject>
#include <QList>

class QTimer;

namespace TimerUtils {

class Coordinator : public QObject
{
    Q_OBJECT
public:
    explicit Coordinator(QObject* parent = nullptr);
    ~Coordinator();
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

void stopTimers(const QList<QTimer*>& timers);
void disconnectTimers(const QList<QTimer*>& timers);
void stopAndDisconnectTimers(const QList<QTimer*>& timers);
void deleteLaterTimer(QTimer*& timer);

} // namespace TimerUtils

#endif