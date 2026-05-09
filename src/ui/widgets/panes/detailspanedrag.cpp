// Sibling translation unit for DetailsPane: thin overrides that route the
// resize-grip mouse-event chain through DetailsPaneResizeGrip. The grip
// controller owns every piece of state the previous in-line implementation
// kept on DetailsPane (drag flags + start positions); these overrides only
// dispatch and fall back to the QWidget defaults.

#include "detailspane.h"
#include "detailspaneresizegrip.h"

#include <QEvent>
#include <QMouseEvent>
#include <QWidget>

void DetailsPane::mousePressEvent(QMouseEvent *event) {
  if (m_resizeGrip && m_resizeGrip->handleMousePress(event)) {
    return;
  }
  QWidget::mousePressEvent(event);
}

void DetailsPane::mouseMoveEvent(QMouseEvent *event) {
  if (m_resizeGrip && m_resizeGrip->handleMouseMove(event)) {
    return;
  }
  QWidget::mouseMoveEvent(event);
}

void DetailsPane::mouseReleaseEvent(QMouseEvent *event) {
  if (m_resizeGrip && m_resizeGrip->handleMouseRelease(event)) {
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void DetailsPane::leaveEvent(QEvent *event) {
  if (m_resizeGrip) {
    m_resizeGrip->handleLeave();
  }
  QWidget::leaveEvent(event);
}

bool DetailsPane::eventFilter(QObject *watched, QEvent *event) {
  auto *child = qobject_cast<QWidget *>(watched);
  if (m_resizeGrip && child && m_resizeGrip->handleChildEvent(child, event)) {
    return true;
  }
  return QWidget::eventFilter(watched, event);
}
