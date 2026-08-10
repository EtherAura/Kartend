#ifndef NAVIGATIONMANAGER_H
#define NAVIGATIONMANAGER_H

#include "applicationcontext.h"
#include "collection/collectioncontext.h"
#include "collection/collectionhierarchycache.h"
#include "collection/generalsettings.h"
#include "errorutils.h"
#include "inavigationmanager.h"
#include "setuputils.h"
#include <functional>
#include <memory>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <utility>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QMenuBar;
class QScrollArea;
class QStackedWidget;
QT_END_NAMESPACE

class IInteractionManager;
class InteractionStateHolder;
class ISettingsManager;
class IDetailsPaneManager;
class IScrollManager;
class IDatabaseManager;
class ISessionManager;
class IArtworkManager;
class IDetailsPane;
class SelectionRestoreCoordinator;
class LoadingOverlay;
class EmptyStateWidget;
class NavigationStackManager;
class CollectionBackgroundController;

/**
 * @brief Setup struct for NavigationManager dependencies.
 *
 * Fields can be set individually, or common fields can be populated
 * from an ApplicationContext via the ctx pointer.
 */
struct NavigationManagerSetup {
  // Sibling managers (and InteractionStateHolder) are read directly from ctx
  // at runtime; never duplicated here.
  const ApplicationContext *ctx = nullptr;

  // UI elements (can be overridden or taken from ctx)
  IDetailsPane *sidebar = nullptr;
  int *currentCollectionIndex = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  const CollectionHierarchyCache *hierarchyCache = nullptr;
  GeneralSettings *generalSettings = nullptr;
  QLineEdit *searchBar = nullptr;
  QWidget *itemsPage = nullptr;
  QWidget *itemsTopBar = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QMenuBar *menubar = nullptr;
  EmptyStateWidget *loadingLabel = nullptr;
  LoadingOverlay *loadingOverlay = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QWidget *gridContainer = nullptr;

  // Callbacks (not in context)
  std::function<bool()> isShuttingDown;
  std::function<void()> refreshTitleCounts;
  /// Kartend-w2n0: fired from updateItemsPageTitle so the items-toolbar
  /// warning badge re-resolves launcher paths on every collection switch.
  /// Optional — when unset (e.g. tests), the badge simply doesn't refresh
  /// from the navigation path.
  std::function<void()> refreshCollectionWarningBadge;

  // UI element accessors that check ctx fallback
  SETUP_GETTER_INLINE_UI_SAME(QScrollArea *, ItemScrollArea, itemScrollArea)
  SETUP_GETTER_INLINE_UI_SAME(QWidget *, GridContainer, gridContainer)
  SETUP_GETTER_INLINE_UI_SAME(QWidget *, ItemsPage, itemsPage)
  SETUP_GETTER_INLINE_UI_SAME(QWidget *, ItemsTopBar, itemsTopBar)
  SETUP_GETTER_INLINE_UI_SAME(QStackedWidget *, StackedWidget, stackedWidget)
  SETUP_GETTER_INLINE_UI_SAME(QMenuBar *, Menubar, menubar)
  SETUP_GETTER_INLINE_UI_SAME(QLineEdit *, SearchBar, searchBar)
  SETUP_GETTER_INLINE_UI_SAME(EmptyStateWidget *, LoadingLabel, loadingLabel)
  SETUP_GETTER_INLINE_UI_SAME(LoadingOverlay *, LoadingOverlay, loadingOverlay)
  SETUP_GETTER_INLINE_UI_SAME(IDetailsPane *, Sidebar, sidebar)
  SETUP_GETTER_INLINE_COL_SAME(QList<CollectionConfig> *, Collections, collections)
  SETUP_GETTER_INLINE_COL_SAME(int *, CurrentCollectionIndex, currentCollectionIndex)
  SETUP_GETTER_INLINE_COL_SAME(const CollectionHierarchyCache *, HierarchyCache, hierarchyCache)
  SETUP_GETTER_INLINE_COL_SAME(GeneralSettings *, GeneralSettings, generalSettings)
};

// QObject must be the first base; INavigationManager is a plain (non-QObject)
// role interface — single-QObject-base multiple inheritance.
class NavigationManager : public QObject, public INavigationManager {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(NavigationManager)
public:
  explicit NavigationManager(QObject *parent = nullptr);
  ~NavigationManager() override;
  [[nodiscard]] bool isNavigationInProgress() const;
  /// True when the application is not in teardown. Kartend-us3b8: this only
  /// consults the app shutdown predicate — it does NOT verify that `this`
  /// NavigationManager is still alive (the old name `isAlive()` wrongly implied
  /// it did). Object liveness for deferred work comes from binding the callback
  /// to `this` as the QObject context (QTimer::singleShot(delay, this, ...) /
  /// queued connections), which Qt auto-disconnects when `this` dies. A
  /// callback NOT bound to a QObject context must capture a QPointer for a real
  /// liveness check. Returns true pre-setup (predicate not yet installed) so
  /// construction-time wiring still routes. Replaced the legacy `parent()`
  /// lifetime guard (Kartend-3v92 / Kartend-d70s).
  [[nodiscard]] bool appNotShuttingDown() const { return !m_isShuttingDown || !m_isShuttingDown(); }

  // Persist current viewport/selection state before shutdown.
  // Call this before blocking signals or clearing collection index.
  void prepareForShutdown();

  // Navigation stack manager for hierarchy traversal
  [[nodiscard]] NavigationStackManager *stackManager() const override {
    return m_stackManager.get();
  }

private:
  std::unique_ptr<NavigationStackManager> m_stackManager;

public:
  void restoreSelectionForCurrentCollection();
  void setupReferences(const NavigationManagerSetup &setup);

  /// Kartend-1fhgz test seam (same precedent as CacheManager's *ForTesting
  /// accessors): the collection index a forceRescanCollection() is still
  /// holding while it waits for ITS uuid's cacheInvalidated echo, -1 when
  /// none. Lets a test pin that a foreign collection's invalidation (routine
  /// since Kartend-xkdxn's background enrichment) does not consume the
  /// pending rescan.
  [[nodiscard]] int pendingRescanCollectionIndexForTesting() const {
    return m_pendingRescanCollectionIndex;
  }

public slots:
  bool showCollectionItems(int collectionIndex) override;
  void navigateWithSharedItems(int collectionIndex);
  void safeReloadCollection(int collectionIndex) override;
  void forceRescanCollection(int collectionIndex) override;
  void onCollectionSelected(int collectionIndex);
  void onSubcollectionEntered(int subcollectionIndex);
  void onVirtualFolderEntered(const QString &folderPath) override;
  void goBackFromVirtualFolder() override;
  void loadCurrentAndSubcollections();
  void loadAllCollectionsView();
  // Renders the synthetic "Home" view: every root collection (parent == -1)
  // shown as a tile grid with no host collection of its own. No DB query;
  // tiles come from the in-memory collection list.
  void loadRootView() override;

  /// True when the synthetic Home view (one tile per root collection,
  /// currentCollectionIndex == -1) is currently active. Search routing
  /// uses this to detect "no collection selected" and divert to
  /// cross-collection search instead of no-op'ing.
  [[nodiscard]] bool isInRootView() const override { return m_inRootView; }
  void goBackToCollections();
  void filterItems(const QString &searchText) override;
  void filterItemsCurrentAndSubcollections(const QString &searchText) override;
  void filterItemsAllCollections(const QString &searchText) override;
  void invalidatePendingItemCount() override;
  void scheduleSelectionRestore(int desiredIndex, int finalEnsureDelayMs) override;
  void onItemsLoaded(const QStringList &filePaths, const QHash<QString, QString> &fileNames);
  void onItemCountLoaded(int count, int requestToken);
  void onBackgroundCollectionScanCompleted(const QString &collectionUuid);
  void onItemsRangeLoaded(int offset, const QStringList &filePaths,
                          const QHash<QString, QString> &fileNames,
                          const QHash<QString, QString> &fileToArtworkDir,
                          const QHash<QString, QString> &fileToMediaDir,
                          const QHash<QString, int> &fileToCollectionIndex);
  void fetchItemsRange(int offset, int limit);
  void onMediaLibraryError(const ErrorUtils::ErrorContext &error);
  void onViewportChanged();
  /// Handles clicks on breadcrumb links in the toolbar title
  void onBreadcrumbLinkClicked(const QString &link);

  // Appearance methods - can be called from SettingsManager after dialog closes
  void applyBackgroundForCollection(int collectionIndex) override;
  void applyPrimaryColorForCollection(int collectionIndex) override;

  /// Re-apply every per-collection derived colour for the given (active)
  /// collection after a runtime system-palette change. Bundles the same
  /// background + primary-colour + details-pane appearance pass used on a
  /// collection switch, then rebuilds the breadcrumb HTML and the per-item
  /// metadata rows (both bake the accent into a string, so a repaint alone
  /// won't refresh them). Called from MainWindow's ApplicationPaletteChange
  /// handler. No-op for an out-of-range index.
  void reapplyActiveCollectionTheming(int collectionIndex);

public slots:
  /// Kartend-2hzy: per-collection collectionBackgroundChanged receiver.
  /// Active-collection-guarded; non-active collections get the new
  /// CollectionBackground on the next switchCollection path.
  void onCollectionBackgroundChanged(int collectionIndex, const CollectionBackground &background);

  /// Kartend-2hzy: collectionFilterPreferencesChanged receiver. Refreshes
  /// the TitleFilter registry for all collections. Already covered inline
  /// in SettingsManager::saveCollections for the main paths, but this
  /// connect catches alternate paths (e.g. a future in-memory mutation
  /// site that emits the diff without going through saveCollections).
  void onCollectionFilterPreferencesChanged(int collectionIndex,
                                            const CollectionFilterPreferences &filter);

  /// Kartend-2hzy: folderBrowsingOptionsChanged receiver.
  /// includeContentSubfolders changes affect ScanService's scan signature;
  /// the user is expected to trigger a rescan to repopulate the DB.
  /// Per-paint reads in QueryManager already pick up the new values, so
  /// this slot is a diagnostic log breadcrumb until a force-rescan path
  /// lands as a follow-up.
  void onFolderBrowsingOptionsChanged(int collectionIndex,
                                      const FolderBrowsingOptions &folderBrowsing);

signals:
  /// Emitted when a media-library load fails. The owner (MainWindow) shows the
  /// error dialog — NavigationManager stays out of the UI-chrome layer.
  void mediaLibraryErrorRaised(const ErrorUtils::ErrorContext &error);

private:
  // Helper methods for navigateWithSharedItems
  auto initializeNavigationState() -> void;
  [[nodiscard]] auto validateAndPrepareNavigation(int collectionIndex) -> bool;
  auto handleSubcollectionNavigation(int collectionIndex, int previousIndex) -> void;
  auto handleRegularNavigation(int collectionIndex) -> void;
  auto finalizeNavigation(int collectionIndex) -> void;

  [[nodiscard]] auto areItemsShared(int fromIndex, int toIndex) const -> bool;
  // reloadBackground=false (Kartend-gzptz): skip the main-view + sidebar
  // background-image reload and only re-apply colours — for a colour-only
  // re-theme where the wallpaper can't have changed.
  auto applyCollectionSettingsOnly(int collectionIndex, bool reloadBackground = true) -> void;
  [[nodiscard]] auto getSubcollections(int parentIndex) const -> QList<int>;
  [[nodiscard]] auto getAllDescendantCollections(int parentIndex) const -> QList<int>;

  // ctx is the single source of truth for sibling managers + state.
  const ApplicationContext *m_ctx = nullptr;
  [[nodiscard]] IInteractionManager *interactionMgr() const {
    return m_ctx ? m_ctx->interactionManager() : nullptr;
  }
  [[nodiscard]] InteractionStateHolder *state() const {
    return m_ctx ? m_ctx->interactionState() : nullptr;
  }
  [[nodiscard]] ISettingsManager *settingsMgr() const {
    return m_ctx ? m_ctx->settingsManager() : nullptr;
  }
  [[nodiscard]] IDetailsPaneManager *detailsPaneMgr() const {
    return m_ctx ? m_ctx->detailsPaneManager() : nullptr;
  }
  // Kartend-h1l8f: keeps the IScrollManager facade — its partials span five
  // scroll roles (lifecycle, grid, search, data, overlay).
  [[nodiscard]] IScrollManager *scrollMgr() const {
    return m_ctx ? m_ctx->scrollManager() : nullptr;
  }
  [[nodiscard]] IDatabaseManager *databaseMgr() const {
    return m_ctx ? m_ctx->databaseManager() : nullptr;
  }
  [[nodiscard]] ISessionManager *sessionMgr() const {
    return m_ctx ? m_ctx->sessionManager() : nullptr;
  }
  [[nodiscard]] IArtworkManager *artworkMgr() const {
    return m_ctx ? m_ctx->artworkManager() : nullptr;
  }

  IDetailsPane *m_MetadataSidebar = nullptr;
  int *m_currentCollectionIndex = nullptr;
  const CollectionHierarchyCache *m_hierarchyCache = nullptr;
  GeneralSettings *m_generalSettings = nullptr;
  QLineEdit *m_searchBar = nullptr;

  // Filter text used for the current DB-backed items view (count + paginated
  // ranges). This keeps itemCount and itemsRange queries consistent even if the
  // UI search bar text changes between the count request and subsequent range
  // fetches.
  QString m_itemsQueryFilter;

  // Monotonic token for count requests; used to ignore stale itemCount
  // responses.
  int m_itemCountRequestToken = 0;
  QWidget *m_itemsPage = nullptr;
  QWidget *m_itemsTopBar = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  QMenuBar *m_menubar = nullptr;
  EmptyStateWidget *m_loadingLabel = nullptr;
  LoadingOverlay *m_loadingOverlay = nullptr;
  QScrollArea *m_itemScrollArea = nullptr;
  QWidget *m_gridContainer = nullptr;
  // Recreated on each onMediaLibraryError() call; QPointer because the
  // Qt parent (m_gridContainer) owns it and may delete it during teardown.
  QPointer<QWidget> m_errorWidget;
  // Owns the items-page background render (video / image / color bg, header
  // logo, vignette, toolbar blur, wallpaper parallax) and the primary-color
  // toolbar/menubar/search-bar theming. applyBackgroundForCollection and
  // applyPrimaryColorForCollection delegate straight to it.
  std::unique_ptr<CollectionBackgroundController> m_backgroundController;
  std::function<bool()> m_isShuttingDown;
  std::function<void()> m_refreshTitleCounts;
  /// Kartend-w2n0: invoked from updateItemsPageTitle so the items-toolbar
  /// warning badge re-resolves launcher paths on every collection switch.
  /// Optional — null in test setups.
  std::function<void()> m_refreshCollectionWarningBadge;

  // Owned manager for selection restore logic
  std::unique_ptr<SelectionRestoreCoordinator> m_selectionRestoreManager;

  bool m_virtualScrollConnected = false;
  [[nodiscard]] auto collectionHasDescendantWithMedia(int parentIndex) const -> bool;
  bool m_allCollectionsActive = false;
  auto setSuppressArrowCenter(QScrollArea *scrollArea, int settleMs) -> void;
  [[nodiscard]] auto getHasSubAndItems(int collectionIndex, bool &hasSub, bool &hasItems) const
      -> bool;
  auto updateItemsPageTitle(int collectionIndex) -> void;
  // Helper methods for goBackToCollections refactoring
  auto performNavigationStackCleanup() -> void;
  auto handleNavigationStackPop() -> void;
  auto handleNavigationFallback() -> void;
  [[nodiscard]] auto findSubcollectionVisualIndex(int targetCollectionIndex,
                                                  int previousIndex) const -> int;
  auto scheduleNavigationReturn(int targetCollectionIndex, int subcollectionVisualIndex) -> void;
  // Helper methods for onItemsLoaded refactoring
  [[nodiscard]] auto validateItemsLoadedContext() const -> bool;
  auto cleanupExistingNoItemsWidgets() -> void;
  [[nodiscard]] auto determineContentAvailability(const QStringList &filePaths,
                                                  const QList<int> &subcollections) const -> bool;
  auto handleEmptyContent() -> void;
  [[nodiscard]] auto setupCollectionContext(const QStringList &filePaths,
                                            const QHash<QString, QString> &fileNames) const
      -> CollectionContext;
  [[nodiscard]] auto lookupRememberedSelectionIndex(int totalItems) const -> int;
  [[nodiscard]] auto calculateSelectionIndex(int totalItems) const -> int;
  [[nodiscard]] auto computeCollectionDepth(int collectionIndex) const -> int;
  auto schedulePostLoadOperations() -> void;
  // Helper methods for showCollectionItems refactoring
  [[nodiscard]] auto validateCollectionIndex(int collectionIndex) const -> bool;
  [[nodiscard]] auto handleSharedItemsNavigation(int collectionIndex) -> bool;
  auto prepareNonSharedNavigation(int collectionIndex) -> void;
  auto loadCollectionData(int collectionIndex) -> void;

  // Attempts fast startup using cached counts to render immediately.
  // Returns true if cached count was used for immediate rendering.
  [[nodiscard]] auto tryUseCachedCountForStartup(const CollectionContext &context) -> bool;

  [[nodiscard]] CollectionContext buildExpandedContextForIndex(int collectionIndex) const;
  [[nodiscard]] CollectionContext getOrBuildExpandedContext(int collectionIndex);
  void requestItemCountForContext(const CollectionContext &context, const QString &filter);

  void persistCurrentSelection();
  void prepareForNonSharedNavigationHelper();
  void suspendItemsPageRendering();
  void resumeItemsPageRendering();
  void applyUiPoliciesForCollection(int collectionIndex);

  QList<CollectionConfig> *m_collections = nullptr;
  bool m_isRescanInProgress = false;       // Track when force rescan is active
  int m_pendingRescanCollectionIndex = -1; // Collection to reload after cache invalidation
  QMetaObject::Connection m_cacheInvalidatedConnection; // One-shot connection for cache
                                                        // invalidation

  bool m_backgroundCountRefreshInProgress = false;
  int m_backgroundCountRefreshCollectionIndex = -1;

  // Set while the synthetic "Home" view (all root collections as tiles) is
  // active. While true, *m_currentCollectionIndex is -1 and Back is a no-op.
  bool m_inRootView = false;

  // Tracks if this is the first load after startup (for cached count fast-path)
  bool m_isInitialStartupLoad = true;

  // When navigating between virtual subfolders, we want the next items view
  // rebuild to start from the top (avoids restoring a stale selection index
  // from a different folder scope).
  bool m_forceTopOnNextItemsViewLoad = false;

  // Guards against stale range-load results arriving after the items view
  // has been rebuilt (e.g., search text changed). Offset->generation.
  quint64 m_itemsViewGeneration = 0;
  QHash<int, quint64> m_pendingRangeGenerations;

  // Debounced reload state for safeReloadCollection().
  // safeReloadCollection() is invoked from multiple UI entry points (virtual
  // folder navigation, settings changes, search clear). If it fires repeatedly
  // in quick succession, the repeated cleanup + delayed reload can result in
  // a persistent blank grid and high CPU from churn.
  QTimer m_safeReloadTimer; // Kartend-a911.5: value member
  bool m_safeReloadTimerInited = false;
  int m_pendingSafeReloadCollectionIndex = -1;

  // Active query context for the current items view (count + on-demand ranges).
  // This decouples DB query scope from the current collection's config.
  bool m_hasItemsQueryContext = false;
  CollectionContext m_itemsQueryContext;

  // Cached expanded context for the current collection to avoid recomputing
  // precomputed descendants, UUIDs, and directory maps on every search
  // keystroke. Invalidated when collection changes.
  int m_cachedExpandedContextIndex = -1;
  CollectionContext m_cachedExpandedContext;

  // Memoizes getHasSubAndItems() so route resolution doesn't re-stat media
  // directories on the GUI thread on every navigation (Kartend-motei). Keyed by
  // collection index -> (hasSub, hasItems); the whole map is dropped on
  // collectionsModified (collection add/remove/reorder/config edit), the same
  // trigger that rebuilds the hierarchy cache, since that's when the FS layout
  // and index keys can change.
  mutable QHash<int, std::pair<bool, bool>> m_hasSubAndItemsCache;
  QMetaObject::Connection m_collectionsModifiedConn;
};

#endif