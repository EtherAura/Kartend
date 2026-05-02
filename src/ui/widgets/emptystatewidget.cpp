// Contextual empty/loading-state widget. See header for details.
#include "emptystatewidget.h"

#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QVBoxLayout>

namespace {
constexpr int SPINNER_SIZE = 28;
constexpr int SPINNER_THICKNESS = 3;
constexpr int SPINNER_DURATION_MS = 1000;
constexpr int CONTENT_SPACING = 10;

// Inline spinner that paints a rotating arc.
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
    painter.translate(width() / 2, height() / 2);
    painter.rotate(m_angle);
    painter.translate(-width() / 2, -height() / 2);

    QColor bg = m_color;
    bg.setAlpha(60);
    QPen bgPen(bg, SPINNER_THICKNESS, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(bgPen);
    painter.drawArc(spinnerRect, 0, 360 * 16);

    QPen fgPen(m_color, SPINNER_THICKNESS, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(fgPen);
    painter.drawArc(spinnerRect, 90 * 16, 270 * 16);

    painter.restore();
  }

private:
  int m_angle = 0;
  QColor m_color = QColor(100, 180, 255);
};
} // namespace

EmptyStateWidget::EmptyStateWidget(QWidget *parent) : QWidget(parent) { setupUI(); }

EmptyStateWidget::~EmptyStateWidget() = default;

void EmptyStateWidget::setupUI() {
  setAttribute(Qt::WA_TranslucentBackground, false);
  setAutoFillBackground(false);

  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);
  outer->addStretch(1);

  auto *content = new QWidget(this);
  auto *layout = new QVBoxLayout(content);
  layout->setAlignment(Qt::AlignCenter);
  layout->setSpacing(CONTENT_SPACING);
  layout->setContentsMargins(16, 16, 16, 16);

  m_iconLabel = new QLabel(content);
  m_iconLabel->setAlignment(Qt::AlignCenter);
  m_iconLabel->setStyleSheet("QLabel { font-size: 32px; }");
  m_iconLabel->setVisible(false);
  layout->addWidget(m_iconLabel, 0, Qt::AlignHCenter);

  m_spinnerWidget = new SpinnerWidget(content);
  m_spinnerWidget->setVisible(false);
  layout->addWidget(m_spinnerWidget, 0, Qt::AlignHCenter);

  m_messageLabel = new QLabel(content);
  m_messageLabel->setAlignment(Qt::AlignCenter);
  m_messageLabel->setWordWrap(true);
  m_messageLabel->setStyleSheet("QLabel { color: palette(text); font-size: 15px;"
                                "  font-weight: 500; }");
  layout->addWidget(m_messageLabel);

  m_hintLabel = new QLabel(content);
  m_hintLabel->setAlignment(Qt::AlignCenter);
  m_hintLabel->setWordWrap(true);
  m_hintLabel->setStyleSheet("QLabel { color: palette(mid); font-size: 12px; }");
  m_hintLabel->setVisible(false);
  layout->addWidget(m_hintLabel);

  m_progressBar = new QProgressBar(content);
  m_progressBar->setRange(0, 100);
  m_progressBar->setValue(0);
  m_progressBar->setTextVisible(true);
  m_progressBar->setFixedWidth(280);
  m_progressBar->setFixedHeight(16);
  m_progressBar->setVisible(false);
  layout->addWidget(m_progressBar, 0, Qt::AlignHCenter);

  outer->addWidget(content, 0, Qt::AlignHCenter);
  outer->addStretch(1);

  m_spinnerAnimation = new QPropertyAnimation(this, "spinnerAngle", this);
  m_spinnerAnimation->setStartValue(0);
  m_spinnerAnimation->setEndValue(360);
  m_spinnerAnimation->setDuration(SPINNER_DURATION_MS);
  m_spinnerAnimation->setLoopCount(-1);

  updateAccentColor();
}

void EmptyStateWidget::updateAccentColor() {
  m_accentColor = palette().color(QPalette::Highlight);
  if (m_spinnerWidget) {
    static_cast<SpinnerWidget *>(m_spinnerWidget)->setColor(m_accentColor);
  }
}

void EmptyStateWidget::showMessage(const QString &message, const QString &hint,
                                   const QString &icon) {
  if (m_iconLabel) {
    if (icon.isEmpty()) {
      m_iconLabel->clear();
      m_iconLabel->setVisible(false);
    } else {
      m_iconLabel->setText(icon);
      m_iconLabel->setVisible(true);
    }
  }
  if (m_messageLabel) m_messageLabel->setText(message);
  if (m_hintLabel) {
    if (hint.isEmpty()) {
      m_hintLabel->clear();
      m_hintLabel->setVisible(false);
    } else {
      m_hintLabel->setText(hint);
      m_hintLabel->setVisible(true);
    }
  }
  setSpinnerActive(false);
  clearProgress();
  setVisible(true);
}

void EmptyStateWidget::showSearchEmpty(const QString &query) {
  const QString trimmed = query.trimmed();
  const QString message = trimmed.isEmpty()
                              ? tr("No matching items")
                              : tr("No matches for \"%1\"").arg(trimmed);
  showMessage(message, tr("Press Esc to clear the search."), QStringLiteral("🔍"));
}

void EmptyStateWidget::showNoMediaDirectory() {
  showMessage(tr("No media directory configured"),
              tr("Open Settings to choose a folder for this collection."),
              QStringLiteral("📁"));
}

void EmptyStateWidget::showScanning(int current, int total) {
  if (m_iconLabel) {
    m_iconLabel->clear();
    m_iconLabel->setVisible(false);
  }
  if (m_messageLabel) m_messageLabel->setText(tr("Scanning collection..."));
  if (m_hintLabel) {
    m_hintLabel->setText(tr("Discovering items, this may take a moment."));
    m_hintLabel->setVisible(true);
  }
  setSpinnerActive(true);
  setProgress(current, total);
  setVisible(true);
}

void EmptyStateWidget::setProgress(int current, int total) {
  if (!m_progressBar) return;
  if (total <= 0) {
    m_progressBar->setVisible(false);
    return;
  }
  m_progressBar->setRange(0, total);
  m_progressBar->setValue(qBound(0, current, total));
  m_progressBar->setVisible(true);
}

void EmptyStateWidget::clearProgress() {
  if (m_progressBar) m_progressBar->setVisible(false);
}

void EmptyStateWidget::setSpinnerActive(bool active) {
  if (!m_spinnerWidget || !m_spinnerAnimation) return;
  m_spinnerWidget->setVisible(active);
  if (active) {
    if (m_spinnerAnimation->state() != QAbstractAnimation::Running) {
      m_spinnerAnimation->start();
    }
  } else {
    m_spinnerAnimation->stop();
  }
}

void EmptyStateWidget::setText(const QString &text) {
  showMessage(text);
}

void EmptyStateWidget::setSpinnerAngle(int angle) {
  m_spinnerAngle = angle;
  if (m_spinnerWidget) {
    static_cast<SpinnerWidget *>(m_spinnerWidget)->setAngle(angle);
  }
}
