// "Now Playing" overlay shown while a runtime-tracked child process is
// running (Kartend-qxv). Mirrors SplashOverlay's visual language but stays
// up indefinitely until hideOverlay() is invoked.
#include "nowplayingoverlay.h"

#include <QApplication>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "uiconstants.h"

namespace {
constexpr int kCardMinimumWidth = 460;
constexpr int kCardMaximumWidth = 620;
constexpr int kCardPaddingH = 36;
constexpr int kCardPaddingV = 30;
constexpr int kIconSize = 96;
constexpr int kIconColumnWidth = 120;
constexpr QColor kOverlayColor = QColor(3, 5, 9, 220);
} // namespace

NowPlayingOverlay::NowPlayingOverlay(QWidget *parent) : QWidget(parent) {
  setupUI();

  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  setFocusPolicy(Qt::NoFocus);

  if (parent) {
    parent->installEventFilter(this);
  }

  QWidget::hide();
}

NowPlayingOverlay::~NowPlayingOverlay() = default;

void NowPlayingOverlay::setupUI() {
  setAutoFillBackground(false);

  auto *opacityEffect = new QGraphicsOpacityEffect(this);
  opacityEffect->setOpacity(1.0);
  setGraphicsEffect(opacityEffect);

  m_fadeAnimation = new QPropertyAnimation(opacityEffect, "opacity", this);
  m_fadeAnimation->setDuration(UIConstants::Overlay::SPLASH_FADE_DURATION_MS);

  m_cardWidget = new QWidget(this);
  m_cardWidget->setObjectName(QStringLiteral("nowPlayingCard"));
  m_cardWidget->setMinimumWidth(kCardMinimumWidth);
  m_cardWidget->setMaximumWidth(kCardMaximumWidth);
  m_cardWidget->setStyleSheet(QStringLiteral(
      "#nowPlayingCard {"
      "  background-color: rgba(20, 23, 30, 245);"
      "  border: 1px solid rgba(255, 255, 255, 50);"
      "  border-radius: 20px;"
      "}"
      "QLabel { color: white; }"));

  auto *rootLayout = new QHBoxLayout(m_cardWidget);
  rootLayout->setContentsMargins(kCardPaddingH, kCardPaddingV, kCardPaddingH, kCardPaddingV);
  rootLayout->setSpacing(24);
  rootLayout->setAlignment(Qt::AlignCenter);

  m_iconLabel = new QLabel(m_cardWidget);
  m_iconLabel->setFixedSize(kIconColumnWidth, kIconColumnWidth);
  m_iconLabel->setAlignment(Qt::AlignCenter);
  m_iconLabel->setPixmap(QIcon(QStringLiteral(":/icon.svg")).pixmap(kIconSize, kIconSize));
  rootLayout->addWidget(m_iconLabel, 0, Qt::AlignCenter);

  auto *textLayout = new QVBoxLayout;
  textLayout->setContentsMargins(0, 0, 0, 0);
  textLayout->setSpacing(8);
  textLayout->setAlignment(Qt::AlignVCenter);
  rootLayout->addLayout(textLayout, 1);

  m_titleLabel = new QLabel(m_cardWidget);
  m_titleLabel->setObjectName(QStringLiteral("nowPlayingTitle"));
  m_titleLabel->setStyleSheet(QStringLiteral(
      "#nowPlayingTitle { font-size: 30px; font-weight: 700; letter-spacing: 0.4px; }"));
  m_titleLabel->setText(tr("Now Playing"));
  textLayout->addWidget(m_titleLabel);

  m_subtitleLabel = new QLabel(m_cardWidget);
  m_subtitleLabel->setObjectName(QStringLiteral("nowPlayingSubtitle"));
  m_subtitleLabel->setWordWrap(true);
  m_subtitleLabel->setStyleSheet(QStringLiteral(
      "#nowPlayingSubtitle { color: rgba(255, 255, 255, 220); font-size: 18px; }"));
  textLayout->addWidget(m_subtitleLabel);

  m_hintLabel = new QLabel(m_cardWidget);
  m_hintLabel->setObjectName(QStringLiteral("nowPlayingHint"));
  m_hintLabel->setStyleSheet(QStringLiteral(
      "#nowPlayingHint { color: rgba(255, 255, 255, 140); font-size: 12px; }"));
  m_hintLabel->setText(tr("Kartend will return when the program closes."));
  textLayout->addWidget(m_hintLabel);
}

void NowPlayingOverlay::showOverlay(const QString &displayName) {
  if (!parentWidget()) {
    return;
  }

  disconnect(m_fadeAnimation, &QPropertyAnimation::finished, this, nullptr);
  m_fadeAnimation->stop();

  m_active = true;
  if (m_subtitleLabel) {
    m_subtitleLabel->setText(displayName);
  }
  updatePosition();

  if (auto *effect = qobject_cast<QGraphicsOpacityEffect *>(graphicsEffect())) {
    effect->setOpacity(0.0);
  }
  m_fadeAnimation->setStartValue(0.0);
  m_fadeAnimation->setEndValue(1.0);
  m_fadeAnimation->start();

  QWidget::show();
  raise();
  if (m_cardWidget) {
    m_cardWidget->raise();
  }
}

void NowPlayingOverlay::hideOverlay() {
  if (!m_active && !isVisible()) {
    return;
  }

  m_active = false;

  disconnect(m_fadeAnimation, &QPropertyAnimation::finished, this, nullptr);
  m_fadeAnimation->stop();

  m_fadeAnimation->setStartValue(1.0);
  m_fadeAnimation->setEndValue(0.0);
  connect(
      m_fadeAnimation, &QPropertyAnimation::finished, this,
      [this]() {
        if (!m_active) {
          QWidget::hide();
        }
      },
      Qt::SingleShotConnection);
  m_fadeAnimation->start();
}

void NowPlayingOverlay::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event)

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.fillRect(rect(), kOverlayColor);

  const QColor accent = QApplication::palette().color(QPalette::Highlight);
  QColor glow = accent;
  glow.setAlpha(60);
  QRadialGradient gradient(rect().center(), qMax(width(), height()) / 2.0);
  gradient.setColorAt(0.0, glow);
  gradient.setColorAt(0.5, QColor(glow.red(), glow.green(), glow.blue(), 14));
  gradient.setColorAt(1.0, QColor(0, 0, 0, 0));
  painter.fillRect(rect(), gradient);
}

void NowPlayingOverlay::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  updatePosition();
}

bool NowPlayingOverlay::eventFilter(QObject *watched, QEvent *event) {
  if (watched == parentWidget() && event && event->type() == QEvent::Resize) {
    updatePosition();
  }
  return QWidget::eventFilter(watched, event);
}

void NowPlayingOverlay::updatePosition() {
  if (!parentWidget()) {
    return;
  }

  setGeometry(parentWidget()->rect());

  if (!m_cardWidget) {
    return;
  }

  m_cardWidget->adjustSize();
  QSize cardSize = m_cardWidget->sizeHint();
  cardSize.setWidth(qBound(kCardMinimumWidth, cardSize.width(), kCardMaximumWidth));
  cardSize.setHeight(qMax(cardSize.height(), kIconColumnWidth + kCardPaddingV * 2));

  const int x = (width() - cardSize.width()) / 2;
  const int y = (height() - cardSize.height()) / 2;
  m_cardWidget->setGeometry(x, y, cardSize.width(), cardSize.height());
}
