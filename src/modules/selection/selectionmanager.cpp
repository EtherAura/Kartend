// Owns selection state and coordinates selection operations with visual
// feedback.
#include "selectionmanager.h"

#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QWidget>

#include "animationmanager.h"
#include "applicationcontext.h"
#include "artworkmanager.h"
#include "collectionutils.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "metadatasidebar.h"
#include "mousemanager.h"
#include "navigationmanager.h"
#include "scrolldatamanager.h"
#include "scrollmanager.h"
#include "selectionhelpers.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "sidebarmanager.h"
#include "uiconstants.h"
#include "viewportmanager.h"

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcSelectionManager, "kartend.selectionmanager")
#define debugLog(msg)                                                          \
  do {                                                                         \
    if (lcSelectionManager().isDebugEnabled()) {                               \
      qCDebug(lcSelectionManager) << msg;                                      \
    }                                                                          \
  } while (0)

// SelectionManagerSetup getter definitions
SETUP_GETTER_DEF_CTX_ONLY(SelectionManagerSetup, InteractionStateHolder *,
                          InteractionState, interactionState)
SETUP_GETTER_DEF_SAME(SelectionManagerSetup, ScrollManager *, ScrollManager,
                      scrollManager)
SETUP_GETTER_DEF_SAME(SelectionManagerSetup, SidebarManager *, SidebarManager,
                      sidebarManager)
SETUP_GETTER_DEF_SAME(SelectionManagerSetup, SessionManager *, SessionManager,
                      sessionManager)
SETUP_GETTER_DEF_SAME(SelectionManagerSetup, SettingsManager *, SettingsManager,
                      settingsManager)
SETUP_GETTER_DEF_SAME(SelectionManagerSetup, NavigationManager *,
                      NavigationManager, navigationManager)
SETUP_GETTER_DEF_SAME(SelectionManagerSetup, AnimationManager *,
                      AnimationManager, animationManager)
SETUP_GETTER_DEF_SAME(SelectionManagerSetup, ViewportManager *, ViewportManager,
                      viewportManager)
SETUP_GETTER_DEF_SAME(SelectionManagerSetup, ArtworkManager *, ArtworkManager,
                      artworkManager)
SETUP_GETTER_DEF_SAME(SelectionManagerSetup, MetadataSidebar *, Sidebar,
                      sidebar)
SETUP_GETTER_DEF_SAME(SelectionManagerSetup, QWidget *, ItemsPage, itemsPage)
SETUP_GETTER_DEF_SAME(SelectionManagerSetup, QWidget *, GridContainer,
                      gridContainer)
SETUP_GETTER_DEF_SAME(SelectionManagerSetup, QScrollArea *, ItemScrollArea,
                      itemScrollArea)
SETUP_GETTER_DEF_SAME(SelectionManagerSetup, QList<CollectionConfig> *,
                      Collections, collections)
SETUP_GETTER_DEF_SAME(SelectionManagerSetup, int *, CurrentCollectionIndex,
                      currentCollectionIndex)
SETUP_GETTER_DEF_SAME(SelectionManagerSetup, const CollectionHierarchyCache *,
                      HierarchyCache, hierarchyCache)
SETUP_GETTER_DEF_SAME(SelectionManagerSetup, QLineEdit *, SearchBar, searchBar)

SelectionManager::SelectionManager(QObject *parent) : QObject(parent) {}

SelectionManager::~SelectionManager() = default;

void SelectionManager::setupReferences(const SelectionManagerSetup &setup) {
  // State holder - from ctx
  m_state = setup.getInteractionState();

  // Manager dependencies - use getters with ctx fallback
  m_scrollManager = setup.getScrollManager();
  m_sidebarManager = setup.getSidebarManager();
  m_sessionManager = setup.getSessionManager();
  m_settingsManager = setup.getSettingsManager();
  m_navigationManager = setup.getNavigationManager();
  m_animationManager = setup.getAnimationManager();
  m_viewportManager = setup.getViewportManager();
  m_artworkManager = setup.getArtworkManager();

  // UI elements - use getters with ctx fallback
  m_MetadataSidebar = setup.getSidebar();
  m_itemsPage = setup.getItemsPage();
  m_gridContainer = setup.getGridContainer();
  m_itemScrollArea = setup.getItemScrollArea();
  m_collections = setup.getCollections();
  m_currentCollectionIndex = setup.getCurrentCollectionIndex();
  m_hierarchyCache = setup.getHierarchyCache();
  m_searchBar = setup.getSearchBar();
}

void SelectionManager::setSelectedIndex(int index) {
  m_selectedItemIndex = index;
}

void SelectionManager::setSelectedFilePath(const QString &path) {
  m_selectedFilePath = path;
}

void SelectionManager::setSelectedWidget(ItemWidget *widget) {
  m_selectedMediaItem = widget;
}

void SelectionManager::clearWidgetSelectionStates() {
  if (!m_scrollManager) {
    return;
  }
  const auto &activeWidgets = m_scrollManager->getActiveWidgets();
  for (auto it = activeWidgets.begin(); it != activeWidgets.end(); ++it) {
    if ((it.value()) && it.value()->isSelected()) {
      it.value()->setSelected(false);
    }
  }
}

void SelectionManager::clearMetadataSidebar() {
  if (m_MetadataSidebar) {
    m_MetadataSidebar->clearMetadata();
  }
}

void SelectionManager::notifyScrollManagerOfSelection(int index) {
  if (m_scrollManager) {
    m_scrollManager->updateSelectionForIndex(index);
  }
}

void SelectionManager::clearSelection(bool isShuttingDown) {
  if (isShuttingDown) {
    m_selectedMediaItem = nullptr;
    m_selectedFilePath.clear();
    m_selectedItemIndex = -1;
    return;
  }

  clearWidgetSelectionStates();

  m_selectedMediaItem = nullptr;
  m_selectedFilePath.clear();
  m_selectedItemIndex = -1;

  clearMetadataSidebar();
  notifyScrollManagerOfSelection(-1);

  emit selectionCleared();
}

void SelectionManager::clearSelectionAndFocus() {
  clearSelection();
  emit requestFocusItemsPage();
}

QList<int> SelectionManager::getSubcollections(int parentIndex) const {
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

void SelectionManager::updateFilePathForSelection(
    int index, const QList<int> & /*subcollections*/) {
  // Use the *rendered* subcollection count, not the hierarchy-cache list:
  // during search the visible sub list is filtered (often to zero) but the
  // hierarchy-cache size still reflects the full parent. Misclassifying a
  // filtered media item as a subcollection here would clear the cached file
  // path and break subsequent launch / sidebar metadata.
  const int actualIndex =
      m_scrollManager ? m_scrollManager->getFilteredIndex(index) : index;
  const int renderedSubCount =
      m_scrollManager ? m_scrollManager->getSubcollectionCount() : 0;
  if (actualIndex >= 0 && actualIndex < renderedSubCount) {
    m_selectedFilePath.clear();
  } else {
    if (m_scrollManager) {
      QString path = m_scrollManager->filePathForVisualIndex(index);
      m_selectedFilePath = path;
    }
  }

  if ((m_sidebarManager) && m_sidebarManager->isSidebarVisible()) {
    ItemWidget *safeWidget = nullptr;
    if (m_scrollManager) {
      const auto &activeWidgets = m_scrollManager->getActiveWidgets();
      safeWidget = activeWidgets.value(index, nullptr);
    }
    m_sidebarManager->updateSidebarMetadata(safeWidget);
  }
}

void SelectionManager::persistSelection(int collectionIndex, int itemIndex,
                                        const QString &title) {
  if (!m_collections || collectionIndex < 0 ||
      collectionIndex >= m_collections->size()) {
    return;
  }

  if (m_settingsManager) {
    m_settingsManager->setLastSelectedItem(collectionIndex, itemIndex);
  }

  // Use a stable session key that also scopes by virtual subfolder (if active).
  QString sessionKey = CollectionUtils::selectionSessionKeyFor(
      (*m_collections)[collectionIndex], *m_collections);
  debugLog("[SelectionRestore] persistSelection: sessionKey="
           << sessionKey << "itemIndex=" << itemIndex << "title=" << title);
  if (m_sessionManager) {
    m_sessionManager->setLastSelected(sessionKey, itemIndex, title);
  }
}

QString
SelectionManager::titleForIndex(int index,
                                const QList<int> & /*subcollections*/) const {
  const int actualIndex =
      m_scrollManager ? m_scrollManager->getFilteredIndex(index) : index;
  const int renderedSubCount =
      m_scrollManager ? m_scrollManager->getSubcollectionCount() : 0;
  if (actualIndex >= 0 && actualIndex < renderedSubCount) {
    int subIdx = (m_scrollManager && m_scrollManager->getDataManager())
                     ? m_scrollManager->getDataManager()
                           ->subcollectionIndexFromActual(actualIndex)
                     : -1;
    if (m_collections && subIdx >= 0 && subIdx < m_collections->size()) {
      return (*m_collections)[subIdx].name;
    }
    return {};
  }

  QString path = m_selectedFilePath;
  if (path.isEmpty() && m_scrollManager) {
    path = m_scrollManager->filePathForVisualIndex(index);
  }
  if (!path.isEmpty()) {
    return QFileInfo(path).completeBaseName().replace('_', ' ').simplified();
  }
  return {};
}

void SelectionManager::applyWidgetSelection(ItemWidget *widget) {
  if (widget) {
    widget->setSelected(true);
  }
}

void SelectionManager::clearWidgetSelection(ItemWidget *widget) {
  if (widget) {
    widget->setSelected(false);
  }
}

ItemWidget *SelectionManager::widgetForIndex(int index) const {
  if (!m_scrollManager) {
    return nullptr;
  }
  const auto &activeWidgets = m_scrollManager->getActiveWidgets();
  return activeWidgets.value(index, nullptr);
}

bool SelectionManager::shouldTreatAsNewRow(int targetIndex,
                                           int gridWidth) const {
  if (!CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return false;
  }
  return SelectionHelpers::shouldTreatAsNewRow(targetIndex, m_lastSelectedRow,
                                               gridWidth);
}

bool SelectionManager::shouldAnimateHorizontalHop(int fromIndex, int toIndex,
                                                  int gridWidth) {
  return SelectionHelpers::shouldAnimateHorizontalHop(fromIndex, toIndex,
                                                      gridWidth);
}

bool SelectionManager::isNewRow(int currentSelection, int newSelection,
                                int gridWidth) {
  return SelectionHelpers::isNewRow(currentSelection, newSelection, gridWidth);
}

int SelectionManager::getCurrentGridWidth() const {
  // Prefer ScrollManager's value for filtered/nested views
  if (m_scrollManager) {
    int width = m_scrollManager->getCurrentGridWidth();
    if (width > 0) {
      return width;
    }
  }
  return CollectionUtils::getGridWidth(m_currentCollectionIndex, m_collections);
}

void SelectionManager::selectItemByIndex(int index,
                                         bool allowHorizontalScroll) {
  Q_UNUSED(allowHorizontalScroll);
  if (!m_scrollManager || !m_itemScrollArea ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return;
  }

  const QStringList &filePaths = m_scrollManager->getFilePaths();
  QList<int> subcollections = getSubcollections(*m_currentCollectionIndex);
  int virtualFolderCount = m_scrollManager->getVirtualFolderCount();
  int totalItems =
      subcollections.size() + virtualFolderCount + filePaths.size();
  if (index < 0 || index >= totalItems) {
    return;
  }

  bool selectionChangedLocal = (index != m_selectedItemIndex);
  m_selectedItemIndex = index;

  // Always update m_lastSelectedRow when selection changes to keep state in
  // sync This was previously only updated in handleSuccessfulSelection, causing
  // stale row tracking when selection was suppressed for new-row click
  // sequences
  int gridWidth = getCurrentGridWidth();
  if (gridWidth > 0) {
    m_lastSelectedRow = index / gridWidth;
  }

  if (selectionChangedLocal && m_state) {
    m_state->scroll().userFreeScroll = false;
  }

  ItemWidget *widget = widgetForIndex(index);
  bool suppressed = m_state && m_state->click().selectionSuppressed &&
                    m_state->click().pendingSelectionIndex == index;
  bool skipCenter = m_state && m_state->click().suppressInitialClickCenter;

  if (widget) {
    m_selectedMediaItem = widget;
    updateFilePathForSelection(index, subcollections);
    if (!suppressed) {
      handleSuccessfulSelection(index);
    }
  } else {
    trySelectWidget(index, subcollections, 0);
  }

  if (m_scrollManager) {
    m_scrollManager->updateSelectionForIndex(m_selectedItemIndex);
    if (m_state && m_state->scroll().clickHoldAdvancing) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  }
  emit selectionChanged(m_selectedItemIndex);

  if (suppressed) {
    persistSuppressedSelectionAndMaybeCenter(index, subcollections, skipCenter);
  }

  if (skipCenter && m_state) {
    m_state->click().suppressInitialClickCenter = false;
  }
}

void SelectionManager::persistSuppressedSelectionAndMaybeCenter(
    int index, const QList<int> &subcollections, bool skipCenter) {
  bool deferCenter = m_state && m_state->click().deferCenterOnClick &&
                     m_state->click().deferredCenterIndex == index;
  if (!deferCenter && !skipCenter) {
    emit requestCenterVertically(index, false);
  }
  int curColl = ((m_currentCollectionIndex) ? *m_currentCollectionIndex : -1);
  if ((m_collections) && curColl >= 0 && curColl < m_collections->size()) {
    QString title = titleForIndex(index, subcollections);
    persistSelection(curColl, index, title);
  }
  // Defer artwork update to allow selection animation to start smoothly
  // before triggering potentially expensive artwork loading operations
  QTimer::singleShot(UIConstants::Timing::SHORT_DELAY_MS, this, [this]() {
    if (!QApplication::closingDown() && m_artworkManager) {
      m_artworkManager->updateViewportArtwork();
    }
  });
}

void SelectionManager::handleSuccessfulSelection(int index) {
  int gridWidth = getCurrentGridWidth();
  if (gridWidth > 0) {
    m_lastSelectedRow = index / gridWidth;
  }

  bool restoringMatch = m_restoringSelection && (index == m_targetRestoreIndex);

  int currentColl =
      ((m_currentCollectionIndex) ? *m_currentCollectionIndex : -1);
  if ((m_collections) && currentColl >= 0 && index >= 0) {
    persistSelectionForIndex(currentColl, index);
  }
  if (QApplication::closingDown()) {
    return;
  }

  bool immediate = m_forceImmediateCenter || restoringMatch;
  emit requestCenterVertically(index, immediate);
  if (m_scrollManager) {
    m_scrollManager->updateVirtualView();
  }
}

QString SelectionManager::titleForIndexInColl(int coll, int idx) const {
  if (!CollectionUtils::isValidIndex(coll, m_collections)) {
    return {};
  }

  // For the *current* collection, consult the rendered scroll data so the
  // discriminator agrees with what the user sees (sub vs media). For other
  // collections, fall back to the hierarchy-cache list since their data is
  // not currently rendered.
  const bool isCurrent =
      m_currentCollectionIndex && coll == *m_currentCollectionIndex;
  if (isCurrent && m_scrollManager) {
    const int actualIdx = m_scrollManager->getFilteredIndex(idx);
    const int renderedSubCount = m_scrollManager->getSubcollectionCount();
    if (actualIdx >= 0 && actualIdx < renderedSubCount) {
      int subIdx = m_scrollManager->getDataManager()
                       ? m_scrollManager->getDataManager()
                             ->subcollectionIndexFromActual(actualIdx)
                       : -1;
      if (subIdx >= 0 && subIdx < m_collections->size()) {
        return (*m_collections)[subIdx].name;
      }
      return {};
    }
  } else {
    QList<int> subs = getSubcollections(coll);
    if (idx >= 0 && idx < subs.size()) {
      int subIdx = subs[idx];
      if (subIdx >= 0 && subIdx < m_collections->size()) {
        return (*m_collections)[subIdx].name;
      }
      return {};
    }
  }

  if (m_selectedFilePath.isEmpty()) {
    return {};
  }
  return QFileInfo(m_selectedFilePath)
      .completeBaseName()
      .replace('_', ' ')
      .simplified();
}

void SelectionManager::persistSelectionForIndex(int coll, int idx) {
  if (!m_collections || coll < 0 || coll >= m_collections->size()) {
    return;
  }
  QString title = titleForIndexInColl(coll, idx);
  persistSelection(coll, idx, title);
}

void SelectionManager::trySelectWidget(int index,
                                       const QList<int> &subcollections,
                                       int attempt) {
  if (!m_scrollManager) {
    return;
  }
  constexpr int kMaxAttempts = 5;
  if (attempt >= kMaxAttempts) {
    return;
  }

  const auto &activeWidgets = m_scrollManager->getActiveWidgets();
  ItemWidget *widget = activeWidgets.value(index, nullptr);
  if (widget) {
    m_selectedMediaItem = widget;
    updateFilePathForSelection(index, subcollections);
    handleSuccessfulSelection(index);
    return;
  }

  constexpr int kSelectRetryBaseMs = 30;
  constexpr int kSelectRetryStepMs = 30;
  int delay = kSelectRetryBaseMs + (attempt * kSelectRetryStepMs);
  // Retry widget selection with increasing delays - widget may not be
  // materialized yet during virtual scroll population
  QTimer::singleShot(delay, this, [this, index, subcollections, attempt]() {
    trySelectWidget(index, subcollections, attempt + 1);
  });
}

