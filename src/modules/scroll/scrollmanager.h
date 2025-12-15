#ifndef SCROLLMANAGER_H
#define SCROLLMANAGER_H

#include "collectionutils.h"
#include "gridlayoutcalculator.h"
#include "setuputils.h"
#include <QDateTime>
#include <QHash>
#include <memory>
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
class SelectionOverlayManager;
class VirtualContainerManager;
class SelectionCoordinator;
class ScrollEventHandler;
class ItemWidgetFactory;
class InteractionStateHolder;
class ArrowKeyScrollHelper;
class ScrollDataManager;
class PreSearchStateManager;
class SelectionStateTracker;

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
  const GeneralSettings *generalSettings = nullptr;
  QWidget *gridContainer = nullptr;
  QScrollArea *mediaScrollArea = nullptr;
  ArtworkManager *artworkManager = nullptr;
  const QList<CollectionConfig> *collections = nullptr;
  const CollectionHierarchyCache *hierarchyCache = nullptr;
  
  SETUP_GETTER_DECL(QWidget*, GridContainer)
  SETUP_GETTER_DECL(QScrollArea*, MediaScrollArea)
  SETUP_GETTER_DECL(ArtworkManager*, ArtworkManager)
  SETUP_GETTER_DECL(const QList<CollectionConfig>*, Collections)
  SETUP_GETTER_DECL(const CollectionHierarchyCache*, HierarchyCache)
  SETUP_GETTER_DECL_CTX_ONLY(InteractionStateHolder*, InteractionState)
  SETUP_GETTER_DECL(const GeneralSettings*, GeneralSettings)
};

/**
 * @brief Manages virtual scrolling, widget pooling, and grid layout for large item collections.
 * 
 * Memory Ownership Model:
 * - Owns helper managers via std::unique_ptr (explicit lifetime management)
 * - Owns QTimer instances via Qt parent ownership (new QTimer(this))
 * - Does NOT own: m_gridContainer, m_mediaScrollArea, m_artworkManager, m_databaseManager,
 *   m_collections, m_hierarchyCache, m_generalSettings, m_state (borrowed references)
 * - Widget ownership: ItemWidgets in m_activeWidgets are managed by WidgetPoolManager
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
  void receiveItemsRange(int offset, const QStringList &filePaths, const QHash<QString, QString> &fileNames);
  void cleanup();
  void updateGridWidth(int newGridWidth);
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
  void savePreSearchState();
  void restorePreSearchState();
  [[nodiscard]] bool hasPreSearchState() const;
  [[nodiscard]] int getFilteredIndex(int visualIndex) const;
  [[nodiscard]] int getScrollbarWidth() const;
  [[nodiscard]] bool willNeedVerticalScrollbar() const;
  [[nodiscard]] int getTotalItems() const;
  void enforceScrollContentConstraints();
  void recreateLayout();
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
  [[nodiscard]] const QHash<int, ItemWidget *> &getActiveWidgets() const {
    return m_activeWidgets;
  }
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

private slots:
  void onScrollChanged();
  void onThrottledUpdate();
  void onSubcollectionDoubleClicked(int subcollectionIndex);
  void onVirtualFolderDoubleClicked(const QString &folderPath);
  void onArrowKeyViewUpdate();

private:
  void createVirtualContainer();
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

  // Filter manager for search and subcollection filtering
  std::unique_ptr<FilterManager> m_filterManager;

  // Selection overlay manager for glide animation
  std::unique_ptr<SelectionOverlayManager> m_overlayManager;

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

  // Data manager for file paths, file names, subcollections, and virtual folders
  std::unique_ptr<ScrollDataManager> m_dataManager;

  // Pre-search state manager for fast search result restoration
  std::unique_ptr<PreSearchStateManager> m_preSearchStateManager;

  // Selection state tracker for selection indices, direction, and row
  std::unique_ptr<SelectionStateTracker> m_selectionState;

public:
  [[nodiscard]] const WidgetPoolManager *getWidgetPool() const { return m_widgetPool.get(); }
  [[nodiscard]] const FilterManager *getFilterManager() const { return m_filterManager.get(); }
  [[nodiscard]] const SelectionOverlayManager *getOverlayManager() const { return m_overlayManager.get(); }
  [[nodiscard]] const VirtualContainerManager *getContainerManager() const { return m_containerManager.get(); }
  [[nodiscard]] const SelectionCoordinator *getSelectionCoordinator() const { return m_selectionCoordinator.get(); }
  [[nodiscard]] const ScrollEventHandler *getScrollEventHandler() const { return m_scrollEventHandler.get(); }
  [[nodiscard]] const ScrollDataManager *getDataManager() const { return m_dataManager.get(); }
  [[nodiscard]] const PreSearchStateManager *getPreSearchStateManager() const { return m_preSearchStateManager.get(); }
  [[nodiscard]] const SelectionStateTracker *getSelectionState() const { return m_selectionState.get(); }

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
  QTimer *m_scrollTimer = nullptr;  // Throttle timer (not debounce)
  QTimer *m_arrowKeyViewUpdateTimer = nullptr;
  int m_totalItems = 0;
  qint64 m_lastScrollTime = 0;
  bool m_isMutating = false;
  DatabaseManager *m_databaseManager = nullptr;
  bool m_destroying = false;
  bool m_processingScrollChange = false;  // Reentrancy guard for onScrollChanged
  TimerUtils::DebouncedTimer *m_userScrollIdleTimer = nullptr;
  TimerUtils::DebouncedTimer *m_prewarmIdleTimer = nullptr;
  
  // Initial scroll index for pre-positioning before widget creation
  int m_initialScrollIndex = -1;

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

  void calculateMovementDirection(int selectedIndex, int prevIndex,
                                  int itemsPerRow, bool &isHorizontalMove);
  [[nodiscard]] QRect selectionOverlayRectForIndex(int visualIndex) const;
  void handleHorizontalMoveAnimation(int selectedIndex, int prevIndex);
  void handleDirectSelectionUpdate(int selectedIndex);
  void prewarmSurroundingWidgets(int selectedIndex);
  void scheduleArrowKeyUpdate(int selectedIndex);

  // Selection update helpers (split from updateSelectionForIndex)
  void updateSelectionDirection(int selectedIndex, int prevIndex);
  void handleSameSelectionUpdate(int selectedIndex, ItemWidget *currentWidget, bool keepOverlay);
  void handleNewSelectionUpdate(int selectedIndex, int prevIndex, ItemWidget *currentWidget);
  void handleMissingWidgetSelection(int selectedIndex, bool keepOverlay);

  void rebuildFilteredView();
};

#endif