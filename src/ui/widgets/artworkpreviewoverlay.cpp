// Artwork / video preview overlay (+).
#include "artworkpreviewoverlay.h"
#include "artworkutils.h"
#include "uiconstants.h"
#include "videopreviewwidget.h"
#include "videoutils.h"

#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

ArtworkPreviewOverlay::ArtworkPreviewOverlay(QWidget *parent) : QWidget(parent) {
  setupUI();
  hide();
}

ArtworkPreviewOverlay::~ArtworkPreviewOverlay() = default;

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
  m_closeButton->setStyleSheet("QPushButton { background-color: palette(window); border: 1px solid "
                               "palette(mid); "
                               "border-radius: 16px; font-weight: bold; font-size: 16px; } "
                               "QPushButton:hover { background-color: palette(highlight); color: "
                               "palette(highlighted-text); }");
  connect(m_closeButton, &QPushButton::clicked, this, &ArtworkPreviewOverlay::hideOverlay);
}

void ArtworkPreviewOverlay::ensureVideoPreview() {
  if (m_videoPreview) {
    return;
  }
  m_videoPreview = new VideoPreviewWidget(this);
  m_videoPreview->setStyleSheet("border: 2px solid palette(highlight); border-radius: 8px;");
  m_videoPreview->hide();
}

void ArtworkPreviewOverlay::showArtworkForFile(const QString &filePath,
                                               const QString &artworkDirectory) {
  m_currentFilePath = filePath;

  QString artworkPath =
      ArtworkUtils::findArtworkForFile(QFileInfo(filePath).fileName(), artworkDirectory);

  if (artworkPath.isEmpty()) {
    return;
  }

  QPixmap artwork(artworkPath);
  if (artwork.isNull()) {
    return;
  }
  displayPixmap(artwork);
}

void ArtworkPreviewOverlay::showArtworkAtPath(const QString &absoluteArtworkPath) {
  if (absoluteArtworkPath.isEmpty()) {
    return;
  }
  // No m_currentFilePath assignment: the gallery click site doesn't have a
  // media file path to associate, and launchRequested fires with an empty
  // path so listeners fall back to the current selection.
  QPixmap artwork(absoluteArtworkPath);
  if (artwork.isNull()) {
    return;
  }
  displayPixmap(artwork);
}

void ArtworkPreviewOverlay::showVideoAtPath(const QString &absoluteVideoPath) {
  if (absoluteVideoPath.isEmpty()) {
    return;
  }
  // Same rationale as showArtworkAtPath: gallery-style entry points don't
  // carry a media file path, so leave m_currentFilePath empty and let the
  // selection-based fallback in InteractionManager pick up the launch.
  displayVideo(absoluteVideoPath);
}

bool ArtworkPreviewOverlay::showMediaForFile(const QString &filePath,
                                             const QString &artworkDirectory,
                                             const QString &videoDirectory) {
  m_currentFilePath = filePath;

  // Video-first per user preference.
  if (!videoDirectory.isEmpty()) {
    const QString videoPath = VideoUtils::findVideoForFile(filePath, videoDirectory);
    if (!videoPath.isEmpty()) {
      displayVideo(videoPath);
      m_currentFilePath = filePath;
      return true;
    }
  }

  // Fall back to artwork.
  if (!artworkDirectory.isEmpty()) {
    const QString artworkPath =
        ArtworkUtils::findArtworkForFile(QFileInfo(filePath).fileName(), artworkDirectory);
    if (!artworkPath.isEmpty()) {
      QPixmap artwork(artworkPath);
      if (!artwork.isNull()) {
        displayPixmap(artwork);
        m_currentFilePath = filePath;
        return true;
      }
    }
  }
  return false;
}

void ArtworkPreviewOverlay::displayPixmap(const QPixmap &pixmap) {
  // Stop any video preview that may have been left from a prior request.
  if (m_videoPreview) {
    m_videoPreview->stop();
    m_videoPreview->hide();
  }

  // Scale artwork to fit within 80% of parent size while maintaining aspect
  // ratio
  QWidget *parentWidget = this->parentWidget();
  if (!parentWidget) {
    return;
  }

  int maxWidth = parentWidget->width() * 0.8;
  int maxHeight = parentWidget->height() * 0.8;

  QPixmap scaled =
      pixmap.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  m_artworkLabel->setPixmap(scaled);
  m_artworkLabel->setFixedSize(scaled.size());
  m_artworkLabel->show();
  m_displayWidget = m_artworkLabel;

  // Resize overlay to cover parent
  resize(parentWidget->size());
  centerContent();

  show();
  raise();
  activateWindow();
  setFocus(Qt::PopupFocusReason);
}

void ArtworkPreviewOverlay::displayVideo(const QString &absoluteVideoPath) {
  QWidget *parentWidget = this->parentWidget();
  if (!parentWidget) {
    return;
  }

  ensureVideoPreview();

  // Hide the artwork label so the click-outside hit test uses the video
  // widget's geometry rather than a leftover artwork pane.
  m_artworkLabel->clear();
  m_artworkLabel->hide();

  const int maxWidth = parentWidget->width() * 0.8;
  const int maxHeight = parentWidget->height() * 0.8;
  m_videoPreview->setFixedSize(maxWidth, maxHeight);
  m_videoPreview->show();
  m_videoPreview->playVideo(absoluteVideoPath);
  m_displayWidget = m_videoPreview;

  resize(parentWidget->size());
  centerContent();

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
    m_artworkLabel->hide();
  }
  if (m_videoPreview) {
    m_videoPreview->stop();
    m_videoPreview->hide();
  }
  m_displayWidget = nullptr;
}

bool ArtworkPreviewOverlay::togglePreviewVideoPause() {
  if (!m_videoPreview || m_displayWidget != m_videoPreview || !m_videoPreview->hasLoadedSource()) {
    return false;
  }
  m_videoPreview->togglePauseResume();
  return true;
}

void ArtworkPreviewOverlay::centerContent() {
  if (!m_displayWidget || !m_closeButton) {
    return;
  }

  // Center the active display widget (artwork label or video preview).
  int x = (width() - m_displayWidget->width()) / 2;
  int y = (height() - m_displayWidget->height()) / 2;
  m_displayWidget->move(x, y);

  // Position close button at top-right of the displayed content.
  int closeX = x + m_displayWidget->width() - m_closeButton->width() / 2;
  int closeY = y - m_closeButton->height() / 2;
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
  // Click anywhere outside the displayed content closes the overlay.
  if (m_displayWidget && !m_displayWidget->geometry().contains(event->pos())) {
    hideOverlay();
    event->accept();
    return;
  }
  // accept inside-content presses too. Without this, Qt
  // propagates the unaccepted event up through the overlay's parent
  // (m_mediaScrollArea), where EventManager's app-level filter treats it as
  // a click on the underlying tile and changes selection.
  event->accept();
}

void ArtworkPreviewOverlay::mouseDoubleClickEvent(QMouseEvent *event) {
  // Double-click on the content itself = second activation = launch.
  // Double-click outside falls through to the press handler which closes.
  if (m_displayWidget && m_displayWidget->geometry().contains(event->pos())) {
    event->accept();
    emit launchRequested(m_currentFilePath);
    return;
  }
  // same anti-propagation reasoning as mousePressEvent.
  event->accept();
}

void ArtworkPreviewOverlay::keyPressEvent(QKeyEvent *event) {
  // Escape closes the overlay
  if (event->key() == Qt::Key_Escape) {
    hideOverlay();
    event->accept();
    return;
  }
  // Enter / Return = second activation = launch the previewed item
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    event->accept();
    emit launchRequested(m_currentFilePath);
    return;
  }
  // K toggles pause/resume on the overlay's video preview.
  // Same letter as YouTube's universal pause key — picked over Space because
  // Space is consumed by coverflow navigation elsewhere in the app.
  if (event->key() == Qt::Key_K) {
    if (togglePreviewVideoPause()) {
      event->accept();
      return;
    }
  }
  QWidget::keyPressEvent(event);
}

void ArtworkPreviewOverlay::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  centerContent();
}

void ArtworkPreviewOverlay::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  emit visibilityChanged(true);
}

void ArtworkPreviewOverlay::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  emit visibilityChanged(false);
}
