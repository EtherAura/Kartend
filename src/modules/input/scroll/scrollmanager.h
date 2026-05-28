#ifndef SCROLLMANAGER_H
#define SCROLLMANAGER_H

#include "artworkpreviewoverlay.h"
#include "collectionutils.h"
#include "gridlayoutcalculator.h"
#include "iscrollmanager.h"
#include "setuputils.h"
#include <memory>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QTimer>

class QWidget;
class QScrollArea;
class QScrollBar;
class ItemWidget;
class DatabaseManager;
class QPropertyAnimation;
class IArtworkManager;
class CoverFlowController;
class WidgetPoolManager;
class FilterManager;
class DataSourceCoordinator;
class SelectionOverlayManager;
class SearchLoadingOverlay;
class VirtualContainerManager;
class SelectionCoordinator;
class ScrollEventHandler;
class ItemWidgetFactory;
class InteractionStateHolder;
class ArrowKeyScrollHelper;
class ScrollDataStore;
class PreSearchStateCache;
class SelectionStateTracker;
class SelectionDisplayManager;
class ListHeaderWidget;
class VirtualScrollEngine;
enum class ListSortColumn;

namespace TimerUtils {
class DebouncedTimer;
}

// Alias for backward compatibility - use GridMetrics from GridLayoutCalculator
using VirtualMetrics = GridMetrics;

#include "applicationcontext_fwd.h"

/**
 * @brief Setup struct for ScrollManager dependencies.
 */
struct ScrollManagerSetup {
  const ApplicationContext *ctx = nullptr;

  // All fields resolved through ApplicationContext typed accessors.
  SETUP_GETTER_DECL_CTX_ONLY(QWidget *, GridContainer)
  SETUP_GETTER_DECL_CTX_ONLY(QScrollArea *, MediaScrollArea)
  SETUP_GETTER_DECL_CTX_ONLY(IArtworkManager *, ArtworkManager)
  SETUP_GETTER_DECL_CTX_ONLY(const QList<CollectionConfig> *, Collections)
  SETUP_GETTER_DECL_CTX_ONLY(const CollectionHierarchyCache *, HierarchyCache)
  SETUP_GETTER_DECL_CTX_ONLY(InteractionStateHolder *, InteractionState)
  SETUP_GETTER_DECL_CTX_ONLY(const GeneralSettings *, GeneralSettings)
};

/**
 * @brief Manages virtual scrolling, widget pooling, and grid layout for large
 * item collections.
 *
 * Memory Ownership Model:
 * - Owns helper managers via std::unique_ptr (explicit lifetime management)
 * - Owns QTimer instances via Qt parent ownership (new QTimer(this))
 * - Does NOT own: m_gridContainer, m_mediaScrollArea, m_artworkManager,
 * m_databaseManager, m_collections, m_hierarchyCache, m_generalSettings,
 * m_state (borrowed references)
 * - Widget ownership: ItemWidgets in m_activeWidgets are managed by
 * WidgetPoolManager
 */
class ScrollManager : public IScrollManager {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ScrollManager)
  // Engine is a sibling helper that drives virtual-scrolling layout and
  // widget materialization. It accesses ScrollManager's state
  // directly via friendship; canonical state ownership remains here.
  friend class VirtualScrollEngine;

public:
  ScrollManager(QObject *parent = nullptr);
  ~ScrollManager() override;
  void setupReferences(const ScrollManagerSetup &setup);
  void setupVirtualScrolling(int totalCount, const CollectionContext &context) override;
  // Updates only the media-file portion of the data model (keeps subcollections
  // and virtual folders intact) and recalculates container metrics without
  // tearing down the view.
  void updateMediaItemCount(int mediaItemCount) override;
  void receiveItemsRange(int offset, const QStringList &filePaths,
                         const QHash<QString, QString> &fileNames,
                         const QHash<QString, QString> &fileToArtworkDir) override;
  void cleanup() override;
  void updateGridWidth(int newGridWidth) override;
  /// live-apply a change to the per-collection items-per-column
  /// for the Horizontal view mode. No-op when the active view is not Horizontal,
  /// since the value only feeds the layout in that mode. Pass 0 to mean
  /// "fall back to gridWidth".
  void updateHorizontalGridHeight(int newHorizontalGridHeight) override;
  /// track whether the sidebar is currently hidden AND its mode
  /// would shrink the grid (Expand). MainWindow updates this whenever the
  /// sidebar's visibility changes, before triggering recalculateContainerMetrics.
  /// When the flag is true and the active collection has gridWidthSidebarHidden
  /// (or horizontalGridHeightSidebarHidden) configured non-zero, those values
  /// override the primary ones in the layout calculator.
  void setSidebarShrinkingActive(bool active) override;
  [[nodiscard]] bool sidebarShrinkingActive() const override { return m_sidebarShrinkingActive; }
  void updateViewType(ViewType viewType) override;
  void updateVirtualView() override;
  [[nodiscard]] int getEffectiveHorizontalSpacing() const override;
  [[nodiscard]] int getFirstVisibleRow() const;
  [[nodiscard]] int getLastVisibleRow() const;
  void updateSelectionForIndex(int selectedIndex) override;
  void refreshSelectionOverlayState() override;
  void setForceSelectionOverlayVisible(bool force) override;
  [[nodiscard]] QString getSubcollectionName(int subcollectionIndex) const override;
  void recenterVirtualContainer() override;
  [[nodiscard]] HorizontalAlignment getCurrentAlignment() const;
  void applyFilter(const QString &searchText) override;
  void cleanupActiveWidgets() override;
  void clearFilter() override;
  void showSearchLoadingOverlay() override;
  void hideSearchLoadingOverlay() override;
  void savePreSearchState() override;
  void restorePreSearchState() override;
  [[nodiscard]] bool hasPreSearchState() const override;
  [[nodiscard]] int getFilteredIndex(int visualIndex) const override;
  [[nodiscard]] int getScrollbarWidth() const;
  [[nodiscard]] bool willNeedVerticalScrollbar() const;
  [[nodiscard]] int getTotalItems() const override;
  [[nodiscard]] const GridMetrics &getMetrics() const override { return m_metrics; }
  void enforceScrollContentConstraints();
  void recreateLayout() override;

  /// Set file path to restore selection to after items are loaded (for sort
  /// changes)
  void setPendingSelectionRestoreByPath(const QString &filePath) override;

  /// Check if there's a pending selection restore by file path
  /// Used to skip index-based selection restore during sort changes
  [[nodiscard]] bool hasPendingSelectionRestoreByPath() const override {
    return !m_pendingRestoreFilePath.isEmpty();
  }

  /// Check if artwork preview overlay is currently visible
  [[nodiscard]] bool isArtworkPreviewVisible() const override;
  /// Hide the artwork preview overlay if visible (returns true if it was
  /// visible)
  bool hideArtworkPreview() override;
  /// Show the artwork preview overlay for the given file path. Used by
  /// expand-mode two-stage activation in InteractionManager.
  void showArtworkPreview(const QString &filePath, const QString &artworkDir);
  /// Show a video-first preview overlay. Falls back to
  /// artwork when no video is found in @p videoDir. Used by expand-mode
  /// (first-stage) and middle-click peek.
  /// Returns true when a preview was shown, false when the item has
  /// neither a video nor artwork (the overlay stays hidden).
  bool showMediaPreview(const QString &filePath, const QString &artworkDir,
                        const QString &videoDir) override;
  /// Forwarder for SelectionDisplayManager::setArtworkPreviewGallery.
  /// Populates the expand-mode overlay's bottom thumb strip so Left/Right
  /// + thumb clicks can cycle between the item's scraped artwork types.
  void setArtworkPreviewGallery(const QList<ArtworkPreviewOverlay::GalleryEntry> &entries);

  void centerHorizontalScrollbar(int currentCollectionIndex,
                                 const QList<CollectionConfig> &collections) override;
  void handleLayoutChange() override;
  void notifyUserActivity();
  [[nodiscard]] int getCurrentGridWidth() const override;
  void updateContextForSubcollection(int subcollectionIndex) override;
  void applySubcollectionFilter(int subcollectionIndex) override;
  [[nodiscard]] int getEffectiveViewportWidth() const;
  void recalculateContainerMetrics() override;
  void forceVirtualViewUpdate() override;
  void preCalculateLayout() override;
  [[nodiscard]] const QHash<int, ItemWidget *> &getActiveWidgets() const override {
    return m_activeWidgets;
  }
  // Injects cached items for instant startup display (bypasses database fetch)
  void injectCachedItems(int startIndex, const QStringList &filePaths,
                         const QHash<QString, QString> &fileNames,
                         const QHash<QString, QString> &artworkPaths = {}) override;
  // Gets current viewport data for session caching (returns true if valid data)
  [[nodiscard]] bool
  getCurrentViewportForCache(int &startIndex, int &totalItems, QStringList &filePaths,
                             QHash<QString, QString> &fileNames,
                             QHash<QString, QString> &artworkPaths) const override;
  // Data accessors - delegate to ScrollDataStore
  [[nodiscard]] const QStringList &getFilePaths() const override;
  [[nodiscard]] const QHash<QString, QString> &getFileNames() const override;
  [[nodiscard]] int getSubcollectionCount() const override;
  [[nodiscard]] int getVirtualFolderCount() const override;
  [[nodiscard]] QString filePathForVisualIndex(int visualIndex) const override;
  [[nodiscard]] QString virtualFolderPathForVisualIndex(int visualIndex) const override;
  void primeLayoutFor(const CollectionConfig &config) override;
  void setInitialScrollIndex(int index) override;

  /// Kartend-yeik: expose the FilterManager owned by m_dataSource so the
  /// ApplicationContext seed (initializeAppContext) and sibling scroll-side
  /// helpers like CoverFlowController can read filter state through ctx
  /// instead of holding their own setup-struct pointer. Defined out-of-line
  /// because m_filterManager is now QPointer<FilterManager> (Kartend-kovt)
  /// and converting it to a raw pointer needs the complete type.
  [[nodiscard]] FilterManager *filterManager() const;

signals:
  void subcollectionEntered(int subcollectionIndex);
  void virtualFolderEntered(const QString &folderPath);
  void requestItemsRange(int offset, int limit);
  // virtualScrollSetupComplete() is declared on IScrollManager (a QObject
  // interface); ScrollManager inherits and emits it. Not redeclared here.
  void filterChanged(int visibleItems, int totalOriginal);
  void
  sortModeChangeRequested(SortMode sortMode); // Request sort mode change from list header click
  void selectItemByIndex(int index); // Request selection of item at index (for post-sort restore)
  void listColumnWidthChanged(int width); // Emitted when list column width is resized
  void
  listArtworkColumnWidthChanged(int width); // Emitted when list artwork column width is resized
  /// Forwarded from SelectionDisplayManager: user activated (Enter /
  /// double-click) while the artwork preview overlay was visible.
  /// @p filePath is the path of the previewed item, or empty for
  /// gallery-style previews — listeners should fall back to the current
  /// selection when empty.
  void artworkPreviewLaunchRequested(const QString &filePath);
  /// bug #7: forwarded from SelectionDisplayManager. MainWindow
  /// wires this to DetailsPaneManager::setOverlayActive so the sidebar lowers
  /// itself below the overlay while it's showing.
  void artworkPreviewVisibilityChanged(bool visible);
  /// emitted when the user activates the centered card in
  /// CoverFlow view (single click on center, double click, Enter, etc.) and
  /// the activated index is a media item. Subcollection / virtual-folder
  /// activations are routed through subcollectionEntered /
  /// virtualFolderEntered above so they share the existing navigation path.
  void coverFlowItemActivated(int visualIndex);
  /// fired when CoverFlow becomes the active view (true) or
  /// any other ViewType replaces it (false). MainWindow wires this to
  /// DetailsPaneManager::setExternallyHidden so the carousel takes the full
  /// viewport without persisting that as the user's sidebar preference.
  void coverFlowActiveChanged(bool active);

public slots:
  /// Receives the visual index for a file path from database query
  void onVisualIndexForPathLoaded(int visualIndex, const QString &filePath);

  /// Kartend-2hzy pilot: per-collection gridLayoutChanged receiver. Fires when
  /// any save path (settings dialog OR kart import / toolbar inline edits /
  /// right-click) actually mutates GridLayoutPreferences on a collection.
  /// No-op unless the diff is for the active collection — non-active
  /// collections pick up the new layout on next switch via the normal
  /// switchCollection path. The settings-dialog flow already drives grid
  /// updates through handleLayoutChanges, so this slot's load is the
  /// alternate-save paths.
  void onGridLayoutChanged(int collectionIndex, const GridLayoutPreferences &gridLayout);

  /// Kartend-2hzy: per-collection listViewOptionsChanged receiver. The list
  /// view widgets (ItemWidget in list mode + the row-color static) consult
  /// ListViewOptions on the next paint — this slot forces a redraw when
  /// the diff hits the active collection. No-op when not in list mode or
  /// non-active.
  void onListViewOptionsChanged(int collectionIndex, const ListViewOptions &listView);

private slots:
  void onScrollChanged();
  void onThrottledUpdate();
  void onSubcollectionDoubleClicked(int subcollectionIndex);
  void onVirtualFolderDoubleClicked(const QString &folderPath);
  void onArrowKeyViewUpdate();
  void onSliderMoved(int position);
  void reconfigureArtworkForActiveWidgets();
  void onArtworkPreviewRequested(const QString &filePath, const QString &artworkDir);

private:
  void createVirtualContainer();
  void updateListHeader();
  void positionVirtualContainer();
  void cleanupVirtualContainer();
  void calculateVirtualMetrics();
  void connectScrollEvents();
  void disconnectScrollEvents();
  void ensureWidgetForIndex(int visualIndex);
  [[nodiscard]] QPoint getItemPosition(int index) const;

  // ─────────────────────────────────────────────────────────────────────────
  // Owned helper managers (unique_ptr for explicit ownership)
  // ─────────────────────────────────────────────────────────────────────────

  // Widget pool manager for recycling ItemWidgets
  std::unique_ptr<WidgetPoolManager> m_widgetPool;
  ItemWidget *acquireWidget();
  void releaseWidget(ItemWidget *widget);

  // Data source manager: owns FilterManager + ScrollDataStore +
  // PreSearchStateCache + SearchLoadingOverlay (extracted from
  // ScrollManager,).
  std::unique_ptr<DataSourceCoordinator> m_dataSource;

  // Aliases into m_dataSource for the in-place filter/data update logic
  // that still lives in ScrollManager (scrollmanagerfilter.cpp). Lifetime
  // is tied to m_dataSource; never delete through these pointers. QPointer
  // (Kartend-kovt) guards against dangling reads if a future refactor
  // changes destruction order or extracts a helper that captures these
  // aliases — today the unique_ptr in m_dataSource destructs first under
  // reverse-declaration teardown, which would null these out implicitly.
  QPointer<FilterManager> m_filterManager;
  QPointer<ScrollDataStore> m_dataManager;
  QPointer<PreSearchStateCache> m_preSearchStateManager;
  QPointer<SearchLoadingOverlay> m_searchLoadingOverlay;

  // Selection display manager owns overlay + state tracker + list header +
  // artwork preview overlay (extracted from ScrollManager,).
  std::unique_ptr<SelectionDisplayManager> m_selectionDisplay;

  // Aliases into m_selectionDisplay for the in-place selection update
  // logic that still lives in ScrollManager. Lifetime is tied to
  // m_selectionDisplay; never delete through these pointers. m_overlayManager
  // is wrapped in QPointer for the same Kartend-kovt rationale as the
  // m_dataSource aliases above. SelectionStateTracker is not a QObject
  // (it's a pure-data tracker, not a Qt-aware manager) so QPointer doesn't
  // apply; it stays raw and its lifetime is guaranteed by m_selectionDisplay.
  QPointer<SelectionOverlayManager> m_overlayManager;
  SelectionStateTracker *m_selectionState = nullptr;

  // Search loading overlay, virtual container, selection coordinator, etc.
  // continue to be owned directly below.

  // Virtual container manager for container lifecycle
  std::unique_ptr<VirtualContainerManager> m_containerManager;

  // Selection coordinator for selection state and movement analysis
  std::unique_ptr<SelectionCoordinator> m_selectionCoordinator;

  // Scroll event handler for scroll event wiring
  std::unique_ptr<ScrollEventHandler> m_scrollEventHandler;

  // Item widget factory for creating and configuring widgets
  std::unique_ptr<ItemWidgetFactory> m_widgetFactory;

  // Arrow key scroll helper for centering animation
  std::unique_ptr<ArrowKeyScrollHelper> m_arrowKeyScrollHelper;

  // Cover-flow controller: owns the CoverFlowWidget and keeps its card list,
  // appearance config, and visibility in sync with the grid. ScrollManager
  // forwards its carousel signals out through coverFlowItemActivated /
  // coverFlowActiveChanged.
  std::unique_ptr<CoverFlowController> m_coverFlow;

  // Data manager and pre-search state manager are now owned by m_dataSource
  // Raw aliases above (m_dataManager, m_preSearchStateManager).

  // List header widget, column widths, and artwork preview overlay are now
  // owned by m_selectionDisplay.

public:
  // Resolve a raw item index to the subcollection index that owns it, or -1
  // if the item is not part of a subcollection (or the data manager isn't
  // wired). Encapsulates ScrollDataStore so callers don't need to reach
  // into ScrollManager's internals.
  [[nodiscard]] int subcollectionIndexFromActual(int actualIndex) const override;

private:
  // ctx is the single source of truth for sibling managers (ArtworkManager,
  // DatabaseManager, InteractionStateHolder) — never cache them as direct
  // fields.
  const ApplicationContext *m_ctx = nullptr;
  const GeneralSettings *m_generalSettings = nullptr;
  QWidget *m_gridContainer = nullptr;
  QScrollArea *m_mediaScrollArea = nullptr;
  const CollectionHierarchyCache *m_hierarchyCache = nullptr;
  QWidget *m_virtualContainer = nullptr;
  QHash<int, ItemWidget *> m_activeWidgets;
  const QList<CollectionConfig> *m_collections = nullptr;
  CollectionContext m_context;
  VirtualMetrics m_metrics;
  QTimer m_scrollTimer; // Throttle timer (Kartend-a911.5: value member)
  QTimer m_arrowKeyViewUpdateTimer;
  int m_totalItems = 0;
  qint64 m_lastScrollTime = 0;
  bool m_isMutating = false;
  bool m_destroying = false;
  // cached sidebar-hidden-and-shrinking predicate, fed by
  // MainWindow on sidebar-visibility changes. Read by VirtualScrollEngine when
  // it builds layout metrics so the alternate per-collection grid sizes apply
  // automatically.
  bool m_sidebarShrinkingActive = false;
  bool m_processingScrollChange = false; // Reentrancy guard for onScrollChanged
  TimerUtils::DebouncedTimer *m_userScrollIdleTimer = nullptr;
  TimerUtils::DebouncedTimer *m_prewarmIdleTimer = nullptr;
  qint64 m_lastArtworkPrewarmTime = 0; // Debounce artwork directory prewarm

  // Initial scroll index for pre-positioning before widget creation
  int m_initialScrollIndex = -1;

  // Pending file path for selection restore after sort/reload
  QString m_pendingRestoreFilePath;

  // Rate-limited debug aid for cases where nothing renders.
  int m_emptyViewDebugBudget = 3;

  // Rate-limited debug aid for range delivery.
  int m_rangeReceiveDebugBudget = 10;

  // Virtual scrolling engine: owns the algorithms for layout, container
  // lifecycle, and widget materialization. Constructed in the
  // ScrollManager constructor; destroyed last among helper managers so it
  // can safely touch ScrollManager state during teardown.
  std::unique_ptr<VirtualScrollEngine> m_engine;

  void initializeSubcollections();
  void initializeVirtualFolders();
  void setupFilePathMappings();
  void setupEmptyVirtualScrolling();
  void setupNormalVirtualScrolling();

  // cover-flow integration was extracted into CoverFlowController
  // (m_coverFlow) — the carousel is a sibling of m_gridContainer in the
  // items-page scroll-area layout, shown only when the active collection's
  // ViewType is CoverFlow.

  void handleProgrammaticScroll();
  void handleUserScroll();
  void setupScrollSuppression();
  void finalizeScrollChanges();

  // Selection update helpers and related internals were moved to
  // SelectionDisplayManager. ScrollManager keeps thin facade
  // methods (updateSelectionForIndex, refreshSelectionOverlayState,
  // setForceSelectionOverlayVisible, selectionOverlayRectForIndex,
  // onArrowKeyViewUpdate) declared in the public/slots sections above.

  void rebuildFilteredView();
};

#endif