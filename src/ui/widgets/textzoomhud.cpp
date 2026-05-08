// Transient HUD that surfaces the current text-zoom percent on Ctrl+/-/0.
// Lifecycle is timer-driven: showZoom() fades the pill in, holds for
// TEXT_ZOOM_HUD_HOLD_DURATION_MS, then fades out. Repeated calls reset the
// hold timer so an in-progress zoom burst keeps the HUD visible.
#include "textzoomhud.h"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QTimer>

#include "uiconstants.h"

namespace {
constexpr int kTopMargin = 24;
constexpr int kHorizontalPadding = 22;
constexpr int kVerticalPadding = 10;

const QString kPillStyle = QStringLiteral("background-color: rgba(20, 23, 30, 230);"
                                          "color: white;"
                                          "font-size: 16px;"
                                          "font-weight: 600;"
                                          "letter-spacing: 0.4px;"
                                          "border: 1px solid rgba(255, 255, 255, 60);"
                                          "border-radius: 14px;"
                                          "padding: %1px %2px;")
                               .arg(kVerticalPadding)
                               .arg(kHorizontalPadding);
} // namespace

TextZoomHud::TextZoomHud(QWidget *parent) : QWidget(parent) {
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  setFocusPolicy(Qt::NoFocus);

  m_label = new QLabel(this);
  m_label->setStyleSheet(kPillStyle);
  m_label->setAlignment(Qt::AlignCenter);

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setAlignment(Qt::AlignCenter);
  layout->addWidget(m_label);

  auto *opacityEffect = new QGraphicsOpacityEffect(this);
  opacityEffect->setOpacity(0.0);
  setGraphicsEffect(opacityEffect);

  m_fadeAnimation = new QPropertyAnimation(opacityEffect, "opacity", this);
  m_fadeAnimation->setDuration(UIConstants::Overlay::TEXT_ZOOM_HUD_FADE_DURATION_MS);

  m_holdTimer = new QTimer(this);
  m_holdTimer->setSingleShot(true);
  m_holdTimer->setInterval(UIConstants::Overlay::TEXT_ZOOM_HUD_HOLD_DURATION_MS);
  QObject::connect(m_holdTimer, &QTimer::timeout, this, [this]() { scheduleHide(); });

  if (parent) {
    parent->installEventFilter(this);
  }

  QWidget::hide();
}

TextZoomHud::~TextZoomHud() = default;

void TextZoomHud::showZoom(int percent) {
  if (!parentWidget() || !m_label) {
    return;
  }

  m_label->setText(QStringLiteral("%1%").arg(percent));
  m_label->adjustSize();
  adjustSize();
  updatePosition();

  // Cancel any in-flight fade-out so a burst of Ctrl++ presses doesn't get
  // stuck mid-fade.
  QObject::disconnect(m_fadeAnimation, &QPropertyAnimation::finished, this, nullptr);
  m_fadeAnimation->stop();

  auto *effect = qobject_cast<QGraphicsOpacityEffect *>(graphicsEffect());
  if (effect) {
    m_fadeAnimation->setStartValue(effect->opacity());
    m_fadeAnimation->setEndValue(1.0);
    m_fadeAnimation->start();
  }

  QWidget::show();
  raise();

  m_holdTimer->start();
}

void TextZoomHud::scheduleHide() {
  if (!isVisible()) {
    return;
  }

  QObject::disconnect(m_fadeAnimation, &QPropertyAnimation::finished, this, nullptr);
  m_fadeAnimation->stop();

  auto *effect = qobject_cast<QGraphicsOpacityEffect *>(graphicsEffect());
  if (!effect) {
    QWidget::hide();
    return;
  }

  m_fadeAnimation->setStartValue(effect->opacity());
  m_fadeAnimation->setEndValue(0.0);
  QObject::connect(
      m_fadeAnimation, &QPropertyAnimation::finished, this,
      [this]() {
        if (!m_holdTimer->isActive()) {
          QWidget::hide();
        }
      },
      Qt::SingleShotConnection);
  m_fadeAnimation->start();
}

bool TextZoomHud::eventFilter(QObject *watched, QEvent *event) {
  if (watched == parentWidget() && event && event->type() == QEvent::Resize) {
    updatePosition();
  }
  return QWidget::eventFilter(watched, event);
}

void TextZoomHud::updatePosition() {
  QWidget *parent = parentWidget();
  if (!parent) {
    return;
  }
  // Cover the full parent rect so the centered child layout produces a
  // top-center pill regardless of the parent's size. Layout handles the
  // horizontal centering; we shift the widget down so the pill sits near
  // the top edge instead of the vertical center.
  QSize hint = sizeHint();
  const int w = parent->width();
  const int h = qMax(hint.height(), m_label ? m_label->height() : 0);
  setGeometry(0, kTopMargin, w, h);
}
