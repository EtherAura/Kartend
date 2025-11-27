// Displays file metadata, artwork preview, and item details in the sidebar panel.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QPixmap>

#include "extensionutils.h"
#include "metadatasidebar.h"
#include "pathutils.h"
#include "uiconstants.h"

// Creates metadata sidebar with scrollable layout for displaying item
// information and artwork
metadataSidebar::metadataSidebar(QWidget *parent)
    : QWidget(parent), ui(new Ui::metadataSidebar) {
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

  setFixedWidth(UIConstants::FIXED_SIDEBAR_WIDTH);
  clearMetadata();
}

metadataSidebar::~metadataSidebar() { delete ui; }

// Sets metadata fields and loads centered artwork from a sibling "artwork"
// directory if present
void metadataSidebar::setmetadata(const QString &filePath,
                                  const QString &itemName) {
  if (filePath.isEmpty()) {
    clearMetadata();
    return;
  }

  ui->itemNameValue->setText(itemName);
  updateFileInfo(filePath);

  QFileInfo fileInfo(filePath);

  QPixmap defaultPixmap(UIConstants::METADATA_ARTWORK_SIZE,
                        UIConstants::METADATA_ARTWORK_SIZE);
  defaultPixmap.fill(palette().color(QPalette::Mid));
  ui->artworkDisplay->setPixmap(defaultPixmap);

  const QString baseName = fileInfo.completeBaseName();
  const QDir fileDir = fileInfo.dir();
  const QDir artworkDir(fileDir.absolutePath() + "/artwork");

  if (artworkDir.exists()) {
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
              artwork.scaled(UIConstants::METADATA_ARTWORK_SIZE,
                             UIConstants::METADATA_ARTWORK_SIZE,
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
          QPixmap centeredArtwork(UIConstants::METADATA_ARTWORK_SIZE,
                                  UIConstants::METADATA_ARTWORK_SIZE);
          centeredArtwork.fill(palette().color(QPalette::Base));

          QPainter painter(&centeredArtwork);
          const int centerX =
              (UIConstants::METADATA_ARTWORK_SIZE - scaledArtwork.width()) / 2;
          const int centerY =
              (UIConstants::METADATA_ARTWORK_SIZE - scaledArtwork.height()) / 2;
          painter.drawPixmap(centerX, centerY, scaledArtwork);
          painter.end();

          ui->artworkDisplay->setPixmap(centeredArtwork);
          break;
        }
      }
    }
  }

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
void metadataSidebar::clearMetadata() {
  ui->itemNameValue->setText("No item selected");
  ui->filePathValue->setText("-");
  ui->fileSizeValue->setText("-");
  ui->lastModifiedValue->setText("-");
  ui->fileExtensionValue->setText("-");
  QPixmap emptyPixmap(UIConstants::METADATA_ARTWORK_SIZE,
                      UIConstants::METADATA_ARTWORK_SIZE);
  emptyPixmap.fill(palette().color(QPalette::Mid));
  ui->artworkDisplay->setPixmap(emptyPixmap);
}

// Updates file information fields including size, modification date, and file
// type
void metadataSidebar::updateFileInfo(const QString &filePath) {
  QFileInfo fileInfo(filePath);

  if (!fileInfo.exists()) {
    ui->filePathValue->setText("File not found");
    ui->fileSizeValue->setText("-");
    ui->lastModifiedValue->setText("-");
    ui->fileExtensionValue->setText("-");
    return;
  }

  QString displayPath = PathUtils::truncatePathForDisplay(
      filePath, UIConstants::PATH_TRUNCATE_LENGTH);
  ui->filePathValue->setText(displayPath);
  ui->filePathValue->setToolTip(filePath);

  ui->fileSizeValue->setText(formatFileSize(fileInfo.size()));
  ui->lastModifiedValue->setText(
      fileInfo.lastModified().toString("yyyy-MM-dd hh:mm:ss"));

  QString extension = fileInfo.suffix().toUpper();
  if (extension.isEmpty()) {
    extension = "Unknown";
  }
  ui->fileExtensionValue->setText(extension + " file");
}

// Formats file size into human-readable string with appropriate units (KB, MB,
// GB)
auto metadataSidebar::formatFileSize(qint64 bytes) -> QString {
  const qint64 kiloBytes = UIConstants::FILE_SIZE_KB;
  const qint64 megaBytes = kiloBytes * UIConstants::FILE_SIZE_KB;
  const qint64 gigaBytes = megaBytes * UIConstants::FILE_SIZE_KB;

  if (bytes >= gigaBytes) {
    return QString::number(bytes / static_cast<double>(gigaBytes), 'f', 2) +
           " GB";
  }
  if (bytes >= megaBytes) {
    return QString::number(bytes / static_cast<double>(megaBytes), 'f', 2) +
           " MB";
  }
  if (bytes >= kiloBytes) {
    return QString::number(bytes / static_cast<double>(kiloBytes), 'f', 2) +
           " KB";
  }
  return QString::number(bytes) + " bytes";
}

// Apply horzontal scrolling policy
void metadataSidebar::setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy policy) {
  if (ui->scrollArea != nullptr) {
    ui->scrollArea->setHorizontalScrollBarPolicy(policy);
    ui->scrollArea->updateGeometry();
    QApplication::processEvents();
  }
}