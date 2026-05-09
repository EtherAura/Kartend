// Sibling translation unit for DetailsPane (grip / drag /
// resize). Hosts every method that participates in the resize-grip state
// machine: hit-testing, the press/move/release/leave handlers, and the
// child-widget eventFilter that forwards grip events from inner widgets.
// Member access only — these remain DetailsPane methods.

#include <QEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QWidget>

#include "collectionutils.h"
#include "detailspane.h"
#include "uiconstants.h"

bool DetailsPane::isOnGrip(const QPoint &posInWidget) const {
  if (m_widthLocked) {
    return false;
  }
  // the grip lives on the inner edge — the side facing the grid.
  // Right dock → left edge; Left dock → right edge; Top dock → bottom edge;
  // Bottom dock → top edge.
  const int grip = UIConstants::DetailsPane::RESIZE_GRIP_PX;
  switch (m_position) {
  case DetailsPanePosition::Left:
    return posInWidget.x() >= width() - grip;
  case DetailsPanePosition::Top:
    return posInWidget.y() >= height() - grip;
  case DetailsPanePosition::Bottom:
    return posInWidget.y() < grip;
  case DetailsPanePosition::Right:
  default:
    return posInWidget.x() < grip;
  }
}

void DetailsPane::mousePressEvent(QMouseEvent *event) {
  if (!m_widthLocked && event->button() == Qt::LeftButton && isOnGrip(event->pos())) {
    if (CollectionUtils::isDetailsPaneHorizontal(m_position)) {
      m_heightDragging = true;
      m_dragStartHeight = height();
      m_dragStartY = event->globalPosition().toPoint().y();
    } else {
      m_widthDragging = true;
      m_dragStartWidth = width();
      m_dragStartX = event->globalPosition().toPoint().x();
    }
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void DetailsPane::mouseMoveEvent(QMouseEvent *event) {
  if (m_widthDragging) {
    const int dx = event->globalPosition().toPoint().x() - m_dragStartX;
    // Right-anchored sidebar: drag-left increases width, drag-right shrinks.
    // Left-anchored: drag-right increases width.
    int candidate =
        m_position == DetailsPanePosition::Left ? m_dragStartWidth + dx : m_dragStartWidth - dx;
    candidate = std::max(candidate, UIConstants::DetailsPane::MIN_WIDTH);
    emit widthDragged(candidate);
    event->accept();
    return;
  }
  if (m_heightDragging) {
    // Top dock grows by dragging down (positive dy); Bottom dock
    // grows by dragging up (negative dy → height increases).
    const int dy = event->globalPosition().toPoint().y() - m_dragStartY;
    int candidate =
        m_position == DetailsPanePosition::Top ? m_dragStartHeight + dy : m_dragStartHeight - dy;
    candidate = std::max(candidate, UIConstants::DetailsPane::MIN_HEIGHT);
    emit heightDragged(candidate);
    event->accept();
    return;
  }
  if (!m_widthLocked) {
    if (isOnGrip(event->pos())) {
      setCursor(CollectionUtils::isDetailsPaneHorizontal(m_position) ? Qt::SplitVCursor
                                                                     : Qt::SplitHCursor);
    } else {
      setCursor(Qt::ArrowCursor);
    }
  }
  QWidget::mouseMoveEvent(event);
}

void DetailsPane::mouseReleaseEvent(QMouseEvent *event) {
  if (m_widthDragging && event->button() == Qt::LeftButton) {
    m_widthDragging = false;
    emit widthCommitted(width());
    event->accept();
    return;
  }
  if (m_heightDragging && event->button() == Qt::LeftButton) {
    m_heightDragging = false;
    emit heightCommitted(height());
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void DetailsPane::leaveEvent(QEvent *event) {
  if (!m_widthDragging && !m_heightDragging) {
    unsetCursor();
  }
  QWidget::leaveEvent(event);
}

bool DetailsPane::eventFilter(QObject *watched, QEvent *event) {
  if (m_widthLocked) {
    return QWidget::eventFilter(watched, event);
  }
  // forward press/move/release in the grip zone from any inner
  // widget back to ourselves. mapTo(this, ...) translates the source widget's
  // local coords into DetailsPane coords so isOnGrip() and the existing
  // drag math both work unmodified.
  auto *child = qobject_cast<QWidget *>(watched);
  if (!child) {
    return QWidget::eventFilter(watched, event);
  }
  switch (event->type()) {
  case QEvent::MouseButtonPress: {
    auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() != Qt::LeftButton) break;
    const QPoint posInPanel = child->mapTo(this, me->position().toPoint());
    if (!isOnGrip(posInPanel)) break;
    if (CollectionUtils::isDetailsPaneHorizontal(m_position)) {
      m_heightDragging = true;
      m_dragStartHeight = height();
      m_dragStartY = me->globalPosition().toPoint().y();
    } else {
      m_widthDragging = true;
      m_dragStartWidth = width();
      m_dragStartX = me->globalPosition().toPoint().x();
    }
    me->accept();
    return true;
  }
  case QEvent::MouseMove: {
    if (!m_widthDragging && !m_heightDragging) {
      // Update the cursor when hovering over the grip zone via a child
      // widget — without this the user gets no visual cue that the grip
      // is reachable from inside the scroll area / content widget.
      auto *me = static_cast<QMouseEvent *>(event);
      const QPoint posInPanel = child->mapTo(this, me->position().toPoint());
      if (isOnGrip(posInPanel)) {
        child->setCursor(CollectionUtils::isDetailsPaneHorizontal(m_position) ? Qt::SplitVCursor
                                                                              : Qt::SplitHCursor);
      } else {
        child->unsetCursor();
      }
      break;
    }
    auto *me = static_cast<QMouseEvent *>(event);
    if (m_widthDragging) {
      const int dx = me->globalPosition().toPoint().x() - m_dragStartX;
      int candidate =
          m_position == DetailsPanePosition::Left ? m_dragStartWidth + dx : m_dragStartWidth - dx;
      candidate = std::max(candidate, UIConstants::DetailsPane::MIN_WIDTH);
      emit widthDragged(candidate);
    } else {
      const int dy = me->globalPosition().toPoint().y() - m_dragStartY;
      int candidate =
          m_position == DetailsPanePosition::Top ? m_dragStartHeight + dy : m_dragStartHeight - dy;
      candidate = std::max(candidate, UIConstants::DetailsPane::MIN_HEIGHT);
      emit heightDragged(candidate);
    }
    me->accept();
    return true;
  }
  case QEvent::MouseButtonRelease: {
    auto *me = static_cast<QMouseEvent *>(event);
    if (m_widthDragging && me->button() == Qt::LeftButton) {
      m_widthDragging = false;
      emit widthCommitted(width());
      me->accept();
      return true;
    }
    if (m_heightDragging && me->button() == Qt::LeftButton) {
      m_heightDragging = false;
      emit heightCommitted(height());
      me->accept();
      return true;
    }
    break;
  }
  default:
    break;
  }
  return QWidget::eventFilter(watched, event);
}
