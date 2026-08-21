// Sibling translation unit for DetailsPane: thin overrides that route the
// resize-grip mouse-event chain through DetailsPaneResizeGrip. The grip
// controller owns every piece of state the previous in-line implementation
// kept on DetailsPane (drag flags + start positions); these overrides only
// dispatch and fall back to the QWidget defaults.

#include "detailspane.h"
#include "detailspaneresizegrip.h"
#include "ui_detailspane.h"
#include "videopreviewwidget.h"

#include <QEvent>
#include <QKeyEvent>
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
  // The VIEWPORT's own resize is the only moment its width is final. Capping
  // from DetailsPane::resizeEvent read the width the viewport was about to
  // stop having, so after a widening drag the content stayed capped at the
  // OLD width (measured 2026-08-20: pane 314 with content still pinned at
  // 242, artwork centred in the stale box).
  if (ui && ui->scrollArea && watched == ui->scrollArea->viewport() &&
      event->type() == QEvent::Resize) {
    constrainContentToViewport();
    return false;
  }
  auto *child = qobject_cast<QWidget *>(watched);
  if (m_resizeGrip && child && m_resizeGrip->handleChildEvent(child, event)) {
    return true;
  }
  // Arrow-key gallery cycle when the main artwork tile or the video
  // preview has focus — gives mouse-free users a way to flip the big
  // preview between scraped artwork types. Constrained to these two
  // widgets so it doesn't shadow the grid's arrow navigation.
  if (event->type() == QEvent::KeyPress &&
      (watched == ui->artworkDisplay || watched == m_videoPlayback.videoPreview)) {
    auto *keyEvent = static_cast<QKeyEvent *>(event);
    if (keyEvent->key() == Qt::Key_Left) {
      cycleMainPreview(-1);
      return true;
    }
    if (keyEvent->key() == Qt::Key_Right) {
      cycleMainPreview(+1);
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}
