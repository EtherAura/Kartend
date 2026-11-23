// Per-collection image wallpaper with proportional parallax (Kartend-w0zi1).
#include "backgroundimagewidget.h"

#include <algorithm>
#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QShowEvent>
#include <QSize>
#include <QtGlobal>

BackgroundImageWidget::BackgroundImageWidget(QWidget *parent) : QWidget(parent) {
  // Pure visual surface: never eat mouse events meant for the grid above.
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  setAutoFillBackground(false);

  // Track the parent's size so the wallpaper keeps filling the viewport when
  // the window is resized.
  if (auto *p = qobject_cast<QWidget *>(parent)) {
    p->installEventFilter(this);
    setGeometry(p->rect());
  }
}

void BackgroundImageWidget::setImage(const QPixmap &image) {
  if (image.isNull()) {
    clear();
    return;
  }
  m_source = image;
  m_scaled = QPixmap();
  update();
}

void BackgroundImageWidget::clear() {
  m_source = QPixmap();
  m_scaled = QPixmap();
  update();
}

void BackgroundImageWidget::setParallaxStrength(int strength) {
  const int clamped = qBound(0, strength, 100);
  if (clamped == m_strength) {
    return;
  }
  m_strength = clamped;
  m_scaled = QPixmap();
  update();
}

void BackgroundImageWidget::setScrollFraction(qreal fraction) {
  const qreal clamped = qBound(0.0, fraction, 1.0);
  if (qFuzzyCompare(1.0 + clamped, 1.0 + m_fraction)) {
    return;
  }
  m_fraction = clamped;
  // A static wallpaper does not move, so a scroll needs no repaint of it.
  if (m_strength > 0) {
    update();
  }
}

QRectF BackgroundImageWidget::paintRect(const QSizeF &imageSize, const QSizeF &viewSize,
                                        int strength, qreal scrollFraction) {
  if (imageSize.isEmpty() || viewSize.isEmpty()) {
    return {};
  }
  const qreal margin = viewSize.height() * qBound(0, strength, 100) / 100.0;
  const qreal scale = std::max(viewSize.width() / imageSize.width(),
                               (viewSize.height() + margin) / imageSize.height());
  const QSizeF scaled = imageSize * scale;
  // The cover-fit can leave more height than the margin needs (a tall image
  // in a wide view). Centre the drift band in it, as a static wallpaper is.
  const qreal slack = scaled.height() - viewSize.height();
  const qreal t = qBound(0.0, scrollFraction, 1.0);
  const qreal x = (viewSize.width() - scaled.width()) / 2.0;
  const qreal y = -((slack - margin) / 2.0 + margin * t);
  return {QPointF(x, y), scaled};
}

qreal BackgroundImageWidget::scrollFractionOf(int value, int minimum, int maximum) {
  if (maximum <= minimum) {
    return 0.0;
  }
  return qBound(0.0, static_cast<qreal>(value - minimum) / (maximum - minimum), 1.0);
}

void BackgroundImageWidget::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);
  if (m_source.isNull() || rect().isEmpty()) {
    return;
  }
  const QRectF target = paintRect(QSizeF(m_source.size()), QSizeF(size()), m_strength, m_fraction);
  if (target.isEmpty()) {
    return;
  }
  // Only the columns inside the view are ever drawn (the cover-fit crops the
  // sides), so only those are cached: at device resolution with a wide
  // margin, the cropped sides would otherwise be tens of megabytes.
  const qreal left = std::max(target.left(), 0.0);
  const qreal right = std::min(target.right(), static_cast<qreal>(width()));
  const QRectF visible(left, target.top(), right - left, target.height());
  if (visible.width() <= 0) {
    return;
  }
  // Scale once per size, at device pixels so a HiDPI screen gets a sharp
  // wallpaper; scrolling then only changes where this pixmap is drawn.
  const qreal dpr = devicePixelRatioF();
  const QSize deviceSize = (visible.size() * dpr).toSize();
  const qreal scale = target.width() / m_source.width();
  const QRect sourceColumns = QRectF((visible.left() - target.left()) / scale, 0,
                                     visible.width() / scale, m_source.height())
                                  .toAlignedRect()
                                  .intersected(m_source.rect());
  if (m_scaled.isNull() || m_scaled.size() != deviceSize || m_scaledColumns != sourceColumns ||
      !qFuzzyCompare(m_scaled.devicePixelRatio(), dpr)) {
    m_scaled = m_source.copy(sourceColumns)
                   .scaled(deviceSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    m_scaled.setDevicePixelRatio(dpr);
    m_scaledColumns = sourceColumns;
  }
  QPainter painter(this);
  painter.drawPixmap(visible.topLeft(), m_scaled);
}

void BackgroundImageWidget::showEvent(QShowEvent *event) {
  // Re-sync to the parent on every show, in case it was resized while this
  // was hidden (same pattern as the other viewport overlays).
  if (auto *p = qobject_cast<QWidget *>(parent())) {
    setGeometry(p->rect());
  }
  QWidget::showEvent(event);
}

bool BackgroundImageWidget::eventFilter(QObject *watched, QEvent *event) {
  if (watched == parent() && event->type() == QEvent::Resize) {
    if (auto *p = qobject_cast<QWidget *>(parent())) {
      setGeometry(p->rect());
    }
  }
  return QWidget::eventFilter(watched, event);
}
