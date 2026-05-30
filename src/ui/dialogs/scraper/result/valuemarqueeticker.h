#ifndef VALUEMARQUEETICKER_H
#define VALUEMARQUEETICKER_H

// Scrolls overflowing value-chip text in the scraper dialog's live
// metadata panel left-to-right, then wraps back to the start. Owns its
// own 150 ms timer and the per-cell pause counters; reaches the host
// dialog's live-metadata group (the QLineEdit children it animates)
// through friend access.

#include <QHash>
#include <QObject>
#include <QTimer>

class ScrapeResultDialog;
QT_BEGIN_NAMESPACE
class QLineEdit;
QT_END_NAMESPACE

class ValueMarqueeTicker : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ValueMarqueeTicker)

public:
  explicit ValueMarqueeTicker(ScrapeResultDialog *dlg);

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
  ScrapeResultDialog *m_dlg = nullptr;
  /// Per-cell pause counter: while a value is parked at the rightmost
  /// scroll position, this counts down before the cell snaps back to
  /// cursor 0. Cells without entries are in the advancing phase.
  QHash<QLineEdit *, int> m_pauseTicks;
  QTimer m_timer;
  bool m_inited = false;
};

#endif // VALUEMARQUEETICKER_H
