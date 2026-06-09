#ifndef VALUEMARQUEETICKER_H
#define VALUEMARQUEETICKER_H

// Scrolls overflowing value-chip text in the scraper dialog's live
// metadata panel left-to-right, then wraps back to the start. Owns its
// own 150 ms timer and the per-cell pause counters.
//
// Kartend-kggn8: de-friended from ScrapeResultDialog. Instead of reaching
// through a back-pointer it is handed exactly what it animates — the
// live-metadata QGroupBox (via setLiveMetadataGroup, once the host builds it)
// — plus a QWidget host used only for the "is the panel visible?" CPU gate.

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>

QT_BEGIN_NAMESPACE
class QGroupBox;
class QLineEdit;
class QWidget;
QT_END_NAMESPACE

class ValueMarqueeTicker : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ValueMarqueeTicker)

public:
  /// @p host is the widget whose visibility gates the tick (the dialog); it is
  /// also the QObject parent. The animated group is supplied later via
  /// setLiveMetadataGroup() because the host builds it after constructing this.
  explicit ValueMarqueeTicker(QWidget *host);

  /// Install the live-metadata group whose QLineEdit children are animated.
  /// Called by the host once the group exists; until then tick() is a no-op.
  void setLiveMetadataGroup(QGroupBox *group);

  /// Begin (or restart) ticking: lazily creates the timer on first use,
  /// clears any in-flight per-cell pause counters, and starts the timer.
  void start();
  /// Stop ticking and clear the pause counters.
  void stop();
  /// Stop the timer but keep the pause counters — used when the dialog is
  /// hidden so an invisible panel doesn't burn CPU on the find-children walk.
  void pause();
  /// Resume ticking after a pause iff the timer was previously started.
  void resume();
  /// Reset every cell back to the advancing phase (clears pause counters)
  /// so a freshly-populated value restarts its L→R scroll from the head.
  void resetCells();

private slots:
  void tick();

private:
  // QPointer: the host owns the group; if it's destroyed/recreated the guard
  // keeps tick() from walking a dangling tree.
  QPointer<QWidget> m_host;
  QPointer<QGroupBox> m_liveMetadataGroup;
  /// Per-cell pause counter: while a value is parked at the rightmost
  /// scroll position, this counts down before the cell snaps back to
  /// cursor 0. Cells without entries are in the advancing phase.
  QHash<QLineEdit *, int> m_pauseTicks;
  QTimer m_timer;
  bool m_inited = false;
};

#endif // VALUEMARQUEETICKER_H
