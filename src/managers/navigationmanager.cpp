// Manages collection switching, navigation stack, and subcollection hierarchy traversal.
#include "navigationmanager.h"
#include "artworkmanager.h"
#include "databasemanager.h"
#include "interactionmanager.h"
#include "metadatasidebar.h"
#include "propertyutils.h"
#include "scrollmanager.h"
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
    : QObject(parent) {}

void NavigationManager::setupReferences(
    const NavigationManagerSetup &setup) {
  m_interactionManager = setup.interactionManager;
  m_settingsManager = setup.settingsManager;
  m_sidebarManager = setup.sidebarManager;
  m_scrollManager = setup.scrollManager;
  m_databaseManager = setup.databaseManager;
  m_sessionManager = setup.sessionManager;
  m_artworkManager = setup.artworkManager;
  m_metadataSidebar = setup.sidebar;
  m_currentCollectionIndex = setup.currentCollectionIndex;
  m_collections = setup.collections;
  m_hierarchyCache = setup.hierarchyCache;
  m_generalSettings = setup.generalSettings;
  m_searchBar = setup.searchBar;
  m_itemsPage = setup.itemsPage;
  m_stackedWidget = setup.stackedWidget;
  m_loadingLabel = setup.loadingLabel;
  m_itemScrollArea = setup.itemScrollArea;
  m_gridContainer = setup.gridContainer;
  m_isShuttingDown = setup.isShuttingDown;
  m_refreshTitleCounts = setup.refreshTitleCounts;
}

NavigationManager::~NavigationManager() = default;

// Navigates to a subcollection using the shared parent view
void NavigationManager::navigateWithSharedItems(int collectionIndex) {
  if (parent() == nullptr) {
    return;
  }

  initializeNavigationState();

  if (!validateAndPrepareNavigation(collectionIndex)) {
    return;
  }

  int previousIndex = (*m_currentCollectionIndex);

  if (m_interactionManager != nullptr) {
    m_interactionManager->clearSelectionAndFocus();
  }
  if (m_metadataSidebar != nullptr) {
    m_metadataSidebar->clearMetadata();
  }

  (*m_currentCollectionIndex) = collectionIndex;

  if (m_interactionManager != nullptr) {
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
  if ((parent() != nullptr) &&
      (m_interactionManager != nullptr)) {
    m_interactionManager->stopRepeat();
    m_interactionManager->m_navigationInProgress = true;
    auto *interactionManager = m_interactionManager;
    interactionManager->setProperty(PropertyKeys::RowChangeFirstClickIndex, -1);
    interactionManager->setProperty(PropertyKeys::RowChangeFirstClickMs, 0);
    interactionManager->setProperty(PropertyKeys::DoubleClickPending, false);
    interactionManager->setProperty(PropertyKeys::DoubleClickPendingIndex, -1);
    interactionManager->setProperty(PropertyKeys::ClickDeferralActive, false);
    interactionManager->setProperty(PropertyKeys::ClickDeferralIndex, -1);
    interactionManager->setProperty(PropertyKeys::DeferCenterOnClick, false);
    interactionManager->setProperty(PropertyKeys::DeferredCenterIndex, -1);
    interactionManager->setProperty(PropertyKeys::SelectionSuppressed, false);
    interactionManager->setProperty(PropertyKeys::PendingSelectionIndex, -1);
  }
  if (m_interactionManager) {
    m_interactionManager->cancelPendingSelectionRestore();
  }
}

auto NavigationManager::validateAndPrepareNavigation(int collectionIndex)
    -> bool {
  if (collectionIndex < 0 ||
      collectionIndex >= (*m_collections).size()) {
    return false;
  }

  bool hasSub = false;
  bool hasItems = false;
  if (!getHasSubAndItems(collectionIndex, hasSub, hasItems)) {
    if ((parent() != nullptr) &&
        (m_interactionManager != nullptr)) {
      QTimer::singleShot(
          UIConstants::NAVIGATION_PROGRESS_CLEAR_EARLY_MS, this,
          [this]() {
            if (parent() && m_interactionManager) {
              m_interactionManager->m_navigationInProgress =
                  false;
            }
          });
    }
    return false;
  }
  return true;
}

auto NavigationManager::shouldRestoreSelection() const -> bool {
  const bool remember = (*m_generalSettings).rememberSelection;
  const bool searchActive =
      ((m_searchBar != nullptr) &&
       !m_searchBar->text().trimmed().isEmpty());
  return remember && !searchActive &&
         (m_scrollManager != nullptr) &&
         (m_interactionManager != nullptr);
}

auto NavigationManager::getSelectionRestoreIndex(int collectionIndex) const
    -> int {
  if (m_scrollManager == nullptr) {
    return -1;
  }

  int total = m_scrollManager->getTotalItems();
  if (total <= 0) {
    return -1;
  }

  QString hierarchicalName =
      CollectionUtils::hierarchicalNameFor((*m_collections)[collectionIndex],
                          (*m_collections));
  int selIdx = -1;
  if (m_sessionManager) {
    selIdx = m_sessionManager->getLastSelectedIndex(hierarchicalName);
    if (selIdx < 0) {
      selIdx = m_sessionManager->getLastSelectedIndex(
          (*m_collections)[collectionIndex].name);
    }
  }
  if (selIdx >= total) {
    selIdx = total - 1;
  }
  return (selIdx >= 0) ? selIdx : -1;
}

auto NavigationManager::createSelectionRestoreLambda(int collectionIndex,
                                                     int selIdx,
                                                     int token)
    -> std::function<void()> {
  QPointer<NavigationManager> guard(this);
  return [guard, collectionIndex, selIdx, token]() {
    if (!guard) {
      return;
    }
    if ((*guard->m_currentCollectionIndex) != collectionIndex) {
      return;
    }
    if (guard->parent()->property(PropertyKeys::SelectionRestoreToken).toInt() != token) {
      return;
    }
    if (!guard->m_interactionManager || !guard->m_scrollManager) {
      return;
    }
    if (guard->m_interactionManager->currentSelectedIndex() != selIdx) {
      guard->m_interactionManager->beginSelectionRestore(selIdx);
    }
  };
}

auto NavigationManager::scheduleSelectionRestoreVerification(
    int collectionIndex, int selIdx, int token) -> void {
  auto restoreLambda =
      createSelectionRestoreLambda(collectionIndex, selIdx, token);

  QTimer::singleShot(UIConstants::SELECTION_RESTORE_EARLY_VERIFY_1_MS,
                     this, restoreLambda);
  QTimer::singleShot(UIConstants::SELECTION_RESTORE_EARLY_VERIFY_2_MS,
                     this, restoreLambda);
}

auto NavigationManager::handleSubcollectionNavigation(int collectionIndex,
                                                      int previousIndex)
    -> void {
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateContextForSubcollection(
        collectionIndex);
    m_scrollManager->applySubcollectionFilter(collectionIndex);
  }

  if (!shouldRestoreSelection()) {
    return;
  }

  int selIdx = getSelectionRestoreIndex(collectionIndex);
  if (selIdx < 0) {
    return;
  }

  int token =
      parent()->property(PropertyKeys::SelectionRestoreToken).toInt() + 1;
  parent()->setProperty(PropertyKeys::SelectionRestoreToken, token);

  m_interactionManager->beginSelectionRestore(selIdx);
  scheduleSelectionRestoreVerification(collectionIndex, selIdx, token);
}

auto NavigationManager::handleRegularNavigation(int collectionIndex) -> void {
  if (m_scrollManager != nullptr) {
    m_scrollManager->clearFilter();
  }
}

auto NavigationManager::finalizeNavigation(int collectionIndex) -> void {
  applyCollectionSettingsOnly(collectionIndex);
  if ((m_searchBar != nullptr) &&
      m_searchBar->text().trimmed().isEmpty()) {
    m_searchBar->clear();
  }
  QTimer::singleShot(UIConstants::MEDIUM_TIMER_DELAY, this, [this]() {
    m_scrollManager->centerHorizontalScrollbar(
        (*m_currentCollectionIndex), (*m_collections));
  });
  if (m_refreshTitleCounts) m_refreshTitleCounts();

  QTimer::singleShot(
      UIConstants::NAVIGATION_PROGRESS_CLEAR_MS, this, [this]() {
        if (parent() && m_interactionManager) {
          m_interactionManager->m_navigationInProgress = false;
        }
      });
}

// Validates basic context for selection restore operations
auto NavigationManager::validateSelectionRestoreContext() const -> bool {
  if ((parent() == nullptr) || QApplication::closingDown() ||
      m_isShuttingDown()) {
    return false;
  }
  if ((m_scrollManager == nullptr) ||
      (m_interactionManager == nullptr)) {
    return false;
  }
  return true;
}

// Initializes and returns the next selection restore token
auto NavigationManager::initializeSelectionRestoreToken() const -> int {
  const int token =
      parent()->property(PropertyKeys::SelectionRestoreToken).toInt() + 1;
  parent()->setProperty(PropertyKeys::SelectionRestoreToken, token);
  parent()->setProperty(PropertyKeys::SelectionRestorePending, true);
  return token;
}

// Creates validation lambda for selection restore state
auto NavigationManager::createRestoreValidationLambda(
    int scheduledCollectionIndex, int token) const -> std::function<bool()> {
  return [this, scheduledCollectionIndex, token]() -> bool {
    if (!validateSelectionRestoreContext()) {
      return false;
    }
    if ((*m_currentCollectionIndex) != scheduledCollectionIndex) {
      parent()->setProperty(PropertyKeys::SelectionRestorePending, false);
      return false;
    }
    if (parent()->property(PropertyKeys::SelectionRestoreToken).toInt() !=
        token) {
      parent()->setProperty(PropertyKeys::SelectionRestorePending, false);
      return false;
    }
    return true;
  };
}

// Executes the selection restore with delayed validation
auto NavigationManager::executeSelectionRestore(int desiredIndex,
                                                int scheduledCollectionIndex,
                                                int token) const -> void {
  auto validator =
      createRestoreValidationLambda(scheduledCollectionIndex, token);

  if (!validator()) {
    return;
  }

  int total = m_scrollManager->getTotalItems();
  if (desiredIndex >= 0 && desiredIndex < total) {
    QTimer::singleShot(
        UIConstants::MEDIUM_TIMER_DELAY, this,
        [this, desiredIndex, validator]() {
          if (validator()) {
            m_interactionManager->beginSelectionRestore(
                desiredIndex);
          }
        });
  }
  parent()->setProperty(PropertyKeys::SelectionRestorePending, false);
}

// Schedules selection restoration for the current collection with a cancelable
// token so late restores do not override user input
auto NavigationManager::scheduleSelectionRestore(int desiredIndex,
                                                 int maxAttempts,
                                                 int attemptDelayMs,
                                                 int finalEnsureDelayMs)
    -> void {
  Q_UNUSED(maxAttempts)
  Q_UNUSED(attemptDelayMs)

  if (!validateSelectionRestoreContext()) {
    return;
  }

  if (desiredIndex < 0) {
    if (m_interactionManager) {
      m_interactionManager->cancelPendingSelectionRestore();
    }
    return;
  }

  const int scheduledCollectionIndex = (*m_currentCollectionIndex);
  const int token = initializeSelectionRestoreToken();

  auto doRestore = [this, desiredIndex, scheduledCollectionIndex, token]() {
    executeSelectionRestore(desiredIndex, scheduledCollectionIndex, token);
  };

  if (m_scrollManager->getTotalItems() > 0) {
    QTimer::singleShot(UIConstants::SHORT_TIMER_DELAY, this, doRestore);
  } else {
    auto validator =
        createRestoreValidationLambda(scheduledCollectionIndex, token);
    auto *conn = new QMetaObject::Connection();
    *conn = connect(m_scrollManager,
                    &ScrollManager::virtualScrollSetupComplete, this,
                    [conn, doRestore, validator]() {
                      disconnect(*conn);
                      delete conn;
                      if (validator()) {
                        doRestore();
                      }
                    });
  }

  if (finalEnsureDelayMs > 0) {
    auto validator =
        createRestoreValidationLambda(scheduledCollectionIndex, token);
    QTimer::singleShot(finalEnsureDelayMs, this,
                       [doRestore, validator]() {
                         if (validator()) {
                           doRestore();
                         }
                       });
  }
}

// Validates collection index for showCollectionItems operation
auto NavigationManager::validateCollectionIndex(int collectionIndex) const
    -> bool {
  if (parent() == nullptr) {
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

  if (m_scrollManager != nullptr) {
    m_scrollManager->primeLayoutFor(
        (*m_collections)[collectionIndex]);
  }
  if (m_interactionManager != nullptr) {
    m_interactionManager
        ->initializeSearchModeForCurrentCollection();
  }

  updateItemsPageTitle(collectionIndex);
  m_stackedWidget->setCurrentWidget(m_itemsPage);
  if (m_itemsPage && m_itemsPage->window()) {
    m_itemsPage->window()->setFocus();
    m_itemsPage->window()->activateWindow();
  }

  if (m_sidebarManager != nullptr) {
    m_sidebarManager->applySidebarStateForCollection(
        (*m_currentCollectionIndex));
  }

  if ((m_searchBar != nullptr) &&
      m_searchBar->text().trimmed().isEmpty()) {
    m_searchBar->clear();
  }

  if (m_scrollManager != nullptr) {
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

  if ((parent() != nullptr) &&
      (m_interactionManager != nullptr)) {
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
// PropertyKeys and time window
auto NavigationManager::setSuppressArrowCenter(QScrollArea *scrollArea,
                                               int settleMs) -> void {
  if (scrollArea == nullptr) {
    return;
  }
  scrollArea->setProperty(PropertyKeys::SuppressArrowCenter, true);
  qint64 until = QDateTime::currentMSecsSinceEpoch() + settleMs;
  scrollArea->setProperty(PropertyKeys::SuppressArrowCenterUntilMs, until);
  QPointer<QScrollArea> guard = scrollArea;
  QTimer::singleShot(
      UIConstants::ARROW_CENTER_CLEAR_AFTER_SET_MS, this, [guard]() {
        if (guard) {
          guard->setProperty(PropertyKeys::SuppressArrowCenter, false);
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

  if (m_sidebarManager != nullptr) {
    m_sidebarManager->applySidebarStateForCollection(
        collectionIndex);
  }
}

void NavigationManager::restoreSelectionForCurrentCollection() {
  if ((parent() == nullptr) || QApplication::closingDown()) {
    return;
  }
  if ((m_scrollManager == nullptr) ||
      (m_interactionManager == nullptr)) {
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
  if (m_settingsManager != nullptr) {
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

  scheduleSelectionRestore(desired, UIConstants::SELECTION_RESTORE_STEPS,
                           UIConstants::SELECTION_RESTORE_STEP_DELAY_MS,
                           UIConstants::SELECTION_RESTORE_MAX_DELAY_MS);
}

// Performs common navigation cleanup operations
auto NavigationManager::performNavigationStackCleanup() -> void {
  if (m_interactionManager != nullptr) {
    m_interactionManager->clearSelectionAndFocus();
  }
  if (m_metadataSidebar != nullptr) {
    m_metadataSidebar->clearMetadata();
  }
  m_artworkManager->stopSilentLoading();
  if (m_artworkManager->getTimerCoordinator() != nullptr) {
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
  QTimer::singleShot(
      UIConstants::SHORT_TIMER_DELAY, this,
      [this, targetCollectionIndex, subcollectionVisualIndex]() {
        showCollectionItems(targetCollectionIndex);

        if (m_interactionManager) {
          m_interactionManager->m_navigationInProgress = false;
        }

        if (subcollectionVisualIndex >= 0 &&
            m_interactionManager) {
          QTimer::singleShot(
              UIConstants::MEDIUM_TIMER_DELAY, this,
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
  int targetCollectionIndex = m_navigationStack.takeLast();
  int previousIndex = (*m_currentCollectionIndex);
  m_navigationDepth =
      qMax(0, m_navigationDepth - 1);

  performNavigationStackCleanup();

  bool shared = areItemsShared(previousIndex, targetCollectionIndex);
  if (m_scrollManager != nullptr) {
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
  m_navigationDepth = 0;
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
    if (m_scrollManager != nullptr) {
      if (shared) {
        m_scrollManager->cleanupActiveWidgets();
      } else {
        m_scrollManager->cleanup();
      }
    }
    showCollectionItems(fallbackIndex);
  }

  if (m_interactionManager != nullptr) {
    m_interactionManager->m_navigationInProgress = false;
  }
}

// Goes back to collections and cancels any active held-key repeats to avoid
// stray timer callbacks
void NavigationManager::goBackToCollections() {
  if (parent() == nullptr) {
    return;
  }
  if ((parent() != nullptr) &&
      (m_interactionManager != nullptr)) {
    m_interactionManager->stopRepeat();
  }

  persistCurrentSelection();

  if (!m_navigationStack.isEmpty()) {
    handleNavigationStackPop();
  } else {
    handleNavigationFallback();
  }
}

void NavigationManager::onCollectionSelected(int collectionIndex) {
  m_navigationStack.clear();
  m_navigationDepth = 0;
  showCollectionItems(collectionIndex);
}

// Validates basic context for items loaded operations
auto NavigationManager::validateItemsLoadedContext() const -> bool {
  return parent() != nullptr && !QApplication::closingDown() &&
         !m_isShuttingDown();
}

// Cleans up existing no-items widgets from previous loads
auto NavigationManager::cleanupExistingNoItemsWidgets() -> void {
  if (m_loadingLabel != nullptr) {
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
  if ((parent() != nullptr) &&
      (m_interactionManager != nullptr)) {
    m_interactionManager->m_navigationInProgress = false;
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
  bool searchActive = ((m_searchBar != nullptr) &&
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
  QTimer::singleShot(UIConstants::START_SILENT_LOAD_AFTER_ITEMS_MS,
                     this, [this]() {
                       if (QApplication::closingDown() ||
                           m_isShuttingDown()) {
                         return;
                       }
                       m_artworkManager->startSilentLoading();
                     });

  if (m_databaseManager != nullptr) {
    m_databaseManager->updateCachedCounts(
        (*m_collections));
  }
  if (m_refreshTitleCounts) m_refreshTitleCounts();

  if (m_allCollectionsActive) {
    m_allCollectionsActive = false;
  }

  QTimer::singleShot(UIConstants::VIEWPORT_DELAY, this, [this]() {
    if (parent() && m_interactionManager) {
      m_interactionManager->m_navigationInProgress = false;
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

  if (m_scrollManager != nullptr) {
    m_scrollManager->setupVirtualScrolling(totalItems, context);
  }

  resumeItemsPageRendering();

  if (m_artworkManager != nullptr) {
    m_artworkManager->updateViewportArtwork();
  }
  if (m_artworkManager != nullptr &&
      m_artworkManager->getTimerCoordinator() != nullptr) {
    m_artworkManager->getTimerCoordinator()->scheduleViewportUpdate();
  }

  bool pendingRestore =
      parent()->property(PropertyKeys::SelectionRestorePending).toBool();
  if (selIdx >= 0 && (m_interactionManager != nullptr) &&
      !pendingRestore) {
    int depth = computeCollectionDepth((*m_currentCollectionIndex));
    if (depth >= 3) {
      m_interactionManager->beginSelectionRestore(selIdx);
    } else {
      scheduleSelectionRestore(selIdx, UIConstants::SELECTION_RESTORE_STEPS,
                               UIConstants::SELECTION_RESTORE_STEP_DELAY_MS,
                               UIConstants::SELECTION_RESTORE_MAX_DELAY_MS);
    }
  }

  schedulePostLoadOperations();
}

void NavigationManager::onMediaLibraryError(const QString &error) {
  if (m_loadingLabel != nullptr) {
    m_loadingLabel->deleteLater();
    m_loadingLabel = nullptr;
  }

  QList<QWidget *> existingLabels =
      m_gridContainer->findChildren<QWidget *>("noItemsWidget");
  for (QWidget *widget : existingLabels) {
    widget->deleteLater();
  }

  auto *errorWidget = new QWidget(m_gridContainer);
  errorWidget->setObjectName("noItemsWidget");

  auto *errorLabel = new QLabel(error, errorWidget);
  errorLabel->setAlignment(Qt::AlignCenter);
  errorLabel->setStyleSheet(
      "QLabel { color: palette(text); font-size: 14px; }");

  auto *layout = new QVBoxLayout(errorWidget);
  layout->addWidget(errorLabel);
  layout->setContentsMargins(0, 0, 0, 0);

  if (m_itemScrollArea != nullptr) {
    QRect viewportRect = m_itemScrollArea->viewport()->rect();
    errorWidget->setGeometry(viewportRect);
  }

  errorWidget->show();
  errorWidget->raise();

  if (m_databaseManager != nullptr) {
    m_databaseManager->updateCachedCounts(
        (*m_collections));
  }
  if (m_refreshTitleCounts) m_refreshTitleCounts();
}

void NavigationManager::onViewportChanged() {
  if ((m_interactionManager != nullptr) &&
      m_interactionManager->isWheelScrolling() &&
      m_stackedWidget->currentWidget() == m_itemsPage) {
    QTimer::singleShot(UIConstants::SHORT_TIMER_DELAY, this, [this]() {
      if (m_scrollManager) {
        m_scrollManager->updateVirtualView();
      }
      m_artworkManager->updateViewportArtwork();
    });
  } else {
    if (auto *coord = m_artworkManager->getTimerCoordinator()) {
      coord->scheduleViewportUpdate();
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
  if (collectionIndex < 0 ||
      collectionIndex >= (*m_collections).size()) {
    return;
  }

  if (auto *coord = m_artworkManager->getTimerCoordinator()) {
    coord->stopAllTimers();
  }

  if (m_scrollManager != nullptr) {
    m_scrollManager->cleanup();
  }

  m_artworkManager->cancelAllArtworkLoading();

  QTimer::singleShot(
      UIConstants::MEDIUM_TIMER_DELAY, this, [this, collectionIndex]() {
        if (collectionIndex < 0 ||
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

        QTimer::singleShot(UIConstants::VIEWPORT_DELAY, this, [this]() {
          if (m_scrollManager) {
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
  if ((parent() == nullptr) || (m_itemsPage == nullptr)) {
    return;
  }
  auto *titleLabel =
      m_itemsPage->findChild<QLabel *>("itemsTitleLabel");
  if (titleLabel == nullptr) {
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
  if ((parent() == nullptr) || collectionIndex < 0 ||
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

  QString mediaDir = (m_settingsManager != nullptr)
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
  if (m_hierarchyCache != nullptr && m_hierarchyCache->isValid()) {
    return m_hierarchyCache->directChildren(parentIndex);
  }
  // Fallback to O(n) scan
  if (m_collections == nullptr) {
    return {};
  }
  return CollectionUtils::directChildrenOf(parentIndex, *m_collections);
}

// Sets the collections pointer used for subcollection queries
void NavigationManager::onSubcollectionEntered(int subcollectionIndex) {
  if (subcollectionIndex >= 0 &&
      subcollectionIndex < (*m_collections).size()) {
    if (m_interactionManager != nullptr) {
      qint64 now = QDateTime::currentMSecsSinceEpoch();
      m_interactionManager->setProperty(
          PropertyKeys::SuppressDoubleClickUntilMs,
          now + UIConstants::DOUBLE_CLICK_SUPPRESS_AFTER_ENTER_MS);
    }

    if ((*m_currentCollectionIndex) >= 0 &&
        (m_interactionManager != nullptr)) {
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
      m_navigationStack.append(
          (*m_currentCollectionIndex));
      m_navigationDepth++;
    }

    m_settingsManager->setLastSelectedItem(subcollectionIndex,
                                                         -1);

    bool success = showCollectionItems(subcollectionIndex);
    if (!success) {
      if (!m_navigationStack.isEmpty()) {
        m_navigationStack.removeLast();
      }
      m_navigationDepth =
          qMax(0, m_navigationDepth - 1);
      return;
    }

    QTimer::singleShot(
        UIConstants::SUBCOLLECTION_SCROLL_CENTER_DELAY_MS, this,
        [this]() {
          m_scrollManager->centerHorizontalScrollbar(
              (*m_currentCollectionIndex),
              (*m_collections));
        });

    QTimer::singleShot(UIConstants::DOUBLE_CLICK_SUPPRESS_CLEAR_DELAY_MS,
                       this, [this]() {
                         if (m_interactionManager) {
                           m_interactionManager->setProperty(
                               PropertyKeys::SuppressDoubleClickUntilMs, 0);
                         }
                       });
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

  if (context.config.showAllSubcollectionItems) {
    m_databaseManager->fetchItemCount(context, (*m_collections));
  } else {
    m_databaseManager->fetchItemCount(context, (*m_collections));
  }

  QTimer::singleShot(UIConstants::FILTER_REAPPLY_DELAY_MS, [this]() {
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

  QTimer::singleShot(UIConstants::FILTER_REAPPLY_DELAY_MS, [this]() {
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
  if (m_hierarchyCache != nullptr && m_hierarchyCache->isValid()) {
    return m_hierarchyCache->allDescendants(parentIndex);
  }
  // Fallback to O(n) recursive scan
  QList<int> result;
  if ((m_collections == nullptr) || parentIndex < 0 ||
      parentIndex >= m_collections->size()) {
    return result;
  }

  QList<int> stack = CollectionUtils::directChildrenOf(parentIndex, *m_collections);
  QSet<int> seen;
  while (!stack.isEmpty()) {
    int idx = stack.takeLast();
    if (idx < 0 || idx >= m_collections->size()) {
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
  if ((m_interactionManager == nullptr) ||
      (m_settingsManager == nullptr) ||
      (m_currentCollectionIndex == nullptr) ||
      (m_collections == nullptr)) {
    return;
  }
  int coll = *m_currentCollectionIndex;
  if (coll < 0 || coll >= m_collections->size()) {
    return;
  }
  int sel = m_interactionManager->currentSelectedIndex();
  if (sel < 0) {
    return;
  }
  m_settingsManager->setLastSelectedItem(coll, sel);
}

void NavigationManager::prepareForNonSharedNavigationHelper() {
  if (m_interactionManager != nullptr) {
    m_interactionManager->clearSelectionAndFocus();
  }
  if (m_metadataSidebar != nullptr) {
    m_metadataSidebar->clearMetadata();
  }
  m_artworkManager->stopSilentLoading();
  if (m_artworkManager->getTimerCoordinator() != nullptr) {
    m_artworkManager->getTimerCoordinator()->stopAllTimers();
  }
  if (m_scrollManager != nullptr) {
    m_scrollManager->cleanup();
  }
}

void NavigationManager::suspendItemsPageRendering() {
  if (m_itemScrollArea != nullptr) {
    m_itemScrollArea->setUpdatesEnabled(false);
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArtwork, true);
  }
  if (m_gridContainer != nullptr) {
    m_gridContainer->setUpdatesEnabled(false);
    m_gridContainer->setVisible(false);
  }
}

void NavigationManager::resumeItemsPageRendering() {
  if (m_gridContainer != nullptr) {
    m_gridContainer->setVisible(true);
    m_gridContainer->setUpdatesEnabled(true);
  }
  if (m_itemScrollArea != nullptr) {
    m_itemScrollArea->setUpdatesEnabled(true);
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArtwork, false);
  }
}

void NavigationManager::applyUiPoliciesForCollection(int collectionIndex) {
  if (m_sidebarManager != nullptr) {
    m_sidebarManager->applySidebarStateForCollection(collectionIndex);
  }
  if (m_settingsManager != nullptr && m_itemScrollArea != nullptr && m_collections != nullptr) {
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

  CollectionContext context;
  context.currentIndex = idx;
  context.config = (*m_collections)[idx];
  context.config.mediaDirectory = SettingsUtils::expandConfigVariables(
      context.config.mediaDirectory, context.config.name);
  context.config.artworkDirectory = SettingsUtils::expandConfigVariables(
      context.config.artworkDirectory, context.config.name);
  context.artworkDirectory = context.config.artworkDirectory;

  m_scrollManager->setupVirtualScrolling(count, context);
  
  if (m_loadingLabel != nullptr) {
    m_loadingLabel->hide();
  }
  if (m_stackedWidget != nullptr && m_itemsPage != nullptr) {
    m_stackedWidget->setCurrentWidget(m_itemsPage);
  }
  
  // Restore selection if needed
  if (shouldRestoreSelection()) {
      int selIdx = getSelectionRestoreIndex(idx);
      if (selIdx >= 0) {
          int token = initializeSelectionRestoreToken();
          scheduleSelectionRestoreVerification(idx, selIdx, token);
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