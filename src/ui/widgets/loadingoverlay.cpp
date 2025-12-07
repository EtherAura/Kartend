// Semi-transparent loading overlay with spinner animation and progress bar.
#include "loadingoverlay.h"

#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

namespace {
constexpr int SPINNER_SIZE = 48;
constexpr int SPINNER_THICKNESS = 4;
constexpr int CONTENT_PADDING = 24;
constexpr int FADE_DURATION_MS = 150;
constexpr int SPINNER_DURATION_MS = 1000;
constexpr QColor OVERLAY_COLOR = QColor(0, 0, 0, 160);
constexpr QColor SPINNER_COLOR = QColor(100, 180, 255);
constexpr QColor SPINNER_BG_COLOR = QColor(60, 60, 60);
} // namespace

LoadingOverlay::LoadingOverlay(QWidget *parent)
    : QWidget(parent) {
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
  m_contentWidget->setStyleSheet(
      "#loadingContent {"
      "  background-color: rgba(40, 40, 40, 230);"
      "  border-radius: 12px;"
      "  padding: 20px;"
      "}");
  
  auto *layout = new QVBoxLayout(m_contentWidget);
  layout->setAlignment(Qt::AlignCenter);
  layout->setSpacing(16);
  layout->setContentsMargins(CONTENT_PADDING, CONTENT_PADDING + SPINNER_SIZE + 8,
                              CONTENT_PADDING, CONTENT_PADDING);
  
  // Message label
  m_messageLabel = new QLabel("Loading...", m_contentWidget);
  m_messageLabel->setAlignment(Qt::AlignCenter);
  m_messageLabel->setStyleSheet(
      "QLabel {"
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
  m_progressBar->setFixedWidth(200);
  m_progressBar->setFixedHeight(8);
  m_progressBar->setStyleSheet(
      "QProgressBar {"
      "  background-color: rgba(60, 60, 60, 200);"
      "  border-radius: 4px;"
      "  text-align: center;"
      "}"
      "QProgressBar::chunk {"
      "  background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
      "    stop:0 #4a9eff, stop:1 #64b4ff);"
      "  border-radius: 4px;"
      "}");
  m_progressBar->hide();
  layout->addWidget(m_progressBar, 0, Qt::AlignCenter);
  
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
  
  updatePosition();
  startSpinnerAnimation();
  
  // Fade in
  if (auto *effect = qobject_cast<QGraphicsOpacityEffect*>(graphicsEffect())) {
    effect->setOpacity(0.0);
  }
  m_fadeAnimation->setStartValue(0.0);
  m_fadeAnimation->setEndValue(1.0);
  m_fadeAnimation->start();
  
  QWidget::show();
  raise();
}

void LoadingOverlay::showWithProgress(const QString &message, int current, int total) {
  m_active = true;
  m_showProgress = true;
  
  m_messageLabel->setText(message);
  m_progressBar->setRange(0, total);
  m_progressBar->setValue(current);
  m_progressBar->show();
  
  updatePosition();
  startSpinnerAnimation();
  
  // Fade in
  if (auto *effect = qobject_cast<QGraphicsOpacityEffect*>(graphicsEffect())) {
    effect->setOpacity(0.0);
  }
  m_fadeAnimation->setStartValue(0.0);
  m_fadeAnimation->setEndValue(1.0);
  m_fadeAnimation->start();
  
  QWidget::show();
  raise();
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
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, [this]() {
      QWidget::hide();
      disconnect(m_fadeAnimation, &QPropertyAnimation::finished, nullptr, nullptr);
    }, Qt::SingleShotConnection);
    m_fadeAnimation->start();
  } else {
    QWidget::hide();
  }
}

void LoadingOverlay::setSpinnerAngle(int angle) {
  m_spinnerAngle = angle;
  update(); // Trigger repaint
}

void LoadingOverlay::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event)
  
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  
  // Draw semi-transparent overlay background
  painter.fillRect(rect(), OVERLAY_COLOR);
  
  // Draw spinner at top-center of content widget
  if (m_contentWidget && m_active) {
    QRect contentRect = m_contentWidget->geometry();
    int spinnerX = contentRect.center().x();
    int spinnerY = contentRect.top() + CONTENT_PADDING + SPINNER_SIZE / 2;
    
    painter.save();
    painter.translate(spinnerX, spinnerY);
    painter.rotate(m_spinnerAngle);
    
    // Draw spinner background arc
    QPen bgPen(SPINNER_BG_COLOR, SPINNER_THICKNESS, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(bgPen);
    QRect spinnerRect(-SPINNER_SIZE / 2, -SPINNER_SIZE / 2, SPINNER_SIZE, SPINNER_SIZE);
    painter.drawArc(spinnerRect, 0, 360 * 16);
    
    // Draw spinner foreground arc (partial)
    QPen fgPen(SPINNER_COLOR, SPINNER_THICKNESS, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(fgPen);
    painter.drawArc(spinnerRect, 90 * 16, 270 * 16); // 270 degree arc
    
    painter.restore();
  }
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
  
  // Center content widget
  if (m_contentWidget) {
    m_contentWidget->adjustSize();
    QSize contentSize = m_contentWidget->sizeHint();
    // Add extra height for spinner
    contentSize.setHeight(contentSize.height() + SPINNER_SIZE + 8);
    int x = (width() - contentSize.width()) / 2;
    int y = (height() - contentSize.height()) / 2;
    m_contentWidget->setGeometry(x, y, contentSize.width(), contentSize.height());
  }
}
