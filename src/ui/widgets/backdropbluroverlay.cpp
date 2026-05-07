// Kartend-eq8r: simulated backdrop blur. QGraphicsBlurEffect attached to
// the widget itself blurs whatever paintEvent draws — in our case a
// cover-fit copy of the wallpaper pixmap. Cheap (single shot per source
// change) and gives a true Gaussian blur without custom shaders.
//
// Limitations vs. CSS backdrop-filter:
//   - Doesn't sample the actual underlying area; uses the wallpaper image
//     directly. Visually themed rather than pixel-continuous.
//   - Image bg only — for video bg the pre-blur pipeline doesn't run, so
//     this widget stays hidden during video playback.
#include "backdropbluroverlay.h"

#include <QEvent>
#include <QGraphicsBlurEffect>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>

BackdropBlurOverlay::BackdropBlurOverlay(QWidget *parent) : QWidget(parent) {
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  setAutoFillBackground(false);

  m_blurEffect = new QGraphicsBlurEffect(this);
  m_blurEffect->setBlurRadius(12);
  // QualityHint = full Gaussian; PerformanceHint = box approximation.
  // Toolbar blur runs once per source change, so quality is the right
  // choice — the cost is paid at apply time, not per frame.
  m_blurEffect->setBlurHints(QGraphicsBlurEffect::QualityHint);
  setGraphicsEffect(m_blurEffect);

  if (auto *p = qobject_cast<QWidget *>(parent)) {
    p->installEventFilter(this);
    setGeometry(p->rect());
  }
}

void BackdropBlurOverlay::setSource(const QPixmap &source, int blurRadius) {
  m_pixmap = source;
  if (m_blurEffect) {
    m_blurEffect->setBlurRadius(qBound(1, blurRadius, 64));
  }
  update();
}

void BackdropBlurOverlay::clear() {
  m_pixmap = QPixmap();
  update();
}

void BackdropBlurOverlay::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);
  if (m_pixmap.isNull() || rect().isEmpty()) {
    return;
  }
  QPainter painter(this);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  // Cover-fit so the toolbar's strip shows a non-letterboxed slice of the
  // wallpaper. KeepAspectRatioByExpanding scales by the larger dimension.
  QPixmap scaled = m_pixmap.scaled(rect().size(), Qt::KeepAspectRatioByExpanding,
                                   Qt::SmoothTransformation);
  const int x = (rect().width() - scaled.width()) / 2;
  const int y = (rect().height() - scaled.height()) / 2;
  painter.drawPixmap(x, y, scaled);
}

bool BackdropBlurOverlay::eventFilter(QObject *watched, QEvent *event) {
  if (watched == parent() && event->type() == QEvent::Resize) {
    if (auto *p = qobject_cast<QWidget *>(parent())) {
      setGeometry(p->rect());
    }
  }
  return QWidget::eventFilter(watched, event);
}

void BackdropBlurOverlay::showEvent(QShowEvent *event) {
  // Same self-healing pattern as the other overlays: re-sync to parent
  // rect when becoming visible in case the parent was resized while we
  // were hidden.
  if (auto *p = qobject_cast<QWidget *>(parent())) {
    setGeometry(p->rect());
  }
  QWidget::showEvent(event);
}
