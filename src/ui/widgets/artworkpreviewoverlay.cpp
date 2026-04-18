// Artwork preview overlay for displaying artwork in list view mode.
#include "artworkpreviewoverlay.h"
#include "artworkutils.h"
#include "uiconstants.h"

#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

ArtworkPreviewOverlay::ArtworkPreviewOverlay(QWidget *parent)
    : QWidget(parent) {
  setupUI();
  hide();
}

void ArtworkPreviewOverlay::setupUI() {
  // Semi-transparent background
  setAttribute(Qt::WA_TranslucentBackground, false);
  setAutoFillBackground(false);
  setFocusPolicy(Qt::StrongFocus);

  // Artwork display label
  m_artworkLabel = new QLabel(this);
  m_artworkLabel->setAlignment(Qt::AlignCenter);
  m_artworkLabel->setStyleSheet("QLabel { background-color: transparent; "
                                "border: 2px solid palette(highlight); "
                                "border-radius: 8px; }");

  // Close button
  m_closeButton = new QPushButton(tr("✕"), this);
  m_closeButton->setFixedSize(32, 32);
  m_closeButton->setCursor(Qt::PointingHandCursor);
  m_closeButton->setStyleSheet(
      "QPushButton { background-color: palette(window); border: 1px solid "
      "palette(mid); "
      "border-radius: 16px; font-weight: bold; font-size: 16px; } "
      "QPushButton:hover { background-color: palette(highlight); color: "
      "palette(highlighted-text); }");
  connect(m_closeButton, &QPushButton::clicked, this,
          &ArtworkPreviewOverlay::hideOverlay);
}

void ArtworkPreviewOverlay::showArtworkForFile(
    const QString &filePath, const QString &artworkDirectory) {
  m_currentFilePath = filePath;

  // Find artwork for this file
  QString artworkPath = ArtworkUtils::findArtworkForFile(
      QFileInfo(filePath).fileName(), artworkDirectory);

  if (artworkPath.isEmpty()) {
    // No artwork found - don't show overlay
    return;
  }

  // Load and display the artwork
  QPixmap artwork(artworkPath);
  if (artwork.isNull()) {
    return;
  }

  // Scale artwork to fit within 80% of parent size while maintaining aspect
  // ratio
  QWidget *parentWidget = this->parentWidget();
  if (!parentWidget) {
    return;
  }

  int maxWidth = parentWidget->width() * 0.8;
  int maxHeight = parentWidget->height() * 0.8;

  QPixmap scaled = artwork.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio,
                                  Qt::SmoothTransformation);
  m_artworkLabel->setPixmap(scaled);
  m_artworkLabel->setFixedSize(scaled.size());

  // Resize overlay to cover parent
  resize(parentWidget->size());
  centerArtwork();

  show();
  raise();
  activateWindow();
  setFocus(Qt::PopupFocusReason);
}

void ArtworkPreviewOverlay::hideOverlay() {
  hide();
  m_currentFilePath.clear();
  if (m_artworkLabel) {
    m_artworkLabel->clear();
  }
}

void ArtworkPreviewOverlay::centerArtwork() {
  if (!m_artworkLabel || !m_closeButton) {
    return;
  }

  // Center the artwork label
  int artX = (width() - m_artworkLabel->width()) / 2;
  int artY = (height() - m_artworkLabel->height()) / 2;
  m_artworkLabel->move(artX, artY);

  // Position close button at top-right of artwork
  int closeX = artX + m_artworkLabel->width() - m_closeButton->width() / 2;
  int closeY = artY - m_closeButton->height() / 2;
  m_closeButton->move(closeX, closeY);
}

void ArtworkPreviewOverlay::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event)

  QPainter painter(this);

  // Semi-transparent dark background
  QColor overlayColor = palette().color(QPalette::Window);
  overlayColor.setAlphaF(0.85);
  painter.fillRect(rect(), overlayColor);
}

void ArtworkPreviewOverlay::mousePressEvent(QMouseEvent *event) {
  // Click anywhere outside artwork closes the overlay
  if (m_artworkLabel && !m_artworkLabel->geometry().contains(event->pos())) {
    hideOverlay();
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void ArtworkPreviewOverlay::keyPressEvent(QKeyEvent *event) {
  // Escape closes the overlay
  if (event->key() == Qt::Key_Escape) {
    hideOverlay();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void ArtworkPreviewOverlay::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  centerArtwork();
}
