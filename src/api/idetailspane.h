#ifndef IDETAILSPANE_H
#define IDETAILSPANE_H

#include <QList>
#include <QString>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

/// One artwork-or-video entry exposed by the details pane's gallery row.
/// Defined here (rather than nested inside DetailsPane) so lower-layer
/// modules can mirror the pane's current gallery into an overlay without
/// pulling in the concrete widget header.
struct DetailsPaneGalleryEntry {
  QString label;
  QString path;
  bool isVideo = false;
};

/**
 * @brief Neutral role interface to the metadata details pane widget.
 *
 * Lets lower-layer module siblings (modules/input navigation/selection/
 * scroll, modules/input/event/wheeleventhandler) reach the handful of pane
 * operations they need without #including the concrete
 * src/ui/widgets/panes/detailspane.h — which would pull a module->ui edge
 * and break the layered DAG.
 *
 * Plain abstract class, not a QObject: DetailsPane already derives QWidget
 * (its QObject base), so it picks this up as a second, non-QObject base.
 * Cross-cast with dynamic_cast, not qobject_cast.
 *
 * Add a method here only when a lower-layer module genuinely needs to call
 * it on the pane. The DetailsPaneManager (the owning sibling, in
 * modules/media/) keeps using the concrete DetailsPane — it builds and
 * lays the widget out and legitimately needs its full Qt surface.
 */
class IDetailsPane {
public:
  virtual ~IDetailsPane() = default;

  /// Drop the currently-displayed item's metadata and return the pane to
  /// its "no selection" placeholder. Called when the focused item changes
  /// or a scroll invalidates the cached selection.
  virtual void clearMetadata() = 0;

  /// Snapshot of the pane's current artwork+video gallery. The interaction
  /// module mirrors this into the artwork-preview overlay's thumb strip so
  /// expand-mode and middle-click peek cycle through the same entries as
  /// the sidebar.
  [[nodiscard]] virtual QList<DetailsPaneGalleryEntry> currentGalleryEntries() const = 0;

  /// QWidget cross-cast for the geometry / visibility queries lower
  /// modules use without coupling to the concrete DetailsPane class
  /// (e.g. wheeleventhandler asks `pane->asWidget()->isVisible()` and
  /// `pane->asWidget()->rect().contains(...)`). DetailsPane returns
  /// `this`.
  /// Open one artwork path in the fullscreen preview overlay (user
  /// request 2026-08-18). Gamepad confirm on a ringed gallery tile uses
  /// this to EXPAND the art, on top of the tile's own click which swaps
  /// the pane's main preview.
  virtual void openArtworkExpanded(const QString &path, bool isVideo) = 0;

  /// Step the pane's MAIN artwork preview one entry in @p direction,
  /// stopping at the ends. Called when the wheel turns over the gallery
  /// strip (user request 2026-08-18). Returns false when there is no
  /// gallery to step, so the caller can fall back to its normal handling.
  virtual bool cycleGalleryPreviewForWheel(int direction) = 0;

  [[nodiscard]] virtual QWidget *asWidget() = 0;
  [[nodiscard]] virtual const QWidget *asWidget() const = 0;
};

#endif // IDETAILSPANE_H
