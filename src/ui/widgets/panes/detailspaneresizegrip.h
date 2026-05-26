#ifndef DETAILSPANERESIZEGRIP_H
#define DETAILSPANERESIZEGRIP_H

#include "collectiontypes.h"

#include <QObject>

class QEvent;
class QMouseEvent;
class QPoint;
class QWidget;

/// Owns the DetailsPane's resize-grip state machine: the inner-edge hit-test,
/// the active drag bookkeeping, and the cursor changes that surface "you can
/// drag here" to the user.
///
/// The grip lives on the inner edge — the side facing the grid — and behaves
/// either as a width-drag (Left / Right docks) or a height-drag (Top / Bottom
/// docks). Only one direction is ever active at a time because the active
/// edge follows m_position.
///
/// Coupling: the controller drives the host QWidget for cursor changes and
/// width()/height() reads. It emits four signals (widthDragged /
/// widthCommitted / heightDragged / heightCommitted) that the host
/// re-emits as its own public signals.
class DetailsPaneResizeGrip : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(DetailsPaneResizeGrip)

public:
  explicit DetailsPaneResizeGrip(QWidget *host, QObject *parent = nullptr);

  /// Lock state: when true the grip is inactive (no hit-test, no drag).
  /// Toggled by the parent based on the user's "lock sidebar width" setting.
  void setLocked(bool locked);
  [[nodiscard]] bool isLocked() const { return m_locked; }

  /// Active dock edge: drives which side hosts the grip and which axis the
  /// drag deltas flow along.
  void setPosition(DetailsPanePosition position);

  /// True if @p posInWidget falls inside the inner-edge grip strip and the
  /// grip is unlocked.
  [[nodiscard]] bool isOnGrip(const QPoint &posInWidget) const;

  /// True while a width or height drag is in flight.
  [[nodiscard]] bool isDragging() const { return m_widthDragging || m_heightDragging; }

  /// Mouse-event handlers. Each returns true when the controller consumed
  /// the event (caller should accept + early-return); false to continue the
  /// QWidget default chain. Callers pass the event positions in host-widget
  /// coordinates.
  bool handleMousePress(QMouseEvent *event);
  bool handleMouseMove(QMouseEvent *event);
  bool handleMouseRelease(QMouseEvent *event);
  void handleLeave();

  /// Forward press/move/release that arrived at a child widget back through
  /// the grip state machine. Translates child-local coords to host-widget
  /// coords internally. Returns true when the controller consumed.
  bool handleChildEvent(QWidget *child, QEvent *event);

signals:
  /// Live drag delta (clamped to MIN_WIDTH).
  void widthDragged(int width);
  /// Final drag-release width — the parent persists this via SettingsManager.
  void widthCommitted(int width);
  /// Top/Bottom dock equivalents (clamped to MIN_HEIGHT).
  void heightDragged(int height);
  void heightCommitted(int height);

private:
  void startWidthDrag(int globalX);
  void startHeightDrag(int globalY);
  void updateWidthDrag(int globalX);
  void updateHeightDrag(int globalY);
  void applyHoverCursor(QWidget *widget, bool onGrip) const;

  QWidget *m_host;
  bool m_locked = true;
  DetailsPanePosition m_position = DetailsPanePosition::Right;
  bool m_widthDragging = false;
  int m_dragStartWidth = 0;
  int m_dragStartX = 0;
  bool m_heightDragging = false;
  int m_dragStartHeight = 0;
  int m_dragStartY = 0;
};

#endif // DETAILSPANERESIZEGRIP_H
