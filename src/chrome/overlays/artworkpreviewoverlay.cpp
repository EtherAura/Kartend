// Artwork / video preview overlay (+).
#include "artworkpreviewoverlay.h"

#include "artworkutils.h"
#include "extensionutils.h"
#include "overlayzorderregistry.h"
#include "uiconstants/artwork.h"
#include "uiconstants/metadata.h"
#include "videopreviewwidget.h"
#include "videoutils.h"

#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLayoutItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QtConcurrent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

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

  // Close button. The visible label is a bare glyph, so give assistive
  // tech (and hover users) a real name.
  m_closeButton = new QPushButton(tr("✕"), this);
  m_closeButton->setAccessibleName(tr("Close"));
  m_closeButton->setToolTip(tr("Close"));
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

  // Gate before QPixmap — Qt's libqpdf imageformats plugin is PDFium-
  // backed and abort()s on some inputs, killing the main thread
  // (Kartend-wquq). findArtworkForFile scans the artwork directory and
  // can return a manual `.pdf` when no image variant exists; without
  // this guard a click here crashes the whole app.
  if (!ExtensionUtils::isDecodableImagePath(artworkPath)) {
    return;
  }
  m_currentArtworkPath = artworkPath;
  startPreviewLoad(artworkPath);
}

void ArtworkPreviewOverlay::showArtworkAtPath(const QString &absoluteArtworkPath) {
  if (absoluteArtworkPath.isEmpty()) {
    return;
  }
  // No m_currentFilePath assignment: the gallery click site doesn't have a
  // media file path to associate, and launchRequested fires with an empty
  // path so listeners fall back to the current selection.
  // Same PDFium-abort gate as showArtworkForFile — this entry point
  // receives raw gallery-click paths that could be a `.pdf` manual.
  if (!ExtensionUtils::isDecodableImagePath(absoluteArtworkPath)) {
    return;
  }
  m_currentArtworkPath = absoluteArtworkPath;
  startPreviewLoad(absoluteArtworkPath);
}

void ArtworkPreviewOverlay::showVideoAtPath(const QString &absoluteVideoPath) {
  if (absoluteVideoPath.isEmpty()) {
    return;
  }
  // Same rationale as showArtworkAtPath: gallery-style entry points don't
  // carry a media file path, so leave m_currentFilePath empty and let the
  // selection-based fallback in InteractionManager pick up the launch.
  m_currentArtworkPath = absoluteVideoPath;
  displayVideo(absoluteVideoPath);
}

bool ArtworkPreviewOverlay::showMediaForFile(const QString &filePath,
                                             const QString &artworkDirectory,
                                             const QString &videoDirectory) {
  m_currentFilePath = filePath;

  // Video-first per user preference. Two lookup roots in priority order
  // (matches detailspanemanagermetadata.cpp): the explicit videoDirectory,
  // then {artworkDirectory}/video/ — the single-root layout the scraper
  // writes to.
  QString videoPath;
  if (!videoDirectory.isEmpty()) {
    videoPath = VideoUtils::findVideoForFile(filePath, videoDirectory);
  }
  if (videoPath.isEmpty() && !artworkDirectory.isEmpty()) {
    videoPath = VideoUtils::findVideoForFile(filePath, QDir(artworkDirectory).filePath("video"));
  }
  if (!videoPath.isEmpty()) {
    m_currentArtworkPath = videoPath;
    displayVideo(videoPath);
    m_currentFilePath = filePath;
    return true;
  }

  // Fall back to artwork. The decode is async, so "shown" here means the
  // overlay opened on its loading placeholder; a decode failure closes it
  // again (see startPreviewLoad's delivery lambda).
  if (!artworkDirectory.isEmpty()) {
    const QString artworkPath =
        ArtworkUtils::findArtworkForFile(QFileInfo(filePath).fileName(), artworkDirectory);
    if (!artworkPath.isEmpty() && ExtensionUtils::isDecodableImagePath(artworkPath)) {
      m_currentArtworkPath = artworkPath;
      startPreviewLoad(artworkPath);
      return true;
    }
  }
  return false;
}

void ArtworkPreviewOverlay::startPreviewLoad(const QString &artworkPath) {
  QWidget *parentWidget = this->parentWidget();
  if (!parentWidget) {
    return;
  }
  // Decode off the GUI thread, mirroring the gallery-thumb loads in
  // rebuildGalleryStrip and the marquee's watcher pattern: a synchronous
  // QPixmap(path) here blocked the UI for the largest image the overlay ever
  // shows, and Left/Right cycling re-paid that per keypress (Kartend-h7xnr.4).
  // A newer request supersedes any in-flight decode so rapid cycling can't
  // queue a backlog — the last request wins.
  cancelPreviewLoad();

  const int maxWidth = parentWidget->width() * 0.8;
  const int maxHeight = parentWidget->height() * 0.8;

  // Initial open: put the overlay up immediately on a neutral loading box so
  // the click responds instantly. While cycling, whatever is currently on
  // screen (image or video) stays up until the new decode lands — same
  // blank-until-decoded behaviour as the thumb strip.
  if (!isVisible() || m_displayWidget == nullptr) {
    showLoadingPlaceholder();
  }

  auto *watcher = new QFutureWatcher<QImage>(this);
  m_previewLoadWatcher = watcher;
  connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher]() {
    if (m_previewLoadWatcher == watcher) {
      m_previewLoadWatcher = nullptr;
    }
    const QImage img = watcher->result();
    watcher->deleteLater();
    if (!img.isNull()) {
      displayPixmap(QPixmap::fromImage(img));
      return;
    }
    // Decode failed. If the overlay is still on its loading placeholder
    // (initial open, no pixmap ever landed), close it — matching the old
    // synchronous behaviour where a null pixmap meant the overlay never
    // appeared. A failed cycle target keeps the previous image instead.
    if (m_displayWidget == m_artworkLabel && m_artworkLabel->pixmap().isNull()) {
      hideOverlay();
    }
  });
  watcher->setFuture(QtConcurrent::run([artworkPath, maxWidth, maxHeight]() -> QImage {
    // Re-guard on the worker: gallery-cycle entries reach here without the
    // entry points' PDFium-abort gate (Kartend-wquq).
    if (!ExtensionUtils::isDecodableImagePath(artworkPath)) {
      return {};
    }
    QImageReader reader(artworkPath);
    reader.setAutoTransform(true);
    reader.setAllocationLimit(UIConstants::Artwork::MAX_DECODE_MB);
    const QSize originalSize = reader.size();
    if (originalSize.isValid()) {
      // Decode at up to 2x the display box so displayPixmap's final
      // SmoothTransformation fit has headroom for a crisp result, but never
      // above the source size — that would just burn memory on an upscale
      // the display pass redoes anyway.
      QSize decodeSize = originalSize;
      decodeSize.scale(maxWidth * 2, maxHeight * 2, Qt::KeepAspectRatio);
      if (decodeSize.width() < originalSize.width()) {
        reader.setScaledSize(decodeSize);
      }
    }
    return reader.read();
  }));
}

void ArtworkPreviewOverlay::cancelPreviewLoad() {
  // Drop any in-flight decode so its late result can't overwrite a newer
  // request or re-show a closed overlay. The QtConcurrent worker can't be
  // cancelled, but disconnecting + deleting the watcher discards its result.
  if (m_previewLoadWatcher) {
    m_previewLoadWatcher->disconnect(this);
    m_previewLoadWatcher->deleteLater();
    m_previewLoadWatcher = nullptr;
  }
}

void ArtworkPreviewOverlay::showLoadingPlaceholder() {
  QWidget *parentWidget = this->parentWidget();
  if (!parentWidget) {
    return;
  }
  // Stop any video preview that may have been left from a prior request.
  if (m_videoPreview) {
    m_videoPreview->stop();
    m_videoPreview->hide();
  }
  m_artworkLabel->setText(tr("Loading…"));
  // Neutral box while the first decode runs; displayPixmap re-sizes the label
  // to the real image when it lands. Clamped so a tiny parent still fits it.
  m_artworkLabel->setFixedSize(qMin(320, static_cast<int>(parentWidget->width() * 0.8)),
                               qMin(180, static_cast<int>(parentWidget->height() * 0.8)));
  m_artworkLabel->show();
  m_displayWidget = m_artworkLabel;
  presentOverlay();
}

void ArtworkPreviewOverlay::presentOverlay() {
  QWidget *parentWidget = this->parentWidget();
  if (!parentWidget) {
    return;
  }
  // Resize overlay to cover parent
  resize(parentWidget->size());
  centerContent();

  show();
  if (m_layerManager) {
    m_layerManager->bringToFront(this);
  } else {
    raise();
  }
  activateWindow();
  setFocus(Qt::PopupFocusReason);
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

  presentOverlay();
}

void ArtworkPreviewOverlay::displayVideo(const QString &absoluteVideoPath) {
  QWidget *parentWidget = this->parentWidget();
  if (!parentWidget) {
    return;
  }

  // A stale image decode landing after this would paint over the video.
  cancelPreviewLoad();

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

  presentOverlay();
}

void ArtworkPreviewOverlay::hideOverlay() {
  // Discard any pending decode: its delivery calls show() and would re-open
  // the overlay the user just dismissed.
  cancelPreviewLoad();
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
  m_currentArtworkPath.clear();
  // Tear the strip down rather than just hiding it — the caller is
  // expected to repopulate via setGalleryEntries() before the next
  // show, and a stale strip from a prior item would race against the
  // new one if we kept it parented.
  if (m_galleryStrip) {
    m_galleryStrip->deleteLater();
    m_galleryStrip = nullptr;
    m_galleryLayout = nullptr;
    m_galleryScroll = nullptr;
  }
  m_galleryEntries.clear();
  m_galleryIndex = -1;
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

  // When the gallery strip is present, reserve the bottom band for it so
  // the centered preview doesn't overlap. Otherwise treat the full overlay
  // as available vertical space.
  const int stripHeight = m_galleryScroll ? m_galleryScroll->height() : 0;
  const int stripMargin = m_galleryScroll ? 16 : 0;
  const int usableHeight = height() - stripHeight - stripMargin;

  // Center the active display widget (artwork label or video preview).
  int x = (width() - m_displayWidget->width()) / 2;
  int y = (usableHeight - m_displayWidget->height()) / 2;
  if (y < 0) y = 0;
  m_displayWidget->move(x, y);

  // Position close button at top-right of the displayed content.
  int closeX = x + m_displayWidget->width() - m_closeButton->width() / 2;
  int closeY = y - m_closeButton->height() / 2;
  m_closeButton->move(closeX, closeY);

  // Anchor the gallery strip along the bottom edge with 80% of overlay
  // width centered. The strip is scrollable, so even a very long entry
  // list fits in the fixed width.
  if (m_galleryScroll) {
    const int stripWidth = qMax(200, static_cast<int>(width() * 0.8));
    const int stripX = (width() - stripWidth) / 2;
    const int stripY = height() - stripHeight - 12;
    m_galleryScroll->setGeometry(stripX, stripY, stripWidth, stripHeight);
    m_galleryScroll->raise();
  }
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
  // Left/Right cycle through the gallery entries when the overlay was
  // populated with related artwork. No-op when fewer than 2 entries
  // (single-image preview, nothing to cycle through).
  if ((event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) &&
      m_galleryEntries.size() >= 2) {
    const int direction = event->key() == Qt::Key_Left ? -1 : +1;
    const int n = m_galleryEntries.size();
    int next = m_galleryIndex;
    if (next < 0) {
      // No previous index pinned (e.g. overlay opened via legacy
      // showAtPath); start the cycle from the first entry.
      next = direction > 0 ? 0 : n - 1;
    } else {
      next = ((next + direction) % n + n) % n;
    }
    showGalleryEntry(next);
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void ArtworkPreviewOverlay::wheelEvent(QWheelEvent *event) {
  // Mirror the Left/Right key cycling so the wheel advances the artwork
  // gallery instead of falling through to grid-selection scrolling. Wheel
  // up = previous, wheel down = next (matches the visual order of the
  // bottom thumb strip and the natural "scroll to advance" direction).
  if (m_galleryEntries.size() < 2) {
    QWidget::wheelEvent(event);
    return;
  }
  const int delta = event->angleDelta().y();
  if (delta == 0) {
    QWidget::wheelEvent(event);
    return;
  }
  const int direction = delta > 0 ? -1 : +1;
  const int n = m_galleryEntries.size();
  int next = m_galleryIndex;
  if (next < 0) {
    next = direction > 0 ? 0 : n - 1;
  } else {
    next = ((next + direction) % n + n) % n;
  }
  showGalleryEntry(next);
  event->accept();
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

void ArtworkPreviewOverlay::setGalleryEntries(const QList<GalleryEntry> &entries) {
  m_galleryEntries = entries;
  // Snap the cycle pointer to whichever entry matches the artwork
  // path currently being previewed. When the overlay was opened via
  // showArtworkAtPath, that path doubles as the entry's `path` field;
  // showMediaForFile + showArtworkForFile use the artwork found by
  // findArtworkForFile, so the match also covers those paths. Falls
  // back to -1 when no entry path lines up (e.g. the active preview
  // is the flat-root mirror at `{artwork}/{base}.png` but the gallery
  // only enumerates typed copies under subdirs).
  m_galleryIndex = -1;
  if (!m_currentArtworkPath.isEmpty()) {
    for (int i = 0; i < m_galleryEntries.size(); ++i) {
      if (m_galleryEntries[i].path == m_currentArtworkPath) {
        m_galleryIndex = i;
        break;
      }
    }
  }
  rebuildGalleryStrip();
}

void ArtworkPreviewOverlay::rebuildGalleryStrip() {
  // Tear down any previously-built strip — the entry list is small
  // (rarely >30) and rebuild is the simplest way to stay consistent
  // when the caller swaps to a new item.
  if (m_galleryStrip) {
    m_galleryStrip->deleteLater();
    m_galleryStrip = nullptr;
    m_galleryLayout = nullptr;
    m_galleryScroll = nullptr;
  }
  if (m_galleryEntries.isEmpty()) return;

  // Strip lives at the bottom of the overlay; centerContent positions
  // it after the main preview is sized. The QScrollArea wrapper means
  // even a 30-thumb strip stays inside the overlay rather than
  // expanding it off-screen.
  m_galleryScroll = new QScrollArea(this);
  m_galleryScroll->setWidgetResizable(true);
  m_galleryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_galleryScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_galleryScroll->setFrameShape(QFrame::NoFrame);
  m_galleryScroll->setStyleSheet(
      "QScrollArea { background-color: rgba(0, 0, 0, 140); border-radius: 6px; }");

  const int thumbSize = UIConstants::Metadata::GALLERY_THUMB_SIZE;
  const int padding = UIConstants::Metadata::GALLERY_THUMB_PADDING;
  const int iconSize = thumbSize - (padding * 2);
  m_galleryScroll->setFixedHeight(thumbSize + 24);

  m_galleryStrip = new QWidget;
  m_galleryLayout = new QHBoxLayout(m_galleryStrip);
  m_galleryLayout->setContentsMargins(8, 6, 8, 6);
  m_galleryLayout->setSpacing(UIConstants::Metadata::GALLERY_THUMB_SPACING);
  m_galleryLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  for (int i = 0; i < m_galleryEntries.size(); ++i) {
    const GalleryEntry &entry = m_galleryEntries[i];
    auto *btn = new QToolButton(m_galleryStrip);
    btn->setAutoRaise(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedSize(thumbSize, thumbSize);
    btn->setIconSize(QSize(iconSize, iconSize));
    btn->setToolTip(entry.label);
    btn->setAccessibleName(entry.label);
    // Video thumbs get a generic placeholder icon — frame-extraction
    // would duplicate the sidebar's gallery extractor, and the
    // overlay is short-lived enough that the cost isn't worth it.
    if (entry.isVideo) {
      btn->setText(QStringLiteral("▶"));
    } else {
      // Decode-at-size off the GUI thread: a full-res QPixmap(path) per entry
      // spiked RAM by hundreds of MB and froze overlay-open on 4K-source
      // galleries (Kartend-r4m0). Mirror the coverflow strip — decode at 2x the
      // icon edge for crisp downscale, capture only value copies, and parent the
      // watcher to the button so the result is dropped if the strip is rebuilt
      // (deleteLater) before the worker finishes.
      const QString path = entry.path;
      const int decodeEdge = iconSize * 2;
      auto *watcher = new QFutureWatcher<QImage>(btn);
      connect(watcher, &QFutureWatcher<QImage>::finished, btn, [btn, watcher]() {
        const QImage img = watcher->result();
        if (!img.isNull()) {
          btn->setIcon(QIcon(QPixmap::fromImage(img)));
        }
        watcher->deleteLater();
      });
      watcher->setFuture(QtConcurrent::run([path]() -> QImage {
        if (!ExtensionUtils::isDecodableImagePath(path)) {
          return {};
        }
        QImageReader reader(path);
        reader.setAutoTransform(true);
        reader.setAllocationLimit(UIConstants::Artwork::MAX_DECODE_MB);
        const QSize originalSize = reader.size();
        if (originalSize.isValid()) {
          QSize scaled = originalSize;
          scaled.scale(decodeEdge, decodeEdge, Qt::KeepAspectRatio);
          reader.setScaledSize(scaled);
        } else {
          reader.setScaledSize(QSize(decodeEdge, decodeEdge));
        }
        return reader.read();
      }));
    }
    const int captured = i;
    connect(btn, &QToolButton::clicked, this, [this, captured]() { showGalleryEntry(captured); });
    m_galleryLayout->addWidget(btn);
  }

  m_galleryScroll->setWidget(m_galleryStrip);
  m_galleryScroll->show();
  // Re-anchor next paint cycle so the strip sits below the main
  // preview rather than over it.
  centerContent();
}

void ArtworkPreviewOverlay::showGalleryEntry(int index) {
  if (index < 0 || index >= m_galleryEntries.size()) return;
  m_galleryIndex = index;
  const GalleryEntry &entry = m_galleryEntries[index];
  m_currentArtworkPath = entry.path;
  if (entry.isVideo) {
    displayVideo(entry.path);
  } else {
    startPreviewLoad(entry.path);
  }
}
