#ifndef DETAILSPANEGALLERYVIEW_H
#define DETAILSPANEGALLERYVIEW_H

#include "detailspane.h"

#include <functional>

#include <QList>
#include <QObject>
#include <QString>

class ArtworkPreviewOverlay;
class OverlayZOrderRegistry;
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
/// Coupling: the host DetailsPane is used only through its public surface
/// (palette, window, showMainPreviewForEntry). The content column, the
/// preview-tile insertion anchors, and the scroll-idle gate are injected
/// via setHostAnchors / setScrollIdlePredicate during DetailsPane's
/// setupWidgets — no friend access into host state.
class DetailsPaneGalleryView : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(DetailsPaneGalleryView)

public:
  explicit DetailsPaneGalleryView(QObject *parent = nullptr);

  /// Bind to the owning DetailsPane. Idempotent — call once during
  /// DetailsPane's setup.
  void setHost(DetailsPane *host);

  /// Inject the host widgets the gallery section builds against:
  /// @p contentWidget is the vertical content column the section is
  /// inserted into; @p artworkAnchor / @p videoAnchor are the preview
  /// tiles the section anchors below (video preferred when present).
  /// Plain QWidget pointers — the helper only needs layout placement,
  /// not the concrete types. Call once during DetailsPane's setup,
  /// before the first ensureSection() runs (prewarmSection/setEntries).
  void setHostAnchors(QWidget *contentWidget, QWidget *artworkAnchor, QWidget *videoAnchor);

  /// Install the scroll-idle gate consulted before firing a video
  /// thumbnail extraction (same forwarding pattern as
  /// DetailsPane::setScrollIdlePredicate). Never wired / null = always
  /// idle, i.e. extraction fires on the next tick.
  void setScrollIdlePredicate(std::function<bool()> predicate);

  /// Hand the central overlay z-order coordinator down so the lazily-created
  /// preview overlay registers at Layer::ArtworkPreview instead of stacking
  /// by raw raise() (which any registered overlay's later bringToFront() /
  /// restack() would bury). Late-bound like SelectionOverlayManager's
  /// equivalent: safe to call before or after the overlay exists; when never
  /// wired the overlay keeps its raise() fallback.
  void setLayerManager(OverlayZOrderRegistry *manager);

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

  /// Force the gallery's lazy widget construction (container + title row +
  /// QScrollArea + thumb layout + insertion into the parent contentLayout)
  /// to run NOW instead of on the first setEntries() call with non-empty
  /// entries. The first lazy invocation was measured at ~2.5s on a slow
  /// filesystem (Kartend-jxp5) and lands on the user's first-click critical
  /// path; calling this from DetailsPane's constructor moves the cost into
  /// startup where it overlaps other init work and is invisible. The
  /// section is hidden (zero visible widgets) until setEntries populates it,
  /// so prewarming has no UI consequence beyond the up-front cost. Idempotent
  /// — repeated calls are no-ops once the section exists.
  void prewarmSection();

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
  /// Host widgets injected by setHostAnchors — see that setter's doc.
  QWidget *m_contentWidget = nullptr;
  QWidget *m_artworkAnchor = nullptr;
  QWidget *m_videoAnchor = nullptr;
  /// Scroll-idle gate injected by setScrollIdlePredicate.
  std::function<bool()> m_scrollIdle;
  QWidget *m_container = nullptr;
  QHBoxLayout *m_thumbLayout = nullptr;
  QWidget *m_thumbsHost = nullptr;
  QPushButton *m_editButton = nullptr;
  ArtworkPreviewOverlay *m_overlay = nullptr;
  OverlayZOrderRegistry *m_layerManager = nullptr;

  QList<DetailsPane::GalleryEntry> m_entries;
  bool m_editEnabled = false;
};

#endif // DETAILSPANEGALLERYVIEW_H
