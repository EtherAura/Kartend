// Displays file metadata, artwork preview, and item details in the sidebar
// panel.
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "extensionutils.h"
#include "metadatasidebar.h"
#include "pathutils.h"
#include "uiconstants.h"
#include "videopreviewwidget.h"
#include "videoutils.h"

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
                                  const QString &artworkDirectory,
                                  const QString &videoDirectory) {
  if (filePath.isEmpty()) {
    clearMetadata();
    return;
  }

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
  const QString videoPath = videoDirectory.isEmpty()
                                ? QString()
                                : VideoUtils::findVideoForFile(filePath, videoDirectory);
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

// Clears all metadata fields and displays default "No item selected" state
void MetadataSidebar::clearMetadata() {
  ui->itemNameValue->setText("No item selected");
  ui->filePathValue->setText("-");
  ui->fileSizeValue->setText("-");
  ui->lastModifiedValue->setText("-");
  ui->fileExtensionValue->setText("-");
  QPixmap emptyPixmap(UIConstants::Metadata::ARTWORK_SIZE, UIConstants::Metadata::ARTWORK_SIZE);
  emptyPixmap.fill(palette().color(QPalette::Mid));
  ui->artworkDisplay->setPixmap(emptyPixmap);
  schedulePreviewVideo(QString());
  showArtworkOnly();
  setManualFile(QString());
  if (m_detailsContainer) {
    clearDetailsSection();
    m_detailsContainer->hide();
  }
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