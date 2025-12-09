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
#include "sidebarmanager.h"
#include "timerutils.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QLabel>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <algorithm>

#ifdef KARTEND_DEBUG_LOGGING
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcNavigationManager, "kartend.navigationmanager")
#define debugLog(msg) qCDebug(lcNavigationManager) << msg
#else
#define debugLog(msg) do {} while(0)
#endif

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
  // Show loading overlay during navigation (will be updated with collection name later)
  if (m_loadingOverlay) {
    m_loadingOverlay->show("Preparing...");
  }
  
  if ((parent()) &&
      (m_interactionManager)) {
    m_interactionManager->stopRepeat();
    m_interactionManager->setNavigationInProgress(true);
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
  if (!parent()) {
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

// Loads data for the current collection
auto NavigationManager::loadCollectionData(int collectionIndex) -> void {
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

    bool hasMediaDirectory = !context.config.mediaDirectory.trimmed().isEmpty();
    if (hasMediaDirectory || context.config.showAllSubcollectionItems) {
      if (context.config.showAllSubcollectionItems) {
        bool hasDescendantWithMedia =
            collectionHasDescendantWithMedia(collectionIndex);
        if (hasDescendantWithMedia || hasMediaDirectory) {
          m_databaseManager->loadItemsWithSubcollections(
              context, (*m_collections));
        } else {
          QStringList emptyFilePaths;
          QHash<QString, QString> emptyFileNames;
          onItemsLoaded(emptyFilePaths, emptyFileNames);
        }
      } else {
        m_databaseManager->loadItems(context);
      }
    } else {
      QStringList emptyFilePaths;
      QHash<QString, QString> emptyFileNames;
      onItemsLoaded(emptyFilePaths, emptyFileNames);
    }
  }
}

// Shows items for a collection, prepares the view, and primes data loading
auto NavigationManager::showCollectionItems(int collectionIndex) -> bool {
  if (!validateCollectionIndex(collectionIndex)) {
    return false;
  }

  if ((parent()) &&
      (m_interactionManager)) {
    m_interactionManager->stopRepeat();
    m_interactionManager->cancelPendingSelectionRestore();
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
  if (m_allCollectionsActive) {
    context.config.showAllSubcollectionItems = true;
  }
  return context;
}

// Calculates the appropriate selection index for restoration
auto NavigationManager::calculateSelectionIndex(int totalItems) const -> int {
  bool searchActive = ((m_searchBar) &&
                       !m_searchBar->text().trimmed().isEmpty());

  if (searchActive || !(*m_generalSettings).rememberSelection ||
      totalItems <= 0) {
    return -1;
  }

  QString hierarchicalName = CollectionUtils::hierarchicalNameFor(
      (*m_collections)[(*m_currentCollectionIndex)],
      (*m_collections));
  int selIdx = -1;
  if (m_sessionManager) {
    selIdx = m_sessionManager->getLastSelectedIndex(hierarchicalName);
    if (selIdx < 0) {
      QString collectionName =
          (*m_collections)[(*m_currentCollectionIndex)].name;
      selIdx = m_sessionManager->getLastSelectedIndex(collectionName);
    }
  }
  if (selIdx >= totalItems) {
    selIdx = totalItems - 1;
  }
  return std::max(selIdx, 0);
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
  if (m_refreshTitleCounts) m_refreshTitleCounts();

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
#ifdef KARTEND_DEBUG_LOGGING
    int depth = computeCollectionDepth((*m_currentCollectionIndex));
    debugLog("[SelectionRestore] depth=" << depth << "for collection" 
             << (*m_collections)[(*m_currentCollectionIndex)].name);
#endif
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

  CollectionContext context;
  context.currentIndex = idx;
  context.config = (*m_collections)[idx];
  context.config.mediaDirectory = SettingsUtils::expandConfigVariables(
      context.config.mediaDirectory, context.config.name);
  context.config.artworkDirectory = SettingsUtils::expandConfigVariables(
      context.config.artworkDirectory, context.config.name);
  context.artworkDirectory = context.config.artworkDirectory;

  m_databaseManager->fetchItemCount(context, (*m_collections), searchText.trimmed());
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

// Safely reloads the specified collection and recenters the horizontal
// scrollbar
void NavigationManager::safeReloadCollection(int collectionIndex) {
  persistCurrentSelection();
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
        context.artworkDirectory = context.config.artworkDirectory;

        if (context.config.showAllSubcollectionItems) {
          m_databaseManager->loadItemsWithSubcollections(
              context, (*m_collections));
        } else {
          m_databaseManager->loadItems(context);
        }

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
// subcollection
auto NavigationManager::updateItemsPageTitle(int collectionIndex) -> void {
  if ((!parent()) || (!m_itemsPage)) {
    return;
  }
  auto *titleLabel =
      m_itemsPage->findChild<QLabel *>("itemsTitleLabel");
  if (!titleLabel) {
    return;
  }

  if (collectionIndex < 0 ||
      collectionIndex >= (*m_collections).size()) {
    titleLabel->clear();
    return;
  }

  QString title = (*m_collections)[collectionIndex].name;
  if ((*m_collections)[collectionIndex].isSubcollection) {
    int parentIdx =
        (*m_collections)[collectionIndex].parentCollectionIndex;
    if (parentIdx >= 0 && parentIdx < (*m_collections).size()) {
      title = (*m_collections)[parentIdx].name + " > " + title;
    }
  }
  titleLabel->setText(title);
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
  
  // Update the current subfolder path in the collection config
  CollectionConfig &config = (*m_collections)[*m_currentCollectionIndex];
  
  // Store previous subfolder for back navigation
  QString previousSubfolder = config.currentSubfolder;
  config.currentSubfolder = folderPath;
  
  // Reload the collection to show the new folder contents
  safeReloadCollection(*m_currentCollectionIndex);
}

void NavigationManager::goBackFromVirtualFolder() {
  if (!m_collections || (*m_currentCollectionIndex) < 0) {
    return;
  }
  
  CollectionConfig &config = (*m_collections)[*m_currentCollectionIndex];
  
  if (config.currentSubfolder.isEmpty()) {
    // Already at root, nothing to go back to
    return;
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

  if (context.config.showAllSubcollectionItems) {
    m_databaseManager->fetchItemCount(context, (*m_collections));
  } else {
    m_databaseManager->fetchItemCount(context, (*m_collections));
  }

  // Delay filter reapplication until item count query completes -
  // ensures filter operates on the updated item list
  QTimer::singleShot(UIConstants::Artwork::FILTER_REAPPLY_DELAY_MS, [this]() {
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
  m_allCollectionsActive = true;
  m_databaseManager->loadAllCollections(
      (*m_collections));

  // Delay filter reapplication until all-collections load completes -
  // ensures filter operates on the aggregated item list
  QTimer::singleShot(UIConstants::Artwork::FILTER_REAPPLY_DELAY_MS, [this]() {
    if (m_searchBar &&
        !m_searchBar->text().trimmed().isEmpty() &&
        m_scrollManager) {
      const QString currentSearchText =
          m_searchBar->text().trimmed();
      m_scrollManager->applyFilter(currentSearchText);
    }
  });
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

  // Update loading overlay to show item count found
  if (m_loadingOverlay && m_loadingOverlay->isActive()) {
    const QString &collectionName = (*m_collections)[idx].name;
    m_loadingOverlay->setMessage(
        QString("Loading %1 (%2 items)...").arg(collectionName).arg(count));
  }

  CollectionContext context;
  context.currentIndex = idx;
  context.config = (*m_collections)[idx];
  context.config.mediaDirectory = SettingsUtils::expandConfigVariables(
      context.config.mediaDirectory, context.config.name);
  context.config.artworkDirectory = SettingsUtils::expandConfigVariables(
      context.config.artworkDirectory, context.config.name);
  context.artworkDirectory = context.config.artworkDirectory;

  m_scrollManager->setupVirtualScrolling(count, context);
  
  if (m_loadingLabel) {
    m_loadingLabel->hide();
  }
  if (m_stackedWidget && m_itemsPage) {
    m_stackedWidget->setCurrentWidget(m_itemsPage);
  }
  
  // Restore selection if needed - delegate to SelectionRestoreManager
  if (m_selectionRestoreManager &&
      m_selectionRestoreManager->shouldRestoreSelection()) {
    int selIdx = m_selectionRestoreManager->getSelectionRestoreIndex(idx);
    if (selIdx >= 0) {
      int token = m_selectionRestoreManager->initializeSelectionRestoreToken();
      m_selectionRestoreManager->scheduleSelectionRestoreVerification(
          idx, selIdx, token);
    }
  }
}

void NavigationManager::onItemsRangeLoaded(int offset, const QStringList &filePaths, const QHash<QString, QString> &fileNames) {
    if (m_scrollManager) {
        m_scrollManager->receiveItemsRange(offset, filePaths, fileNames);
    }
}

void NavigationManager::fetchItemsRange(int offset, int limit) {
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

  QString filter;
  if (m_searchBar) {
      filter = m_searchBar->text().trimmed();
  }

  m_databaseManager->fetchItemsRange(context, *m_collections, offset, limit, filter);
}