// Displays file metadata, artwork preview, and item details in the sidebar
// panel.
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSize>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "artworkpreviewoverlay.h"
#include "extensionutils.h"
#include "metadatasidebar.h"
#include "pathutils.h"
#include "uiconstants.h"
#include "videopreviewwidget.h"
#include "videothumbnailextractor.h"
#include "videoutils.h"

#include <QPointer>
#include <QPolygon>

// Creates metadata sidebar with scrollable layout for displaying item
// information and artwork
MetadataSidebar::MetadataSidebar(QWidget *parent) : QWidget(parent), ui(new Ui::MetadataSidebar) {
  ui->setupUi(this);
  setAutoFillBackground(true);

  QPalette pal = palette();
  pal.setColor(QPalette::Window, pal.color(QPalette::Window));
  setPalette(pal);

  ui->scrollArea->setAutoFillBackground(true);
  QPalette scrollPalette = ui->scrollArea->palette();
  scrollPalette.setColor(QPalette::Window, palette().color(QPalette::Window));
  ui->scrollArea->setPalette(scrollPalette);

  ui->contentWidget->setAutoFillBackground(true);
  QPalette contentPalette = ui->contentWidget->palette();
  contentPalette.setColor(QPalette::Window, palette().color(QPalette::Window));
  ui->contentWidget->setPalette(contentPalette);

  setFixedWidth(UIConstants::Sidebar::FIXED_WIDTH);

  // Insert a preview video widget into the artwork pane, sized to match the
  // artwork display. Hidden by default; shown only when a preview video is
  // found for the current selection.
  m_videoPreview = new VideoPreviewWidget(this);
  m_videoPreview->setFixedSize(UIConstants::Metadata::ARTWORK_SIZE,
                               UIConstants::Metadata::ARTWORK_SIZE);
  m_videoPreview->hide();
  if (auto *artworkParentLayout =
          qobject_cast<QVBoxLayout *>(ui->artworkDisplay->parentWidget()->layout())) {
    int idx = artworkParentLayout->indexOf(ui->artworkDisplay);
    if (idx >= 0) {
      artworkParentLayout->insertWidget(idx + 1, m_videoPreview);
    } else {
      artworkParentLayout->addWidget(m_videoPreview);
    }
  }

  // Debounce timer: avoid loading a video for every transient selection
  // change while the user is scrolling. Single-shot, restarted on each new
  // selection that has a video.
  m_videoStartTimer = new QTimer(this);
  m_videoStartTimer->setSingleShot(true);
  m_videoStartTimer->setInterval(UIConstants::Sidebar::VIDEO_PREVIEW_DEBOUNCE_MS);
  connect(m_videoStartTimer, &QTimer::timeout, this, [this]() {
    if (m_pendingVideoPath.isEmpty() || !m_videoPreview) {
      return;
    }
    ui->artworkDisplay->hide();
    m_videoPreview->show();
    m_videoPreview->playVideo(m_pendingVideoPath);
  });

  clearMetadata();
}

MetadataSidebar::~MetadataSidebar() {
  delete ui;
}

// Sets metadata fields and loads centered artwork from the configured
// artwork directory or a sibling "artwork" directory if present
void MetadataSidebar::setMetadata(const QString &filePath, const QString &itemName,
                                  const QString &artworkDirectory, const QString &videoDirectory) {
  if (filePath.isEmpty()) {
    clearMetadata();
    return;
  }

  // Coming back from collection-summary mode (Kartend-3mn) — restore the
  // item-view chrome that was hidden while showing collection details.
  m_hasItemDisplayed = true;
  applyItemViewVisibility(true);
  ui->titleLabel->setText(tr("Item Information"));
  ui->itemNameLabel->setText(tr("Name:"));

  ui->itemNameValue->setText(itemName);
  updateFileInfo(filePath);

  QFileInfo fileInfo(filePath);
  const QString baseName = fileInfo.completeBaseName();

  // Default placeholder
  QPixmap defaultPixmap(UIConstants::Metadata::ARTWORK_SIZE, UIConstants::Metadata::ARTWORK_SIZE);
  defaultPixmap.fill(palette().color(QPalette::Mid));
  ui->artworkDisplay->setPixmap(defaultPixmap);

  // Try collection's artwork directory first if provided
  if (!artworkDirectory.isEmpty()) {
    loadArtwork(baseName, artworkDirectory);
  } else {
    // Fallback to sibling "artwork" directory
    const QDir fileDir = fileInfo.dir();
    const QString siblingArtworkDir = fileDir.absolutePath() + "/artwork";
    loadArtwork(baseName, siblingArtworkDir);
  }

  // Resolve and (debounced) start preview video. Always reset the artwork
  // pane back to the artwork display first; the timer will swap to the video
  // widget once the debounce elapses if a video was found.
  showArtworkOnly();
  const QString videoPath =
      videoDirectory.isEmpty() ? QString() : VideoUtils::findVideoForFile(filePath, videoDirectory);
  schedulePreviewVideo(videoPath);

  ui->titleLabel->show();
  ui->artworkLabel->show();
  ui->artworkDisplay->show();
  ui->itemNameLabel->show();
  ui->itemNameValue->show();
  ui->filePathLabel->show();
  ui->filePathValue->show();
  ui->fileSizeLabel->show();
  ui->fileSizeValue->show();
  ui->lastModifiedLabel->show();
  ui->lastModifiedValue->show();
  ui->fileExtensionLabel->show();
  ui->fileExtensionValue->show();
}

// Clears all metadata fields. When a collection summary has been cached
// (Kartend-3mn) the sidebar renders that instead of the legacy "No item
// selected" placeholder; this lets every existing clearMetadata() call site
// pick up the new no-selection display without per-caller plumbing.
void MetadataSidebar::clearMetadata() {
  m_hasItemDisplayed = false;

  // Tear down item-only chrome (artwork preview, video, gallery, details
  // rows, manual button) regardless of which mode we land in.
  schedulePreviewVideo(QString());
  showArtworkOnly();
  setManualFile(QString());
  if (m_detailsContainer) {
    clearDetailsSection();
    m_detailsContainer->hide();
  }
  setArtworkEditEnabled(false);
  setArtworkGallery({});

  if (m_collectionSummary.isValid()) {
    renderCollectionSummary();
    return;
  }

  applyItemViewVisibility(true);
  ui->titleLabel->setText(tr("Item Information"));
  ui->itemNameLabel->setText(tr("Name:"));
  ui->itemNameValue->setText(tr("No item selected"));
  ui->filePathValue->setText("-");
  ui->fileSizeValue->setText("-");
  ui->lastModifiedValue->setText("-");
  ui->fileExtensionValue->setText("-");
  QPixmap emptyPixmap(UIConstants::Metadata::ARTWORK_SIZE, UIConstants::Metadata::ARTWORK_SIZE);
  emptyPixmap.fill(palette().color(QPalette::Mid));
  ui->artworkDisplay->setPixmap(emptyPixmap);
}

void MetadataSidebar::setCollectionSummary(const CollectionSummary &summary) {
  m_collectionSummary = summary;
  // Re-render whenever the sidebar is currently in the no-selection state
  // so first-time application (during applySidebarStateForCollection) and
  // background refreshes (scan completion, settings save) both land. While
  // an item is selected we just update the cache and apply on next clear.
  if (!m_hasItemDisplayed) {
    clearMetadata();
  }
}

void MetadataSidebar::applyItemViewVisibility(bool visible) {
  // Per-item chrome that doesn't apply to collection summaries. Artwork +
  // file-info sections collapse together so the sidebar doesn't leave a
  // bare "Artwork" header above a hidden image.
  ui->artworkLabel->setVisible(visible);
  ui->artworkDisplay->setVisible(visible);
  if (m_videoPreview && !visible) {
    m_videoPreview->hide();
  }
  ui->fileInfoTitle->setVisible(visible);
  ui->filePathLabel->setVisible(visible);
  ui->filePathValue->setVisible(visible);
  ui->fileSizeLabel->setVisible(visible);
  ui->fileSizeValue->setVisible(visible);
  ui->lastModifiedLabel->setVisible(visible);
  ui->lastModifiedValue->setVisible(visible);
  ui->fileExtensionLabel->setVisible(visible);
  ui->fileExtensionValue->setVisible(visible);
}

void MetadataSidebar::renderCollectionSummary() {
  applyItemViewVisibility(false);
  ui->titleLabel->setText(tr("Collection Information"));
  ui->itemNameLabel->setText(tr("Collection:"));
  ui->itemNameValue->setText(m_collectionSummary.name);

  ensureDetailsSection();
  if (!m_detailsContainer) {
    return;
  }
  clearDetailsSection();

  if (!m_collectionSummary.type.trimmed().isEmpty()) {
    appendDetailRow(tr("Type"), m_collectionSummary.type);
  }
  if (m_collectionSummary.itemCount >= 0) {
    appendDetailRow(tr("Items"), QString::number(m_collectionSummary.itemCount));
  }
  appendDetailRow(tr("Last scanned"), formatLastScanned(m_collectionSummary.lastScanned));
  if (!m_collectionSummary.parentName.trimmed().isEmpty()) {
    appendDetailRow(tr("Parent"), m_collectionSummary.parentName);
  }
  appendDetailRow(tr("Media"), m_collectionSummary.mediaDirectory, /*wrap=*/true);
  appendDetailRow(tr("Artwork"), m_collectionSummary.artworkDirectory, /*wrap=*/true);
  appendDetailRow(tr("Video"), m_collectionSummary.videoDirectory, /*wrap=*/true);
  appendDetailRow(tr("Manuals"), m_collectionSummary.manualDirectory, /*wrap=*/true);
  if (!m_collectionSummary.extensions.isEmpty()) {
    appendDetailRow(tr("Extensions"), m_collectionSummary.extensions.join(QStringLiteral(", ")),
                    /*wrap=*/true);
  }

  m_detailsContainer->show();
}

QString MetadataSidebar::formatLastScanned(const QDateTime &lastScanned) {
  if (!lastScanned.isValid()) {
    return tr("never");
  }
  return lastScanned.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm"));
}

// Updates file information fields including size, modification date, and file
// type
void MetadataSidebar::updateFileInfo(const QString &filePath) {
  QFileInfo fileInfo(filePath);

  if (!fileInfo.exists()) {
    ui->filePathValue->setText("File not found");
    ui->fileSizeValue->setText("-");
    ui->lastModifiedValue->setText("-");
    ui->fileExtensionValue->setText("-");
    return;
  }

  QString displayPath =
      PathUtils::truncatePathForDisplay(filePath, UIConstants::Metadata::PATH_TRUNCATE_LENGTH);
  ui->filePathValue->setText(displayPath);
  ui->filePathValue->setToolTip(filePath);

  ui->fileSizeValue->setText(formatFileSize(fileInfo.size()));
  ui->lastModifiedValue->setText(fileInfo.lastModified().toString("yyyy-MM-dd hh:mm:ss"));

  QString extension = fileInfo.suffix().toUpper();
  if (extension.isEmpty()) {
    extension = "Unknown";
  }
  ui->fileExtensionValue->setText(extension + " file");
}

// Formats file size into human-readable string with appropriate units (KB, MB,
// GB)
auto MetadataSidebar::formatFileSize(qint64 bytes) -> QString {
  const qint64 kiloBytes = UIConstants::Metadata::FILE_SIZE_KB;
  const qint64 megaBytes = kiloBytes * UIConstants::Metadata::FILE_SIZE_KB;
  const qint64 gigaBytes = megaBytes * UIConstants::Metadata::FILE_SIZE_KB;

  if (bytes >= gigaBytes) {
    return QString::number(bytes / static_cast<double>(gigaBytes), 'f', 2) + " GB";
  }
  if (bytes >= megaBytes) {
    return QString::number(bytes / static_cast<double>(megaBytes), 'f', 2) + " MB";
  }
  if (bytes >= kiloBytes) {
    return QString::number(bytes / static_cast<double>(kiloBytes), 'f', 2) + " KB";
  }
  return QString::number(bytes) + " bytes";
}

// Load artwork from specified directory
void MetadataSidebar::loadArtwork(const QString &baseName, const QString &artworkDirectory) {
  QDir artworkDir(artworkDirectory);
  if (!artworkDir.exists()) {
    return;
  }

  const QStringList &bases = ExtensionUtils::imageBaseExtensions();
  for (const QString &ext : bases) {
    const QString &lower = ext;
    const QString upper = ext.toUpper();

    QString artworkPath = artworkDir.absoluteFilePath(baseName + "." + lower);
    if (!QFile::exists(artworkPath)) {
      artworkPath = artworkDir.absoluteFilePath(baseName + "." + upper);
    }
    if (QFile::exists(artworkPath)) {
      QPixmap artwork(artworkPath);
      if (!artwork.isNull()) {
        QPixmap scaledArtwork =
            artwork.scaled(UIConstants::Metadata::ARTWORK_SIZE, UIConstants::Metadata::ARTWORK_SIZE,
                           Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPixmap centeredArtwork(UIConstants::Metadata::ARTWORK_SIZE,
                                UIConstants::Metadata::ARTWORK_SIZE);
        centeredArtwork.fill(palette().color(QPalette::Base));

        QPainter painter(&centeredArtwork);
        const int centerX = (UIConstants::Metadata::ARTWORK_SIZE - scaledArtwork.width()) / 2;
        const int centerY = (UIConstants::Metadata::ARTWORK_SIZE - scaledArtwork.height()) / 2;
        painter.drawPixmap(centerX, centerY, scaledArtwork);
        painter.end();

        ui->artworkDisplay->setPixmap(centeredArtwork);
        return; // Found and loaded artwork
      }
    }
  }
}

// Apply horzontal scrolling policy
void MetadataSidebar::setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy policy) {
  if (ui->scrollArea) {
    ui->scrollArea->setHorizontalScrollBarPolicy(policy);
    ui->scrollArea->updateGeometry();
    QApplication::processEvents();
  }
}

// Stops any current preview video and shows the static artwork display
// instead. Called whenever selection changes (before the debounce timer
// resolves) and when metadata is cleared.
void MetadataSidebar::showArtworkOnly() {
  if (m_videoPreview) {
    m_videoPreview->stop();
    m_videoPreview->hide();
  }
  ui->artworkDisplay->show();
}

// Schedule preview video playback after the debounce interval. Passing an
// empty path cancels any pending playback.
void MetadataSidebar::schedulePreviewVideo(const QString &videoPath) {
  m_pendingVideoPath = videoPath;
  if (!m_videoStartTimer) {
    return;
  }
  m_videoStartTimer->stop();
  if (!videoPath.isEmpty()) {
    m_videoStartTimer->start();
  }
}

// Lazily construct the Details section. Appended once to the bottom of the
// content layout; subsequent calls reuse the existing widgets so we do not
// churn the layout on every selection change.
void MetadataSidebar::ensureDetailsSection() {
  if (m_detailsContainer) {
    return;
  }
  auto *contentLayout = qobject_cast<QVBoxLayout *>(ui->contentWidget->layout());
  if (!contentLayout) {
    return;
  }

  m_detailsContainer = new QWidget(ui->contentWidget);
  auto *outer = new QVBoxLayout(m_detailsContainer);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(UIConstants::Metadata::LABEL_SPACING);

  auto *separator = new QFrame(m_detailsContainer);
  separator->setFrameShape(QFrame::HLine);
  separator->setFrameShadow(QFrame::Sunken);
  separator->setStyleSheet("color: palette(mid);");
  outer->addWidget(separator);

  m_detailsTitle = new QLabel(tr("Details"), m_detailsContainer);
  QFont titleFont = m_detailsTitle->font();
  titleFont.setBold(true);
  titleFont.setPointSize(11);
  m_detailsTitle->setFont(titleFont);
  m_detailsTitle->setStyleSheet("color: palette(highlight); padding: 4px 0px;");
  outer->addWidget(m_detailsTitle);

  // Manual button is owned by the outer Details layout (above the per-row
  // sub-layout) so clearDetailsSection() — called on every selection change
  // before rows are rebuilt — does not destroy and recreate it. Visibility
  // is driven by setManualFile().
  m_manualButton = new QPushButton(tr("Open Manual"), m_detailsContainer);
  m_manualButton->setCursor(Qt::PointingHandCursor);
  m_manualButton->hide();
  connect(m_manualButton, &QPushButton::clicked, this, &MetadataSidebar::openCurrentManual);
  outer->addWidget(m_manualButton);

  m_detailsLayout = new QVBoxLayout();
  m_detailsLayout->setSpacing(UIConstants::Metadata::LABEL_SPACING);
  outer->addLayout(m_detailsLayout);

  contentLayout->addWidget(m_detailsContainer);
  m_detailsContainer->hide();
}

void MetadataSidebar::clearDetailsSection() {
  if (!m_detailsLayout) {
    return;
  }
  while (QLayoutItem *child = m_detailsLayout->takeAt(0)) {
    if (QWidget *w = child->widget()) {
      w->deleteLater();
    }
    delete child;
  }
}

void MetadataSidebar::appendDetailRow(const QString &label, const QString &value, bool wrap) {
  if (!m_detailsLayout || value.trimmed().isEmpty()) {
    return;
  }
  auto *labelWidget = new QLabel(label + ":", m_detailsContainer);
  QFont labelFont = labelWidget->font();
  labelFont.setBold(true);
  labelWidget->setFont(labelFont);
  labelWidget->setStyleSheet("color: palette(windowtext); padding: 2px 0px;");

  auto *valueWidget = new QLabel(value, m_detailsContainer);
  valueWidget->setStyleSheet("color: palette(windowtext); padding: 2px 0px 8px 12px;");
  valueWidget->setWordWrap(wrap);
  valueWidget->setTextInteractionFlags(Qt::TextSelectableByMouse);

  m_detailsLayout->addWidget(labelWidget);
  m_detailsLayout->addWidget(valueWidget);
}

void MetadataSidebar::setExtendedMetadata(const ItemMetadataStore::ItemMetadata &metadata) {
  if (metadata.isEmpty()) {
    if (m_detailsContainer) {
      clearDetailsSection();
      // Keep the container visible only if a manual button is active;
      // otherwise hide it so the bottom of the sidebar stays clean.
      if (!m_manualButton || !m_manualButton->isVisible()) {
        m_detailsContainer->hide();
      }
    }
    return;
  }

  ensureDetailsSection();
  if (!m_detailsContainer) {
    return;
  }
  clearDetailsSection();

  // If a scraped title is present, override the file-derived itemName so the
  // user sees the canonical title rather than the rom file stem.
  if (!metadata.title.isEmpty()) {
    ui->itemNameValue->setText(metadata.title);
  }

  appendDetailRow(tr("Description"), metadata.description, /*wrap=*/true);
  appendDetailRow(tr("Genre"), metadata.genre);
  appendDetailRow(tr("Developer"), metadata.developer);
  appendDetailRow(tr("Publisher"), metadata.publisher);
  appendDetailRow(tr("Release date"), metadata.releaseDate);
  appendDetailRow(tr("Rating"), metadata.contentRating);
  appendDetailRow(tr("Players"), metadata.players);
  appendDetailRow(tr("Runtime"), formatRuntime(metadata.runtimeSeconds));
  appendDetailRow(tr("Tags"), formatTags(metadata.tags), /*wrap=*/true);

  // User-defined custom fields (Kartend-hpln). Rendered after the structured
  // fields so they appear as a contiguous block at the bottom of Details.
  // parseCustomFields() returns rows in alphabetical key order for stable
  // display regardless of edit history.
  const auto customFields = ItemMetadataStore::parseCustomFields(metadata.customFields);
  for (const auto &pair : customFields) {
    appendDetailRow(pair.first, pair.second, /*wrap=*/true);
  }

  m_detailsContainer->show();
}

void MetadataSidebar::setUsageStats(const UsageStatsStore::ItemUsageStats &stats) {
  // Tracking columns default to zero/empty for items that have never been
  // launched; treat them as "no rows to add" so the Details section stays
  // hidden on bare items.
  if (stats.isEmpty()) {
    return;
  }
  // The Details section may already be hidden if extended metadata was empty
  // — reveal it for usage rows alone so first-launched items still surface
  // play_count without scraper data.
  ensureDetailsSection();
  if (!m_detailsContainer) {
    return;
  }
  if (stats.playCount > 0) {
    appendDetailRow(tr("Play count"), QString::number(stats.playCount));
  }
  if (!stats.lastPlayed.isEmpty()) {
    appendDetailRow(tr("Last played"), UsageStatsStore::formatTimestamp(stats.lastPlayed));
  }
  if (stats.totalPlaySeconds > 0) {
    appendDetailRow(tr("Time played"), UsageStatsStore::formatDuration(stats.totalPlaySeconds));
  }
  m_detailsContainer->show();
}

QString MetadataSidebar::formatRuntime(int seconds) {
  if (seconds < 0) {
    return {};
  }
  const int hours = seconds / 3600;
  const int minutes = (seconds % 3600) / 60;
  const int secs = seconds % 60;
  if (hours > 0) {
    return QStringLiteral("%1h %2m").arg(hours).arg(minutes, 2, 10, QChar('0'));
  }
  if (minutes > 0) {
    return QStringLiteral("%1m %2s").arg(minutes).arg(secs, 2, 10, QChar('0'));
  }
  return QStringLiteral("%1s").arg(secs);
}

void MetadataSidebar::ensureManualButton() {
  // Manual button lives inside the Details container's outer layout. Build
  // the container on-demand so an item with only a manual (no extended
  // metadata) still gets a visible button.
  ensureDetailsSection();
}

void MetadataSidebar::setManualFile(const QString &manualPath) {
  m_manualPath = manualPath;
  const bool hasManual = !manualPath.isEmpty();
  if (hasManual) {
    ensureManualButton();
  }
  if (!m_manualButton) {
    return;
  }
  m_manualButton->setVisible(hasManual);
  if (hasManual) {
    m_manualButton->setToolTip(manualPath);
    if (m_detailsContainer) {
      m_detailsContainer->show();
    }
  } else {
    m_manualButton->setToolTip(QString());
    // Only hide the container when there are also no detail rows; a
    // populated row layout means setExtendedMetadata wants it visible.
    if (m_detailsContainer && m_detailsLayout && m_detailsLayout->count() == 0) {
      m_detailsContainer->hide();
    }
  }
}

void MetadataSidebar::openCurrentManual() {
  if (m_manualPath.isEmpty()) {
    return;
  }
  // QDesktopServices::openUrl wraps xdg-open on Linux / open on macOS /
  // ShellExecute on Windows, so the user's default handler for the file
  // type takes over (Okular for PDF, web browser for HTML, etc.).
  QDesktopServices::openUrl(QUrl::fromLocalFile(m_manualPath));
}

void MetadataSidebar::ensureGallerySection() {
  if (m_galleryContainer) {
    return;
  }
  auto *contentLayout = qobject_cast<QVBoxLayout *>(ui->contentWidget->layout());
  if (!contentLayout) {
    return;
  }

  m_galleryContainer = new QWidget(ui->contentWidget);
  auto *outer = new QVBoxLayout(m_galleryContainer);
  outer->setContentsMargins(0, UIConstants::Metadata::LABEL_SPACING, 0,
                            UIConstants::Metadata::LABEL_SPACING);
  outer->setSpacing(UIConstants::Metadata::LABEL_SPACING);

  // Title row pairs the section heading with the per-item "Edit links…"
  // button (Kartend-53vk). Keeping them on one row saves vertical space and
  // makes the affordance discoverable next to the thumbnails it controls.
  auto *titleRow = new QHBoxLayout();
  titleRow->setContentsMargins(0, 0, 0, 0);
  titleRow->setSpacing(UIConstants::Metadata::LABEL_SPACING);

  auto *title = new QLabel(tr("Media gallery"), m_galleryContainer);
  QFont titleFont = title->font();
  titleFont.setBold(true);
  title->setFont(titleFont);
  title->setStyleSheet("color: palette(windowtext); padding: 2px 0px;");
  titleRow->addWidget(title);
  titleRow->addStretch(1);

  m_galleryEditButton = new QPushButton(tr("Edit links…"), m_galleryContainer);
  m_galleryEditButton->setCursor(Qt::PointingHandCursor);
  m_galleryEditButton->setToolTip(
      tr("Pick override files for any artwork type (standard or custom)."));
  m_galleryEditButton->setVisible(m_galleryEditEnabled);
  connect(m_galleryEditButton, &QPushButton::clicked, this, &MetadataSidebar::editArtworkRequested);
  titleRow->addWidget(m_galleryEditButton);
  outer->addLayout(titleRow);

  // Horizontal layout wrapped in a plain widget; if more than ~4 thumbs are
  // present they wrap onto the next row by virtue of QLayout's setAlignment.
  // A QScrollArea was considered but adds vertical chrome that fights the
  // sidebar's own scroll area.
  m_galleryThumbsHost = new QWidget(m_galleryContainer);
  m_galleryLayout = new QHBoxLayout(m_galleryThumbsHost);
  m_galleryLayout->setContentsMargins(0, 0, 0, 0);
  m_galleryLayout->setSpacing(UIConstants::Metadata::GALLERY_THUMB_SPACING);
  m_galleryLayout->setAlignment(Qt::AlignLeft);
  outer->addWidget(m_galleryThumbsHost);

  // Insert just below the artwork preview pane (and the dynamically-added
  // video preview, if any) so all visual artwork stays clustered. Falling
  // back to "append" keeps the section visible if the layout shape changes.
  int insertIndex = -1;
  if (auto *artworkParentLayout =
          qobject_cast<QVBoxLayout *>(ui->artworkDisplay->parentWidget()->layout())) {
    if (artworkParentLayout == contentLayout) {
      const int videoIdx = m_videoPreview ? contentLayout->indexOf(m_videoPreview) : -1;
      const int artIdx = contentLayout->indexOf(ui->artworkDisplay);
      const int anchor = videoIdx >= 0 ? videoIdx : artIdx;
      if (anchor >= 0) {
        insertIndex = anchor + 1;
      }
    }
  }
  if (insertIndex >= 0) {
    contentLayout->insertWidget(insertIndex, m_galleryContainer);
  } else {
    contentLayout->addWidget(m_galleryContainer);
  }
  m_galleryContainer->hide();
}

void MetadataSidebar::clearGallerySection() {
  if (!m_galleryLayout) {
    return;
  }
  while (QLayoutItem *child = m_galleryLayout->takeAt(0)) {
    if (QWidget *w = child->widget()) {
      w->deleteLater();
    }
    delete child;
  }
}

void MetadataSidebar::setArtworkGallery(const QList<GalleryEntry> &entries) {
  if (entries.isEmpty()) {
    if (m_galleryContainer) {
      clearGallerySection();
      if (m_galleryThumbsHost) {
        m_galleryThumbsHost->hide();
      }
      // Keep the section visible when the user can still edit links so
      // the "Edit links…" button stays available for items with no current
      // artwork — that's the exact case where adding a manual link matters
      // most. When edit is disabled we fall back to the legacy "hide
      // section entirely" behaviour.
      if (m_galleryEditEnabled) {
        m_galleryContainer->show();
      } else {
        m_galleryContainer->hide();
      }
    }
    return;
  }

  ensureGallerySection();
  if (!m_galleryContainer || !m_galleryLayout) {
    return;
  }
  clearGallerySection();

  const int thumbSize = UIConstants::Metadata::GALLERY_THUMB_SIZE;
  const int padding = UIConstants::Metadata::GALLERY_THUMB_PADDING;
  const int iconSize = thumbSize - (padding * 2);
  for (const GalleryEntry &entry : entries) {
    QPixmap pixmap;
    if (entry.isVideo) {
      // Prefer a cached extracted frame; otherwise show a placeholder and
      // request async extraction. Cached *null* pixmaps mean a prior
      // extraction failed — keep the placeholder rather than thrashing.
      auto *extractor = VideoThumbnailExtractor::instance();
      if (extractor->hasCacheEntry(entry.path)) {
        pixmap = extractor->cached(entry.path);
      }
      if (pixmap.isNull()) {
        pixmap = makeVideoPlaceholder(iconSize);
      }
    } else {
      pixmap = QPixmap(entry.path);
      if (pixmap.isNull()) {
        // Skip rows whose file vanished between load and render. Don't add
        // a broken-image placeholder — the user gets a tighter gallery and
        // can still launch the file from the main viewport.
        continue;
      }
    }

    auto *button = new QToolButton(m_galleryContainer);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(thumbSize, thumbSize);
    button->setIconSize(QSize(iconSize, iconSize));
    button->setIcon(QIcon(pixmap));
    button->setToolTip(entry.label);
    button->setAccessibleName(entry.label);

    // Capture entry by value so the click handler keeps working after the
    // caller's list goes out of scope.
    const GalleryEntry capturedEntry = entry;
    connect(button, &QToolButton::clicked, this,
            [this, capturedEntry]() { openGalleryPreview(capturedEntry); });

    if (entry.isVideo) {
      // Request async frame extraction. Receiver is the button itself so
      // the connection auto-disconnects when the button is destroyed on
      // the next gallery rebuild — no manual cleanup needed.
      const QString videoPath = entry.path;
      const int targetIconSize = iconSize;
      connect(VideoThumbnailExtractor::instance(), &VideoThumbnailExtractor::frameReady, button,
              [button, videoPath, targetIconSize](const QString &p, const QPixmap &pix) {
                if (p != videoPath || pix.isNull()) {
                  return;
                }
                const QPixmap scaled = pix.scaled(targetIconSize, targetIconSize,
                                                  Qt::KeepAspectRatio, Qt::SmoothTransformation);
                button->setIcon(QIcon(scaled));
              });
      VideoThumbnailExtractor::instance()->requestFrame(videoPath);
    }

    m_galleryLayout->addWidget(button);
  }

  // If every entry failed to load we end up with an empty gallery. When
  // the user can edit links we keep the section visible (so the affordance
  // remains reachable); otherwise hide it to avoid leaving a bare title.
  if (m_galleryLayout->count() == 0) {
    if (m_galleryThumbsHost) {
      m_galleryThumbsHost->hide();
    }
    if (m_galleryEditEnabled) {
      m_galleryContainer->show();
    } else {
      m_galleryContainer->hide();
    }
    return;
  }
  if (m_galleryThumbsHost) {
    m_galleryThumbsHost->show();
  }
  m_galleryContainer->show();
}

void MetadataSidebar::setArtworkEditEnabled(bool enabled) {
  m_galleryEditEnabled = enabled;
  if (enabled) {
    ensureGallerySection();
  }
  if (m_galleryEditButton) {
    m_galleryEditButton->setVisible(enabled);
  }
  // When the section was previously hidden (e.g. an item with no artwork
  // and edit disabled), turning edit back on should reveal at least the
  // title + button so the user has somewhere to click. Conversely, when we
  // disable editing on a section that has no thumbnails, hide it again.
  if (m_galleryContainer) {
    const bool hasThumbs = m_galleryLayout && m_galleryLayout->count() > 0;
    if (enabled || hasThumbs) {
      m_galleryContainer->show();
    } else {
      m_galleryContainer->hide();
    }
  }
}

QPixmap MetadataSidebar::makeVideoPlaceholder(int iconSize) const {
  // Simple play triangle on a muted-tile background — visible at thumb
  // size without needing a separate image asset. Not themeable beyond
  // palette colors, which is fine for an initial-extraction placeholder.
  if (iconSize <= 0) {
    return {};
  }
  QPixmap pix(iconSize, iconSize);
  pix.fill(palette().color(QPalette::Mid));
  QPainter painter(&pix);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setBrush(palette().color(QPalette::Window));
  painter.setPen(Qt::NoPen);
  const int margin = iconSize / 4;
  QPolygon triangle;
  triangle << QPoint(margin, margin) << QPoint(margin, iconSize - margin)
           << QPoint(iconSize - margin, iconSize / 2);
  painter.drawPolygon(triangle);
  return pix;
}

void MetadataSidebar::openGalleryPreview(const GalleryEntry &entry) {
  if (entry.path.isEmpty()) {
    return;
  }
  if (!m_galleryOverlay) {
    // Parent to the top-level window so the overlay can cover the full UI
    // (the sidebar itself is a narrow strip). window() may be the sidebar
    // itself in unparented test scenarios; fall back to `this` so the
    // overlay still has a parent.
    QWidget *overlayParent = window();
    if (!overlayParent || overlayParent == this) {
      overlayParent = this;
    }
    m_galleryOverlay = new ArtworkPreviewOverlay(overlayParent);
  }
  if (entry.isVideo) {
    m_galleryOverlay->showVideoAtPath(entry.path);
  } else {
    m_galleryOverlay->showArtworkAtPath(entry.path);
  }
}

QString MetadataSidebar::formatTags(const QString &raw) {
  // Accept either a JSON array string or a comma-separated list. We do not
  // pull in QJsonDocument here to keep this widget lightweight; the Details
  // section just renders whatever the source provides with light cleanup.
  QString trimmed = raw.trimmed();
  if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
    trimmed.chop(1);
    trimmed.remove(0, 1);
    trimmed.replace('"', QString());
  }
  QStringList parts = trimmed.split(',', Qt::SkipEmptyParts);
  for (QString &p : parts) {
    p = p.trimmed();
  }
  parts.removeAll(QString());
  return parts.join(", ");
}