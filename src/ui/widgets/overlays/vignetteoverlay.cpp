// edge-darkening vignette overlay. Paints a radial gradient
// from transparent at center to alpha-blended black at the corners. The
// gradient is anchored to a circle that touches the corners of the rect so
// the darkening lands evenly on a non-square viewport.
#include "vignetteoverlay.h"

#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QtMath>

VignetteOverlay::VignetteOverlay(QWidget *parent) : QWidget(parent) {
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  setAutoFillBackground(false);

  if (auto *p = qobject_cast<QWidget *>(parent)) {
    p->installEventFilter(this);
    setGeometry(p->rect());
  }
}

void VignetteOverlay::setIntensity(int percent) {
  int clamped = qBound(0, percent, 100);
  if (clamped == m_intensity) {
    return;
  }
  m_intensity = clamped;
  update();
}

void VignetteOverlay::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);
  if (m_intensity <= 0 || rect().isEmpty()) {
    return;
  }

  // Anchor the radial gradient at the center of the rect with a radius that
  // reaches the corners. Stop 0 (center) is fully transparent so titles in
  // the middle stay unaffected; stop 1 (corners) is alpha-blended black at
  // intensity %.
  const QPointF center = rect().center();
  const qreal radius = qSqrt(static_cast<qreal>(rect().width() * rect().width() +
                                                rect().height() * rect().height())) /
                       2.0;
  QRadialGradient grad(center, radius, center);
  grad.setColorAt(0.0, QColor(0, 0, 0, 0));
  // Soft mid-stop keeps the inner ~half of the rect untouched and ramps the
  // darkening into the outer half. Without this the grid items near the
  // edge would already be visibly dimmed at low intensities.
  grad.setColorAt(0.55, QColor(0, 0, 0, 0));
  const int cornerAlpha = static_cast<int>((m_intensity * 255) / 100);
  grad.setColorAt(1.0, QColor(0, 0, 0, cornerAlpha));

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.fillRect(rect(), grad);
}

void VignetteOverlay::showEvent(QShowEvent *event) {
  // Same self-healing pattern as HeaderLogoOverlay — geometry from parent
  // may be stale at construction time, and Qt won't always re-emit Resize
  // on the first navigation from a hidden stacked-widget page.
  if (auto *p = qobject_cast<QWidget *>(parent())) {
    setGeometry(p->rect());
  }
  QWidget::showEvent(event);
}

bool VignetteOverlay::eventFilter(QObject *watched, QEvent *event) {
  if (watched == parent() && event->type() == QEvent::Resize) {
    if (auto *p = qobject_cast<QWidget *>(parent())) {
      setGeometry(p->rect());
    }
  }
  return QWidget::eventFilter(watched, event);
}
