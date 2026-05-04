#ifndef METADATASIDEBAR_H
#define METADATASIDEBAR_H

#include <QDateTime>
#include <QFrame>
#include <QLabel>
#include <QList>
#include <QScrollArea>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include "itemmetadata.h"
#include "ui_metadatasidebar.h"
#include "usagestatsstore.h"

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
  /// Renders per-item usage statistics (Kartend-7vi) appended to the Details
  /// section: play count, last played, total time played. Empty rows are
  /// skipped so an item that has never been launched shows nothing.
  void setUsageStats(const UsageStatsStore::ItemUsageStats &stats);
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
  /// Controls whether the gallery section's edit affordance is shown
  /// (Kartend-53vk). Independent of `setArtworkGallery` so the "Edit links…"
  /// button stays available for items with no current artwork — that is the
  /// exact case where the user wants to add a manual link.
  void setArtworkEditEnabled(bool enabled);
  /// Snapshot of collection-level fields rendered when no item is selected
  /// (Kartend-3mn). Empty `name` clears the cached summary, restoring the
  /// legacy "No item selected" placeholder layout.
  struct CollectionSummary {
    QString name;
    QString type;
    qint64 itemCount = -1; // -1 = unknown / not yet computed
    QDateTime lastScanned;
    QString mediaDirectory;
    QString artworkDirectory;
    QString videoDirectory;
    QString manualDirectory;
    QStringList extensions;
    QString parentName;
    [[nodiscard]] bool isValid() const { return !name.trimmed().isEmpty(); }
  };
  /// Caches the collection summary used when no item is selected. The
  /// summary is rendered immediately if the sidebar is currently in the
  /// no-selection state; otherwise it is held until the user deselects.
  void setCollectionSummary(const CollectionSummary &summary);
  void clearMetadata();
  void setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy policy);

signals:
  /// Fired when the user activates the gallery's "Edit links…" button
  /// (Kartend-53vk). The sidebar widget itself has no item context, so the
  /// owning manager handles persistence and refresh.
  void editArtworkRequested();

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
  /// Wraps the thumbnail row so the section title + edit button can stay
  /// visible while the thumbs are hidden (Kartend-53vk).
  QWidget *m_galleryThumbsHost = nullptr;
  QPushButton *m_galleryEditButton = nullptr;
  bool m_galleryEditEnabled = false;
  // Owned-on-demand preview overlay reparented to the top-level window so it
  // can cover the full UI rather than just the narrow sidebar. nullptr until
  // the first thumbnail is clicked.
  ArtworkPreviewOverlay *m_galleryOverlay = nullptr;
  void ensureGallerySection();
  void clearGallerySection();
  void openGalleryPreview(const GalleryEntry &entry);

  // Collection-summary state (Kartend-3mn). The summary is cached on the
  // widget so every existing clearMetadata() call site can fall through to
  // a meaningful no-selection display without each caller re-pushing it.
  // m_hasItemDisplayed tracks whether the sidebar is currently rendering
  // an item — refreshes that arrive mid-selection update the cache silently
  // and apply on next deselect.
  CollectionSummary m_collectionSummary;
  bool m_hasItemDisplayed = false;
  void renderCollectionSummary();
  void applyItemViewVisibility(bool visible);
  [[nodiscard]] static QString formatLastScanned(const QDateTime &lastScanned);
  /// Builds the small placeholder shown for video tiles while their frame
  /// is being extracted (and as a permanent fallback when extraction
  /// fails). Renders a play triangle on a muted-tile background sized to
  /// `iconSize`.
  [[nodiscard]] QPixmap makeVideoPlaceholder(int iconSize) const;
};

#endif