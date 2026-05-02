#ifndef METADATASIDEBAR_H
#define METADATASIDEBAR_H

#include <QFrame>
#include <QLabel>
#include <QList>
#include <QScrollArea>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include "itemmetadata.h"
#include "ui_metadatasidebar.h"

class ArtworkPreviewOverlay;
class VideoPreviewWidget;
QT_BEGIN_NAMESPACE
class QHBoxLayout;
class QPushButton;
class QTimer;
QT_END_NAMESPACE

class MetadataSidebar : public QWidget {
  Q_OBJECT
public:
  explicit MetadataSidebar(QWidget *parent = nullptr);
  ~MetadataSidebar();
  void setMetadata(const QString &filePath, const QString &itemName,
                   const QString &artworkDirectory = QString(),
                   const QString &videoDirectory = QString());
  /// Renders extended metadata in a Details section. Hides the section
  /// entirely when `metadata.isEmpty()` so empty rows do not clutter the UI.
  void setExtendedMetadata(const ItemMetadataStore::ItemMetadata &metadata);
  /// Sets the resolved manual file path for the current selection. When
  /// non-empty, a "Manual" button appears in the Details section that opens
  /// the file via QDesktopServices::openUrl (xdg-open on Linux). Pass an
  /// empty path to hide the button. Independent from `setExtendedMetadata`
  /// so callers can show a manual button on items with no other metadata.
  void setManualFile(const QString &manualPath);
  /// Renders the media gallery for the current selection (Kartend-un3l +
  /// Kartend-ljey). Each entry carries a display label, absolute file path,
  /// and an `isVideo` flag; videos use an async-extracted frame as the
  /// thumbnail (with a placeholder play icon while extraction runs) and
  /// open the video preview overlay on click. Pass an empty list to hide
  /// the section.
  struct GalleryEntry {
    QString label;
    QString path;
    bool isVideo = false;
  };
  void setArtworkGallery(const QList<GalleryEntry> &entries);
  void clearMetadata();
  void setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy policy);

private:
  void setupUI();
  void updateFileInfo(const QString &filePath);
  void loadArtwork(const QString &baseName, const QString &artworkDirectory);
  void schedulePreviewVideo(const QString &videoPath);
  void showArtworkOnly();
  void ensureDetailsSection();
  void clearDetailsSection();
  void appendDetailRow(const QString &label, const QString &value, bool wrap = false);
  [[nodiscard]] static QString formatFileSize(qint64 bytes);
  [[nodiscard]] static QString formatRuntime(int seconds);
  [[nodiscard]] static QString formatTags(const QString &raw);
  Ui::MetadataSidebar *ui;
  VideoPreviewWidget *m_videoPreview = nullptr;
  QTimer *m_videoStartTimer = nullptr;
  QString m_pendingVideoPath;

  // Dynamically-built "Details" section appended to the content layout.
  // Built lazily on first use so existing layouts (and tests that don't show
  // the sidebar) are unaffected.
  QWidget *m_detailsContainer = nullptr;
  QVBoxLayout *m_detailsLayout = nullptr;
  QLabel *m_detailsTitle = nullptr;

  // Manual button (Kartend-9jdv). Persisted across selection changes so the
  // button doesn't appear in the middle of the Details list — it's always
  // pinned at the top of the section when a manual is available.
  QPushButton *m_manualButton = nullptr;
  QString m_manualPath;
  void ensureManualButton();
  void openCurrentManual();

  // Artwork gallery (Kartend-un3l). Lazy-built on first non-empty
  // setArtworkGallery() call. Sits between the artwork preview pane and the
  // file information section so all visual artwork stays clustered at the
  // top of the sidebar.
  QWidget *m_galleryContainer = nullptr;
  QHBoxLayout *m_galleryLayout = nullptr;
  // Owned-on-demand preview overlay reparented to the top-level window so it
  // can cover the full UI rather than just the narrow sidebar. nullptr until
  // the first thumbnail is clicked.
  ArtworkPreviewOverlay *m_galleryOverlay = nullptr;
  void ensureGallerySection();
  void clearGallerySection();
  void openGalleryPreview(const GalleryEntry &entry);
  /// Builds the small placeholder shown for video tiles while their frame
  /// is being extracted (and as a permanent fallback when extraction
  /// fails). Renders a play triangle on a muted-tile background sized to
  /// `iconSize`.
  [[nodiscard]] QPixmap makeVideoPlaceholder(int iconSize) const;
};

#endif