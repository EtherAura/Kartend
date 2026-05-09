#ifndef DETAILSPANEGALLERYVIEW_H
#define DETAILSPANEGALLERYVIEW_H

#include "detailspane.h"

#include <QList>
#include <QObject>
#include <QString>

class ArtworkPreviewOverlay;
class QHBoxLayout;
class QPushButton;
class QWidget;

/// Owns the per-item media gallery row that lives below the artwork preview
/// pane in the vertical (Left/Right dock) layout: section construction +
/// teardown, populate from a QList<DetailsPane::GalleryEntry>, edit-button
/// affordance, video-placeholder rendering, and the click-to-preview
/// overlay.
///
/// The horizontal-dock view keeps its own thumb strip with separate widget
/// state — its rebuild reads back the cached entries via this helper's
/// entries() accessor and keeps using its own pixmap cache. Two distinct
/// QWidget hierarchies, one shared entry list.
///
/// Coupling: takes the host DetailsPane via setHost so the helper can pull
/// the artwork-preview anchor (for ordered insertion into the content
/// layout) and read the shared primary-artwork path that synthesizes the
/// "Artwork" thumb. Click-overlay parents to the host's top-level window.
class DetailsPaneGalleryView : public QObject {
  Q_OBJECT

public:
  explicit DetailsPaneGalleryView(QObject *parent = nullptr);

  /// Bind to the owning DetailsPane. Idempotent — call once during
  /// DetailsPane's setup.
  void setHost(DetailsPane *host);

  /// Replace the gallery contents. Synthesizes a primary-artwork thumb when
  /// @p primaryArtworkPath is non-empty and not already represented. Pass
  /// the active tab so item-only chrome stays hidden on File / Collection
  /// views regardless of contents.
  void setEntries(const QList<DetailsPane::GalleryEntry> &entries,
                  const QString &primaryArtworkPath, DetailsPaneTab activeTab);

  /// Toggle the per-item edit affordance. When the section was previously
  /// hidden (e.g. an item with no artwork and edit disabled), enabling
  /// reveals at least the title + button so the user has somewhere to
  /// click. Pass the active tab so the section stays hidden on
  /// non-Item tabs.
  void setEditEnabled(bool enabled, DetailsPaneTab activeTab);

  /// React to a tab switch — rerun the show/hide policy without rebuilding
  /// the thumb widgets. Cheap; safe to call from applyTabVisibility().
  void applyTabState(DetailsPaneTab activeTab);

  /// Hide the gallery container unconditionally — used by tab switches that
  /// want to suppress stale thumbs until a follow-up setEntries() repopulate.
  void hideSection();

  /// Resize every existing thumb button to match the supplied size. Cheap
  /// to call after a dock-orientation change or sidebar resize; uses
  /// QToolButton::setIconSize internally so the pixmap rescales without a
  /// full rebuild.
  void applyThumbSize(int thumbSize);

  /// Last-pushed entry list. Used by the horizontal view's rebuild path so
  /// callers don't have to re-supply the data on every dock-orientation
  /// change.
  [[nodiscard]] const QList<DetailsPane::GalleryEntry> &entries() const { return m_entries; }
  [[nodiscard]] bool isEditEnabled() const { return m_editEnabled; }

  /// Open the gallery preview overlay for @p entry. Lazy-creates the
  /// overlay parented to the host's top-level window so it covers the full
  /// UI rather than the narrow sidebar.
  void openPreview(const DetailsPane::GalleryEntry &entry);

  /// Build a play-triangle placeholder for an in-flight video thumbnail
  /// extraction. Used by both the vertical and horizontal galleries.
  [[nodiscard]] QPixmap makeVideoPlaceholder(int iconSize) const;

signals:
  /// Forwarded from the Edit button click — DetailsPane re-emits as its own
  /// editArtworkRequested signal.
  void editRequested();
  /// Forwarded from the preview overlay's visibility transitions.
  void overlayVisibilityChanged(bool visible);

private:
  void ensureSection();
  void clearThumbs();
  void rebuildThumbs(DetailsPaneTab activeTab);
  void applyVisibility(DetailsPaneTab activeTab);

  DetailsPane *m_host = nullptr;
  QWidget *m_container = nullptr;
  QHBoxLayout *m_thumbLayout = nullptr;
  QWidget *m_thumbsHost = nullptr;
  QPushButton *m_editButton = nullptr;
  ArtworkPreviewOverlay *m_overlay = nullptr;

  QList<DetailsPane::GalleryEntry> m_entries;
  bool m_editEnabled = false;
};

#endif // DETAILSPANEGALLERYVIEW_H
