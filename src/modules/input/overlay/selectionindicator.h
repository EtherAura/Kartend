#ifndef SELECTIONINDICATOR_H
#define SELECTIONINDICATOR_H

#include <QPointer>
#include <QTimer>
#include <QWidget>

/// Thin, rounded, pulsing outline drawn around whatever the gamepad is
/// currently pointed at (user request 2026-08-18). Two callers share it:
///
///  * details-pane navigation — marks the selected region (artwork strip,
///    description, metadata) so the user can see what the right stick is
///    about to scroll;
///  * the section chord — marks the focused section while the modifier is
///    held, alongside the dimmed backdrop.
///
/// Positioned in TOP-LEVEL window coordinates so it can ring a widget in
/// any dock, and mouse-transparent so it never intercepts a click. It
/// paints only the ring; the widget underneath keeps painting itself.
class SelectionIndicator : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(SelectionIndicator)

public:
  explicit SelectionIndicator(QWidget *parent = nullptr);
  ~SelectionIndicator() override;

  /// Ring @p target (a widget anywhere under this indicator's window) and
  /// start pulsing. Passing null hides the indicator.
  void showFor(QWidget *target);
  void hideIndicator();
  [[nodiscard]] QWidget *target() const { return m_target; }

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  /// Re-derives geometry from the target — called on every pulse tick so a
  /// scrolled or relaid-out target keeps its ring without extra plumbing.
  void syncGeometry();

  QPointer<QWidget> m_target;
  QTimer m_pulse;
  qreal m_phase = 0.0;
};

#endif // SELECTIONINDICATOR_H
