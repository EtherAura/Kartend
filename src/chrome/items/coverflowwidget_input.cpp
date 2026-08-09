// Cover-flow view mode — input handling (wheel / mouse / keyboard).

#include "coverflowwidget.h"

#include "coverflowgallerystrip.h"

#include <algorithm>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

void CoverFlowWidget::wheelEvent(QWheelEvent *event) {
  if (m_cards.isEmpty()) {
    event->ignore();
    return;
  }
  // angleDelta().y() is +120 per notch on a typical wheel. Treat any
  // direction (vertical or horizontal) as a navigation step — cover flow
  // is one-dimensional regardless of input axis.
  int delta = event->angleDelta().y();
  if (delta == 0) {
    delta = event->angleDelta().x();
  }
  if (delta == 0) {
    event->ignore();
    return;
  }
  int step = delta > 0 ? -1 : 1;
  int newIdx = std::clamp(m_selectedIndex + step, 0, static_cast<int>(m_cards.size()) - 1);
  if (newIdx != m_selectedIndex) {
    emit selectionChangeRequested(newIdx);
  }
  event->accept();
}

void CoverFlowWidget::mousePressEvent(QMouseEvent *event) {
  if (m_cards.isEmpty()) {
    QWidget::mousePressEvent(event);
    return;
  }
  // Gallery toolbar takes priority over card hit-test so clicks near the
  // bottom edge can switch artwork variants without first focusing some
  // background card.
  if (event->button() == Qt::LeftButton) {
    int galleryHit = m_galleryStrip ? m_galleryStrip->hitTest(event->pos()) : -1;
    if (galleryHit >= 0) {
      const auto &entry = m_gallery[galleryHit];
      m_galleryActiveIndex = galleryHit;
      // Video gallery entries auto-enable video mode and route the preview
      // through the chosen entry's path; image entries swap the centered
      // card's displayed pixmap and turn video off.
      m_videoMode = entry.isVideo;
      m_galleryThumbCache.remove(QStringLiteral("__active__")); // future use
      applyVideoPreviewState();
      update();
      event->accept();
      return;
    }
  }
  int hit = hitTestCard(event->pos());
  // Middle click on the centered card toggles between artwork and the
  // video preview, mirroring the modifier+middle-click cycle the grid uses
  // for artwork-type swaps. A middle click on a side card focuses it
  // first; the next middle click (now on the new center) toggles preview.
  if (event->button() == Qt::MiddleButton) {
    if (hit < 0) {
      QWidget::mousePressEvent(event);
      return;
    }
    if (hit != m_selectedIndex) {
      emit selectionChangeRequested(hit);
      event->accept();
      return;
    }
    m_videoMode = !m_videoMode;
    applyVideoPreviewState();
    update();
    event->accept();
    return;
  }
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  if (hit < 0) {
    QWidget::mousePressEvent(event);
    return;
  }
  // Side card: focus it. Center card: no-op — launching requires a
  // double-click or Enter, matching grid-mode semantics so a stray click
  // on the centered cover doesn't fire a launcher.
  if (hit != m_selectedIndex) {
    emit selectionChangeRequested(hit);
  }
  event->accept();
}

void CoverFlowWidget::mouseDoubleClickEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton || m_cards.isEmpty()) {
    QWidget::mouseDoubleClickEvent(event);
    return;
  }
  // Gallery toolbar first, exactly as mousePressEvent orders it
  // (Kartend-5jtyw). Without this the double click went straight to
  // hitTestCard, which misses because the strip is laid out below every card
  // rect — so double-clicking a thumbnail did nothing at all. Claiming the
  // event here also keeps it that way by construction rather than by
  // geometry: whatever the layout, the strip never activates an item.
  const int galleryHit = m_galleryStrip ? m_galleryStrip->hitTest(event->pos()) : -1;
  if (galleryHit >= 0 && galleryHit < m_gallery.size()) {
    const auto &entry = m_gallery[galleryHit];
    if (!entry.path.isEmpty()) {
      emit galleryPreviewRequested(entry.path, entry.isVideo);
    }
    event->accept();
    return;
  }
  int hit = hitTestCard(event->pos());
  if (hit >= 0) {
    if (hit != m_selectedIndex) {
      emit selectionChangeRequested(hit);
    }
    emit itemActivated(hit);
    event->accept();
    return;
  }
  QWidget::mouseDoubleClickEvent(event);
}

void CoverFlowWidget::keyPressEvent(QKeyEvent *event) {
  if (m_cards.isEmpty()) {
    QWidget::keyPressEvent(event);
    return;
  }
  switch (event->key()) {
  case Qt::Key_Left:
  case Qt::Key_Up: {
    int newIdx = std::max(0, m_selectedIndex - 1);
    if (newIdx != m_selectedIndex) {
      emit selectionChangeRequested(newIdx);
    }
    event->accept();
    return;
  }
  case Qt::Key_Right:
  case Qt::Key_Down: {
    int newIdx = std::min(static_cast<int>(m_cards.size()) - 1, m_selectedIndex + 1);
    if (newIdx != m_selectedIndex) {
      emit selectionChangeRequested(newIdx);
    }
    event->accept();
    return;
  }
  case Qt::Key_PageUp: {
    int newIdx = std::max(0, m_selectedIndex - kVisibleSideCards);
    if (newIdx != m_selectedIndex) {
      emit selectionChangeRequested(newIdx);
    }
    event->accept();
    return;
  }
  case Qt::Key_PageDown: {
    int newIdx =
        std::min(static_cast<int>(m_cards.size()) - 1, m_selectedIndex + kVisibleSideCards);
    if (newIdx != m_selectedIndex) {
      emit selectionChangeRequested(newIdx);
    }
    event->accept();
    return;
  }
  case Qt::Key_Home:
    emit selectionChangeRequested(0);
    event->accept();
    return;
  case Qt::Key_End:
    emit selectionChangeRequested(static_cast<int>(m_cards.size()) - 1);
    event->accept();
    return;
  case Qt::Key_Return:
  case Qt::Key_Enter:
  case Qt::Key_Space:
    emit itemActivated(m_selectedIndex);
    event->accept();
    return;
  default:
    break;
  }
  QWidget::keyPressEvent(event);
}

int CoverFlowWidget::hitTestCard(const QPoint &pt) const {
  // Walk the layout in reverse paint order (front-most first) so the
  // centered/topmost card wins ties with side cards.
  const auto layout = computeVisibleLayout();
  for (auto it = layout.rbegin(); it != layout.rend(); ++it) {
    if (it->rect.contains(pt)) {
      return it->index;
    }
  }
  return -1;
}
