#ifndef SCROLLMANAGER_H
#define SCROLLMANAGER_H

#include "collectionutils.h"
#include "gridlayoutcalculator.h"
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
class ArtworkManager;
class WidgetPoolManager;
class FilterManager;
class DataSourceManager;
class SelectionOverlayManager;
class SearchLoadingOverlay;
class VirtualContainerManager;
class SelectionCoordinator;
class ScrollEventHandler;
class ItemWidgetFactory;
class InteractionStateHolder;
class ArrowKeyScrollHelper;
class ScrollDataManager;
class PreSearchStateManager;
class SelectionStateTracker;
class SelectionDisplayManager;
class ListHeaderWidget;
class ArtworkPreviewOverlay;
enum class ListSortColumn;

namespace TimerUtils {
class DebouncedTimer;
}

// Alias for backward compatibility - use GridMetrics from GridLayoutCalculator
using VirtualMetrics = GridMetrics;

struct ApplicationContext;

/**
 * @brief Setup struct for ScrollManager dependencies.
 */
struct ScrollManagerSetup {
  const ApplicationContext *ctx = nullptr;

  // All fields resolved through ApplicationContext typed accessors.
  SETUP_GETTER_DECL_CTX_ONLY(QWidget *, GridContainer)
  SETUP_GETTER_DECL_CTX_ONLY(QScrollArea *, MediaScrollArea)
  SETUP_GETTER_DECL_CTX_ONLY(ArtworkManager *, ArtworkManager)
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
class ScrollManager : public QObject {
  Q_OBJECT
public:
  ScrollManager(QObject *parent = nullptr);
  ~ScrollManager() override;
  void setupReferences(const ScrollManagerSetup &setup);
  void setupVirtualScrolling(int totalCount, const CollectionContext &context);
  // Updates only the media-file portion of the data model (keeps subcollections
  // and virtual folders intact) and recalculates container metrics without
  // tearing down the view.
  void updateMediaItemCount(int mediaItemCount);
  void receiveItemsRange(int offset, const QStringList &filePaths,
                         const QHash<QString, QString> &fileNames,
                         const QHash<QString, QString> &fileToArtworkDir);
  void cleanup();
  void updateGridWidth(int newGridWidth);
  void updateViewType(ViewType viewType);
  void updateVirtualView();
  [[nodiscard]] int getEffectiveHorizontalSpacing() const;
  [[nodiscard]] int getFirstVisibleRow() const;
  [[nodiscard]] int getLastVisibleRow() const;
  void updateSelectionForIndex(int selectedIndex);
  void refreshSelectionOverlayState();
  void setForceSelectionOverlayVisible(bool force);
  [[nodiscard]] QString getSubcollectionName(int subcollectionIndex) const;
  void setDatabaseManager(DatabaseManager *manager);
  void recenterVirtualContainer();
  [[nodiscard]] HorizontalAlignment getCurrentAlignment() const;
  void applyFilter(const QString &searchText);
  void cleanupActiveWidgets();
  void clearFilter();
  void showSearchLoadingOverlay();
  void hideSearchLoadingOverlay();
  void savePreSearchState();
  void restorePreSearchState();
  [[nodiscard]] bool hasPreSearchState() const;
  [[nodiscard]] int getFilteredIndex(int visualIndex) const;
  [[nodiscard]] int getScrollbarWidth() const;
  [[nodiscard]] bool willNeedVerticalScrollbar() const;
  [[nodiscard]] int getTotalItems() const;
  [[nodiscard]] const GridMetrics &getMetrics() const { return m_metrics; }
  void enforceScrollContentConstraints();
  void recreateLayout();

  /// Set file path to restore selection to after items are loaded (for sort
  /// changes)
  void setPendingSelectionRestoreByPath(const QString &filePath);

  /// Check if there's a pending selection restore by file path
  /// Used to skip index-based selection restore during sort changes
  [[nodiscard]] bool hasPendingSelectionRestoreByPath() const {
    return !m_pendingRestoreFilePath.isEmpty();
  }

  /// Check if artwork preview overlay is currently visible
  [[nodiscard]] bool isArtworkPreviewVisible() const;
  /// Hide the artwork preview overlay if visible (returns true if it was
  /// visible)
  bool hideArtworkPreview();

  void centerHorizontalScrollbar(int currentCollectionIndex,
                                 const QList<CollectionConfig> &collections);
  void handleLayoutChange();
  void notifyUserActivity();
  [[nodiscard]] int getCurrentGridWidth() const;
  void updateContextForSubcollection(int subcollectionIndex);
  void applySubcollectionFilter(int subcollectionIndex);
  [[nodiscard]] int getEffectiveViewportWidth() const;
  void recalculateContainerMetrics();
  void forceVirtualViewUpdate();
  void preCalculateLayout();
  [[nodiscard]] const QHash<int, ItemWidget *> &getActiveWidgets() const { return m_activeWidgets; }
  // Injects cached items for instant startup display (bypasses database fetch)
  void injectCachedItems(int startIndex, const QStringList &filePaths,
                         const QHash<QString, QString> &fileNames,
                         const QHash<QString, QString> &artworkPaths = {});
  // Gets current viewport data for session caching (returns true if valid data)
  [[nodiscard]] bool getCurrentViewportForCache(int &startIndex, int &totalItems,
                                                QStringList &filePaths,
                                                QHash<QString, QString> &fileNames,
                                                QHash<QString, QString> &artworkPaths) const;
  // Data accessors - delegate to ScrollDataManager
  [[nodiscard]] const QStringList &getFilePaths() const;
  [[nodiscard]] const QHash<QString, QString> &getFileNames() const;
  [[nodiscard]] int getSubcollectionCount() const;
  [[nodiscard]] int getVirtualFolderCount() const;
  [[nodiscard]] QString filePathForVisualIndex(int visualIndex) const;
  [[nodiscard]] QString virtualFolderPathForVisualIndex(int visualIndex) const;
  void primeLayoutFor(const CollectionConfig &config);
  void setInitialScrollIndex(int index);

signals:
  void subcollectionEntered(int subcollectionIndex);
  void virtualFolderEntered(const QString &folderPath);
  void requestItemsRange(int offset, int limit);
  void virtualScrollSetupComplete();
  void filterChanged(int visibleItems, int totalOriginal);
  void
  sortModeChangeRequested(SortMode sortMode); // Request sort mode change from list header click
  void selectItemByIndex(int index); // Request selection of item at index (for post-sort restore)
  void listColumnWidthChanged(int width); // Emitted when list column width is resized
  void
  listArtworkColumnWidthChanged(int width); // Emitted when list artwork column width is resized

public slots:
  /// Receives the visual index for a file path from database query
  void onVisualIndexForPathLoaded(int visualIndex, const QString &filePath);

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

  // Data source manager: owns FilterManager + ScrollDataManager +
  // PreSearchStateManager + SearchLoadingOverlay (extracted from
  // ScrollManager, Kartend-gg2).
  std::unique_ptr<DataSourceManager> m_dataSource;

  // Raw aliases into m_dataSource for the in-place filter/data update
  // logic that still lives in ScrollManager (scrollmanagerfilter.cpp).
  // Lifetime is tied to m_dataSource; never delete through these pointers.
  FilterManager *m_filterManager = nullptr;
  ScrollDataManager *m_dataManager = nullptr;
  PreSearchStateManager *m_preSearchStateManager = nullptr;
  SearchLoadingOverlay *m_searchLoadingOverlay = nullptr;

  // Selection display manager owns overlay + state tracker + list header +
  // artwork preview overlay (extracted from ScrollManager, Kartend-3u5).
  std::unique_ptr<SelectionDisplayManager> m_selectionDisplay;

  // Raw aliases into m_selectionDisplay for the in-place selection update
  // logic that still lives in ScrollManager. Lifetime is tied to
  // m_selectionDisplay; never delete through these pointers.
  SelectionOverlayManager *m_overlayManager = nullptr;
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

  // Data manager and pre-search state manager are now owned by m_dataSource
  // (Kartend-gg2). Raw aliases above (m_dataManager, m_preSearchStateManager).

  // List header widget, column widths, and artwork preview overlay are now
  // owned by m_selectionDisplay (Kartend-3u5).

public:
  [[nodiscard]] const WidgetPoolManager *getWidgetPool() const { return m_widgetPool.get(); }
  [[nodiscard]] const FilterManager *getFilterManager() const { return m_filterManager; }
  [[nodiscard]] const SelectionOverlayManager *getOverlayManager() const {
    return m_overlayManager;
  }
  [[nodiscard]] const VirtualContainerManager *getContainerManager() const {
    return m_containerManager.get();
  }
  [[nodiscard]] const SelectionCoordinator *getSelectionCoordinator() const {
    return m_selectionCoordinator.get();
  }
  [[nodiscard]] const ScrollEventHandler *getScrollEventHandler() const {
    return m_scrollEventHandler.get();
  }
  [[nodiscard]] const ScrollDataManager *getDataManager() const { return m_dataManager; }
  [[nodiscard]] const PreSearchStateManager *getPreSearchStateManager() const {
    return m_preSearchStateManager;
  }
  [[nodiscard]] const SelectionStateTracker *getSelectionState() const { return m_selectionState; }

private:
  const GeneralSettings *m_generalSettings = nullptr;
  InteractionStateHolder *m_state = nullptr;
  QWidget *m_gridContainer = nullptr;
  QScrollArea *m_mediaScrollArea = nullptr;
  ArtworkManager *m_artworkManager = nullptr;
  const CollectionHierarchyCache *m_hierarchyCache = nullptr;
  QWidget *m_virtualContainer = nullptr;
  QHash<int, ItemWidget *> m_activeWidgets;
  const QList<CollectionConfig> *m_collections = nullptr;
  CollectionContext m_context;
  VirtualMetrics m_metrics;
  QTimer *m_scrollTimer = nullptr; // Throttle timer (not debounce)
  QTimer *m_arrowKeyViewUpdateTimer = nullptr;
  int m_totalItems = 0;
  qint64 m_lastScrollTime = 0;
  bool m_isMutating = false;
  DatabaseManager *m_databaseManager = nullptr;
  bool m_destroying = false;
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

  // Helper methods to reduce cognitive complexity
  [[nodiscard]] QSet<int> calculateNeededIndices() const;
  void removeUnneededWidgets(const QSet<int> &needed);
  void updateArtworkIfAllowed();

  void initializeSubcollections();
  void initializeVirtualFolders();
  void setupFilePathMappings();
  void setupEmptyVirtualScrolling();
  void setupNormalVirtualScrolling();

  void handleProgrammaticScroll();
  void handleUserScroll();
  void setupScrollSuppression();
  void finalizeScrollChanges();

  // Selection update helpers and related internals were moved to
  // SelectionDisplayManager (Kartend-p79). ScrollManager keeps thin facade
  // methods (updateSelectionForIndex, refreshSelectionOverlayState,
  // setForceSelectionOverlayVisible, selectionOverlayRectForIndex,
  // onArrowKeyViewUpdate) declared in the public/slots sections above.

  void rebuildFilteredView();
};

#endif