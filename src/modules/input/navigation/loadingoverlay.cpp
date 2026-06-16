// Semi-transparent loading overlay with spinner animation and progress bar.
#include "loadingoverlay.h"

#include "overlaylayermanager.h"

#include <QApplication>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QResizeEvent>

namespace {
constexpr int SPINNER_SIZE = 48;
constexpr int SPINNER_THICKNESS = 4;
constexpr int CONTENT_PADDING = 24;
constexpr int FADE_DURATION_MS = 150;
constexpr int SPINNER_DURATION_MS = 1000;
constexpr QColor OVERLAY_COLOR = QColor(0, 0, 0, 160);
constexpr QColor SPINNER_BG_COLOR = QColor(45, 45, 45); // Match overlay background
} // namespace

// Custom spinner widget that draws the animated arc
class SpinnerWidget : public QWidget {
public:
  explicit SpinnerWidget(QWidget *parent = nullptr) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(SPINNER_SIZE + SPINNER_THICKNESS * 2, SPINNER_SIZE + SPINNER_THICKNESS * 2);
  }

  void setAngle(int angle) {
    m_angle = angle;
    update();
  }
  void setColor(const QColor &color) {
    m_color = color;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    int offset = SPINNER_THICKNESS;
    QRect spinnerRect(offset, offset, SPINNER_SIZE, SPINNER_SIZE);

    painter.save();
    painter.translate(width() / 2.0, height() / 2.0);
    painter.rotate(m_angle);
    painter.translate(-width() / 2.0, -height() / 2.0);

    // Draw background arc
    QPen bgPen(SPINNER_BG_COLOR, SPINNER_THICKNESS, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(bgPen);
    painter.drawArc(spinnerRect, 0, 360 * 16);

    // Draw foreground arc
    QPen fgPen(m_color, SPINNER_THICKNESS, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(fgPen);
    painter.drawArc(spinnerRect, 90 * 16, 270 * 16);

    painter.restore();
  }

private:
  int m_angle = 0;
  QColor m_color = QColor(100, 180, 255);
};

LoadingOverlay::LoadingOverlay(QWidget *parent) : QWidget(parent) {
  setupUI();

  // Install event filter on parent to track resizes
  if (parent) {
    parent->installEventFilter(this);
  }

  // Start hidden
  QWidget::hide();
}

void LoadingOverlay::setupUI() {
  // Semi-transparent background
  setAttribute(Qt::WA_TranslucentBackground, false);
  setAutoFillBackground(false);

  // Content container (centered box)
  m_contentWidget = new QWidget(this);
  m_contentWidget->setObjectName("loadingContent");
  m_contentWidget->setStyleSheet("#loadingContent {"
                                 "  background-color: rgba(45, 45, 45, 255);"
                                 "  border-radius: 12px;"
                                 "  padding: 20px;"
                                 "}");

  auto *layout = new QVBoxLayout(m_contentWidget);
  layout->setAlignment(Qt::AlignCenter);
  layout->setSpacing(16);
  layout->setContentsMargins(CONTENT_PADDING, CONTENT_PADDING + SPINNER_SIZE + 8, CONTENT_PADDING,
                             CONTENT_PADDING);

  // Message label
  m_messageLabel = new QLabel("Loading...", m_contentWidget);
  m_messageLabel->setAlignment(Qt::AlignCenter);
  m_messageLabel->setWordWrap(true);
  m_messageLabel->setMinimumWidth(300);
  m_messageLabel->setMaximumWidth(500);
  m_messageLabel->setStyleSheet("QLabel {"
                                "  color: white;"
                                "  font-size: 14px;"
                                "  font-weight: 500;"
                                "}");
  layout->addWidget(m_messageLabel);

  // Progress bar (hidden by default)
  m_progressBar = new QProgressBar(m_contentWidget);
  m_progressBar->setRange(0, 100);
  m_progressBar->setValue(0);
  m_progressBar->setTextVisible(true);
  m_progressBar->setFixedWidth(300);
  m_progressBar->setFixedHeight(20);
  m_progressBar->hide();
  layout->addWidget(m_progressBar, 0, Qt::AlignCenter);

  // Spinner widget (sits on top of content widget)
  m_spinnerWidget = new SpinnerWidget(this);

  // Spinner animation
  m_spinnerAnimation = new QPropertyAnimation(this, "spinnerAngle", this);
  m_spinnerAnimation->setDuration(SPINNER_DURATION_MS);
  m_spinnerAnimation->setStartValue(0);
  m_spinnerAnimation->setEndValue(360);
  m_spinnerAnimation->setLoopCount(-1); // Infinite loop

  // Fade animation for opacity effect
  auto *opacityEffect = new QGraphicsOpacityEffect(this);
  opacityEffect->setOpacity(1.0);
  setGraphicsEffect(opacityEffect);

  m_fadeAnimation = new QPropertyAnimation(opacityEffect, "opacity", this);
  m_fadeAnimation->setDuration(FADE_DURATION_MS);
}

void LoadingOverlay::show(const QString &message) {
  m_active = true;
  m_showProgress = false;

  m_messageLabel->setText(message);
  m_progressBar->hide();

  updateAccentColor();
  updatePosition();
  startSpinnerAnimation();

  // Fade in
  if (auto *effect = qobject_cast<QGraphicsOpacityEffect *>(graphicsEffect())) {
    effect->setOpacity(0.0);
  }
  m_fadeAnimation->setStartValue(0.0);
  m_fadeAnimation->setEndValue(1.0);
  m_fadeAnimation->start();

  QWidget::show();
  if (m_layerManager) {
    m_layerManager->bringToFront(this);
  } else {
    raise();
  }
  if (m_spinnerWidget) m_spinnerWidget->raise();
}

void LoadingOverlay::showWithProgress(const QString &message, int current, int total) {
  m_active = true;
  m_showProgress = true;

  m_messageLabel->setText(message);
  m_progressBar->setRange(0, total);
  m_progressBar->setValue(current);
  m_progressBar->show();

  updateAccentColor();
  updatePosition();
  startSpinnerAnimation();

  // Fade in
  if (auto *effect = qobject_cast<QGraphicsOpacityEffect *>(graphicsEffect())) {
    effect->setOpacity(0.0);
  }
  m_fadeAnimation->setStartValue(0.0);
  m_fadeAnimation->setEndValue(1.0);
  m_fadeAnimation->start();

  QWidget::show();
  if (m_layerManager) {
    m_layerManager->bringToFront(this);
  } else {
    raise();
  }
  if (m_spinnerWidget) m_spinnerWidget->raise();
}

void LoadingOverlay::setProgress(int current, int total) {
  if (!m_active) return;

  m_progressBar->setRange(0, total);
  m_progressBar->setValue(current);

  if (!m_showProgress && total > 0) {
    m_showProgress = true;
    m_progressBar->show();
    updatePosition();
  }
}

void LoadingOverlay::setMessage(const QString &message) {
  m_messageLabel->setText(message);
}

void LoadingOverlay::hide(bool animated) {
  if (!m_active) return;

  m_active = false;
  stopSpinnerAnimation();

  if (animated) {
    m_fadeAnimation->setStartValue(1.0);
    m_fadeAnimation->setEndValue(0.0);
    connect(
        m_fadeAnimation, &QPropertyAnimation::finished, this,
        [this]() {
          QWidget::hide();
          disconnect(m_fadeAnimation, &QPropertyAnimation::finished, nullptr, nullptr);
        },
        Qt::SingleShotConnection);
    m_fadeAnimation->start();
  } else {
    QWidget::hide();
  }
}

void LoadingOverlay::setSpinnerAngle(int angle) {
  m_spinnerAngle = angle;
  if (m_spinnerWidget) {
    static_cast<SpinnerWidget *>(m_spinnerWidget)->setAngle(angle);
  }
}

void LoadingOverlay::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event)

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // Draw semi-transparent overlay background only
  painter.fillRect(rect(), OVERLAY_COLOR);
}

void LoadingOverlay::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  updatePosition();
}

bool LoadingOverlay::eventFilter(QObject *watched, QEvent *event) {
  if (watched == parent() && event->type() == QEvent::Resize) {
    updatePosition();
  }
  return QWidget::eventFilter(watched, event);
}

void LoadingOverlay::startSpinnerAnimation() {
  if (m_spinnerAnimation->state() != QAbstractAnimation::Running) {
    m_spinnerAnimation->start();
  }
}

void LoadingOverlay::stopSpinnerAnimation() {
  m_spinnerAnimation->stop();
}

void LoadingOverlay::updatePosition() {
  if (!parentWidget()) return;

  // Fill parent widget
  setGeometry(parentWidget()->rect());

  // Center content widget with generous sizing for text
  if (m_contentWidget) {
    m_contentWidget->adjustSize();
    QSize contentSize = m_contentWidget->sizeHint();
    // Ensure minimum width for longer messages
    contentSize.setWidth(qMax(contentSize.width(), 350));
    // Add extra height for spinner and ensure text fits
    contentSize.setHeight(contentSize.height() + SPINNER_SIZE + 16);
    int x = (width() - contentSize.width()) / 2;
    int y = (height() - contentSize.height()) / 2;
    m_contentWidget->setGeometry(x, y, contentSize.width(), contentSize.height());

    // Position spinner widget at top center of content box
    // The spinner draws on top of the content widget with proper z-order
    if (m_spinnerWidget) {
      int spinnerX = x + (contentSize.width() - SPINNER_SIZE) / 2;
      int spinnerY = y + 12; // 12px from top of content box
      m_spinnerWidget->setGeometry(spinnerX, spinnerY, SPINNER_SIZE, SPINNER_SIZE);
      m_spinnerWidget->raise(); // Ensure spinner is on top
    }
  }
}

void LoadingOverlay::updateAccentColor() {
  // Cache the accent color for consistent use in spinner and progress bar
  m_accentColor = QApplication::palette().color(QPalette::Highlight);

  // Update spinner widget color
  if (m_spinnerWidget) {
    static_cast<SpinnerWidget *>(m_spinnerWidget)->setColor(m_accentColor);
  }

  if (!m_progressBar) return;

  // Apply accent color to progress bar (flat color, no gradient, to match
  // spinner)
  QString styleSheet = QString("QProgressBar {"
                               "  background-color: rgba(60, 60, 60, 200);"
                               "  border-radius: 4px;"
                               "  text-align: center;"
                               "  color: white;"
                               "  font-size: 11px;"
                               "}"
                               "QProgressBar::chunk {"
                               "  background-color: %1;"
                               "  border-radius: 4px;"
                               "}")
                           .arg(m_accentColor.name());

  m_progressBar->setStyleSheet(styleSheet);
}

void LoadingOverlay::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  // The accent is cached into m_accentColor (spinner) and baked as a literal
  // hex into the progress-bar stylesheet, so a repaint alone won't refresh it.
  // Re-resolve from the new system palette when it changes at runtime.
  if (event->type() == QEvent::PaletteChange) {
    updateAccentColor();
    update();
  }
}
