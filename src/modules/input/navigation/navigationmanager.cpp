// Manages collection switching, navigation stack, and subcollection hierarchy
// traversal.
#include "navigationmanager.h"
#include "artworkutils.h"
#include "collectionbackgroundcontroller.h"
#include "iartworkmanager.h"
#include "idatabasemanager.h"
#include "idetailspane.h"
#include "idetailspanemanager.h"
#include "interactionmanager.h"
#include "interactionstateholder.h"
#include "isessionmanager.h"
#include "isettingsmanager.h"
#include "loadingoverlay.h"
#include "loggingcategories.h"
#include "navigationstackmanager.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "selectionrestoremanager.h"
#include "settingsutils.h"
#include "timerutils.h"
#include "uiconstants/keyboard.h"
#include "uiconstants/timing.h"
#include <algorithm>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QtGlobal>
#include <QTimer>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcNavigationManager, "kartend.navigationmanager")
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcNavigationManager().isDebugEnabled()) {                                                  \
      qCDebug(lcNavigationManager) << msg;                                                         \
    }                                                                                              \
  } while (0)

NavigationManager::NavigationManager(QObject *parent)
    : QObject(parent), m_stackManager(std::make_unique<NavigationStackManager>(this)),
      m_backgroundController(std::make_unique<CollectionBackgroundController>(this)),
      m_selectionRestoreManager(std::make_unique<SelectionRestoreManager>(this)) {}

NavigationManager::~NavigationManager() = default;

void NavigationManager::setupReferences(const NavigationManagerSetup &setup) {
  m_ctx = setup.ctx;

  // UI elements - use accessors that check context fallback
  m_MetadataSidebar = setup.getSidebar();
  m_currentCollectionIndex = setup.getCurrentCollectionIndex();
  m_collections = setup.getCollections();
  m_hierarchyCache = setup.getHierarchyCache();
  m_generalSettings = setup.getGeneralSettings();
  m_searchBar = setup.getSearchBar();
  m_itemsPage = setup.getItemsPage();
  m_itemsTopBar = setup.getItemsTopBar();
  m_stackedWidget = setup.getStackedWidget();
  m_menubar = setup.getMenubar();
  m_loadingLabel = setup.getLoadingLabel();
  m_loadingOverlay = setup.getLoadingOverlay();
  m_itemScrollArea = setup.getItemScrollArea();
  m_gridContainer = setup.getGridContainer();

  // Callbacks
  m_isShuttingDown = setup.isShuttingDown;
  m_refreshTitleCounts = setup.refreshTitleCounts;

  // Setup SelectionRestoreManager. Sibling managers come from ctx.
  if (m_selectionRestoreManager) {
    SelectionRestoreManagerSetup restoreSetup;
    restoreSetup.ctx = setup.ctx;
    restoreSetup.isShuttingDown = m_isShuttingDown;
    m_selectionRestoreManager->setupReferences(restoreSetup);
  }

  // Setup the background controller with the borrowed UI surface it drives.
  if (m_backgroundController) {
    CollectionBackgroundControllerSetup bgSetup;
    bgSetup.itemScrollArea = m_itemScrollArea;
    bgSetup.itemsTopBar = m_itemsTopBar;
    bgSetup.menubar = m_menubar;
    bgSetup.searchBar = m_searchBar;
    bgSetup.currentCollectionIndex = m_currentCollectionIndex;
    bgSetup.collections = m_collections;
    m_backgroundController->setupReferences(bgSetup);
  }
}

bool NavigationManager::isNavigationInProgress() const {
  return m_stackManager && m_stackManager->isInProgress();
}

void NavigationManager::prepareForShutdown() {
  persistCurrentSelection();
}

// Navigates to a subcollection using the shared parent view
void NavigationManager::navigateWithSharedItems(int collectionIndex) {
  if (!parent()) {
    return;
  }

  initializeNavigationState();

  if (!validateAndPrepareNavigation(collectionIndex)) {
    return;
  }

  int previousIndex = (*m_currentCollectionIndex);

  if (interactionMgr()) {
    interactionMgr()->clearSelectionAndFocus();
  }
  if (m_MetadataSidebar) {
    m_MetadataSidebar->clearMetadata();
  }

  (*m_currentCollectionIndex) = collectionIndex;

  if (interactionMgr()) {
    interactionMgr()->initializeSearchModeForCurrentCollection();
  }

  updateItemsPageTitle(collectionIndex);

  bool isNavigatingToSubcollection =
      ((*m_collections)[collectionIndex].parentCollectionIndex == previousIndex);

  if (isNavigatingToSubcollection) {
    handleSubcollectionNavigation(collectionIndex, previousIndex);
  } else {
    handleRegularNavigation(collectionIndex);
  }

  finalizeNavigation(collectionIndex);
}

auto NavigationManager::initializeNavigationState() -> void {
  const bool isStartupNavigation = (m_currentCollectionIndex && (*m_currentCollectionIndex) < 0);

  // Show loading overlay during navigation (will be updated with collection
  // name later). On startup, keep the UI immediately navigable while background
  // scanning runs.
  if (!isStartupNavigation) {
    if (m_loadingOverlay) {
      m_loadingOverlay->show("Preparing...");
    }
  }

  if ((parent()) && (interactionMgr())) {
    interactionMgr()->stopRepeat();
    if (!isStartupNavigation) {
      interactionMgr()->setNavigationInProgress(true);
    }
    if (state()) {
      state()->click().rowChangeFirstClickIndex = -1;
      state()->click().rowChangeFirstClickMs = 0;
      state()->click().doubleClickPending = false;
      state()->click().doubleClickPendingIndex = -1;
      state()->click().clickDeferralActive = false;
      state()->click().clickDeferralIndex = -1;
      state()->click().deferCenterOnClick = false;
      state()->click().deferredCenterIndex = -1;
      state()->click().selectionSuppressed = false;
      state()->click().pendingSelectionIndex = -1;
    }
  }
  // Reset restore state for new navigation - clears any pending restores
  // and allows automatic restore to proceed (userSelectionMade = false)
  if (interactionMgr()) {
    interactionMgr()->resetSelectionRestoreState();
  }
}

auto NavigationManager::showCollectionItems(int collectionIndex) -> bool {
  if (!validateCollectionIndex(collectionIndex)) {
    return false;
  }

  // Leaving the synthetic Home view: tile-click on a root collection routes
  // through here, so clear the flag before the regular collection-load path
  // re-establishes a real currentCollectionIndex.
  m_inRootView = false;

  // Invalidate cached expanded context when changing collections
  m_cachedExpandedContextIndex = -1;

  // Clear artwork directory cache when changing collections to ensure
  // fresh filesystem data and prevent stale cache entries
  ArtworkUtils::clearDirectoryCache();

  // Clear initial startup flag after any navigation
  // (fast path only applies to very first load)
  if (*m_currentCollectionIndex >= 0) {
    m_isInitialStartupLoad = false;
  }

  if ((parent()) && (interactionMgr())) {
    interactionMgr()->stopRepeat();
    // Only cancel pending restore when navigating FROM an existing collection
    // On initial startup (*m_currentCollectionIndex < 0), allow restore to
    // proceed
    if (*m_currentCollectionIndex >= 0) {
      interactionMgr()->cancelPendingSelectionRestore();
    }
  }

  persistCurrentSelection();

  if (handleSharedItemsNavigation(collectionIndex)) {
    return true;
  }

  prepareNonSharedNavigation(collectionIndex);
  loadCollectionData(collectionIndex);
  if (m_refreshTitleCounts) m_refreshTitleCounts();
  return true;
}

// Temporarily suppresses arrow-key centering for a scroll area using
// InteractionStateHolder and time window
auto NavigationManager::setSuppressArrowCenter(QScrollArea *scrollArea, int settleMs) -> void {
  if (!scrollArea || !state()) {
    return;
  }
  state()->suppressArrowCenterFor(settleMs);
  // Clear arrow center suppression after settle period expires -
  // allows navigation animations to complete before centering resumes
  QTimer::singleShot(UIConstants::Keyboard::ARROW_CENTER_CLEAR_AFTER_SET_MS, this, [this]() {
    if (state()) {
      state()->clearArrowCenterSuppression();
    }
  });
}

auto NavigationManager::areItemsShared(int fromIndex, int toIndex) const -> bool {
  if (fromIndex < 0 || toIndex < 0 || fromIndex >= (*m_collections).size() ||
      toIndex >= (*m_collections).size()) {
    return false;
  }

  const auto &fromCollection = (*m_collections)[fromIndex];
  const auto &toCollection = (*m_collections)[toIndex];

  bool isDirectChild = (toCollection.parentCollectionIndex == fromIndex);
  if (!isDirectChild) {
    return false;
  }

  bool parentShowsAll = fromCollection.showAllSubcollectionItems;
  if (!parentShowsAll) {
    return false;
  }

  // Shared-items navigation filters the parent's in-memory file list to show
  // only the target subcollection. With on-demand virtual scrolling
  // (showAllSubcollectionItems no longer eagerly loads all items), the
  // parent's file list is mostly empty placeholders, causing the filtered
  // child view to silently truncate at whatever offset has been loaded.
  // Until the optimization can guarantee a fully materialized parent list,
  // fall through to the standard DB count + on-demand range pipeline, which
  // is fast (~10ms per page in practice). See bd.
  return false;
}

// Performs common navigation cleanup operations
void NavigationManager::onViewportChanged() {
  if ((interactionMgr()) && interactionMgr()->isWheelScrolling() && m_stackedWidget &&
      m_stackedWidget->currentWidget() == m_itemsPage) {
    // Delay viewport update during wheel scrolling to batch rapid events -
    // reduces CPU load from frequent artwork updates during fast scrolling
    QTimer::singleShot(UIConstants::Timing::SHORT_DELAY_MS, this, [this]() {
      if (scrollMgr()) {
        scrollMgr()->updateVirtualView();
      }
      if (artworkMgr()) {
        artworkMgr()->updateViewportArtwork();
      }
    });
  } else {
    if (artworkMgr()) {
      if (auto *coord = artworkMgr()->getTimerCoordinator()) {
        coord->scheduleViewportUpdate();
      }
    }
  }
}

// Returns whether the collection has direct items or any subcollections and
// outputs both flags
auto NavigationManager::getHasSubAndItems(int collectionIndex, bool &hasSub, bool &hasItems) const
    -> bool {
  hasSub = false;
  hasItems = false;
  if ((!parent()) || collectionIndex < 0 || collectionIndex >= (*m_collections).size()) {
    return false;
  }

  const CollectionConfig &collection = (*m_collections)[collectionIndex];

  for (const auto &coll : (*m_collections)) {
    if (coll.parentCollectionIndex == collectionIndex) {
      hasSub = true;
      break;
    }
  }

  QString mediaDir =
      (settingsMgr())
          ? SettingsUtils::expandConfigVariables(collection.mediaDirectory, collection.name)
          : collection.mediaDirectory;
  if (!mediaDir.trimmed().isEmpty()) {
    QDir dir(mediaDir);
    if (dir.exists()) {
      QStringList filters = collection.extensions.isEmpty() ? QStringList() : collection.extensions;
      QStringList files =
          filters.isEmpty() ? dir.entryList(QDir::Files) : dir.entryList(filters, QDir::Files);
      hasItems = !files.isEmpty();

      // When showAllSubfolderItems is true, also check subdirectories for files
      // This handles cases where the root mediaDirectory has no files but
      // subdirs do
      if (!hasItems && collection.folderBrowsing.includeContentSubfolders &&
          collection.folderBrowsing.showAllSubfolderItems) {
        QDir::Filters dirFilters = QDir::Dirs | QDir::NoDotAndDotDot;
        if (collection.folderBrowsing.showHiddenFolders) {
          dirFilters |= QDir::Hidden;
        }
        QStringList subdirs = dir.entryList(dirFilters);
        for (const QString &subdir : subdirs) {
          QDir subDir(dir.filePath(subdir));
          QStringList subFiles = filters.isEmpty() ? subDir.entryList(QDir::Files)
                                                   : subDir.entryList(filters, QDir::Files);
          if (!subFiles.isEmpty()) {
            hasItems = true;
            break;
          }
        }
      }

      // Also check for virtual folders (subdirectories as navigable content)
      // when includeContentSubfolders is enabled and showAllSubfolderItems is
      // false
      if (!hasItems && collection.folderBrowsing.includeContentSubfolders &&
          !collection.folderBrowsing.showAllSubfolderItems) {
        QDir::Filters dirFilters = QDir::Dirs | QDir::NoDotAndDotDot;
        if (collection.folderBrowsing.showHiddenFolders) {
          dirFilters |= QDir::Hidden;
        }
        hasItems = !dir.entryList(dirFilters).isEmpty();
      }
    }
  }

  return (hasItems || hasSub);
}

// Returns the direct child subcollection indices for a parent collection
auto NavigationManager::getSubcollections(int parentIndex) const -> QList<int> {
  // Use cache for O(1) lookup if available
  if (m_hierarchyCache && m_hierarchyCache->isValid()) {
    return m_hierarchyCache->directChildren(parentIndex);
  }
  // Fallback to O(n) scan
  if (!m_collections) {
    return {};
  }
  return CollectionUtils::directChildrenOf(parentIndex, *m_collections);
}

// Sets the collections pointer used for subcollection queries
auto NavigationManager::getAllDescendantCollections(int parentIndex) const -> QList<int> {
  // Use cache for O(1) lookup if available
  if (m_hierarchyCache && m_hierarchyCache->isValid()) {
    return m_hierarchyCache->allDescendants(parentIndex);
  }
  // Fallback to O(n) recursive scan
  QList<int> result;
  if ((!m_collections) || parentIndex < 0 || parentIndex >= m_collections->size()) {
    return result;
  }

  QList<int> stack = CollectionUtils::directChildrenOf(parentIndex, *m_collections);
  QSet<int> seen;
  while (!stack.isEmpty()) {
    int idx = stack.takeLast();
    if (!CollectionUtils::isValidIndex(idx, m_collections)) {
      continue;
    }
    if (seen.contains(idx)) {
      continue;
    }
    seen.insert(idx);
    result.append(idx);

    const QList<int> children = CollectionUtils::directChildrenOf(idx, *m_collections);
    for (int childIdx : children) {
      if (!seen.contains(childIdx)) {
        stack.append(childIdx);
      }
    }
  }
  return result;
}

void NavigationManager::prepareForNonSharedNavigationHelper() {
  if (interactionMgr()) {
    interactionMgr()->clearSelectionAndFocus();
  }
  if (m_MetadataSidebar) {
    m_MetadataSidebar->clearMetadata();
  }
  artworkMgr()->stopSilentLoading();
  if (artworkMgr()->getTimerCoordinator()) {
    artworkMgr()->getTimerCoordinator()->stopAllTimers();
  }
  if (scrollMgr()) {
    scrollMgr()->cleanup();
  }
}

void NavigationManager::suspendItemsPageRendering() {
  if (m_itemScrollArea) {
    m_itemScrollArea->setUpdatesEnabled(false);
  }
  if (state()) {
    state()->artwork().suppressArtwork = true;
  }
  if (m_gridContainer) {
    m_gridContainer->setUpdatesEnabled(false);
    m_gridContainer->setVisible(false);
  }
}

void NavigationManager::resumeItemsPageRendering() {
  if (m_gridContainer) {
    m_gridContainer->setVisible(true);
    m_gridContainer->setUpdatesEnabled(true);
  }
  if (m_itemScrollArea) {
    m_itemScrollArea->setUpdatesEnabled(true);
  }
  if (state()) {
    state()->artwork().suppressArtwork = false;
  }
}
