#include "navigationmanager.h"
#include "artworkmanager.h"
#include "databasemanager.h"
#include "interactionmanager.h"
#include "mainwindow.h"
#include "metadatasidebar.h"
#include "propertyutils.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
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

namespace {
inline void persistCurrentSelection(MainWindow *mainWindow) {
  if ((mainWindow == nullptr) ||
      (mainWindow->getInteractionManager() == nullptr) ||
      (mainWindow->getSettingsManager() == nullptr)) {
    return;
  }
  int coll = mainWindow->currentCollectionIndex;
  if (coll < 0 || coll >= mainWindow->m_collections.size()) {
    return;
  }
  int sel = mainWindow->getInteractionManager()->currentSelectedIndex();
  if (sel < 0) {
    return;
  }
  mainWindow->getSettingsManager()->setLastSelectedItem(coll, sel);
}


inline void prepareForNonSharedNavigation(MainWindow *mainWindow) {
  if (mainWindow == nullptr) {
    return;
  }
  if (mainWindow->getInteractionManager() != nullptr) {
    mainWindow->getInteractionManager()->clearSelectionAndFocus();
  }
  if (mainWindow->getMetadataSidebar() != nullptr) {
    mainWindow->getMetadataSidebar()->clearMetadata();
  }
  ArtworkManager::instance().stopSilentLoading();
  if (ArtworkManager::instance().getTimerCoordinator() != nullptr) {
    ArtworkManager::instance().getTimerCoordinator()->stopAllTimers();
  }
  if (mainWindow->getScrollManager() != nullptr) {
    mainWindow->getScrollManager()->cleanup();
  }
}

inline void suspendItemsPageRendering(MainWindow *mainWindow) {
  if (mainWindow == nullptr) {
    return;
  }
  if (QScrollArea *scrollArea = mainWindow->ui->itemScrollArea) {
    scrollArea->setUpdatesEnabled(false);
    scrollArea->setProperty(PropertyKeys::SuppressArtwork, true);
  }
  if (QWidget *gridContainer = mainWindow->gridContainer) {
    gridContainer->setUpdatesEnabled(false);
    gridContainer->setVisible(false);
  }
}

inline void resumeItemsPageRendering(MainWindow *mainWindow) {
  if (mainWindow == nullptr) {
    return;
  }
  if (QWidget *gridContainer = mainWindow->gridContainer) {
    gridContainer->setVisible(true);
    gridContainer->setUpdatesEnabled(true);
  }
  if (QScrollArea *scrollArea = mainWindow->ui->itemScrollArea) {
    scrollArea->setUpdatesEnabled(true);
    scrollArea->setProperty(PropertyKeys::SuppressArtwork, false);
  }
}

inline void applyUiPoliciesForCollection(MainWindow *mainWindow,
                                         int collectionIndex) {
  if (mainWindow == nullptr) {
    return;
  }
  if (mainWindow->getSidebarManager() != nullptr) {
    mainWindow->getSidebarManager()->applySidebarStateForCollection(
        collectionIndex);
  }
  if (mainWindow->getSettingsManager() != nullptr) {
    SettingsManager::applyHorizontalScrollbarSetting(
        mainWindow, collectionIndex, mainWindow->m_collections);
    SettingsManager::applyVerticalScrollbarSetting(mainWindow, collectionIndex,
                                                   mainWindow->m_collections);
  }
}
} // namespace

NavigationManager::NavigationManager(MainWindow *mainWindow)
    : QObject(mainWindow), m_mainWindow(mainWindow) {}

NavigationManager::~NavigationManager() = default;

// Navigates to a subcollection using the shared parent view
void NavigationManager::navigateWithSharedItems(int collectionIndex) {
  if (m_mainWindow == nullptr) {
    return;
  }

  initializeNavigationState();

  if (!validateAndPrepareNavigation(collectionIndex)) {
    return;
  }

  int previousIndex = m_mainWindow->currentCollectionIndex;

  if (m_mainWindow->getInteractionManager() != nullptr) {
    m_mainWindow->getInteractionManager()->clearSelectionAndFocus();
  }
  if (m_mainWindow->getMetadataSidebar() != nullptr) {
    m_mainWindow->getMetadataSidebar()->clearMetadata();
  }

  m_mainWindow->currentCollectionIndex = collectionIndex;

  if (m_mainWindow->getInteractionManager() != nullptr) {
    m_mainWindow->getInteractionManager()
        ->initializeSearchModeForCurrentCollection();
  }

  updateItemsPageTitle(collectionIndex);

  bool isNavigatingToSubcollection =
      (m_mainWindow->m_collections[collectionIndex].parentCollectionIndex ==
       previousIndex);

  if (isNavigatingToSubcollection) {
    handleSubcollectionNavigation(collectionIndex, previousIndex);
  } else {
    handleRegularNavigation(collectionIndex);
  }

  finalizeNavigation(collectionIndex);
}

auto NavigationManager::initializeNavigationState() -> void {
  if ((m_mainWindow != nullptr) &&
      (m_mainWindow->getInteractionManager() != nullptr)) {
    m_mainWindow->getInteractionManager()->stopRepeat();
    m_mainWindow->getInteractionManager()->m_navigationInProgress = true;
    auto *interactionManager = m_mainWindow->getInteractionManager();
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
  if (m_mainWindow->getInteractionManager()) {
    m_mainWindow->getInteractionManager()->cancelPendingSelectionRestore();
  }
}

auto NavigationManager::validateAndPrepareNavigation(int collectionIndex)
    -> bool {
  if (collectionIndex < 0 ||
      collectionIndex >= m_mainWindow->m_collections.size()) {
    return false;
  }

  bool hasSub = false;
  bool hasItems = false;
  if (!getHasSubAndItems(collectionIndex, hasSub, hasItems)) {
    if ((m_mainWindow != nullptr) &&
        (m_mainWindow->getInteractionManager() != nullptr)) {
      QTimer::singleShot(
          UIConstants::NAVIGATION_PROGRESS_CLEAR_EARLY_MS, m_mainWindow,
          [this]() {
            if (m_mainWindow && m_mainWindow->getInteractionManager()) {
              m_mainWindow->getInteractionManager()->m_navigationInProgress =
                  false;
            }
          });
    }
    return false;
  }
  return true;
}

auto NavigationManager::shouldRestoreSelection() const -> bool {
  const bool remember = m_mainWindow->m_generalSettings.rememberSelection;
  const bool searchActive =
      ((m_mainWindow->searchBar != nullptr) &&
       !m_mainWindow->searchBar->text().trimmed().isEmpty());
  return remember && !searchActive &&
         (m_mainWindow->getScrollManager() != nullptr) &&
         (m_mainWindow->getInteractionManager() != nullptr);
}

auto NavigationManager::getSelectionRestoreIndex(int collectionIndex) const
    -> int {
  if (m_mainWindow->getScrollManager() == nullptr) {
    return -1;
  }

  int total = m_mainWindow->getScrollManager()->getTotalItems();
  if (total <= 0) {
    return -1;
  }

  QString hierarchicalName =
      hierarchicalNameFor(m_mainWindow->m_collections[collectionIndex],
                          m_mainWindow->m_collections);
  int selIdx =
      SessionManager::instance().getLastSelectedIndex(hierarchicalName);
  if (selIdx < 0) {
    selIdx = SessionManager::instance().getLastSelectedIndex(
        m_mainWindow->m_collections[collectionIndex].name);
  }
  if (selIdx >= total) {
    selIdx = total - 1;
  }
  return (selIdx >= 0) ? selIdx : -1;
}

auto NavigationManager::createSelectionRestoreLambda(int collectionIndex,
                                                     int selIdx,
                                                     int token) const
    -> std::function<void()> {
  QPointer<MainWindow> guard(m_mainWindow);
  return [guard, collectionIndex, selIdx, token]() {
    if (!guard) {
      return;
    }
    if (guard->currentCollectionIndex != collectionIndex) {
      return;
    }
    if (guard->property(PropertyKeys::SelectionRestoreToken).toInt() != token) {
      return;
    }
    if (!guard->getInteractionManager() || !guard->getScrollManager()) {
      return;
    }
    if (guard->getInteractionManager()->currentSelectedIndex() != selIdx) {
      guard->getInteractionManager()->beginSelectionRestore(selIdx);
    }
  };
}

auto NavigationManager::scheduleSelectionRestoreVerification(
    int collectionIndex, int selIdx, int token) -> void {
  auto restoreLambda =
      createSelectionRestoreLambda(collectionIndex, selIdx, token);

  QTimer::singleShot(UIConstants::SELECTION_RESTORE_EARLY_VERIFY_1_MS,
                     m_mainWindow, restoreLambda);
  QTimer::singleShot(UIConstants::SELECTION_RESTORE_EARLY_VERIFY_2_MS,
                     m_mainWindow, restoreLambda);
}

auto NavigationManager::handleSubcollectionNavigation(int collectionIndex,
                                                      int previousIndex)
    -> void {
  bool targetHasNestedItems =
      m_mainWindow->m_collections[collectionIndex].showAllSubcollectionItems;
  bool targetHasChildren = false;
  if (targetHasNestedItems) {
    targetHasChildren = collectionHasDescendantWithMedia(collectionIndex);
  }

  if (targetHasChildren) {
    return;
  }

  if (m_mainWindow->getScrollManager() != nullptr) {
    m_mainWindow->getScrollManager()->updateContextForSubcollection(
        collectionIndex);
    m_mainWindow->getScrollManager()->applySubcollectionFilter(collectionIndex);
  }

  if (!shouldRestoreSelection()) {
    return;
  }

  int selIdx = getSelectionRestoreIndex(collectionIndex);
  if (selIdx < 0) {
    return;
  }

  int token =
      m_mainWindow->property(PropertyKeys::SelectionRestoreToken).toInt() + 1;
  m_mainWindow->setProperty(PropertyKeys::SelectionRestoreToken, token);

  m_mainWindow->getInteractionManager()->beginSelectionRestore(selIdx);
  scheduleSelectionRestoreVerification(collectionIndex, selIdx, token);
}

auto NavigationManager::handleRegularNavigation(int collectionIndex) -> void {
  if (m_mainWindow->getScrollManager() != nullptr) {
    m_mainWindow->getScrollManager()->clearFilter();
  }
}

auto NavigationManager::finalizeNavigation(int collectionIndex) -> void {
  applyCollectionSettingsOnly(collectionIndex);
  if ((m_mainWindow->searchBar != nullptr) &&
      m_mainWindow->searchBar->text().trimmed().isEmpty()) {
    m_mainWindow->searchBar->clear();
  }
  QTimer::singleShot(UIConstants::MEDIUM_TIMER_DELAY, m_mainWindow, [this]() {
    m_mainWindow->getScrollManager()->centerHorizontalScrollbar(
        m_mainWindow->currentCollectionIndex, m_mainWindow->m_collections);
  });
  m_mainWindow->refreshTitleCounts();

  QTimer::singleShot(
      UIConstants::NAVIGATION_PROGRESS_CLEAR_MS, m_mainWindow, [this]() {
        if (m_mainWindow && m_mainWindow->getInteractionManager()) {
          m_mainWindow->getInteractionManager()->m_navigationInProgress = false;
        }
      });
}

// Validates basic context for selection restore operations
auto NavigationManager::validateSelectionRestoreContext() const -> bool {
  if ((m_mainWindow == nullptr) || QApplication::closingDown() ||
      m_mainWindow->m_isShuttingDown) {
    return false;
  }
  if ((m_mainWindow->getScrollManager() == nullptr) ||
      (m_mainWindow->getInteractionManager() == nullptr)) {
    return false;
  }
  return true;
}

// Initializes and returns the next selection restore token
auto NavigationManager::initializeSelectionRestoreToken() const -> int {
  const int token =
      m_mainWindow->property(PropertyKeys::SelectionRestoreToken).toInt() + 1;
  m_mainWindow->setProperty(PropertyKeys::SelectionRestoreToken, token);
  m_mainWindow->setProperty(PropertyKeys::SelectionRestorePending, true);
  return token;
}

// Creates validation lambda for selection restore state
auto NavigationManager::createRestoreValidationLambda(
    int scheduledCollectionIndex, int token) const -> std::function<bool()> {
  return [this, scheduledCollectionIndex, token]() -> bool {
    if (!validateSelectionRestoreContext()) {
      return false;
    }
    if (m_mainWindow->currentCollectionIndex != scheduledCollectionIndex) {
      m_mainWindow->setProperty(PropertyKeys::SelectionRestorePending, false);
      return false;
    }
    if (m_mainWindow->property(PropertyKeys::SelectionRestoreToken).toInt() !=
        token) {
      m_mainWindow->setProperty(PropertyKeys::SelectionRestorePending, false);
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

  int total = m_mainWindow->getScrollManager()->getTotalItems();
  if (desiredIndex >= 0 && desiredIndex < total) {
    QTimer::singleShot(
        UIConstants::MEDIUM_TIMER_DELAY, m_mainWindow,
        [this, desiredIndex, validator]() {
          if (validator()) {
            m_mainWindow->getInteractionManager()->beginSelectionRestore(
                desiredIndex);
          }
        });
  }
  m_mainWindow->setProperty(PropertyKeys::SelectionRestorePending, false);
}

// Schedules selection restoration for the current collection with a cancelable
// token so late restores do not override user input
auto NavigationManager::scheduleSelectionRestore(int desiredIndex,
                                                 int maxAttempts,
                                                 int attemptDelayMs,
                                                 int finalEnsureDelayMs) const
    -> void {
  Q_UNUSED(maxAttempts)
  Q_UNUSED(attemptDelayMs)

  if (!validateSelectionRestoreContext()) {
    return;
  }

  if (desiredIndex < 0) {
    if (m_mainWindow->getInteractionManager()) {
      m_mainWindow->getInteractionManager()->cancelPendingSelectionRestore();
    }
    return;
  }

  const int scheduledCollectionIndex = m_mainWindow->currentCollectionIndex;
  const int token = initializeSelectionRestoreToken();

  auto doRestore = [this, desiredIndex, scheduledCollectionIndex, token]() {
    executeSelectionRestore(desiredIndex, scheduledCollectionIndex, token);
  };

  if (m_mainWindow->getScrollManager()->getTotalItems() > 0) {
    QTimer::singleShot(UIConstants::SHORT_TIMER_DELAY, m_mainWindow, doRestore);
  } else {
    auto validator =
        createRestoreValidationLambda(scheduledCollectionIndex, token);
    auto *conn = new QMetaObject::Connection();
    *conn = connect(m_mainWindow->getScrollManager(),
                    &ScrollManager::virtualScrollSetupComplete, m_mainWindow,
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
    QTimer::singleShot(finalEnsureDelayMs, m_mainWindow,
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
  if (m_mainWindow == nullptr) {
    return false;
  }
  if (collectionIndex < 0 ||
      collectionIndex >= m_mainWindow->m_collections.size()) {
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
      areItemsShared(m_mainWindow->currentCollectionIndex, collectionIndex);
  if (itemsAreShared) {
    navigateWithSharedItems(collectionIndex);
    m_mainWindow->refreshTitleCounts();
    return true;
  }
  return false;
}

// Prepares UI for non-shared navigation
auto NavigationManager::prepareNonSharedNavigation(int collectionIndex)
    -> void {
  prepareForNonSharedNavigation(m_mainWindow);
  suspendItemsPageRendering(m_mainWindow);

  m_mainWindow->currentCollectionIndex = collectionIndex;

  applyUiPoliciesForCollection(m_mainWindow, collectionIndex);
  applyCollectionSettingsOnly(collectionIndex);

  if (m_mainWindow->getScrollManager() != nullptr) {
    m_mainWindow->getScrollManager()->primeLayoutFor(
        m_mainWindow->m_collections[collectionIndex]);
  }
  if (m_mainWindow->getInteractionManager() != nullptr) {
    m_mainWindow->getInteractionManager()
        ->initializeSearchModeForCurrentCollection();
  }

  updateItemsPageTitle(collectionIndex);
  m_mainWindow->stackedWidget->setCurrentWidget(m_mainWindow->itemsPage);
  m_mainWindow->setFocus();
  m_mainWindow->activateWindow();

  if (m_mainWindow->getSidebarManager() != nullptr) {
    m_mainWindow->getSidebarManager()->applySidebarStateForCollection(
        m_mainWindow->currentCollectionIndex);
  }

  if ((m_mainWindow->searchBar != nullptr) &&
      m_mainWindow->searchBar->text().trimmed().isEmpty()) {
    m_mainWindow->searchBar->clear();
  }

  if (m_mainWindow->getScrollManager() != nullptr) {
    m_mainWindow->getScrollManager()->clearFilter();
  }
}

// Loads data for the current collection
auto NavigationManager::loadCollectionData(int collectionIndex) -> void {
  if (m_mainWindow->currentCollectionIndex >= 0 &&
      m_mainWindow->currentCollectionIndex <
          m_mainWindow->m_collections.size()) {
    CollectionContext context;
    context.currentIndex = m_mainWindow->currentCollectionIndex;
    context.config =
        m_mainWindow->m_collections[m_mainWindow->currentCollectionIndex];
    context.config.mediaDirectory = SettingsManager::expandConfigVariables(
        context.config.mediaDirectory, context.config.name);
    context.config.artworkDirectory = SettingsManager::expandConfigVariables(
        context.config.artworkDirectory, context.config.name);
    context.artworkDirectory = context.config.artworkDirectory;

    bool hasMediaDirectory = !context.config.mediaDirectory.trimmed().isEmpty();
    if (hasMediaDirectory || context.config.showAllSubcollectionItems) {
      if (context.config.showAllSubcollectionItems) {
        bool hasDescendantWithMedia =
            collectionHasDescendantWithMedia(collectionIndex);
        if (hasDescendantWithMedia) {
          m_mainWindow->getDatabaseManager()->loadItemsWithSubcollections(
              context, m_mainWindow->m_collections);
        } else {
          QStringList emptyFilePaths;
          QHash<QString, QString> emptyFileNames;
          onItemsLoaded(emptyFilePaths, emptyFileNames);
        }
      } else {
        m_mainWindow->getDatabaseManager()->loadItems(context);
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

  if ((m_mainWindow != nullptr) &&
      (m_mainWindow->getInteractionManager() != nullptr)) {
    m_mainWindow->getInteractionManager()->stopRepeat();
    m_mainWindow->getInteractionManager()->cancelPendingSelectionRestore();
  }

  persistCurrentSelection(m_mainWindow);

  if (handleSharedItemsNavigation(collectionIndex)) {
    return true;
  }

  prepareNonSharedNavigation(collectionIndex);
  loadCollectionData(collectionIndex);
  m_mainWindow->refreshTitleCounts();
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
      UIConstants::ARROW_CENTER_CLEAR_AFTER_SET_MS, m_mainWindow, [guard]() {
        if (guard) {
          guard->setProperty(PropertyKeys::SuppressArrowCenter, false);
        }
      });
}

auto NavigationManager::areItemsShared(int fromIndex, int toIndex) const
    -> bool {
  if (fromIndex < 0 || toIndex < 0 ||
      fromIndex >= m_mainWindow->m_collections.size() ||
      toIndex >= m_mainWindow->m_collections.size()) {
    return false;
  }

  const auto &fromCollection = m_mainWindow->m_collections[fromIndex];
  const auto &toCollection = m_mainWindow->m_collections[toIndex];

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
            childIndex >= m_mainWindow->m_collections.size()) {
          return false;
        }
        QString childMediaDir = SettingsManager::expandConfigVariables(
            m_mainWindow->m_collections[childIndex].mediaDirectory,
            m_mainWindow->m_collections[childIndex].name);
        return !childMediaDir.trimmed().isEmpty();
      });
  return !anyDescendantWithMedia;
}

auto NavigationManager::applyCollectionSettingsOnly(int collectionIndex)
    -> void {
  if (collectionIndex < 0 ||
      collectionIndex >= m_mainWindow->m_collections.size()) {
    return;
  }

  const CollectionConfig &collection =
      m_mainWindow->m_collections[collectionIndex];

  if (collection.gridWidth !=
      m_mainWindow->getScrollManager()->getCurrentGridWidth()) {
    m_mainWindow->getScrollManager()->updateGridWidth(collection.gridWidth);
  }

  m_mainWindow->getSettingsManager()->applyHorizontalScrollbarSetting(
      m_mainWindow, collectionIndex, m_mainWindow->m_collections);
  m_mainWindow->getSettingsManager()->applyVerticalScrollbarSetting(
      m_mainWindow, collectionIndex, m_mainWindow->m_collections);

  if (m_mainWindow->getSidebarManager() != nullptr) {
    m_mainWindow->getSidebarManager()->applySidebarStateForCollection(
        collectionIndex);
  }
}

void NavigationManager::restoreSelectionForCurrentCollection() {
  if ((m_mainWindow == nullptr) || QApplication::closingDown()) {
    return;
  }
  if ((m_mainWindow->getScrollManager() == nullptr) ||
      (m_mainWindow->getInteractionManager() == nullptr)) {
    return;
  }
  int coll = m_mainWindow->currentCollectionIndex;
  if (coll < 0 || coll >= m_mainWindow->m_collections.size()) {
    return;
  }
  int total = m_mainWindow->getScrollManager()->getTotalItems();
  if (total <= 0) {
    return;
  }
  int desired = -1;
  if (m_mainWindow->getSettingsManager() != nullptr) {
    desired = m_mainWindow->getSettingsManager()->getLastSelectedItem(coll);
  } else {
    desired = m_mainWindow->getSettingsManager()->getLastSelectedItem(coll);
  }
  if (desired < 0 || desired >= total) {
    desired = 0;
  }
  if (m_mainWindow->getInteractionManager()->currentSelectedIndex() == desired) {
    return;
  }

  scheduleSelectionRestore(desired, UIConstants::SELECTION_RESTORE_STEPS,
                           UIConstants::SELECTION_RESTORE_STEP_DELAY_MS,
                           UIConstants::SELECTION_RESTORE_MAX_DELAY_MS);
}

// Performs common navigation cleanup operations
auto NavigationManager::performNavigationStackCleanup() -> void {
  if (m_mainWindow->getInteractionManager() != nullptr) {
    m_mainWindow->getInteractionManager()->clearSelectionAndFocus();
  }
  if (m_mainWindow->getMetadataSidebar() != nullptr) {
    m_mainWindow->getMetadataSidebar()->clearMetadata();
  }
  ArtworkManager::instance().stopSilentLoading();
  if (ArtworkManager::instance().getTimerCoordinator() != nullptr) {
    ArtworkManager::instance().getTimerCoordinator()->stopAllTimers();
  }
}

// Finds the visual index of a subcollection within its parent
auto NavigationManager::findSubcollectionVisualIndex(int targetCollectionIndex,
                                                     int previousIndex) const
    -> int {
  if (targetCollectionIndex < 0 ||
      targetCollectionIndex >= m_mainWindow->m_collections.size()) {
    return -1;
  }

  QList<int> subcollections;
  for (int i = 0; i < m_mainWindow->m_collections.size(); ++i) {
    if (m_mainWindow->m_collections[i].parentCollectionIndex ==
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
      UIConstants::SHORT_TIMER_DELAY, m_mainWindow,
      [this, targetCollectionIndex, subcollectionVisualIndex]() {
        showCollectionItems(targetCollectionIndex);

        if (m_mainWindow->getInteractionManager()) {
          m_mainWindow->getInteractionManager()->m_navigationInProgress = false;
        }

        if (subcollectionVisualIndex >= 0 &&
            m_mainWindow->getInteractionManager()) {
          QTimer::singleShot(
              UIConstants::MEDIUM_TIMER_DELAY, m_mainWindow,
              [this, subcollectionVisualIndex]() {
                if (m_mainWindow->getInteractionManager()) {
                  m_mainWindow->getInteractionManager()->beginSelectionRestore(
                      subcollectionVisualIndex);
                }
              });
        }
      });
}

// Handles navigation when the navigation stack is not empty
auto NavigationManager::handleNavigationStackPop() -> void {
  int targetCollectionIndex = m_navigationStack.takeLast();
  int previousIndex = m_mainWindow->currentCollectionIndex;
  m_navigationDepth =
      qMax(0, m_navigationDepth - 1);

  performNavigationStackCleanup();

  bool shared = areItemsShared(previousIndex, targetCollectionIndex);
  if (m_mainWindow->getScrollManager() != nullptr) {
    if (shared) {
      m_mainWindow->getScrollManager()->cleanupActiveWidgets();
    } else {
      m_mainWindow->getScrollManager()->cleanup();
    }
  }

  int subcollectionVisualIndex =
      findSubcollectionVisualIndex(targetCollectionIndex, previousIndex);
  scheduleNavigationReturn(targetCollectionIndex, subcollectionVisualIndex);
}

// Handles navigation fallback when the navigation stack is empty
auto NavigationManager::handleNavigationFallback() -> void {
  m_navigationDepth = 0;
  int previousIndex = m_mainWindow->currentCollectionIndex;

  int fallbackIndex = m_mainWindow->currentCollectionIndex;
  if (fallbackIndex < 0 ||
      fallbackIndex >= m_mainWindow->m_collections.size()) {
    fallbackIndex = -1;
    for (int i = 0; i < m_mainWindow->m_collections.size(); ++i) {
      if (m_mainWindow->m_collections[i].parentCollectionIndex == -1) {
        fallbackIndex = i;
        break;
      }
    }
    if (fallbackIndex < 0 && !m_mainWindow->m_collections.isEmpty()) {
      fallbackIndex = 0;
    }
  }

  if (fallbackIndex >= 0) {
    bool shared = areItemsShared(previousIndex, fallbackIndex);
    if (m_mainWindow->getScrollManager() != nullptr) {
      if (shared) {
        m_mainWindow->getScrollManager()->cleanupActiveWidgets();
      } else {
        m_mainWindow->getScrollManager()->cleanup();
      }
    }
    showCollectionItems(fallbackIndex);
  }

  if (m_mainWindow->getInteractionManager() != nullptr) {
    m_mainWindow->getInteractionManager()->m_navigationInProgress = false;
  }
}

// Goes back to collections and cancels any active held-key repeats to avoid
// stray timer callbacks
void NavigationManager::goBackToCollections() {
  if (m_mainWindow == nullptr) {
    return;
  }
  if ((m_mainWindow != nullptr) &&
      (m_mainWindow->getInteractionManager() != nullptr)) {
    m_mainWindow->getInteractionManager()->stopRepeat();
  }

  persistCurrentSelection(m_mainWindow);

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
  return m_mainWindow != nullptr && !QApplication::closingDown() &&
         !m_mainWindow->m_isShuttingDown;
}

// Cleans up existing no-items widgets from previous loads
auto NavigationManager::cleanupExistingNoItemsWidgets() -> void {
  if (m_mainWindow->ui->loadingLabel != nullptr) {
    m_mainWindow->ui->loadingLabel->setVisible(false);
  }

  QList<QWidget *> existingLabels =
      m_mainWindow->gridContainer->findChildren<QWidget *>("noItemsWidget");
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

  if (m_mainWindow->currentCollectionIndex >= 0 &&
      m_mainWindow->currentCollectionIndex <
          m_mainWindow->m_collections.size()) {
    const CollectionConfig &collection =
        m_mainWindow->m_collections[m_mainWindow->currentCollectionIndex];
    if (collection.showAllSubcollectionItems && filePaths.isEmpty() &&
        !subcollections.isEmpty()) {
      QList<int> allDescendants =
          getAllDescendantCollections(m_mainWindow->currentCollectionIndex);
      for (int descendantIndex : allDescendants) {
        if (descendantIndex >= 0 &&
            descendantIndex < m_mainWindow->m_collections.size()) {
          if (!m_mainWindow->m_collections[descendantIndex]
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
  if (!QApplication::closingDown() && !m_mainWindow->m_isShuttingDown) {
    m_mainWindow->ui->loadingLabel->setText("No items found");
    m_mainWindow->ui->loadingLabel->setVisible(true);
    resumeItemsPageRendering(m_mainWindow);
    m_mainWindow->refreshTitleCounts();
  }
  if ((m_mainWindow != nullptr) &&
      (m_mainWindow->getInteractionManager() != nullptr)) {
    m_mainWindow->getInteractionManager()->m_navigationInProgress = false;
  }
}

// Sets up collection context for items loaded
auto NavigationManager::setupCollectionContext(
    const QStringList &filePaths,
    const QHash<QString, QString> &fileNames) const -> CollectionContext {
  CollectionContext context;
  context.currentIndex = m_mainWindow->currentCollectionIndex;
  context.config =
      m_mainWindow->m_collections[m_mainWindow->currentCollectionIndex];
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
  bool searchActive = ((m_mainWindow->searchBar != nullptr) &&
                       !m_mainWindow->searchBar->text().trimmed().isEmpty());

  if (searchActive || !m_mainWindow->m_generalSettings.rememberSelection ||
      totalItems <= 0) {
    return -1;
  }

  QString hierarchicalName = hierarchicalNameFor(
      m_mainWindow->m_collections[m_mainWindow->currentCollectionIndex],
      m_mainWindow->m_collections);
  int selIdx =
      SessionManager::instance().getLastSelectedIndex(hierarchicalName);
  if (selIdx < 0) {
    QString collectionName =
        m_mainWindow->m_collections[m_mainWindow->currentCollectionIndex].name;
    selIdx = SessionManager::instance().getLastSelectedIndex(collectionName);
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
  while (idx >= 0 && idx < m_mainWindow->m_collections.size()) {
    ++depth;
    int parent = m_mainWindow->m_collections[idx].parentCollectionIndex;
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
                     m_mainWindow, [this]() {
                       if (QApplication::closingDown() ||
                           (m_mainWindow && m_mainWindow->m_isShuttingDown)) {
                         return;
                       }
                       ArtworkManager::instance().startSilentLoading();
                     });

  if (m_mainWindow->getDatabaseManager() != nullptr) {
    m_mainWindow->getDatabaseManager()->updateCachedCounts(
        m_mainWindow->m_collections);
  }
  m_mainWindow->refreshTitleCounts();

  if (m_allCollectionsActive) {
    m_allCollectionsActive = false;
  }

  QTimer::singleShot(UIConstants::VIEWPORT_DELAY, m_mainWindow, [this]() {
    if (m_mainWindow && m_mainWindow->getInteractionManager()) {
      m_mainWindow->getInteractionManager()->m_navigationInProgress = false;
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
      getSubcollections(m_mainWindow->currentCollectionIndex);
  bool hasContent = determineContentAvailability(filePaths, subcollections);

  if (!hasContent) {
    handleEmptyContent();
    return;
  }

  CollectionContext context = setupCollectionContext(filePaths, fileNames);
  int totalItems = subcollections.size() + filePaths.size();
  int selIdx = calculateSelectionIndex(totalItems);

  if (m_mainWindow->getScrollManager() != nullptr) {
    m_mainWindow->getScrollManager()->setupVirtualScrolling(filePaths, fileNames,
                                                         context);
  }

  resumeItemsPageRendering(m_mainWindow);

  if ((ArtworkManager::s_instance.load() != nullptr) &&
      !ArtworkManager::s_shuttingDown.load()) {
    ArtworkManager::instance().updateViewportArtwork();
  }
  if (ArtworkManager::instance().getTimerCoordinator() != nullptr) {
    ArtworkManager::instance().getTimerCoordinator()->scheduleViewportUpdate();
  }

  bool pendingRestore =
      m_mainWindow->property(PropertyKeys::SelectionRestorePending).toBool();
  if (selIdx >= 0 && (m_mainWindow->getInteractionManager() != nullptr) &&
      !pendingRestore) {
    int depth = computeCollectionDepth(m_mainWindow->currentCollectionIndex);
    if (depth >= 3) {
      m_mainWindow->getInteractionManager()->beginSelectionRestore(selIdx);
    } else {
      scheduleSelectionRestore(selIdx, UIConstants::SELECTION_RESTORE_STEPS,
                               UIConstants::SELECTION_RESTORE_STEP_DELAY_MS,
                               UIConstants::SELECTION_RESTORE_MAX_DELAY_MS);
    }
  }

  schedulePostLoadOperations();
}

void NavigationManager::onMediaLibraryError(const QString &error) {
  if (m_mainWindow->loadingLabel != nullptr) {
    m_mainWindow->loadingLabel->deleteLater();
    m_mainWindow->loadingLabel = nullptr;
  }

  QList<QWidget *> existingLabels =
      m_mainWindow->gridContainer->findChildren<QWidget *>("noItemsWidget");
  for (QWidget *widget : existingLabels) {
    widget->deleteLater();
  }

  auto *errorWidget = new QWidget(m_mainWindow->gridContainer);
  errorWidget->setObjectName("noItemsWidget");

  auto *errorLabel = new QLabel(error, errorWidget);
  errorLabel->setAlignment(Qt::AlignCenter);
  errorLabel->setStyleSheet(
      "QLabel { color: palette(text); font-size: 14px; }");

  auto *layout = new QVBoxLayout(errorWidget);
  layout->addWidget(errorLabel);
  layout->setContentsMargins(0, 0, 0, 0);

  if (m_mainWindow->ui->itemScrollArea != nullptr) {
    QRect viewportRect = m_mainWindow->ui->itemScrollArea->viewport()->rect();
    errorWidget->setGeometry(viewportRect);
  }

  errorWidget->show();
  errorWidget->raise();

  if (m_mainWindow->getDatabaseManager() != nullptr) {
    m_mainWindow->getDatabaseManager()->updateCachedCounts(
        m_mainWindow->m_collections);
  }
  m_mainWindow->refreshTitleCounts();
}

void NavigationManager::onViewportChanged() {
  if ((m_mainWindow->getInteractionManager() != nullptr) &&
      m_mainWindow->getInteractionManager()->isWheelScrolling() &&
      m_mainWindow->stackedWidget->currentWidget() == m_mainWindow->itemsPage) {
    QTimer::singleShot(UIConstants::SHORT_TIMER_DELAY, m_mainWindow, [this]() {
      if (m_mainWindow->getScrollManager()) {
        m_mainWindow->getScrollManager()->updateVirtualView();
      }
      ArtworkManager::instance().updateViewportArtwork();
    });
  } else {
    if (auto *coord = ArtworkManager::instance().getTimerCoordinator()) {
      coord->scheduleViewportUpdate();
    }
  }
}

void NavigationManager::filterItems(const QString &searchText) {
  if (m_mainWindow->getScrollManager() != nullptr) {
    if (searchText.trimmed().isEmpty()) {
      m_mainWindow->getScrollManager()->clearFilter();
    } else {
      m_mainWindow->getScrollManager()->applyFilter(searchText);
    }
  }
}

// Return true if any descendant of parentIndex has a non-empty mediaDirectory
auto NavigationManager::collectionHasDescendantWithMedia(int parentIndex) const
    -> bool {
  QList<int> childCollections = getAllDescendantCollections(parentIndex);
  return std::ranges::any_of(childCollections, [this](int childIndex) {
    if (childIndex < 0 || childIndex >= m_mainWindow->m_collections.size()) {
      return false;
    }
    const QString &mediaDir =
        m_mainWindow->m_collections[childIndex].mediaDirectory;
    return !mediaDir.trimmed().isEmpty();
  });
}

// Safely reloads the specified collection and recenters the horizontal
// scrollbar
void NavigationManager::safeReloadCollection(int collectionIndex) {
  persistCurrentSelection(m_mainWindow);
  if (collectionIndex < 0 ||
      collectionIndex >= m_mainWindow->m_collections.size()) {
    return;
  }

  if (auto *coord = ArtworkManager::instance().getTimerCoordinator()) {
    coord->stopAllTimers();
  }

  if (m_mainWindow->getScrollManager() != nullptr) {
    m_mainWindow->getScrollManager()->cleanup();
  }

  ArtworkManager::instance().cancelAllArtworkLoading();

  QTimer::singleShot(
      UIConstants::MEDIUM_TIMER_DELAY, m_mainWindow, [this, collectionIndex]() {
        if (collectionIndex < 0 ||
            collectionIndex >= m_mainWindow->m_collections.size()) {
          return;
        }

        CollectionContext context;
        context.currentIndex = collectionIndex;
        context.config = m_mainWindow->m_collections[collectionIndex];
        context.artworkDirectory = context.config.artworkDirectory;

        if (context.config.showAllSubcollectionItems) {
          m_mainWindow->getDatabaseManager()->loadItemsWithSubcollections(
              context, m_mainWindow->m_collections);
        } else {
          m_mainWindow->getDatabaseManager()->loadItems(context);
        }

        QTimer::singleShot(UIConstants::VIEWPORT_DELAY, m_mainWindow, [this]() {
          if (m_mainWindow->getScrollManager()) {
            m_mainWindow->getScrollManager()->centerHorizontalScrollbar(
                m_mainWindow->currentCollectionIndex,
                m_mainWindow->m_collections);
          }
        });
      });
}

// Updates the items page title to show "Parent > Child" when viewing a
// subcollection
auto NavigationManager::updateItemsPageTitle(int collectionIndex) -> void {
  if ((m_mainWindow == nullptr) || (m_mainWindow->itemsPage == nullptr)) {
    return;
  }
  auto *titleLabel =
      m_mainWindow->itemsPage->findChild<QLabel *>("itemsTitleLabel");
  if (titleLabel == nullptr) {
    return;
  }

  if (collectionIndex < 0 ||
      collectionIndex >= m_mainWindow->m_collections.size()) {
    titleLabel->clear();
    return;
  }

  QString title = m_mainWindow->m_collections[collectionIndex].name;
  if (m_mainWindow->m_collections[collectionIndex].isSubcollection) {
    int parentIdx =
        m_mainWindow->m_collections[collectionIndex].parentCollectionIndex;
    if (parentIdx >= 0 && parentIdx < m_mainWindow->m_collections.size()) {
      title = m_mainWindow->m_collections[parentIdx].name + " > " + title;
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
  if ((m_mainWindow == nullptr) || collectionIndex < 0 ||
      collectionIndex >= m_mainWindow->m_collections.size()) {
    return false;
  }

  const CollectionConfig &collection =
      m_mainWindow->m_collections[collectionIndex];

  for (auto &m_collection : m_mainWindow->m_collections) {
    if (m_collection.parentCollectionIndex == collectionIndex) {
      hasSub = true;
      break;
    }
  }

  QString mediaDir = (m_mainWindow->getSettingsManager() != nullptr)
                         ? SettingsManager::expandConfigVariables(
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
  if (m_collections == nullptr) {
    return {};
  }
  return directChildrenOf(parentIndex, *m_collections);
}

// Sets the collections pointer used for subcollection queries
void NavigationManager::setCollections(QList<CollectionConfig> *collections) {
  m_collections = collections;
}

void NavigationManager::onSubcollectionEntered(int subcollectionIndex) {
  if (subcollectionIndex >= 0 &&
      subcollectionIndex < m_mainWindow->m_collections.size()) {
    if (m_mainWindow->getInteractionManager() != nullptr) {
      qint64 now = QDateTime::currentMSecsSinceEpoch();
      m_mainWindow->getInteractionManager()->setProperty(
          PropertyKeys::SuppressDoubleClickUntilMs,
          now + UIConstants::DOUBLE_CLICK_SUPPRESS_AFTER_ENTER_MS);
    }

    if (m_mainWindow->currentCollectionIndex >= 0 &&
        (m_mainWindow->getInteractionManager() != nullptr)) {
      int currentSelection =
          m_mainWindow->getInteractionManager()->currentSelectedIndex();
      if (currentSelection >= 0) {
        m_mainWindow->getSettingsManager()->setLastSelectedItem(
            m_mainWindow->currentCollectionIndex, currentSelection);
      }
    }

    if (m_mainWindow->currentCollectionIndex >= 0 &&
        m_mainWindow->currentCollectionIndex <
            m_mainWindow->m_collections.size()) {
      m_navigationStack.append(
          m_mainWindow->currentCollectionIndex);
      m_navigationDepth++;
    }

    m_mainWindow->getSettingsManager()->setLastSelectedItem(subcollectionIndex,
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
        UIConstants::SUBCOLLECTION_SCROLL_CENTER_DELAY_MS, m_mainWindow,
        [this]() {
          m_mainWindow->getScrollManager()->centerHorizontalScrollbar(
              m_mainWindow->currentCollectionIndex,
              m_mainWindow->m_collections);
        });

    QTimer::singleShot(UIConstants::DOUBLE_CLICK_SUPPRESS_CLEAR_DELAY_MS,
                       m_mainWindow, [this]() {
                         if (m_mainWindow->getInteractionManager()) {
                           m_mainWindow->getInteractionManager()->setProperty(
                               PropertyKeys::SuppressDoubleClickUntilMs, 0);
                         }
                       });
  }
}

// Loads items for the current collection with optional subcollection
// aggregation and reapplies any active filter
void NavigationManager::loadCurrentAndSubcollections() {
  const int idx = m_mainWindow->currentCollectionIndex;
  if (idx < 0 || idx >= m_mainWindow->m_collections.size()) {
    return;
  }

  CollectionContext context;
  context.currentIndex = idx;
  context.config = m_mainWindow->m_collections[idx];
  context.config.mediaDirectory = SettingsManager::expandConfigVariables(
      context.config.mediaDirectory, context.config.name);
  context.config.artworkDirectory = SettingsManager::expandConfigVariables(
      context.config.artworkDirectory, context.config.name);
  context.artworkDirectory = context.config.artworkDirectory;

  if (context.config.showAllSubcollectionItems) {
    m_mainWindow->getDatabaseManager()->loadItemsWithSubcollections(
        context, m_mainWindow->m_collections);
  } else {
    m_mainWindow->getDatabaseManager()->loadItems(context);
  }

  QTimer::singleShot(UIConstants::FILTER_REAPPLY_DELAY_MS, [this]() {
    if (m_mainWindow->searchBar &&
        !m_mainWindow->searchBar->text().trimmed().isEmpty() &&
        m_mainWindow->getScrollManager()) {
      const QString currentSearchText =
          m_mainWindow->searchBar->text().trimmed();
      m_mainWindow->getScrollManager()->applyFilter(currentSearchText);
    }
  });
}

// Loads the aggregated view across all collections and reapplies any active
// filter
void NavigationManager::loadAllCollectionsView() {
  m_allCollectionsActive = true;
  m_mainWindow->getDatabaseManager()->loadAllCollections(
      m_mainWindow->m_collections);

  QTimer::singleShot(UIConstants::FILTER_REAPPLY_DELAY_MS, [this]() {
    if (m_mainWindow->searchBar &&
        !m_mainWindow->searchBar->text().trimmed().isEmpty() &&
        m_mainWindow->getScrollManager()) {
      const QString currentSearchText =
          m_mainWindow->searchBar->text().trimmed();
      m_mainWindow->getScrollManager()->applyFilter(currentSearchText);
    }
  });
}

auto NavigationManager::getAllDescendantCollections(int parentIndex) const
    -> QList<int> {
  QList<int> result;
  if ((m_collections == nullptr) || parentIndex < 0 ||
      parentIndex >= m_collections->size()) {
    return result;
  }

  QList<int> stack = directChildrenOf(parentIndex, *m_collections);
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

    const QList<int> children = directChildrenOf(idx, *m_collections);
    for (int childIdx : children) {
      if (!seen.contains(childIdx)) {
        stack.append(childIdx);
      }
    }
  }
  return result;
}