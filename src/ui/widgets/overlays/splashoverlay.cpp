// Transient splash overlay shown on startup and when the main window regains focus.
#include "splashoverlay.h"

#include "overlayzorderregistry.h"

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

#include "uiconstants/overlay.h"

namespace {
constexpr int kCardMinimumWidth = 420;
constexpr int kCardMaximumWidth = 560;
constexpr int kCardPaddingH = 34;
constexpr int kCardPaddingV = 28;
constexpr int kIconSize = 92;
constexpr int kIconColumnWidth = 116;
constexpr QColor kOverlayColor = QColor(5, 8, 12, 190);

auto appDisplayName() -> QString {
#ifdef APP_DISPLAY_NAME
  return QStringLiteral(APP_DISPLAY_NAME);
#else
  return QStringLiteral("Kartend");
#endif
}

auto appVersion() -> QString {
#ifdef APP_VERSION
  return QStringLiteral(APP_VERSION);
#else
  return {};
#endif
}
} // namespace

SplashOverlay::SplashOverlay(QWidget *parent) : QWidget(parent) {
  setupUI();

  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  setFocusPolicy(Qt::NoFocus);

  if (parent) {
    parent->installEventFilter(this);
  }
  if (qApp) {
    qApp->installEventFilter(this);
  }

  m_hideTimer.setSingleShot(true);
  connect(&m_hideTimer, &QTimer::timeout, this, [this]() { hideSplash(true); });

  QWidget::hide();
}

SplashOverlay::~SplashOverlay() {
  if (qApp) {
    qApp->removeEventFilter(this);
  }
}

void SplashOverlay::setupUI() {
  setAutoFillBackground(false);

  auto *opacityEffect = new QGraphicsOpacityEffect(this);
  opacityEffect->setOpacity(1.0);
  setGraphicsEffect(opacityEffect);

  m_fadeAnimation = new QPropertyAnimation(opacityEffect, "opacity", this);
  m_fadeAnimation->setDuration(UIConstants::Overlay::SPLASH_FADE_DURATION_MS);

  m_cardWidget = new QWidget(this);
  m_cardWidget->setObjectName(QStringLiteral("splashCard"));
  m_cardWidget->setMinimumWidth(kCardMinimumWidth);
  m_cardWidget->setMaximumWidth(kCardMaximumWidth);
  m_cardWidget->setStyleSheet(QStringLiteral("#splashCard {"
                                             "  background-color: rgba(24, 27, 34, 242);"
                                             "  border: 1px solid rgba(255, 255, 255, 38);"
                                             "  border-radius: 18px;"
                                             "}"
                                             "QLabel { color: white; }"));

  auto *rootLayout = new QHBoxLayout(m_cardWidget);
  rootLayout->setContentsMargins(kCardPaddingH, kCardPaddingV, kCardPaddingH, kCardPaddingV);
  rootLayout->setSpacing(22);
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
  m_titleLabel->setObjectName(QStringLiteral("splashTitle"));
  m_titleLabel->setStyleSheet(
      QStringLiteral("#splashTitle { font-size: 32px; font-weight: 700; letter-spacing: 0.4px; }"));
  textLayout->addWidget(m_titleLabel);

  m_subtitleLabel = new QLabel(m_cardWidget);
  m_subtitleLabel->setObjectName(QStringLiteral("splashSubtitle"));
  m_subtitleLabel->setWordWrap(true);
  m_subtitleLabel->setStyleSheet(
      QStringLiteral("#splashSubtitle { color: rgba(255, 255, 255, 210); font-size: 15px; }"));
  textLayout->addWidget(m_subtitleLabel);

  m_versionLabel = new QLabel(m_cardWidget);
  m_versionLabel->setObjectName(QStringLiteral("splashVersion"));
  m_versionLabel->setStyleSheet(
      QStringLiteral("#splashVersion { color: rgba(255, 255, 255, 145); font-size: 12px; }"));
  textLayout->addWidget(m_versionLabel);
}

void SplashOverlay::showSplash(Reason reason, const QString &titleOverride,
                               const QString &subtitleOverride) {
  if (!parentWidget()) {
    return;
  }

  disconnect(m_fadeAnimation, &QPropertyAnimation::finished, this, nullptr);
  m_fadeAnimation->stop();

  m_active = true;
  updateContent(reason, titleOverride, subtitleOverride);
  updatePosition();

  if (auto *effect = qobject_cast<QGraphicsOpacityEffect *>(graphicsEffect())) {
    effect->setOpacity(0.0);
  }
  m_fadeAnimation->setStartValue(0.0);
  m_fadeAnimation->setEndValue(1.0);
  m_fadeAnimation->start();

  QWidget::show();
  // Route this widget's restack through the central layer manager when
  // installed so all currently-visible overlays end up in their documented
  // z-order. The inner card-widget raise is intra-overlay (sub-widget
  // ordering within this overlay's own paint tree) so it stays direct.
  if (m_layerManager) {
    m_layerManager->bringToFront(this);
  } else {
    raise();
  }
  if (m_cardWidget) {
    m_cardWidget->raise();
  }

  m_hideTimer.start(UIConstants::Overlay::SPLASH_VISIBLE_DURATION_MS);
}

void SplashOverlay::hideSplash(bool animated) {
  if (!m_active && !isVisible()) {
    return;
  }

  m_active = false;
  m_hideTimer.stop();

  disconnect(m_fadeAnimation, &QPropertyAnimation::finished, this, nullptr);
  m_fadeAnimation->stop();

  if (!animated) {
    QWidget::hide();
    return;
  }

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

void SplashOverlay::updateContent(Reason reason, const QString &titleOverride,
                                  const QString &subtitleOverride) {
  const QString displayName = appDisplayName();
  const QString defaultTitle = reason == Reason::Startup ? displayName : tr("Welcome back");
  // Resume splash deliberately has no default subtitle — the title alone
  // ("Welcome back") carries the message. Custom text added via settings
  // shows it back; otherwise the label is hidden so the card sizes tightly.
  const QString defaultSubtitle =
      reason == Reason::Startup ? tr("Preparing your media collections") : QString();
  if (m_titleLabel) {
    m_titleLabel->setText(titleOverride.isEmpty() ? defaultTitle : titleOverride);
  }
  if (m_subtitleLabel) {
    const QString subtitle = subtitleOverride.isEmpty() ? defaultSubtitle : subtitleOverride;
    m_subtitleLabel->setText(subtitle);
    m_subtitleLabel->setVisible(!subtitle.isEmpty());
  }
  if (m_versionLabel) {
    const QString version = appVersion();
    m_versionLabel->setText(version.isEmpty() ? tr("Media collection launcher")
                                              : tr("Version %1").arg(version));
  }
}

void SplashOverlay::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event)

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.fillRect(rect(), kOverlayColor);

  const QColor accent = QApplication::palette().color(QPalette::Highlight);
  QColor glow = accent;
  glow.setAlpha(70);
  QRadialGradient gradient(rect().center(), qMax(width(), height()) / 2.0);
  gradient.setColorAt(0.0, glow);
  gradient.setColorAt(0.46, QColor(glow.red(), glow.green(), glow.blue(), 18));
  gradient.setColorAt(1.0, QColor(0, 0, 0, 0));
  painter.fillRect(rect(), gradient);
}

void SplashOverlay::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  updatePosition();
}

bool SplashOverlay::eventFilter(QObject *watched, QEvent *event) {
  if (watched == parentWidget() && event && event->type() == QEvent::Resize) {
    updatePosition();
  }

  if (shouldDismissForEvent(watched, event)) {
    hideSplash(true);
  }

  return QWidget::eventFilter(watched, event);
}

void SplashOverlay::updatePosition() {
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

bool SplashOverlay::shouldDismissForEvent(QObject *watched, QEvent *event) const {
  if (!m_active || !event || !isObjectInOverlayWindow(watched)) {
    return false;
  }

  switch (event->type()) {
  case QEvent::KeyPress:
  case QEvent::MouseButtonPress:
  case QEvent::MouseButtonDblClick:
  case QEvent::Wheel:
  case QEvent::TouchBegin:
    return true;
  default:
    return false;
  }
}

bool SplashOverlay::isObjectInOverlayWindow(QObject *watched) const {
  const QWidget *parent = parentWidget();
  if (!parent || !watched) {
    return false;
  }

  if (watched == this || watched == parent) {
    return true;
  }

  const auto *widget = qobject_cast<const QWidget *>(watched);
  if (!widget) {
    return false;
  }

  return widget == parent || parent->isAncestorOf(widget) || widget->window() == parent->window();
}
