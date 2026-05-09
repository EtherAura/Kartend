#include "detailspaneresizegrip.h"

#include "collectionutils.h"
#include "uiconstants.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QWidget>

DetailsPaneResizeGrip::DetailsPaneResizeGrip(QWidget *host, QObject *parent)
    : QObject(parent), m_host(host) {}

void DetailsPaneResizeGrip::setLocked(bool locked) {
  m_locked = locked;
}

void DetailsPaneResizeGrip::setPosition(DetailsPanePosition position) {
  m_position = position;
}

bool DetailsPaneResizeGrip::isOnGrip(const QPoint &posInWidget) const {
  if (m_locked || !m_host) {
    return false;
  }
  // The grip lives on the inner edge — the side facing the grid:
  //   Right dock → left edge; Left dock → right edge;
  //   Top dock   → bottom edge; Bottom dock → top edge.
  const int grip = UIConstants::DetailsPane::RESIZE_GRIP_PX;
  switch (m_position) {
  case DetailsPanePosition::Left:
    return posInWidget.x() >= m_host->width() - grip;
  case DetailsPanePosition::Top:
    return posInWidget.y() >= m_host->height() - grip;
  case DetailsPanePosition::Bottom:
    return posInWidget.y() < grip;
  case DetailsPanePosition::Right:
  default:
    return posInWidget.x() < grip;
  }
}

void DetailsPaneResizeGrip::startWidthDrag(int globalX) {
  m_widthDragging = true;
  m_dragStartWidth = m_host ? m_host->width() : 0;
  m_dragStartX = globalX;
}

void DetailsPaneResizeGrip::startHeightDrag(int globalY) {
  m_heightDragging = true;
  m_dragStartHeight = m_host ? m_host->height() : 0;
  m_dragStartY = globalY;
}

void DetailsPaneResizeGrip::updateWidthDrag(int globalX) {
  const int dx = globalX - m_dragStartX;
  // Right-anchored sidebar: drag-left increases width, drag-right shrinks.
  // Left-anchored: drag-right increases width.
  int candidate =
      m_position == DetailsPanePosition::Left ? m_dragStartWidth + dx : m_dragStartWidth - dx;
  candidate = std::max(candidate, UIConstants::DetailsPane::MIN_WIDTH);
  emit widthDragged(candidate);
}

void DetailsPaneResizeGrip::updateHeightDrag(int globalY) {
  // Top dock grows by dragging down (positive dy); Bottom dock grows by
  // dragging up (negative dy → height increases).
  const int dy = globalY - m_dragStartY;
  int candidate =
      m_position == DetailsPanePosition::Top ? m_dragStartHeight + dy : m_dragStartHeight - dy;
  candidate = std::max(candidate, UIConstants::DetailsPane::MIN_HEIGHT);
  emit heightDragged(candidate);
}

void DetailsPaneResizeGrip::applyHoverCursor(QWidget *widget, bool onGrip) const {
  if (!widget) {
    return;
  }
  if (onGrip) {
    widget->setCursor(CollectionUtils::isDetailsPaneHorizontal(m_position) ? Qt::SplitVCursor
                                                                           : Qt::SplitHCursor);
  } else {
    widget->unsetCursor();
  }
}

bool DetailsPaneResizeGrip::handleMousePress(QMouseEvent *event) {
  if (m_locked || event->button() != Qt::LeftButton || !isOnGrip(event->pos())) {
    return false;
  }
  if (CollectionUtils::isDetailsPaneHorizontal(m_position)) {
    startHeightDrag(event->globalPosition().toPoint().y());
  } else {
    startWidthDrag(event->globalPosition().toPoint().x());
  }
  event->accept();
  return true;
}

bool DetailsPaneResizeGrip::handleMouseMove(QMouseEvent *event) {
  if (m_widthDragging) {
    updateWidthDrag(event->globalPosition().toPoint().x());
    event->accept();
    return true;
  }
  if (m_heightDragging) {
    updateHeightDrag(event->globalPosition().toPoint().y());
    event->accept();
    return true;
  }
  if (!m_locked && m_host) {
    applyHoverCursor(m_host, isOnGrip(event->pos()));
    if (!isOnGrip(event->pos())) {
      m_host->setCursor(Qt::ArrowCursor);
    }
  }
  return false;
}

bool DetailsPaneResizeGrip::handleMouseRelease(QMouseEvent *event) {
  if (m_widthDragging && event->button() == Qt::LeftButton) {
    m_widthDragging = false;
    if (m_host) {
      emit widthCommitted(m_host->width());
    }
    event->accept();
    return true;
  }
  if (m_heightDragging && event->button() == Qt::LeftButton) {
    m_heightDragging = false;
    if (m_host) {
      emit heightCommitted(m_host->height());
    }
    event->accept();
    return true;
  }
  return false;
}

void DetailsPaneResizeGrip::handleLeave() {
  if (!m_widthDragging && !m_heightDragging && m_host) {
    m_host->unsetCursor();
  }
}

bool DetailsPaneResizeGrip::handleChildEvent(QWidget *child, QEvent *event) {
  if (m_locked || !m_host || !child) {
    return false;
  }
  // Forward press/move/release in the grip zone from any inner widget back
  // to ourselves. mapTo(host, ...) translates the source widget's local
  // coords into host-widget coords so isOnGrip() and the existing drag
  // math both work unmodified.
  switch (event->type()) {
  case QEvent::MouseButtonPress: {
    auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() != Qt::LeftButton) {
      return false;
    }
    const QPoint posInPanel = child->mapTo(m_host, me->position().toPoint());
    if (!isOnGrip(posInPanel)) {
      return false;
    }
    if (CollectionUtils::isDetailsPaneHorizontal(m_position)) {
      startHeightDrag(me->globalPosition().toPoint().y());
    } else {
      startWidthDrag(me->globalPosition().toPoint().x());
    }
    me->accept();
    return true;
  }
  case QEvent::MouseMove: {
    auto *me = static_cast<QMouseEvent *>(event);
    if (!isDragging()) {
      // Update the cursor when hovering over the grip zone via a child
      // widget — without this the user gets no visual cue that the grip
      // is reachable from inside the scroll area / content widget.
      const QPoint posInPanel = child->mapTo(m_host, me->position().toPoint());
      applyHoverCursor(child, isOnGrip(posInPanel));
      return false;
    }
    if (m_widthDragging) {
      updateWidthDrag(me->globalPosition().toPoint().x());
    } else {
      updateHeightDrag(me->globalPosition().toPoint().y());
    }
    me->accept();
    return true;
  }
  case QEvent::MouseButtonRelease: {
    auto *me = static_cast<QMouseEvent *>(event);
    if (m_widthDragging && me->button() == Qt::LeftButton) {
      m_widthDragging = false;
      emit widthCommitted(m_host->width());
      me->accept();
      return true;
    }
    if (m_heightDragging && me->button() == Qt::LeftButton) {
      m_heightDragging = false;
      emit heightCommitted(m_host->height());
      me->accept();
      return true;
    }
    return false;
  }
  default:
    return false;
  }
}
