#ifndef OVERLAYZORDERREGISTRY_H
#define OVERLAYZORDERREGISTRY_H

#include <QList>
#include <QObject>
#include <QPointer>

class QWidget;

/**
 * @brief Centralized z-order coordinator for the application's overlay
 *        widgets.
 *
 * Replaces the prior pattern where each overlay called QWidget::raise() on
 * itself inside its show*() entry points. With ~8 independently-managed
 * overlays sharing the same top-level parent, the visible-on-top overlay
 * was order-of-creation dependent when two showed at once — a bug class
 * surfaced by graphify's betweenness analysis of raise() (centrality 0.035
 * across 9 communities).
 *
 * Usage:
 *   1. MainWindow constructs one OverlayZOrderRegistry, parented to itself.
 *   2. After every overlay widget is constructed, MainWindow registers it
 *      with the manager pointer + a documented Layer enum value.
 *   3. Each overlay class accepts a setLayerManager() back-pointer; its
 *      show*() entry point calls m_layerManager->bringToFront(this) instead
 *      of raise(). Unregistered or null cases fall back to raise() — safe
 *      during construction-time ordering edge cases.
 *
 * The Layer enum is ordered low → high; lower values render below higher.
 * Calling bringToFront() re-applies raise() across every registered
 * overlay in order from lowest to highest layer, so a single bringToFront
 * deterministically restacks the entire overlay surface.
 */
class OverlayZOrderRegistry : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(OverlayZOrderRegistry)
public:
  /// Z-order from bottom-most (Backdrop) to top-most (Splash). Adding a
  /// new layer requires picking the right slot relative to its neighbours.
  /// Keep the enum stable — registered overlays bind to specific values.
  enum class Layer : int {
    /// Backdrop blur / vignette layer painted under the items grid.
    Backdrop = 0,
    /// SelectionOverlayManager's animated glow that follows the active item.
    SelectionHighlight = 100,
    /// Modal search-loading spinner that overlays the items grid while a
    /// search filter is being applied.
    SearchLoading = 200,
    /// Top-level loading overlay for collection / scan progress.
    Loading = 300,
    /// Bottom-corner now-playing card.
    NowPlaying = 400,
    /// Startup video overlay (boot-time fullscreen video, dismissible).
    StartupVideo = 500,
    /// Fullscreen detail-page overlay (collection details, item details).
    DetailPage = 600,
    /// Fullscreen artwork preview (image / video preview).
    ArtworkPreview = 700,
    /// Transient text-zoom HUD shown while Ctrl+= / Ctrl+- is held.
    TextZoomHud = 800,
    /// Splash overlay shown on app boot / window-focus return. Top-most
    /// because it must obscure everything else (including in-flight
    /// scrapes) until dismissed.
    Splash = 900,
  };
  Q_ENUM(Layer)

  explicit OverlayZOrderRegistry(QObject *parent = nullptr);
  ~OverlayZOrderRegistry() override;

  /// Register @p overlay at @p layer. Idempotent: re-registering an already-
  /// registered overlay updates its layer assignment without duplicating
  /// the entry. Safe to call from an overlay's setLayerManager() handler.
  void registerOverlay(QWidget *overlay, Layer layer);

  /// Drop @p overlay from the registry. Safe to call on an already-
  /// unregistered overlay (no-op). Automatic if the overlay is destroyed
  /// while still registered — QPointer drops the dangling reference and
  /// the next bringToFront / restack sweep purges the slot.
  void unregisterOverlay(QWidget *overlay);

  /// Re-stack @p overlay to the top of its layer's bracket, then re-apply
  /// raise() across every registered overlay from lowest to highest layer
  /// so the documented z-order holds. Unregistered widgets fall back to
  /// QWidget::raise() so out-of-order calls during construction don't
  /// crash.
  void bringToFront(QWidget *overlay);

  /// Re-apply raise() across every registered overlay in z-order (lowest
  /// layer raised first, highest last). Use after a parent reparent / show
  /// event when the widget tree's natural order may have drifted.
  void restack();

private:
  struct Entry {
    QPointer<QWidget> widget;
    Layer layer = Layer::Backdrop;
  };

  /// Drop entries whose QPointer<QWidget> has gone null (overlay destroyed
  /// without an explicit unregisterOverlay). Called at the top of every
  /// public method that iterates m_entries.
  void purgeDestroyed();

  QList<Entry> m_entries;
};

#endif // OVERLAYZORDERREGISTRY_H
