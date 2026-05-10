#ifndef NAVIGATIONMANAGER_H
#define NAVIGATIONMANAGER_H

#include "applicationcontext.h"
#include "collectionutils.h"
#include "errorutils.h"
#include "setuputils.h"
#include <functional>
#include <memory>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QMenuBar;
class QScrollArea;
class QStackedWidget;
QT_END_NAMESPACE

class InteractionManager;
class InteractionStateHolder;
class ISettingsManager;
class DetailsPaneManager;
class ScrollManager;
class IDatabaseManager;
class SessionManager;
class ArtworkManager;
class DetailsPane;
class SelectionRestoreManager;
class LoadingOverlay;
class EmptyStateWidget;
class NavigationStackManager;
class BackgroundVideoWidget;
class BackdropBlurOverlay;
class HeaderLogoOverlay;
class VignetteOverlay;

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
  DetailsPane *sidebar = nullptr;
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
  SETUP_GETTER_INLINE_UI_SAME(DetailsPane *, Sidebar, sidebar)
  SETUP_GETTER_INLINE_COL_SAME(QList<CollectionConfig> *, Collections, collections)
  SETUP_GETTER_INLINE_COL_SAME(int *, CurrentCollectionIndex, currentCollectionIndex)
  SETUP_GETTER_INLINE_COL_SAME(const CollectionHierarchyCache *, HierarchyCache, hierarchyCache)
  SETUP_GETTER_INLINE_COL_SAME(GeneralSettings *, GeneralSettings, generalSettings)
};

class NavigationManager : public QObject {
  Q_OBJECT
public:
  explicit NavigationManager(QObject *parent = nullptr);
  ~NavigationManager() override;
  [[nodiscard]] bool isNavigationInProgress() const;

  // Persist current viewport/selection state before shutdown.
  // Call this before blocking signals or clearing collection index.
  void prepareForShutdown();

  // Navigation stack manager for hierarchy traversal
  [[nodiscard]] NavigationStackManager *stackManager() const { return m_stackManager.get(); }

private:
  std::unique_ptr<NavigationStackManager> m_stackManager;

public:
  void restoreSelectionForCurrentCollection();
  void setupReferences(const NavigationManagerSetup &setup);

public slots:
  auto showCollectionItems(int collectionIndex) -> bool;
  void navigateWithSharedItems(int collectionIndex);
  void safeReloadCollection(int collectionIndex);
  void forceRescanCollection(int collectionIndex);
  void onCollectionSelected(int collectionIndex);
  void onSubcollectionEntered(int subcollectionIndex);
  void onVirtualFolderEntered(const QString &folderPath);
  void goBackFromVirtualFolder();
  void loadCurrentAndSubcollections();
  void loadAllCollectionsView();
  // Renders the synthetic "Home" view: every root collection (parent == -1)
  // shown as a tile grid with no host collection of its own. No DB query;
  // tiles come from the in-memory collection list.
  void loadRootView();

  /// True when the synthetic Home view (one tile per root collection,
  /// currentCollectionIndex == -1) is currently active. Search routing
  /// uses this to detect "no collection selected" and divert to
  /// cross-collection search instead of no-op'ing.
  [[nodiscard]] bool isInRootView() const { return m_inRootView; }
  void goBackToCollections();
  void filterItems(const QString &searchText);
  void filterItemsCurrentAndSubcollections(const QString &searchText);
  void filterItemsAllCollections(const QString &searchText);
  auto scheduleSelectionRestore(int desiredIndex, int maxAttempts, int attemptDelayMs,
                                int finalEnsureDelayMs) -> void;
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
  void applyBackgroundForCollection(int collectionIndex);
  void applyPrimaryColorForCollection(int collectionIndex);

private:
  // Helper methods for navigateWithSharedItems
  auto initializeNavigationState() -> void;
  [[nodiscard]] auto validateAndPrepareNavigation(int collectionIndex) -> bool;
  auto handleSubcollectionNavigation(int collectionIndex, int previousIndex) -> void;
  auto handleRegularNavigation(int collectionIndex) -> void;
  auto finalizeNavigation(int collectionIndex) -> void;

  [[nodiscard]] auto areItemsShared(int fromIndex, int toIndex) const -> bool;
  auto applyCollectionSettingsOnly(int collectionIndex) -> void;
  [[nodiscard]] auto getSubcollections(int parentIndex) const -> QList<int>;
  [[nodiscard]] auto getAllDescendantCollections(int parentIndex) const -> QList<int>;

  // ctx is the single source of truth for sibling managers + state.
  const ApplicationContext *m_ctx = nullptr;
  [[nodiscard]] InteractionManager *interactionMgr() const {
    return m_ctx ? m_ctx->interactionManager() : nullptr;
  }
  [[nodiscard]] InteractionStateHolder *state() const {
    return m_ctx ? m_ctx->interactionState() : nullptr;
  }
  [[nodiscard]] ISettingsManager *settingsMgr() const {
    return m_ctx ? m_ctx->settingsManager() : nullptr;
  }
  [[nodiscard]] DetailsPaneManager *detailsPaneMgr() const {
    return m_ctx ? m_ctx->detailsPaneManager() : nullptr;
  }
  [[nodiscard]] ScrollManager *scrollMgr() const {
    return m_ctx ? m_ctx->scrollManager() : nullptr;
  }
  [[nodiscard]] IDatabaseManager *databaseMgr() const {
    return m_ctx ? m_ctx->databaseManager() : nullptr;
  }
  [[nodiscard]] SessionManager *sessionMgr() const {
    return m_ctx ? m_ctx->sessionManager() : nullptr;
  }
  [[nodiscard]] ArtworkManager *artworkMgr() const {
    return m_ctx ? m_ctx->artworkManager() : nullptr;
  }

  DetailsPane *m_MetadataSidebar = nullptr;
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
  // lazy-created on first collection that requests a video
  // background. Parented to the items viewport so its lifetime mirrors the
  // scroll area's. Hidden + decoder-released when the active collection
  // doesn't use video, so collections without one pay zero playback cost.
  BackgroundVideoWidget *m_backgroundVideo = nullptr;
  // lazy-created header logo overlay; same lifetime/parenting
  // pattern as m_backgroundVideo. Hidden when the active collection has no
  // logo configured.
  HeaderLogoOverlay *m_headerLogo = nullptr;
  // lazy-created vignette overlay layered above the logo so
  // the logo isn't darkened along with the corners.
  VignetteOverlay *m_vignette = nullptr;
  // lazy-created toolbar backdrop blur. Hidden whenever the
  // active collection has the toggle off or uses a video bg (sampling
  // frames every paint is too expensive without GPU shaders).
  BackdropBlurOverlay *m_toolbarBlur = nullptr;
  // throttle for parallax stylesheet rebuilds. Lazy-created
  // on first scroll while parallax is active. Caps QSS updates at ~60Hz
  // so a fast scroll doesn't thrash Qt's stylesheet engine.
  QTimer *m_parallaxThrottle = nullptr;
  bool m_scrollListenerConnected = false;
  // Helpers exposed via the .cpp.
  void onItemsScrolled();
  void applyParallaxOffset();
  std::function<bool()> m_isShuttingDown;
  std::function<void()> m_refreshTitleCounts;

  // Owned manager for selection restore logic
  std::unique_ptr<SelectionRestoreManager> m_selectionRestoreManager;

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
  QTimer *m_safeReloadTimer = nullptr;
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
};

#endif