// Manages collection switching, navigation stack, and subcollection hierarchy traversal.
#include "navigationmanager.h"
#include "artworkmanager.h"
#include "databasemanager.h"
#include "errordialog.h"
#include "interactionmanager.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "loadingoverlay.h"
#include "metadatasidebar.h"
#include "navigationstackmanager.h"
#include "scrollmanager.h"
#include "selectionrestoremanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "pathutils.h"
#include "sidebarmanager.h"
#include "timerutils.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QLabel>
#include <QScrollBar>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <algorithm>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcNavigationManager, "kartend.navigationmanager")
#define debugLog(msg) do { if (lcNavigationManager().isDebugEnabled()) { qCDebug(lcNavigationManager) << msg; } } while (0)

NavigationManager::NavigationManager(QObject *parent)
    : QObject(parent),
      m_stackManager(std::make_unique<NavigationStackManager>(this)),
      m_selectionRestoreManager(std::make_unique<SelectionRestoreManager>(this)) {}

void NavigationManager::setupReferences(
    const NavigationManagerSetup &setup) {
  // Manager dependencies - use accessors that check context fallback
  m_interactionManager = setup.getInteractionManager();
  m_state = setup.getInteractionState();
  m_settingsManager = setup.getSettingsManager();
  m_sidebarManager = setup.getSidebarManager();
  m_scrollManager = setup.getScrollManager();
  m_databaseManager = setup.getDatabaseManager();
  m_sessionManager = setup.getSessionManager();
  m_artworkManager = setup.getArtworkManager();
  
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

  // Setup SelectionRestoreManager
  if (m_selectionRestoreManager) {
    SelectionRestoreManagerSetup restoreSetup;
    restoreSetup.ctx = setup.ctx;
    restoreSetup.interactionManager = m_interactionManager;
    restoreSetup.scrollManager = m_scrollManager;
    restoreSetup.sessionManager = m_sessionManager;
    restoreSetup.settingsManager = m_settingsManager;
    restoreSetup.isShuttingDown = m_isShuttingDown;
    m_selectionRestoreManager->setupReferences(restoreSetup);
  }
}

NavigationManager::~NavigationManager() = default;

bool NavigationManager::isNavigationInProgress() const {
  return m_stackManager && m_stackManager->isInProgress();
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

  if (m_interactionManager) {
    m_interactionManager->clearSelectionAndFocus();
  }
  if (m_MetadataSidebar) {
    m_MetadataSidebar->clearMetadata();
  }

  (*m_currentCollectionIndex) = collectionIndex;

  if (m_interactionManager) {
    m_interactionManager
        ->initializeSearchModeForCurrentCollection();
  }

  updateItemsPageTitle(collectionIndex);

  bool isNavigatingToSubcollection =
      ((*m_collections)[collectionIndex].parentCollectionIndex ==
       previousIndex);

  if (isNavigatingToSubcollection) {
    handleSubcollectionNavigation(collectionIndex, previousIndex);
  } else {
    handleRegularNavigation(collectionIndex);
  }

  finalizeNavigation(collectionIndex);
}

auto NavigationManager::initializeNavigationState() -> void {
  const bool isStartupNavigation = (m_currentCollectionIndex && (*m_currentCollectionIndex) < 0);

  // Show loading overlay during navigation (will be updated with collection name later).
  // On startup, keep the UI immediately navigable while background scanning runs.
  if (!isStartupNavigation) {
    if (m_loadingOverlay) {
      m_loadingOverlay->show("Preparing...");
    }
  }
  
  if ((parent()) &&
      (m_interactionManager)) {
    m_interactionManager->stopRepeat();
    if (!isStartupNavigation) {
      m_interactionManager->setNavigationInProgress(true);
    }
    if (m_state) {
      m_state->click().rowChangeFirstClickIndex = -1;
      m_state->click().rowChangeFirstClickMs = 0;
      m_state->click().doubleClickPending = false;
      m_state->click().doubleClickPendingIndex = -1;
      m_state->click().clickDeferralActive = false;
      m_state->click().clickDeferralIndex = -1;
      m_state->click().deferCenterOnClick = false;
      m_state->click().deferredCenterIndex = -1;
      m_state->click().selectionSuppressed = false;
      m_state->click().pendingSelectionIndex = -1;
    }
  }
  // Reset restore state for new navigation - clears any pending restores
  // and allows automatic restore to proceed (userSelectionMade = false)
  if (m_interactionManager) {
    m_interactionManager->resetSelectionRestoreState();
  }
}

auto NavigationManager::validateAndPrepareNavigation(int collectionIndex)
    -> bool {
  if (collectionIndex < 0 ||
      collectionIndex >= (*m_collections).size()) {
    return false;
  }

  // Update loading overlay with collection name for better UX
  if (m_loadingOverlay && m_collections) {
    const QString &collectionName = (*m_collections)[collectionIndex].name;
    m_loadingOverlay->setMessage(QString("Loading %1...").arg(collectionName));
  }

  bool hasSub = false;
  bool hasItems = false;
  if (!getHasSubAndItems(collectionIndex, hasSub, hasItems)) {
    if ((parent()) &&
        (m_interactionManager)) {
      // Clear navigation progress flag early when validation fails -
      // prevents navigation from being blocked indefinitely
      QTimer::singleShot(
          UIConstants::Navigation::PROGRESS_CLEAR_EARLY_MS, this,
          [this]() {
            if (parent() && m_interactionManager) {
              m_interactionManager->setNavigationInProgress(false);
            }
          });
    }
    return false;
  }
  return true;
}

auto NavigationManager::handleSubcollectionNavigation(int collectionIndex,
                                                      int previousIndex)
    -> void {
  Q_UNUSED(previousIndex)
  if (m_scrollManager) {
    m_scrollManager->updateContextForSubcollection(
        collectionIndex);
    m_scrollManager->applySubcollectionFilter(collectionIndex);
  }

  // Delegate selection restore to SelectionRestoreManager
  if (m_selectionRestoreManager) {
    m_selectionRestoreManager->handleSubcollectionRestore(collectionIndex);
  }
}

auto NavigationManager::handleRegularNavigation(int collectionIndex) -> void {
  if (m_scrollManager) {
    m_scrollManager->clearFilter();
  }
}

auto NavigationManager::finalizeNavigation(int collectionIndex) -> void {
  applyCollectionSettingsOnly(collectionIndex);
  if ((m_searchBar) &&
      m_searchBar->text().trimmed().isEmpty()) {
    m_searchBar->clear();
  }
  // Delay horizontal centering until layout has settled after navigation
  QTimer::singleShot(UIConstants::Timing::MEDIUM_DELAY_MS, this, [this]() {
    if (m_scrollManager && m_currentCollectionIndex && m_collections) {
      m_scrollManager->centerHorizontalScrollbar(
          (*m_currentCollectionIndex), (*m_collections));
    }
  });
  if (m_refreshTitleCounts) m_refreshTitleCounts();

  // Hide loading overlay after navigation completes
  if (m_loadingOverlay) {
    m_loadingOverlay->hide();
  }

  // Clear navigation progress flag after all animations complete -
  // allows user input to be processed again
  QTimer::singleShot(
      UIConstants::Navigation::PROGRESS_CLEAR_MS, this, [this]() {
        if (parent() && m_interactionManager) {
          m_interactionManager->setNavigationInProgress(false);
        }
      });
}

// Delegates to SelectionRestoreManager for selection restoration
auto NavigationManager::scheduleSelectionRestore(int desiredIndex,
                                                 int maxAttempts,
                                                 int attemptDelayMs,
                                                 int finalEnsureDelayMs)
    -> void {
  if (m_selectionRestoreManager) {
    m_selectionRestoreManager->scheduleSelectionRestore(
        desiredIndex, maxAttempts, attemptDelayMs, finalEnsureDelayMs);
  }
}

// Validates collection index for showCollectionItems operation
auto NavigationManager::validateCollectionIndex(int collectionIndex) const
    -> bool {
  if (!parent() || !m_collections) {
    return false;
  }
  if (collectionIndex < 0 ||
      collectionIndex >= (*m_collections).size()) {
    return false;
  }

  bool hasSub = false;
  bool hasItems = false;
  return getHasSubAndItems(collectionIndex, hasSub, hasItems);
}

// Handles navigation when items are shared between collections
auto NavigationManager::handleSharedItemsNavigation(int collectionIndex)
    -> bool {
  bool itemsAreShared =
      areItemsShared((*m_currentCollectionIndex), collectionIndex);
  if (itemsAreShared) {
    navigateWithSharedItems(collectionIndex);
    if (m_refreshTitleCounts) m_refreshTitleCounts();
    return true;
  }
  return false;
}

// Prepares UI for non-shared navigation
auto NavigationManager::prepareNonSharedNavigation(int collectionIndex)
    -> void {
  if (!m_collections || !m_currentCollectionIndex) {
    return;
  }
  
  prepareForNonSharedNavigationHelper();
  suspendItemsPageRendering();

  (*m_currentCollectionIndex) = collectionIndex;

  applyUiPoliciesForCollection(collectionIndex);
  applyCollectionSettingsOnly(collectionIndex);

  if (m_scrollManager) {
    m_scrollManager->primeLayoutFor(
        (*m_collections)[collectionIndex]);
  }
  if (m_interactionManager) {
    m_interactionManager
        ->initializeSearchModeForCurrentCollection();
  }

  updateItemsPageTitle(collectionIndex);
  m_stackedWidget->setCurrentWidget(m_itemsPage);
  if (m_itemsPage && m_itemsPage->window()) {
    m_itemsPage->window()->setFocus();
    m_itemsPage->window()->activateWindow();
  }

  if (m_sidebarManager) {
    m_sidebarManager->applySidebarStateForCollection(
        (*m_currentCollectionIndex));
  }

  if ((m_searchBar) &&
      m_searchBar->text().trimmed().isEmpty()) {
    m_searchBar->clear();
  }

  if (m_scrollManager) {
    m_scrollManager->clearFilter();
  }
}

// Loads data for the current collection using count-based virtual scrolling
// for immediate responsiveness. Items are fetched on-demand as user scrolls.
auto NavigationManager::loadCollectionData(int collectionIndex) -> void {
  if (!m_databaseManager || !m_collections || !m_currentCollectionIndex) {
    return;
  }
  
  if ((*m_currentCollectionIndex) >= 0 &&
      (*m_currentCollectionIndex) <
          (*m_collections).size()) {
    CollectionContext context;
    context.currentIndex = (*m_currentCollectionIndex);
    context.config =
        (*m_collections)[(*m_currentCollectionIndex)];
        context.config.mediaDirectory = SettingsUtils::expandConfigVariables(
        context.config.mediaDirectory, context.config.name);
    context.config.artworkDirectory = SettingsUtils::expandConfigVariables(
        context.config.artworkDirectory, context.config.name);
    context.artworkDirectory = context.config.artworkDirectory;
    if (m_generalSettings) {
      context.sortMode = m_generalSettings->sortMode;
      context.excludeSubfoldersFromSort = m_generalSettings->excludeSubfoldersFromSort;
    }

    bool hasMediaDirectory = !context.config.mediaDirectory.trimmed().isEmpty();
    if (hasMediaDirectory || context.config.showAllSubcollectionItems) {
      // NOTE: Don't show overlay here - it will be shown by scanStarting signal
      // if a scan is actually needed. For cached collections, no overlay appears.
      
      // Use count-based loading for immediate UI responsiveness
      // Items are fetched on-demand via fetchItemsRange as user scrolls
      requestItemCountForContext(context, QString());
    } else {
      QStringList emptyFilePaths;
      QHash<QString, QString> emptyFileNames;
      onItemsLoaded(emptyFilePaths, emptyFileNames);
    }
  }
}

CollectionContext NavigationManager::buildExpandedContextForIndex(int collectionIndex) const {
  CollectionContext context;
  context.currentIndex = collectionIndex;
  if (!m_collections || collectionIndex < 0 || collectionIndex >= (*m_collections).size()) {
    return context;
  }

  context.config = (*m_collections)[collectionIndex];
  context.config.mediaDirectory = SettingsUtils::expandConfigVariables(
      context.config.mediaDirectory, context.config.name);
  context.config.artworkDirectory = SettingsUtils::expandConfigVariables(
      context.config.artworkDirectory, context.config.name);
  context.artworkDirectory = context.config.artworkDirectory;
  if (m_generalSettings) {
    context.sortMode = m_generalSettings->sortMode;
    context.excludeSubfoldersFromSort = m_generalSettings->excludeSubfoldersFromSort;
  }
  return context;
}

void NavigationManager::requestItemCountForContext(const CollectionContext &context,
                                                  const QString &filter) {
  if (!m_databaseManager || !m_collections) {
    return;
  }
  m_hasItemsQueryContext = true;
  m_itemsQueryContext = context;
  m_databaseManager->fetchItemCount(m_itemsQueryContext, (*m_collections), filter.trimmed());
}

// Shows items for a collection, prepares the view, and primes data loading
auto NavigationManager::showCollectionItems(int collectionIndex) -> bool {
  if (!validateCollectionIndex(collectionIndex)) {
    return false;
  }

  if ((parent()) &&
      (m_interactionManager)) {
    m_interactionManager->stopRepeat();
    // Only cancel pending restore when navigating FROM an existing collection
    // On initial startup (*m_currentCollectionIndex < 0), allow restore to proceed
    if (*m_currentCollectionIndex >= 0) {
      m_interactionManager->cancelPendingSelectionRestore();
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
auto NavigationManager::setSuppressArrowCenter(QScrollArea *scrollArea,
                                               int settleMs) -> void {
  if (!scrollArea || !m_state) {
    return;
  }
  m_state->suppressArrowCenterFor(settleMs);
  // Clear arrow center suppression after settle period expires -
  // allows navigation animations to complete before centering resumes
  QTimer::singleShot(
      UIConstants::Keyboard::ARROW_CENTER_CLEAR_AFTER_SET_MS, this, [this]() {
        if (m_state) {
          m_state->clearArrowCenterSuppression();
        }
      });
}

auto NavigationManager::areItemsShared(int fromIndex, int toIndex) const
    -> bool {
  if (fromIndex < 0 || toIndex < 0 ||
      fromIndex >= (*m_collections).size() ||
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

  // Check if target is a container collection (no media directory)
  // Container collections cannot share items - they only show subcollection folders
  QString toMediaDir = SettingsUtils::expandConfigVariables(
      toCollection.mediaDirectory, toCollection.name);
  if (toMediaDir.trimmed().isEmpty()) {
    return false;
  }

  if (!toCollection.showAllSubcollectionItems) {
    return true;
  }

  QList<int> childCollections = getAllDescendantCollections(toIndex);
  const bool anyDescendantWithMedia =
      std::ranges::any_of(childCollections, [this](int childIndex) {
        if (childIndex < 0 ||
            childIndex >= (*m_collections).size()) {
          return false;
        }
        QString childMediaDir = SettingsUtils::expandConfigVariables(
            (*m_collections)[childIndex].mediaDirectory,
            (*m_collections)[childIndex].name);
        return !childMediaDir.trimmed().isEmpty();
      });
  return !anyDescendantWithMedia;
}

auto NavigationManager::applyCollectionSettingsOnly(int collectionIndex)
    -> void {
  if (collectionIndex < 0 ||
      collectionIndex >= (*m_collections).size()) {
    return;
  }

  const CollectionConfig &collection =
      (*m_collections)[collectionIndex];

  if (collection.gridWidth !=
      m_scrollManager->getCurrentGridWidth()) {
    m_scrollManager->updateGridWidth(collection.gridWidth);
  }

  SettingsUtils::applyHorizontalScrollbarSetting(
      m_itemScrollArea, collectionIndex, (*m_collections));
  SettingsUtils::applyVerticalScrollbarSetting(
      m_itemScrollArea, collectionIndex, (*m_collections));

  applyBackgroundForCollection(collectionIndex);
  applyPrimaryColorForCollection(collectionIndex);

  if (m_sidebarManager) {
    m_sidebarManager->applySidebarStateForCollection(
        collectionIndex);
  }
}

void NavigationManager::applyBackgroundForCollection(int collectionIndex) {
  if (!m_itemScrollArea || collectionIndex < 0 ||
      collectionIndex >= (*m_collections).size()) {
    return;
  }

  const CollectionConfig &collection = (*m_collections)[collectionIndex];
  QWidget *viewport = m_itemScrollArea->viewport();
  if (!viewport) {
    return;
  }

  QString styleSheet;
  if (collection.backgroundType == BackgroundType::Image &&
      !collection.backgroundImage.isEmpty()) {
    // Background image mode
    QString imagePath = collection.backgroundImage;
    // Escape backslashes for CSS
    imagePath.replace("\\", "/");
    styleSheet = QString(
        "QWidget { "
        "background-image: url(\"%1\"); "
        "background-repeat: no-repeat; "
        "background-position: center; "
        "background-attachment: fixed; "
        "}")
        .arg(imagePath);
  } else if (!collection.backgroundColor.isEmpty()) {
    // Background color mode
    styleSheet = QString("QWidget { background-color: %1; }")
                     .arg(collection.backgroundColor);
  } else {
    // Clear any custom background (use system default)
    styleSheet.clear();
  }

  viewport->setStyleSheet(styleSheet);
}

void NavigationManager::applyPrimaryColorForCollection(int collectionIndex) {
  if (collectionIndex < 0 || collectionIndex >= (*m_collections).size()) {
    return;
  }

  const CollectionConfig &collection = (*m_collections)[collectionIndex];
  ItemWidget::setPrimaryColor(collection.primaryColor);
  ItemWidget::setTileColor(collection.tileColor);
  ItemWidget::setSelectionColor(collection.selectionColor);

  bool hasPrimaryColor = !collection.primaryColor.isEmpty() && 
                         QColor::isValidColorName(collection.primaryColor);

  // Apply primary color to toolbar/top bar (exact color, not tinted)
  if (m_itemsTopBar) {
    QString toolbarStyle;
    if (hasPrimaryColor) {
      toolbarStyle = QString(
          "QWidget#itemsTopBar { background-color: %1; }")
          .arg(collection.primaryColor);
    }
    m_itemsTopBar->setStyleSheet(toolbarStyle);
  }

  // Apply primary color to menubar
  if (m_menubar) {
    QString menubarStyle;
    if (hasPrimaryColor) {
      menubarStyle = QString(
          "QMenuBar { background-color: %1; }"
          "QMenuBar::item { background-color: transparent; }"
          "QMenuBar::item:selected { background-color: rgba(255,255,255,0.2); }")
          .arg(collection.primaryColor);
    }
    m_menubar->setStyleSheet(menubarStyle);
  }

  // Apply primary color to search bar background
  if (m_searchBar) {
    QString searchBarStyle;
    if (hasPrimaryColor) {
      // Tint the primary color slightly for the search bar background
      QColor baseColor(collection.primaryColor);
      QColor bgColor = baseColor.lighter(130);
      searchBarStyle = QString(
          "QLineEdit { background-color: %1; border: 1px solid %2; border-radius: 4px; padding: 4px; }"
          "QLineEdit:focus { border-color: %3; }")
          .arg(bgColor.name())
          .arg(baseColor.darker(110).name())
          .arg(baseColor.name());
    }
    m_searchBar->setStyleSheet(searchBarStyle);
  }
}

void NavigationManager::restoreSelectionForCurrentCollection() {
  if ((!parent()) || QApplication::closingDown()) {
    return;
  }
  if ((!m_scrollManager) ||
      (!m_interactionManager)) {
    return;
  }
  int coll = (*m_currentCollectionIndex);
  if (coll < 0 || coll >= (*m_collections).size()) {
    return;
  }
  int total = m_scrollManager->getTotalItems();
  if (total <= 0) {
    return;
  }
  int desired = -1;
  if (m_settingsManager) {
    desired = m_settingsManager->getLastSelectedItem(coll);
  } else {
    desired = m_settingsManager->getLastSelectedItem(coll);
  }
  if (desired < 0 || desired >= total) {
    desired = 0;
  }
  if (m_interactionManager->currentSelectedIndex() == desired) {
    return;
  }

  scheduleSelectionRestore(desired, UIConstants::Selection::RESTORE_STEPS,
                           UIConstants::Selection::RESTORE_STEP_DELAY_MS,
                           UIConstants::Selection::RESTORE_MAX_DELAY_MS);
}

// Performs common navigation cleanup operations
auto NavigationManager::performNavigationStackCleanup() -> void {
  if (m_interactionManager) {
    m_interactionManager->clearSelectionAndFocus();
  }
  if (m_MetadataSidebar) {
    m_MetadataSidebar->clearMetadata();
  }
  m_artworkManager->stopSilentLoading();
  if (m_artworkManager->getTimerCoordinator()) {
    m_artworkManager->getTimerCoordinator()->stopAllTimers();
  }
}

// Finds the visual index of a subcollection within its parent
auto NavigationManager::findSubcollectionVisualIndex(int targetCollectionIndex,
                                                     int previousIndex) const
    -> int {
  if (targetCollectionIndex < 0 ||
      targetCollectionIndex >= (*m_collections).size()) {
    return -1;
  }

  QList<int> subcollections;
  for (int i = 0; i < (*m_collections).size(); ++i) {
    if ((*m_collections)[i].parentCollectionIndex ==
        targetCollectionIndex) {
      subcollections.append(i);
    }
  }
  return subcollections.indexOf(previousIndex);
}

// Schedules the navigation return with proper timing and selection restoration
auto NavigationManager::scheduleNavigationReturn(int targetCollectionIndex,
                                                 int subcollectionVisualIndex)
    -> void {
  // Delay navigation return to allow current animations to complete -
  // nested timer handles selection restoration after layout settles
  QTimer::singleShot(
      UIConstants::Timing::SHORT_DELAY_MS, this,
      [this, targetCollectionIndex, subcollectionVisualIndex]() {
        showCollectionItems(targetCollectionIndex);

        if (m_interactionManager) {
          m_interactionManager->setNavigationInProgress(false);
        }

        if (subcollectionVisualIndex >= 0 &&
            m_interactionManager) {
          // Delay selection restore until layout is stable after navigation
          QTimer::singleShot(
              UIConstants::Timing::MEDIUM_DELAY_MS, this,
              [this, subcollectionVisualIndex]() {
                if (m_interactionManager) {
                  m_interactionManager->beginSelectionRestore(
                      subcollectionVisualIndex);
                }
              });
        }
      });
}

// Handles navigation when the navigation stack is not empty
auto NavigationManager::handleNavigationStackPop() -> void {
  int targetCollectionIndex = m_stackManager->pop();
  int previousIndex = (*m_currentCollectionIndex);

  performNavigationStackCleanup();

  bool shared = areItemsShared(previousIndex, targetCollectionIndex);
  if (m_scrollManager) {
    if (shared) {
      m_scrollManager->cleanupActiveWidgets();
    } else {
      m_scrollManager->cleanup();
    }
  }

  int subcollectionVisualIndex =
      findSubcollectionVisualIndex(targetCollectionIndex, previousIndex);
  scheduleNavigationReturn(targetCollectionIndex, subcollectionVisualIndex);
}

// Handles navigation fallback when the navigation stack is empty
auto NavigationManager::handleNavigationFallback() -> void {
  m_stackManager->clear();  // Ensure depth is reset
  int previousIndex = (*m_currentCollectionIndex);

  int fallbackIndex = (*m_currentCollectionIndex);
  if (fallbackIndex < 0 ||
      fallbackIndex >= (*m_collections).size()) {
    fallbackIndex = -1;
    for (int i = 0; i < (*m_collections).size(); ++i) {
      if ((*m_collections)[i].parentCollectionIndex == -1) {
        fallbackIndex = i;
        break;
      }
    }
    if (fallbackIndex < 0 && !(*m_collections).isEmpty()) {
      fallbackIndex = 0;
    }
  }

  if (fallbackIndex >= 0) {
    bool shared = areItemsShared(previousIndex, fallbackIndex);
    if (m_scrollManager) {
      if (shared) {
        m_scrollManager->cleanupActiveWidgets();
      } else {
        m_scrollManager->cleanup();
      }
    }
    showCollectionItems(fallbackIndex);
  }

  if (m_interactionManager) {
    m_interactionManager->setNavigationInProgress(false);
  }
}

// Goes back to collections and cancels any active held-key repeats to avoid
// stray timer callbacks
void NavigationManager::goBackToCollections() {
  if (!parent()) {
    return;
  }
  if ((parent()) &&
      (m_interactionManager)) {
    m_interactionManager->stopRepeat();
  }

  persistCurrentSelection();

  if (!m_stackManager->isEmpty()) {
    handleNavigationStackPop();
  } else {
    handleNavigationFallback();
  }
}

void NavigationManager::onCollectionSelected(int collectionIndex) {
  m_stackManager->clear();
  showCollectionItems(collectionIndex);
}

// Validates basic context for items loaded operations
auto NavigationManager::validateItemsLoadedContext() const -> bool {
  return parent() && !QApplication::closingDown() &&
         !m_isShuttingDown();
}

// Cleans up existing no-items widgets from previous loads
auto NavigationManager::cleanupExistingNoItemsWidgets() -> void {
  if (m_loadingLabel) {
    m_loadingLabel->setVisible(false);
  }

  QList<QWidget *> existingLabels =
      m_gridContainer->findChildren<QWidget *>("noItemsWidget");
  for (QWidget *widget : existingLabels) {
    widget->deleteLater();
  }
}

// Determines if content is available considering subcollections and descendant
// media
auto NavigationManager::determineContentAvailability(
    const QStringList &filePaths, const QList<int> &subcollections) const
    -> bool {
  bool hasContent = !filePaths.isEmpty() || !subcollections.isEmpty();

  if ((*m_currentCollectionIndex) >= 0 &&
      (*m_currentCollectionIndex) <
          (*m_collections).size()) {
    const CollectionConfig &collection =
        (*m_collections)[(*m_currentCollectionIndex)];
    if (collection.showAllSubcollectionItems && filePaths.isEmpty() &&
        !subcollections.isEmpty()) {
      QList<int> allDescendants =
          getAllDescendantCollections((*m_currentCollectionIndex));
      for (int descendantIndex : allDescendants) {
        if (descendantIndex >= 0 &&
            descendantIndex < (*m_collections).size()) {
          if (!(*m_collections)[descendantIndex]
                   .mediaDirectory.trimmed()
                   .isEmpty()) {
            hasContent = true;
            break;
          }
        }
      }
    }
  }
  return hasContent;
}

// Handles the case when no content is available
auto NavigationManager::handleEmptyContent() -> void {
  // Hide loading overlay since we're done loading (even if empty)
  if (m_loadingOverlay) {
    m_loadingOverlay->hide();
  }
  
  if (!QApplication::closingDown() && !m_isShuttingDown()) {
    m_loadingLabel->setText("No items found");
    m_loadingLabel->setVisible(true);
    resumeItemsPageRendering();
    if (m_refreshTitleCounts) m_refreshTitleCounts();
  }
  if ((parent()) &&
      (m_interactionManager)) {
    m_interactionManager->setNavigationInProgress(false);
  }
}

// Sets up collection context for items loaded
auto NavigationManager::setupCollectionContext(
    const QStringList &filePaths,
    const QHash<QString, QString> &fileNames) const -> CollectionContext {
  CollectionContext context;
  context.currentIndex = (*m_currentCollectionIndex);
  context.config =
      (*m_collections)[(*m_currentCollectionIndex)];
  context.artworkDirectory = context.config.artworkDirectory;
  context.filePaths = filePaths;
  context.fileNames = fileNames;
  if (m_generalSettings) {
    context.sortMode = m_generalSettings->sortMode;
    context.excludeSubfoldersFromSort = m_generalSettings->excludeSubfoldersFromSort;
  }
  if (m_allCollectionsActive) {
    context.config.showAllSubcollectionItems = true;
  }
  return context;
}

auto NavigationManager::lookupRememberedSelectionIndex(int totalItems) const
    -> int {
  if (!m_sessionManager || !m_collections || !m_currentCollectionIndex) {
    return -1;
  }
  if (totalItems <= 0) {
    return -1;
  }

  const CollectionConfig &cfg = (*m_collections)[(*m_currentCollectionIndex)];
  const bool subfolderActive = !cfg.currentSubfolder.trimmed().isEmpty();

  int selIdx = -1;
  if (subfolderActive) {
    const QString sessionKey =
        CollectionUtils::selectionSessionKeyFor(cfg, (*m_collections));
    selIdx = m_sessionManager->getLastSelectedIndex(sessionKey);
  } else {
    const QString hierarchicalName =
        CollectionUtils::hierarchicalNameFor(cfg, (*m_collections));
    selIdx = m_sessionManager->getLastSelectedIndex(hierarchicalName);
    if (selIdx < 0) {
      selIdx = m_sessionManager->getLastSelectedIndex(cfg.name);
    }
  }

  if (selIdx < 0) {
    return -1;
  }
  if (selIdx >= totalItems) {
    selIdx = totalItems - 1;
  }
  return std::max(selIdx, 0);
}

// Calculates the appropriate selection index for restoration
auto NavigationManager::calculateSelectionIndex(int totalItems) const -> int {
  bool searchActive = ((m_searchBar) &&
                       !m_searchBar->text().trimmed().isEmpty());

  if (searchActive || !(*m_generalSettings).rememberSelection ||
      totalItems <= 0) {
    return -1;
  }

  int selIdx = lookupRememberedSelectionIndex(totalItems);
  if (selIdx < 0) {
    return 0;
  }
  return selIdx;
}

// Computes the depth of a collection in the hierarchy
auto NavigationManager::computeCollectionDepth(int collectionIndex) const
    -> int {
  int depth = 0;
  int idx = collectionIndex;
  while (idx >= 0 && idx < (*m_collections).size()) {
    ++depth;
    int parent = (*m_collections)[idx].parentCollectionIndex;
    if (parent < 0) {
      break;
    }
    idx = parent;
  }
  return depth;
}

// Schedules post-load operations including artwork loading and navigation
// cleanup
auto NavigationManager::schedulePostLoadOperations() -> void {
  // Track if we're completing a rescan (affects title refresh timing)
  bool wasRescan = m_isRescanInProgress;
  m_isRescanInProgress = false;
  
  // Hide loading overlay after items have loaded
  if (m_loadingOverlay) {
    m_loadingOverlay->hide();
  }
  
  // Delay silent loading start until items are visible and user can interact -
  // prioritizes initial viewport over background prefetching
  QTimer::singleShot(UIConstants::Artwork::START_SILENT_LOAD_AFTER_ITEMS_MS,
                     this, [this]() {
                       if (QApplication::closingDown() ||
                           m_isShuttingDown()) {
                         return;
                       }
                       if (m_artworkManager) {
                         m_artworkManager->startSilentLoading();
                       }
                     });

  if (m_databaseManager) {
    m_databaseManager->updateCachedCounts(
        (*m_collections));
  }
  
  // After a rescan, delay title refresh to allow main thread database to see
  // the new data committed by the worker thread (SQLite WAL read visibility)
  if (wasRescan) {
    QTimer::singleShot(100, this, [this]() {
      if (m_refreshTitleCounts) m_refreshTitleCounts();
    });
  } else {
    if (m_refreshTitleCounts) m_refreshTitleCounts();
  }

  if (m_allCollectionsActive) {
    m_allCollectionsActive = false;
  }

  // Clear navigation progress flag after viewport settles -
  // allows user input to be processed again after load completes
  QTimer::singleShot(UIConstants::Timing::VIEWPORT_DELAY_MS, this, [this]() {
    if (parent() && m_interactionManager) {
      m_interactionManager->setNavigationInProgress(false);
    }
  });
}

// Handles items loaded; restores selection and resumes rendering after layout
// priming
void NavigationManager::onItemsLoaded(
    const QStringList &filePaths, const QHash<QString, QString> &fileNames) {
  if (!validateItemsLoadedContext()) {
    return;
  }

  cleanupExistingNoItemsWidgets();

  QList<int> subcollections =
      getSubcollections((*m_currentCollectionIndex));
  bool hasContent = determineContentAvailability(filePaths, subcollections);

  if (!hasContent) {
    handleEmptyContent();
    return;
  }

  CollectionContext context = setupCollectionContext(filePaths, fileNames);
  int totalItems = subcollections.size() + filePaths.size();
  int selIdx = calculateSelectionIndex(totalItems);

  if (m_scrollManager) {
    // Pre-set scroll position to avoid visual jump where list briefly
    // shows at position 0 then jumps to remembered position
    if (selIdx >= 0) {
      m_scrollManager->setInitialScrollIndex(selIdx);
    }
    m_scrollManager->setupVirtualScrolling(totalItems, context);
  }

  resumeItemsPageRendering();

  if (m_artworkManager) {
    m_artworkManager->updateViewportArtwork();
  }
  if (m_artworkManager &&
      m_artworkManager->getTimerCoordinator()) {
    m_artworkManager->getTimerCoordinator()->scheduleViewportUpdate();
  }

  bool pendingRestore =
      m_state ? m_state->selectionRestore().restorePending : false;
  debugLog("[SelectionRestore] onItemsLoaded: selIdx=" << selIdx 
           << "totalItems=" << totalItems << "pendingRestore=" << pendingRestore
           << "collectionIndex=" << (*m_currentCollectionIndex));
  if (selIdx >= 0 && (m_interactionManager) &&
      !pendingRestore) {
    if (lcNavigationManager().isDebugEnabled()) {
      int depth = computeCollectionDepth((*m_currentCollectionIndex));
      debugLog("[SelectionRestore] depth=" << depth << "for collection" 
               << (*m_collections)[(*m_currentCollectionIndex)].name);
    }
    // Use scheduleSelectionRestore for all depths to ensure widgets are
    // materialized before selection restore attempts sidebar update
    scheduleSelectionRestore(selIdx, UIConstants::Selection::RESTORE_STEPS,
                             UIConstants::Selection::RESTORE_STEP_DELAY_MS,
                             UIConstants::Selection::RESTORE_MAX_DELAY_MS);
  }

  schedulePostLoadOperations();
}

void NavigationManager::onMediaLibraryError(const ErrorUtils::ErrorContext &error) {
  if (m_loadingLabel) {
    m_loadingLabel->deleteLater();
    m_loadingLabel = nullptr;
  }

  // Hide loading overlay if visible
  if (m_loadingOverlay) {
    m_loadingOverlay->hide();
  }

  QList<QWidget *> existingLabels =
      m_gridContainer->findChildren<QWidget *>("noItemsWidget");
  for (QWidget *widget : existingLabels) {
    widget->deleteLater();
  }

  // Show error dialog for database errors - use the full ErrorContext
  QWidget *parentWidget = m_gridContainer ? m_gridContainer->window() : nullptr;
  ErrorDialog::showError(parentWidget, error);

  auto *errorWidget = new QWidget(m_gridContainer);
  errorWidget->setObjectName("noItemsWidget");

  auto *errorLabel = new QLabel(error.message, errorWidget);
  errorLabel->setAlignment(Qt::AlignCenter);
  errorLabel->setStyleSheet(
      "QLabel { color: palette(text); font-size: 14px; }");

  auto *layout = new QVBoxLayout(errorWidget);
  layout->addWidget(errorLabel);
  layout->setContentsMargins(0, 0, 0, 0);

  if (m_itemScrollArea) {
    QRect viewportRect = m_itemScrollArea->viewport()->rect();
    errorWidget->setGeometry(viewportRect);
  }

  errorWidget->show();
  errorWidget->raise();

  if (m_databaseManager) {
    m_databaseManager->updateCachedCounts(
        (*m_collections));
  }
  if (m_refreshTitleCounts) m_refreshTitleCounts();
}

void NavigationManager::onViewportChanged() {
  if ((m_interactionManager) &&
      m_interactionManager->isWheelScrolling() &&
      m_stackedWidget && m_stackedWidget->currentWidget() == m_itemsPage) {
    // Delay viewport update during wheel scrolling to batch rapid events -
    // reduces CPU load from frequent artwork updates during fast scrolling
    QTimer::singleShot(UIConstants::Timing::SHORT_DELAY_MS, this, [this]() {
      if (m_scrollManager) {
        m_scrollManager->updateVirtualView();
      }
      if (m_artworkManager) {
        m_artworkManager->updateViewportArtwork();
      }
    });
  } else {
    if (m_artworkManager) {
      if (auto *coord = m_artworkManager->getTimerCoordinator()) {
        coord->scheduleViewportUpdate();
      }
    }
  }
}

void NavigationManager::filterItems(const QString &searchText) {
  const int idx = (*m_currentCollectionIndex);
  if (idx < 0 || idx >= (*m_collections).size()) {
    return;
  }

  CollectionContext context = buildExpandedContextForIndex(idx);
  requestItemCountForContext(context, searchText);
}

void NavigationManager::filterItemsCurrentAndSubcollections(const QString &searchText) {
  const int idx = (*m_currentCollectionIndex);
  if (idx < 0 || idx >= (*m_collections).size()) {
    return;
  }

  CollectionContext context = buildExpandedContextForIndex(idx);
  context.queryIncludeDescendants = true;
  requestItemCountForContext(context, searchText);
}

void NavigationManager::filterItemsAllCollections(const QString &searchText) {
  const int idx = (*m_currentCollectionIndex);
  if (idx < 0 || idx >= (*m_collections).size()) {
    return;
  }

  CollectionContext context = buildExpandedContextForIndex(idx);
  context.queryIncludeAllCollections = true;
  context.hasSubcollectionOverride = true;
  context.subcollectionOverride = {};
  context.suppressVirtualFolders = true;
  requestItemCountForContext(context, searchText);
}

// Return true if any descendant of parentIndex has a non-empty mediaDirectory
auto NavigationManager::collectionHasDescendantWithMedia(int parentIndex) const
    -> bool {
  QList<int> childCollections = getAllDescendantCollections(parentIndex);
  return std::ranges::any_of(childCollections, [this](int childIndex) {
    if (childIndex < 0 || childIndex >= (*m_collections).size()) {
      return false;
    }
    const QString &mediaDir =
        (*m_collections)[childIndex].mediaDirectory;
    return !mediaDir.trimmed().isEmpty();
  });
}

// Forces a full rescan of the collection by clearing cached data first
void NavigationManager::forceRescanCollection(int collectionIndex) {
  if (!m_collections || !m_databaseManager ||
      collectionIndex < 0 || collectionIndex >= (*m_collections).size()) {
    return;
  }
  
  // Show loading overlay during rescan
  if (m_loadingOverlay) {
    m_loadingOverlay->show("Clearing cache...");
  }
  
  // Clear cached items to force a fresh scan (async operation)
  const CollectionConfig &config = (*m_collections)[collectionIndex];
  QString mediaDir = SettingsUtils::expandConfigVariables(
      config.mediaDirectory, config.name);
  QString uuid = CollectionUtils::computeCollectionUuid(config.name, mediaDir);
  
  // Mark that we're rescanning so onItemCountLoaded knows to hide the overlay
  m_isRescanInProgress = true;
  m_pendingRescanCollectionIndex = collectionIndex;
  
  // Disconnect any previous connection to avoid duplicate calls
  if (m_cacheInvalidatedConnection) {
    QObject::disconnect(m_cacheInvalidatedConnection);
  }
  
  // Connect one-shot handler to proceed after cache invalidation completes
  m_cacheInvalidatedConnection = QObject::connect(
      m_databaseManager, &DatabaseManager::cacheInvalidated,
      this, [this](const QString &/*invalidatedUuid*/) {
        // Disconnect immediately to ensure one-shot behavior
        QObject::disconnect(m_cacheInvalidatedConnection);
        m_cacheInvalidatedConnection = {};
        
        if (m_pendingRescanCollectionIndex >= 0) {
          // Update overlay message now that scan is starting
          if (m_loadingOverlay) {
            m_loadingOverlay->setMessage("Rescanning collection...");
          }
          
          int idx = m_pendingRescanCollectionIndex;
          m_pendingRescanCollectionIndex = -1;
          
          // Now reload - ensureCollectionScanned will scan since cache is cleared
          safeReloadCollection(idx);
        }
      });
  
  // Initiate async cache invalidation
  m_databaseManager->invalidateCollectionCache(uuid);
}

// Safely reloads the specified collection and recenters the horizontal
// scrollbar
void NavigationManager::safeReloadCollection(int collectionIndex) {
  persistCurrentSelection();
  // We're about to rebuild the items view. Clear any stale selection-restore
  // suppression so automatic restore can re-apply the selection rectangle.
  if (m_interactionManager) {
    m_interactionManager->resetSelectionRestoreState();
  }
  if (!m_collections || collectionIndex < 0 ||
      collectionIndex >= (*m_collections).size()) {
    return;
  }

  if (m_artworkManager) {
    if (auto *coord = m_artworkManager->getTimerCoordinator()) {
      coord->stopAllTimers();
    }
  }

  if (m_scrollManager) {
    m_scrollManager->cleanup();
  }

  if (m_artworkManager) {
    m_artworkManager->cancelAllArtworkLoading();
  }

  // Delay collection reload to allow cleanup operations to complete -
  // nested timer ensures horizontal centering happens after items load
  QTimer::singleShot(
      UIConstants::Timing::MEDIUM_DELAY_MS, this, [this, collectionIndex]() {
        if (!m_collections || !m_databaseManager ||
            collectionIndex < 0 ||
            collectionIndex >= (*m_collections).size()) {
          return;
        }

        CollectionContext context;
        context.currentIndex = collectionIndex;
        context.config = (*m_collections)[collectionIndex];
        context.config.mediaDirectory = SettingsUtils::expandConfigVariables(
            context.config.mediaDirectory, context.config.name);
        context.config.artworkDirectory = SettingsUtils::expandConfigVariables(
            context.config.artworkDirectory, context.config.name);
        context.artworkDirectory = context.config.artworkDirectory;
        if (m_generalSettings) {
          context.sortMode = m_generalSettings->sortMode;
          context.excludeSubfoldersFromSort = m_generalSettings->excludeSubfoldersFromSort;
        }

        // Use count-based loading for immediate UI responsiveness
        // Items are fetched on-demand via fetchItemsRange as user scrolls
        requestItemCountForContext(context, QString());

        // Update toolbar title to reflect subfolder if navigated into one
        updateItemsPageTitle(collectionIndex);

        // Delay centering until items are loaded and layout is calculated
        QTimer::singleShot(UIConstants::Timing::VIEWPORT_DELAY_MS, this, [this]() {
          if (m_scrollManager && m_currentCollectionIndex && m_collections) {
            m_scrollManager->centerHorizontalScrollbar(
                (*m_currentCollectionIndex),
                (*m_collections));
          }
        });
      });
}

// Updates the items page title to show "Parent > Child" when viewing a
// subcollection, and displays the current subfolder path if navigated into one
auto NavigationManager::updateItemsPageTitle(int collectionIndex) -> void {
  if ((!parent()) || (!m_itemsPage)) {
    return;
  }
  auto *titleLabel =
      m_itemsPage->findChild<QLabel *>("itemsTitleLabel");
  auto *subfolderLabel =
      m_itemsPage->findChild<QLabel *>("subfolderPathLabel");

  if (!titleLabel) {
    return;
  }

  if (collectionIndex < 0 ||
      collectionIndex >= (*m_collections).size()) {
    titleLabel->clear();
    if (subfolderLabel) subfolderLabel->setVisible(false);
    return;
  }

  // Connect linkActivated signal if not already connected
  static bool linkConnected = false;
  if (!linkConnected) {
    QObject::connect(titleLabel, &QLabel::linkActivated,
                     this, &NavigationManager::onBreadcrumbLinkClicked);
    linkConnected = true;
  }

  const CollectionConfig &config = (*m_collections)[collectionIndex];
  
  // Build breadcrumb with clickable parent links
  QString titleHtml;
  
  // Get tinted color for clickable links
  QPalette pal = titleLabel->palette();
  QColor highlightColor = pal.color(QPalette::Highlight);
  int h, s, l;
  highlightColor.getHsl(&h, &s, &l);
  QColor linkColor = QColor::fromHsl(h, s / 2, 170);  // Tinted, slightly saturated
  QString linkColorHex = linkColor.name();
  
  // Check if we're in a subfolder - if so, make the collection name clickable
  bool inSubfolder = !config.currentSubfolder.isEmpty();
  
  if (config.isSubcollection) {
    int parentIdx = config.parentCollectionIndex;
    if (parentIdx >= 0 && parentIdx < (*m_collections).size()) {
      // Parent is clickable - navigates back to parent collection
      QString parentName = (*m_collections)[parentIdx].name.toHtmlEscaped();
      if (inSubfolder) {
        // Both parent and current collection are clickable when in subfolder
        titleHtml = QString("<a href=\"collection:%1\" style=\"color:%2; text-decoration:none;\">%3</a> › "
                           "<a href=\"root:\" style=\"color:%2; text-decoration:none;\">%4</a>")
            .arg(parentIdx)
            .arg(linkColorHex)
            .arg(parentName)
            .arg(config.name.toHtmlEscaped());
      } else {
        titleHtml = QString("<a href=\"collection:%1\" style=\"color:%2; text-decoration:none;\">%3</a> › %4")
            .arg(parentIdx)
            .arg(linkColorHex)
            .arg(parentName)
            .arg(config.name.toHtmlEscaped());
      }
    } else {
      titleHtml = config.name.toHtmlEscaped();
    }
  } else {
    // Root collection - make clickable only if in a subfolder
    if (inSubfolder) {
      titleHtml = QString("<a href=\"root:\" style=\"color:%1; text-decoration:none;\">%2</a>")
          .arg(linkColorHex)
          .arg(config.name.toHtmlEscaped());
    } else {
      titleHtml = config.name.toHtmlEscaped();
    }
  }
  
  titleLabel->setText(titleHtml);

  // Show subfolder path if we're navigated into a virtual folder
  if (subfolderLabel) {
    const QString &subfolder = config.currentSubfolder;
    if (!subfolder.isEmpty()) {
      // Connect subfolder label linkActivated if not already connected
      static bool subfolderLinkConnected = false;
      if (!subfolderLinkConnected) {
        QObject::connect(subfolderLabel, &QLabel::linkActivated,
                         this, &NavigationManager::onBreadcrumbLinkClicked);
        subfolderLabel->setTextFormat(Qt::RichText);
        subfolderLabel->setOpenExternalLinks(false);
        subfolderLinkConnected = true;
      }
      
      // Build subfolder breadcrumb - show path segments as clickable links
      QStringList pathParts = subfolder.split('/', Qt::SkipEmptyParts);
      QString subfolderHtml;
      QString accumulatedPath;
      
      for (int i = 0; i < pathParts.size(); ++i) {
        if (!accumulatedPath.isEmpty()) {
          accumulatedPath += '/';
        }
        accumulatedPath += pathParts[i];
        
        if (i > 0) {
          subfolderHtml += " › ";
        }
        
        if (i < pathParts.size() - 1) {
          // Intermediate folder - clickable to navigate to that level
          subfolderHtml += QString("<a href=\"subfolder:%1\" style=\"color:%2; text-decoration:none;\">%3</a>")
              .arg(accumulatedPath.toHtmlEscaped())
              .arg(linkColorHex)
              .arg(pathParts[i].toHtmlEscaped());
        } else {
          // Current folder - not clickable, just styled
          subfolderHtml += pathParts[i].toHtmlEscaped();
        }
      }
      
      subfolderLabel->setText(subfolderHtml);
      subfolderLabel->setStyleSheet(QString("color: %1;").arg(linkColorHex));
      subfolderLabel->setVisible(true);
    } else {
      subfolderLabel->setVisible(false);
    }
  }
}

// Returns whether the collection has direct items or any subcollections and
// outputs both flags
auto NavigationManager::getHasSubAndItems(int collectionIndex, bool &hasSub,
                                          bool &hasItems) const -> bool {
  hasSub = false;
  hasItems = false;
  if ((!parent()) || collectionIndex < 0 ||
      collectionIndex >= (*m_collections).size()) {
    return false;
  }

  const CollectionConfig &collection =
      (*m_collections)[collectionIndex];

  for (auto &m_collection : (*m_collections)) {
    if (m_collection.parentCollectionIndex == collectionIndex) {
      hasSub = true;
      break;
    }
  }

  QString mediaDir = (m_settingsManager)
                         ? SettingsUtils::expandConfigVariables(
                               collection.mediaDirectory, collection.name)
                         : collection.mediaDirectory;
  if (!mediaDir.trimmed().isEmpty()) {
    QDir dir(mediaDir);
    if (dir.exists()) {
      QStringList filters = collection.extensions.isEmpty()
                                ? QStringList()
                                : collection.extensions;
      QStringList files = filters.isEmpty()
                              ? dir.entryList(QDir::Files)
                              : dir.entryList(filters, QDir::Files);
      hasItems = !files.isEmpty();
      
      // When showAllSubfolderItems is true, also check subdirectories for files
      // This handles cases where the root mediaDirectory has no files but subdirs do
      if (!hasItems && collection.includeContentSubfolders && collection.showAllSubfolderItems) {
        QDir::Filters dirFilters = QDir::Dirs | QDir::NoDotAndDotDot;
        if (collection.showHiddenFolders) {
          dirFilters |= QDir::Hidden;
        }
        QStringList subdirs = dir.entryList(dirFilters);
        for (const QString &subdir : subdirs) {
          QDir subDir(dir.filePath(subdir));
          QStringList subFiles = filters.isEmpty()
                                     ? subDir.entryList(QDir::Files)
                                     : subDir.entryList(filters, QDir::Files);
          if (!subFiles.isEmpty()) {
            hasItems = true;
            break;
          }
        }
      }
      
      // Also check for virtual folders (subdirectories as navigable content)
      // when includeContentSubfolders is enabled and showAllSubfolderItems is false
      if (!hasItems && collection.includeContentSubfolders && !collection.showAllSubfolderItems) {
        QDir::Filters dirFilters = QDir::Dirs | QDir::NoDotAndDotDot;
        if (collection.showHiddenFolders) {
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
void NavigationManager::onSubcollectionEntered(int subcollectionIndex) {
  if (subcollectionIndex >= 0 &&
      subcollectionIndex < (*m_collections).size()) {
    if (m_state) {
      qint64 now = QDateTime::currentMSecsSinceEpoch();
      m_state->click().suppressDoubleClickUntilMs =
          now + UIConstants::Selection::DOUBLE_CLICK_SUPPRESS_AFTER_ENTER_MS;
    }

    if ((*m_currentCollectionIndex) >= 0 &&
        (m_interactionManager)) {
      int currentSelection =
          m_interactionManager->currentSelectedIndex();
      if (currentSelection >= 0) {
        m_settingsManager->setLastSelectedItem(
            (*m_currentCollectionIndex), currentSelection);
      }
    }

    if ((*m_currentCollectionIndex) >= 0 &&
        (*m_currentCollectionIndex) <
            (*m_collections).size()) {
      m_stackManager->push(*m_currentCollectionIndex);
    }

    m_settingsManager->setLastSelectedItem(subcollectionIndex,
                                                         -1);

    bool success = showCollectionItems(subcollectionIndex);
    if (!success) {
      // Undo the push if navigation failed
      (void)m_stackManager->pop();
      return;
    }

    // Delay horizontal centering until subcollection layout stabilizes
    QTimer::singleShot(
        UIConstants::Navigation::SUBCOLLECTION_SCROLL_CENTER_DELAY_MS, this,
        [this]() {
          m_scrollManager->centerHorizontalScrollbar(
              (*m_currentCollectionIndex),
              (*m_collections));
        });

    // Clear double-click suppression after navigation animation completes
    QTimer::singleShot(UIConstants::Selection::DOUBLE_CLICK_SUPPRESS_CLEAR_DELAY_MS,
                       this, [this]() {
                         if (m_state) {
                           m_state->click().suppressDoubleClickUntilMs = 0;
                         }
                       });
  }
}

void NavigationManager::onVirtualFolderEntered(const QString &folderPath) {
  if (!m_collections || (*m_currentCollectionIndex) < 0) {
    return;
  }

  // Persist the currently selected folder tile in the parent scope so that
  // leaving the subfolder restores selection back to the entered folder.
  if (m_interactionManager) {
    m_interactionManager->saveCurrentSelection();
  }

  if (m_interactionManager) {
    m_interactionManager->stopScrollAnimations();
  }
  
  // Update the current subfolder path in the collection config
  CollectionConfig &config = (*m_collections)[*m_currentCollectionIndex];

  // If the folder didn't actually change, avoid an unnecessary reload.
  if (config.currentSubfolder == folderPath) {
    return;
  }

  // Entering a new subfolder should start at the top of that folder's list.
  m_forceTopOnNextItemsViewLoad = true;
  if (m_itemScrollArea && m_itemScrollArea->verticalScrollBar()) {
    m_itemScrollArea->verticalScrollBar()->setValue(0);
  }

  config.currentSubfolder = folderPath;
  
  // Reload the collection to show the new folder contents
  safeReloadCollection(*m_currentCollectionIndex);
}

void NavigationManager::goBackFromVirtualFolder() {
  if (!m_collections || (*m_currentCollectionIndex) < 0) {
    return;
  }

  // Persist the current selection within this virtual subfolder scope before
  // changing currentSubfolder so re-entry can restore it.
  if (m_interactionManager) {
    m_interactionManager->saveCurrentSelection();
  }

  if (m_interactionManager) {
    m_interactionManager->stopScrollAnimations();
  }
  
  CollectionConfig &config = (*m_collections)[*m_currentCollectionIndex];
  
  if (config.currentSubfolder.isEmpty()) {
    // Already at root, nothing to go back to
    return;
  }

  // Navigating to a different folder scope should start at the top.
  m_forceTopOnNextItemsViewLoad = true;
  if (m_itemScrollArea && m_itemScrollArea->verticalScrollBar()) {
    m_itemScrollArea->verticalScrollBar()->setValue(0);
  }
  
  // Go up one level
  int lastSlash = config.currentSubfolder.lastIndexOf('/');
  if (lastSlash > 0) {
    config.currentSubfolder = config.currentSubfolder.left(lastSlash);
  } else {
    config.currentSubfolder.clear();
  }
  
  // Reload to show parent folder
  safeReloadCollection(*m_currentCollectionIndex);
}

void NavigationManager::onBreadcrumbLinkClicked(const QString &link) {
  if (link.startsWith("collection:")) {
    // Navigate to parent collection
    bool ok = false;
    int targetIndex = link.mid(11).toInt(&ok);  // Skip "collection:" prefix
    if (ok && targetIndex >= 0 && targetIndex < (*m_collections).size()) {
      // Clear current subfolder before navigating to parent
      if (*m_currentCollectionIndex >= 0 && 
          *m_currentCollectionIndex < (*m_collections).size()) {
        (*m_collections)[*m_currentCollectionIndex].currentSubfolder.clear();
      }
      goBackToCollections();
    }
  } else if (link.startsWith("subfolder:")) {
    // Navigate to a specific subfolder level
    QString targetPath = link.mid(10);  // Skip "subfolder:" prefix
    if (*m_currentCollectionIndex >= 0 && 
        *m_currentCollectionIndex < (*m_collections).size()) {
      CollectionConfig &config = (*m_collections)[*m_currentCollectionIndex];
      if (config.currentSubfolder != targetPath) {
        if (m_interactionManager) {
          m_interactionManager->saveCurrentSelection();
        }
        if (m_interactionManager) {
          m_interactionManager->stopScrollAnimations();
        }
        m_forceTopOnNextItemsViewLoad = true;
        if (m_itemScrollArea && m_itemScrollArea->verticalScrollBar()) {
          m_itemScrollArea->verticalScrollBar()->setValue(0);
        }
      }
      config.currentSubfolder = targetPath;
      safeReloadCollection(*m_currentCollectionIndex);
    }
  } else if (link == "root:") {
    // Navigate back to root of current collection (clear subfolder)
    if (*m_currentCollectionIndex >= 0 && 
        *m_currentCollectionIndex < (*m_collections).size()) {
      CollectionConfig &config = (*m_collections)[*m_currentCollectionIndex];
      if (!config.currentSubfolder.isEmpty()) {
        if (m_interactionManager) {
          m_interactionManager->saveCurrentSelection();
        }
        if (m_interactionManager) {
          m_interactionManager->stopScrollAnimations();
        }
        m_forceTopOnNextItemsViewLoad = true;
        if (m_itemScrollArea && m_itemScrollArea->verticalScrollBar()) {
          m_itemScrollArea->verticalScrollBar()->setValue(0);
        }
      }
      config.currentSubfolder.clear();
      safeReloadCollection(*m_currentCollectionIndex);
    }
  }
}

// Loads items for the current collection with optional subcollection
// aggregation and reapplies any active filter
void NavigationManager::loadCurrentAndSubcollections() {
  const int idx = (*m_currentCollectionIndex);
  if (idx < 0 || idx >= (*m_collections).size()) {
    return;
  }

  CollectionContext context;
  context.currentIndex = idx;
  context.config = (*m_collections)[idx];
  context.config.mediaDirectory = SettingsUtils::expandConfigVariables(
      context.config.mediaDirectory, context.config.name);
  context.config.artworkDirectory = SettingsUtils::expandConfigVariables(
      context.config.artworkDirectory, context.config.name);
  context.artworkDirectory = context.config.artworkDirectory;
  if (m_generalSettings) {
    context.sortMode = m_generalSettings->sortMode;
    context.excludeSubfoldersFromSort = m_generalSettings->excludeSubfoldersFromSort;
  }

  context.queryIncludeDescendants = true;
  requestItemCountForContext(context, QString());

  // Delay filter reapplication until item count query completes -
  // ensures filter operates on the updated item list
  QTimer::singleShot(UIConstants::Artwork::FILTER_REAPPLY_DELAY_MS, this, [this]() {
    if (m_searchBar &&
        !m_searchBar->text().trimmed().isEmpty() &&
        m_scrollManager) {
      const QString currentSearchText =
          m_searchBar->text().trimmed();
      m_scrollManager->applyFilter(currentSearchText);
    }
  });
}

// Loads the aggregated view across all collections and reapplies any active
// filter
void NavigationManager::loadAllCollectionsView() {
  // DB-backed all-collections view (count + on-demand ranges).
  // Primarily used for AllCollections search mode.
  const int idx = (*m_currentCollectionIndex);
  if (idx < 0 || idx >= (*m_collections).size()) {
    return;
  }

  CollectionContext context = buildExpandedContextForIndex(idx);
  context.queryIncludeAllCollections = true;
  context.hasSubcollectionOverride = true;
  context.subcollectionOverride = {};
  context.suppressVirtualFolders = true;

  const QString currentFilter = (m_searchBar) ? m_searchBar->text().trimmed() : QString();
  requestItemCountForContext(context, currentFilter);
}

auto NavigationManager::getAllDescendantCollections(int parentIndex) const
    -> QList<int> {
  // Use cache for O(1) lookup if available
  if (m_hierarchyCache && m_hierarchyCache->isValid()) {
    return m_hierarchyCache->allDescendants(parentIndex);
  }
  // Fallback to O(n) recursive scan
  QList<int> result;
  if ((!m_collections) || parentIndex < 0 ||
      parentIndex >= m_collections->size()) {
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

void NavigationManager::persistCurrentSelection() {
  if ((!m_interactionManager) ||
      (!m_settingsManager) ||
      (!m_currentCollectionIndex) ||
      (!m_collections)) {
    return;
  }
  int coll = *m_currentCollectionIndex;
  if (!CollectionUtils::isValidIndex(coll, m_collections)) {
    return;
  }
  int sel = m_interactionManager->currentSelectedIndex();
  if (sel < 0) {
    return;
  }
  m_settingsManager->setLastSelectedItem(coll, sel);
}

void NavigationManager::prepareForNonSharedNavigationHelper() {
  if (m_interactionManager) {
    m_interactionManager->clearSelectionAndFocus();
  }
  if (m_MetadataSidebar) {
    m_MetadataSidebar->clearMetadata();
  }
  m_artworkManager->stopSilentLoading();
  if (m_artworkManager->getTimerCoordinator()) {
    m_artworkManager->getTimerCoordinator()->stopAllTimers();
  }
  if (m_scrollManager) {
    m_scrollManager->cleanup();
  }
}

void NavigationManager::suspendItemsPageRendering() {
  if (m_itemScrollArea) {
    m_itemScrollArea->setUpdatesEnabled(false);
  }
  if (m_state) {
    m_state->artwork().suppressArtwork = true;
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
  if (m_state) {
    m_state->artwork().suppressArtwork = false;
  }
}

void NavigationManager::applyUiPoliciesForCollection(int collectionIndex) {
  if (m_sidebarManager) {
    m_sidebarManager->applySidebarStateForCollection(collectionIndex);
  }
  if (m_settingsManager && m_itemScrollArea && m_collections) {
    SettingsUtils::applyHorizontalScrollbarSetting(
        m_itemScrollArea, collectionIndex, *m_collections);
    SettingsUtils::applyVerticalScrollbarSetting(m_itemScrollArea, collectionIndex,
                                                   *m_collections);
  }
}

void NavigationManager::onItemCountLoaded(int count) {
  const int idx = (*m_currentCollectionIndex);
  if (idx < 0 || idx >= (*m_collections).size()) {
    return;
  }

  const QString searchText = (m_searchBar) ? m_searchBar->text().trimmed() : QString();
  const bool searchActive = !searchText.isEmpty();

  if (m_backgroundCountRefreshInProgress &&
      m_backgroundCountRefreshCollectionIndex == idx &&
      m_scrollManager && m_scrollManager->getTotalItems() > 0) {
    // Background scan completed; refresh media count without resetting
    // scroll/selection state.
    m_backgroundCountRefreshInProgress = false;
    m_backgroundCountRefreshCollectionIndex = -1;

    m_scrollManager->updateMediaItemCount(count);
    if (m_artworkManager && m_artworkManager->getTimerCoordinator()) {
      m_artworkManager->getTimerCoordinator()->scheduleViewportUpdate();
    }
    if (m_refreshTitleCounts) {
      m_refreshTitleCounts();
    }
    if (m_databaseManager && m_collections) {
      m_databaseManager->updateCachedCounts((*m_collections));
    }
    return;
  }

  const bool forceTopForThisLoad = m_forceTopOnNextItemsViewLoad;
  m_forceTopOnNextItemsViewLoad = false;

  // Any full rebuild invalidates in-flight range requests.
  ++m_itemsViewGeneration;
  m_pendingRangeGenerations.clear();

  // Clean up existing "no items" widgets
  cleanupExistingNoItemsWidgets();

  CollectionContext context = m_hasItemsQueryContext ? m_itemsQueryContext
                                                     : buildExpandedContextForIndex(idx);

  // Get subcollections for this view (shown as folder tiles)
  QList<int> subcollections;
  if (context.hasSubcollectionOverride) {
    subcollections = context.subcollectionOverride;
  } else {
    subcollections = getSubcollections(idx);
    if (searchActive) {
      QList<int> filtered;
      filtered.reserve(subcollections.size());
      for (int subIdx : subcollections) {
        if (subIdx < 0 || subIdx >= (*m_collections).size()) {
          continue;
        }
        const QString &name = (*m_collections)[subIdx].name;
        if (name.contains(searchText, Qt::CaseInsensitive)) {
          filtered.append(subIdx);
        }
      }
      subcollections = filtered;
    }
  }

  // Count virtual folders (subdirectories shown as navigable tiles)
  const bool suppressVirtualFolders = context.suppressVirtualFolders || searchActive;
  int virtualFolderCount = suppressVirtualFolders ? 0
                                                  : CollectionUtils::countVirtualFolders(context.config);
  
  int totalItems = subcollections.size() + virtualFolderCount + count;

  // Check if collection has any content
  if (totalItems == 0) {
    handleEmptyContent();
    return;
  }

  // Update loading overlay to show item count found
  if (m_loadingOverlay && m_loadingOverlay->isActive()) {
    const QString &collectionName = (*m_collections)[idx].name;
    m_loadingOverlay->setMessage(
        QString("Loading %1 (%2 items)...").arg(collectionName).arg(totalItems));
  }

  if (searchActive) {
    context.hasSubcollectionOverride = true;
    context.subcollectionOverride = subcollections;
    context.suppressVirtualFolders = true;
  }

  // Calculate selection index for initial scroll position
  int rememberedSelIdx = -1;
  if (!searchActive && m_generalSettings && m_generalSettings->rememberSelection) {
    rememberedSelIdx = lookupRememberedSelectionIndex(totalItems);
  }

  int selIdx = calculateSelectionIndex(totalItems);

  if (forceTopForThisLoad && !searchActive && rememberedSelIdx < 0) {
    selIdx = 0;
  }
  
  if (m_scrollManager) {
    // Pre-set scroll position to avoid visual jump
    if (searchActive) {
      m_scrollManager->setInitialScrollIndex(0);
    } else if (selIdx >= 0) {
      m_scrollManager->setInitialScrollIndex(selIdx);
    }
    m_scrollManager->setupVirtualScrolling(totalItems, context);
  }

  // Resume rendering on the items page
  resumeItemsPageRendering();
  
  if (m_loadingLabel) {
    m_loadingLabel->hide();
  }
  if (m_stackedWidget && m_itemsPage) {
    m_stackedWidget->setCurrentWidget(m_itemsPage);
  }

  // Update artwork for visible items
  if (m_artworkManager) {
    m_artworkManager->updateViewportArtwork();
  }
  if (m_artworkManager && m_artworkManager->getTimerCoordinator()) {
    m_artworkManager->getTimerCoordinator()->scheduleViewportUpdate();
  }
  
  // Restore selection if needed
  bool pendingRestore =
      m_state ? m_state->selectionRestore().restorePending : false;
  if (selIdx >= 0 && m_interactionManager && !pendingRestore) {
    scheduleSelectionRestore(selIdx, UIConstants::Selection::RESTORE_STEPS,
                             UIConstants::Selection::RESTORE_STEP_DELAY_MS,
                             UIConstants::Selection::RESTORE_MAX_DELAY_MS);
  }

  schedulePostLoadOperations();
}

void NavigationManager::onBackgroundCollectionScanCompleted(const QString &collectionUuid) {
  if (!m_databaseManager || !m_collections || !m_currentCollectionIndex) {
    return;
  }
  const int idx = (*m_currentCollectionIndex);
  if (idx < 0 || idx >= (*m_collections).size()) {
    return;
  }

  // Only refresh counts if the completed scan affects the currently visible
  // collection (or its descendants when showAllSubcollectionItems is active).
  CollectionConfig cur = (*m_collections)[idx];
  cur.mediaDirectory = PathUtils::validateAndExpandPath(cur.mediaDirectory, cur.name);
  if (!cur.mediaDirectory.trimmed().isEmpty()) {
    const QString curUuid = CollectionUtils::computeCollectionUuid(cur.name, cur.mediaDirectory);
    if (curUuid == collectionUuid) {
      m_backgroundCountRefreshInProgress = true;
      m_backgroundCountRefreshCollectionIndex = idx;
      loadCollectionData(idx);
      return;
    }
  }

  if (cur.showAllSubcollectionItems) {
    QList<int> descendants = CollectionUtils::collectDescendantIndices(idx, (*m_collections));
    for (int descendantIndex : descendants) {
      if (descendantIndex < 0 || descendantIndex >= (*m_collections).size()) {
        continue;
      }
      CollectionConfig subCol = (*m_collections)[descendantIndex];
      subCol.mediaDirectory = PathUtils::validateAndExpandPath(subCol.mediaDirectory, subCol.name);
      if (subCol.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      const QString subUuid = CollectionUtils::computeCollectionUuid(subCol.name, subCol.mediaDirectory);
      if (subUuid == collectionUuid) {
        m_backgroundCountRefreshInProgress = true;
        m_backgroundCountRefreshCollectionIndex = idx;
        loadCollectionData(idx);
        return;
      }
    }
  }
}

void NavigationManager::onItemsRangeLoaded(int offset, const QStringList &filePaths, const QHash<QString, QString> &fileNames) {
  const auto expectedGen = m_pendingRangeGenerations.value(offset, 0);
  if (expectedGen != m_itemsViewGeneration) {
    return;
  }
  m_pendingRangeGenerations.remove(offset);
  if (m_scrollManager) {
    m_scrollManager->receiveItemsRange(offset, filePaths, fileNames);
  }
}

void NavigationManager::fetchItemsRange(int offset, int limit) {
  const int idx = (*m_currentCollectionIndex);
  if (idx < 0 || idx >= (*m_collections).size()) {
    return;
  }

  CollectionContext context = m_hasItemsQueryContext ? m_itemsQueryContext
                                                     : buildExpandedContextForIndex(idx);

  QString filter;
  if (m_searchBar) {
      filter = m_searchBar->text().trimmed();
  }

  m_pendingRangeGenerations.insert(offset, m_itemsViewGeneration);

  m_databaseManager->fetchItemsRange(context, *m_collections, offset, limit, filter);
}