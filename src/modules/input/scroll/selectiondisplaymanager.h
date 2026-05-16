#ifndef SELECTIONDISPLAYMANAGER_H
#define SELECTIONDISPLAYMANAGER_H

#include "artworkpreviewoverlay.h"
#include "collectionutils.h"
#include "gridlayoutcalculator.h"
#include <functional>
#include <memory>
#include <QHash>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QString>

class QWidget;
class QScrollArea;
class QTimer;
class ItemWidget;
class ItemWidgetFactory;
class SelectionOverlayManager;
class SelectionStateTracker;
class SelectionCoordinator;
class ArrowKeyScrollHelper;
class InteractionStateHolder;
class ListHeaderWidget;
struct GeneralSettings;
enum class ListSortColumn;

/**
 * @brief Owns selection-related visual elements extracted from ScrollManager.
 *
 * Bundles three concerns previously interleaved in ScrollManager:
 *  - Glide overlay (SelectionOverlayManager)
 *  - Selection state (SelectionStateTracker)
 *  - List view chrome (header widget, column widths, artwork preview overlay)
 *
 * ScrollManager holds a unique_ptr to this class and accesses the inner
 * sub-objects through the overlay()/state()/listHeader() getters. The selection
 * update logic in ScrollManager continues to run there, but routes through
 * these getters instead of owning the sub-objects directly.
 *
 * Memory ownership:
 *  - Owns SelectionOverlayManager and SelectionStateTracker via unique_ptr
 *  - Owns ArtworkPreviewOverlay via unique_ptr (lazy-created on first request)
 *  - ListHeaderWidget is parented to the scroll area's viewport (Qt parent
 *    ownership) and is destroyed via destroyListHeader() before the viewport
 *    goes away.
 *  - Borrowed pointers (scroll area, virtual container, factory, active widgets
 *    map, metrics, context, settings) are not owned.
 */
class SelectionDisplayManager : public QObject {
  Q_OBJECT
public:
  explicit SelectionDisplayManager(QObject *parent = nullptr);
  ~SelectionDisplayManager() override;

  // ─────────────────────────────────────────────────────────────────────
  // Configuration (called from ScrollManager::setupReferences and around
  // virtual container lifecycle)
  // ─────────────────────────────────────────────────────────────────────

  void setMediaScrollArea(QScrollArea *area) { m_mediaScrollArea = area; }
  void setVirtualContainer(QWidget *container) { m_virtualContainer = container; }
  void setWidgetFactory(ItemWidgetFactory *factory) { m_widgetFactory = factory; }
  void setActiveWidgets(const QHash<int, ItemWidget *> *widgets) { m_activeWidgets = widgets; }
  void setMetrics(const GridMetrics *metrics) { m_metrics = metrics; }
  void setCollectionContext(const CollectionContext *context) { m_context = context; }
  void setSelectionCoordinator(SelectionCoordinator *coord) { m_selectionCoordinator = coord; }
  void setArrowKeyScrollHelper(ArrowKeyScrollHelper *helper) { m_arrowKeyScrollHelper = helper; }
  void setInteractionState(InteractionStateHolder *state) { m_state = state; }
  void setArrowKeyViewUpdateTimer(QTimer *timer) { m_arrowKeyViewUpdateTimer = timer; }
  void setEnsureWidgetCallback(std::function<void(int)> cb) { m_ensureWidget = std::move(cb); }
  void setItemPositionCallback(std::function<QPoint(int)> cb) { m_itemPosition = std::move(cb); }
  void setTotalItemsProvider(std::function<int()> cb) { m_totalItemsProvider = std::move(cb); }
  void setDestroyingProvider(std::function<bool()> cb) { m_destroyingProvider = std::move(cb); }
  /// Applies persisted column widths from settings, if any.
  void applyGeneralSettings(const GeneralSettings *settings);

  // ─────────────────────────────────────────────────────────────────────
  // Sub-object access (kept stable for ScrollManager's selection logic)
  // ─────────────────────────────────────────────────────────────────────

  [[nodiscard]] SelectionOverlayManager *overlay() const { return m_overlay.get(); }
  [[nodiscard]] SelectionStateTracker *state() const { return m_stateTracker.get(); }
  [[nodiscard]] ListHeaderWidget *listHeader() const { return m_listHeader; }

  [[nodiscard]] int collectionColumnWidth() const { return m_collectionColumnWidth; }
  [[nodiscard]] int artworkColumnWidth() const { return m_artworkColumnWidth; }

  // ─────────────────────────────────────────────────────────────────────
  // List header lifecycle and rendering
  // ─────────────────────────────────────────────────────────────────────

  /// Creates (lazily) the list header in list-view mode and positions it at
  /// the top of the viewport. Hides it in grid mode.
  void updateListHeader();
  /// Destroys the list header widget. Used by ScrollManager destructor before
  /// the viewport goes away.
  void destroyListHeader();

  // ─────────────────────────────────────────────────────────────────────
  // Artwork preview overlay (list-mode hover)
  // ─────────────────────────────────────────────────────────────────────

  [[nodiscard]] bool isArtworkPreviewVisible() const;
  /// Hides the preview if visible. Returns true if it was visible.
  bool hideArtworkPreview();
  /// Lazy-creates the overlay if needed and shows it for the given file.
  void showArtworkPreview(const QString &filePath, const QString &artworkDir);
  /// Lazy-creates the overlay and shows a video-first preview for the
  /// given file, falling back to artwork when no video is found.
  /// Returns true when a preview was shown, false when neither a video
  /// nor artwork exists (the overlay stays hidden).
  bool showMediaPreview(const QString &filePath, const QString &artworkDir,
                        const QString &videoDir);
  /// Populate the expand-mode overlay's bottom thumb strip with the
  /// item's related artwork so Left/Right and thumb clicks can cycle
  /// the main preview between cover / screenshot / fanart / video.
  /// Caller passes the same gallery entries the sidebar uses
  /// (DetailsPane::currentGalleryEntries). No-op if the overlay has not
  /// been created yet.
  void setArtworkPreviewGallery(const QList<ArtworkPreviewOverlay::GalleryEntry> &entries);

  // ─────────────────────────────────────────────────────────────────────
  // Selection update logic (moved from ScrollManager,)
  // ─────────────────────────────────────────────────────────────────────

  /// Updates selection visuals and manages prewarming, overlay animation, and
  /// arrow-centering for the given visual index.
  void updateSelectionForIndex(int selectedIndex);
  /// Refreshes overlay visibility based on current selection and force flag.
  void refreshSelectionOverlayState();
  /// Sets whether the selection overlay must remain visible (e.g. during
  /// click-hold scrolling) and refreshes accordingly.
  void setForceSelectionOverlayVisible(bool force);
  /// Returns the overlay rect for the given visual index.
  [[nodiscard]] QRect selectionOverlayRectForIndex(int visualIndex) const;
  /// Slot for the arrow-key view update timer; recenters the view on the
  /// selected item.
  void onArrowKeyViewUpdate();

signals:
  /// Emitted when the user clicks a list header column to change sort.
  void sortModeChangeRequested(SortMode sortMode);
  /// Emitted when the user resizes the collection column.
  void listColumnWidthChanged(int width);
  /// Emitted when the user resizes the artwork column.
  void listArtworkColumnWidthChanged(int width);
  /// Emitted when the user activates (Enter / double-click) while the
  /// artwork preview overlay is visible. Used by expand-mode to launch
  /// the previewed item on the second activation. @p filePath is the path
  /// of the item the overlay is displaying; may be empty for gallery
  /// thumbnail previews that don't carry an associated media path, in
  /// which case the listener should fall back to the current selection.
  void artworkPreviewLaunchRequested(const QString &filePath);
  /// bug #7: emitted from the overlay's show/hide events. Wired
  /// up at lazy-creation time so DetailsPaneManager can lower the sidebar while
  /// the overlay is on top.
  void artworkPreviewVisibilityChanged(bool visible);

private slots:
  void onListColumnClicked(ListSortColumn column);
  void onListColumnWidthChanged(int collectionWidth);
  void onListArtworkColumnWidthChanged(int artworkWidth);

private:
  // Selection update internal helpers (moved from ScrollManager,)
  void prewarmSurroundingWidgets(int selectedIndex);
  void scheduleArrowKeyUpdate(int selectedIndex);
  void updateSelectionDirection(int selectedIndex, int prevIndex);
  void handleSameSelectionUpdate(int selectedIndex, ItemWidget *currentWidget, bool keepOverlay);
  void handleNewSelectionUpdate(int selectedIndex, int prevIndex, ItemWidget *currentWidget);
  void handleMissingWidgetSelection(int selectedIndex, bool keepOverlay);
  void handleHorizontalMoveAnimation(int selectedIndex, int prevIndex);
  void handleDirectSelectionUpdate(int selectedIndex);
  static void calculateMovementDirection(int selectedIndex, int prevIndex, int itemsPerRow,
                                         bool &isHorizontalMove);

  // Owned sub-objects
  std::unique_ptr<SelectionOverlayManager> m_overlay;
  std::unique_ptr<SelectionStateTracker> m_stateTracker;
  std::unique_ptr<ArtworkPreviewOverlay> m_artworkPreviewOverlay;
  ListHeaderWidget *m_listHeader = nullptr; // Qt-parented to viewport

  // Borrowed dependencies
  QScrollArea *m_mediaScrollArea = nullptr;
  QWidget *m_virtualContainer = nullptr;
  ItemWidgetFactory *m_widgetFactory = nullptr;
  const QHash<int, ItemWidget *> *m_activeWidgets = nullptr;
  const GridMetrics *m_metrics = nullptr;
  const CollectionContext *m_context = nullptr;
  SelectionCoordinator *m_selectionCoordinator = nullptr;
  ArrowKeyScrollHelper *m_arrowKeyScrollHelper = nullptr;
  InteractionStateHolder *m_state = nullptr;
  QTimer *m_arrowKeyViewUpdateTimer = nullptr;

  // Callbacks back into ScrollManager (avoid cyclic include + keep state
  // ownership clean)
  std::function<void(int)> m_ensureWidget;
  std::function<QPoint(int)> m_itemPosition;
  std::function<int()> m_totalItemsProvider;
  std::function<bool()> m_destroyingProvider;

  // List view column widths
  int m_collectionColumnWidth = 150;
  int m_artworkColumnWidth = 32;
};

#endif // SELECTIONDISPLAYMANAGER_H
