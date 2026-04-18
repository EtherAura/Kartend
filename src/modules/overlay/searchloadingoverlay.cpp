// Provides a subtle loading overlay during search operations.
#include "searchloadingoverlay.h"
#include "uiconstants.h"
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {
// Overlay styling
constexpr int FADE_DURATION_MS =
    UIConstants::Overlay::SEARCH_LOADING_FADE_DURATION_MS;
constexpr int PULSE_INTERVAL_MS =
    UIConstants::Overlay::SEARCH_LOADING_PULSE_INTERVAL_MS;
constexpr double OVERLAY_OPACITY_MAX = 0.95;
constexpr double LABEL_OPACITY_MIN = 0.4;
constexpr double LABEL_OPACITY_MAX = 0.7;

// Use a color that matches the typical dark theme background
const QString OVERLAY_STYLE =
    QStringLiteral("background-color: rgba(30, 30, 30, 230);"
                   "border: none;");

const QString LABEL_STYLE = QStringLiteral("color: rgba(180, 180, 180, 180);"
                                           "font-size: 13px;"
                                           "font-weight: normal;"
                                           "background: transparent;");
} // namespace

SearchLoadingOverlay::SearchLoadingOverlay(QObject *parent) : QObject(parent) {
  m_pulseTimer = new QTimer(this);
  m_pulseTimer->setInterval(PULSE_INTERVAL_MS);
  connect(m_pulseTimer, &QTimer::timeout, this, [this]() {
    if (m_label && m_label->graphicsEffect()) {
      auto *effect =
          qobject_cast<QGraphicsOpacityEffect *>(m_label->graphicsEffect());
      if (effect) {
        double targetOpacity =
            m_pulseDimming ? LABEL_OPACITY_MIN : LABEL_OPACITY_MAX;

        // Animate opacity change
        if (m_pulseAnimation) {
          m_pulseAnimation->stop();
        }
        m_pulseAnimation = new QPropertyAnimation(effect, "opacity", this);
        m_pulseAnimation->setDuration(PULSE_INTERVAL_MS - 50);
        m_pulseAnimation->setStartValue(effect->opacity());
        m_pulseAnimation->setEndValue(targetOpacity);
        m_pulseAnimation->setEasingCurve(QEasingCurve::InOutSine);
        m_pulseAnimation->start(QAbstractAnimation::DeleteWhenStopped);

        m_pulseDimming = !m_pulseDimming;
      }
    }
  });
}

SearchLoadingOverlay::~SearchLoadingOverlay() { hideImmediate(); }

void SearchLoadingOverlay::setParentWidget(QWidget *parent) {
  if (m_parentWidget != parent) {
    hideImmediate();
    m_parentWidget = parent;
  }
}

void SearchLoadingOverlay::ensureOverlay() {
  if (m_overlay || !m_parentWidget) {
    return;
  }

  m_overlay = new QWidget(m_parentWidget);
  m_overlay->setStyleSheet(OVERLAY_STYLE);
  m_overlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);

  // Create opacity effect for fade animation
  auto *overlayEffect = new QGraphicsOpacityEffect(m_overlay);
  overlayEffect->setOpacity(0.0);
  m_overlay->setGraphicsEffect(overlayEffect);

  // Create centered label with search indicator
  auto *layout = new QVBoxLayout(m_overlay);
  layout->setAlignment(Qt::AlignCenter);

  m_label = new QLabel(QStringLiteral("Searching..."), m_overlay);
  m_label->setStyleSheet(LABEL_STYLE);
  m_label->setAlignment(Qt::AlignCenter);

  // Separate opacity effect for label pulsing
  auto *labelEffect = new QGraphicsOpacityEffect(m_label);
  labelEffect->setOpacity(LABEL_OPACITY_MAX);
  m_label->setGraphicsEffect(labelEffect);

  layout->addWidget(m_label);

  m_overlay->setLayout(layout);
  m_overlay->hide();
}

void SearchLoadingOverlay::show() {
  ensureOverlay();
  if (!m_overlay) {
    return;
  }

  updateGeometry();
  m_overlay->raise();
  m_overlay->show();

  // Fade in
  auto *effect =
      qobject_cast<QGraphicsOpacityEffect *>(m_overlay->graphicsEffect());
  if (effect) {
    if (m_fadeAnimation) {
      m_fadeAnimation->stop();
    }
    m_fadeAnimation = new QPropertyAnimation(effect, "opacity", this);
    m_fadeAnimation->setDuration(FADE_DURATION_MS);
    m_fadeAnimation->setStartValue(effect->opacity());
    m_fadeAnimation->setEndValue(OVERLAY_OPACITY_MAX);
    m_fadeAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_fadeAnimation->start(QAbstractAnimation::DeleteWhenStopped);
  }

  startPulseAnimation();
}

void SearchLoadingOverlay::hide() {
  if (!m_overlay || !m_overlay->isVisible()) {
    return;
  }

  stopPulseAnimation();

  // Fade out
  auto *effect =
      qobject_cast<QGraphicsOpacityEffect *>(m_overlay->graphicsEffect());
  if (effect) {
    if (m_fadeAnimation) {
      m_fadeAnimation->stop();
    }
    m_fadeAnimation = new QPropertyAnimation(effect, "opacity", this);
    m_fadeAnimation->setDuration(FADE_DURATION_MS);
    m_fadeAnimation->setStartValue(effect->opacity());
    m_fadeAnimation->setEndValue(0.0);
    m_fadeAnimation->setEasingCurve(QEasingCurve::InCubic);
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, [this]() {
      if (m_overlay) {
        m_overlay->hide();
      }
    });
    m_fadeAnimation->start(QAbstractAnimation::DeleteWhenStopped);
  }
}

void SearchLoadingOverlay::hideImmediate() {
  stopPulseAnimation();
  if (m_fadeAnimation) {
    m_fadeAnimation->stop();
    m_fadeAnimation = nullptr;
  }
  if (m_overlay) {
    m_overlay->hide();
    m_overlay->deleteLater();
    m_overlay = nullptr;
    m_label = nullptr;
  }
}

bool SearchLoadingOverlay::isVisible() const {
  return m_overlay && m_overlay->isVisible();
}

void SearchLoadingOverlay::updateGeometry() {
  if (m_overlay && m_parentWidget) {
    m_overlay->setGeometry(m_parentWidget->rect());
  }
}

void SearchLoadingOverlay::startPulseAnimation() {
  m_pulseDimming = true;
  m_pulseTimer->start();
}

void SearchLoadingOverlay::stopPulseAnimation() {
  m_pulseTimer->stop();
  if (m_pulseAnimation) {
    m_pulseAnimation->stop();
    m_pulseAnimation = nullptr;
  }
}
