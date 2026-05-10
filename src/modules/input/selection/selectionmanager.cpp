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
#include "detailspane.h"
#include "detailspanemanager.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "mousemanager.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "selectionhelpers.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "uiconstants.h"
#include "viewportmanager.h"

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcSelectionManager, "kartend.selectionmanager")
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcSelectionManager().isDebugEnabled()) {                                                   \
      qCDebug(lcSelectionManager) << msg;                                                          \
    }                                                                                              \
  } while (0)

// SelectionManagerSetup getter definitions (non-manager fields only).
SETUP_GETTER_DEF_UI_SAME(SelectionManagerSetup, DetailsPane *, Sidebar, sidebar)
SETUP_GETTER_DEF_UI_SAME(SelectionManagerSetup, QWidget *, ItemsPage, itemsPage)
SETUP_GETTER_DEF_UI_SAME(SelectionManagerSetup, QWidget *, GridContainer, gridContainer)
SETUP_GETTER_DEF_UI_SAME(SelectionManagerSetup, QScrollArea *, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_COL_SAME(SelectionManagerSetup, QList<CollectionConfig> *, Collections,
                          collections)
SETUP_GETTER_DEF_COL_SAME(SelectionManagerSetup, int *, CurrentCollectionIndex,
                          currentCollectionIndex)
SETUP_GETTER_DEF_COL_SAME(SelectionManagerSetup, const CollectionHierarchyCache *, HierarchyCache,
                          hierarchyCache)
SETUP_GETTER_DEF_UI_SAME(SelectionManagerSetup, QLineEdit *, SearchBar, searchBar)

SelectionManager::SelectionManager(QObject *parent) : QObject(parent) {}

SelectionManager::~SelectionManager() = default;

void SelectionManager::setupReferences(const SelectionManagerSetup &setup) {
  m_ctx = setup.ctx;
  // SelectionManager proxies all selection-restore state through ctx, so the
  // InteractionStateHolder must be wired before setup. Failing fast here is
  // far easier to diagnose than a null deref deep in a click handler.
  Q_ASSERT(state());

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

bool SelectionManager::isRestoringSelection() const {
  return state()->selectionRestore().restoring;
}

int SelectionManager::targetRestoreIndex() const {
  return state()->selectionRestore().targetIndex;
}

void SelectionManager::setRestoringSelection(bool restoring) {
  state()->selectionRestore().restoring = restoring;
}

void SelectionManager::setTargetRestoreIndex(int index) {
  state()->selectionRestore().targetIndex = index;
}

void SelectionManager::setForceImmediateCenter(bool force) {
  state()->selectionRestore().forceImmediateCenter = force;
}

bool SelectionManager::forceImmediateCenter() const {
  return state()->selectionRestore().forceImmediateCenter;
}

void SelectionManager::setSelectedIndex(int index) {
  m_selectedItemIndex = index;
}

void SelectionManager::notifySelectionChanged() {
  emit selectionChanged(m_selectedItemIndex);
}

void SelectionManager::setSelectedFilePath(const QString &path) {
  m_selectedFilePath = path;
}

void SelectionManager::setSelectedWidget(ItemWidget *widget) {
  m_selectedMediaItem = widget;
}

void SelectionManager::clearWidgetSelectionStates() {
  if (!scrollMgr()) {
    return;
  }
  const auto &activeWidgets = scrollMgr()->getActiveWidgets();
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
  if (scrollMgr()) {
    scrollMgr()->updateSelectionForIndex(index);
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

void SelectionManager::updateFilePathForSelection(int index,
                                                  const QList<int> & /*subcollections*/) {
  // Use the *rendered* subcollection count, not the hierarchy-cache list:
  // during search the visible sub list is filtered (often to zero) but the
  // hierarchy-cache size still reflects the full parent. Misclassifying a
  // filtered media item as a subcollection here would clear the cached file
  // path and break subsequent launch / sidebar metadata.
  const int actualIndex = scrollMgr() ? scrollMgr()->getFilteredIndex(index) : index;
  const int renderedSubCount = scrollMgr() ? scrollMgr()->getSubcollectionCount() : 0;
  if (actualIndex >= 0 && actualIndex < renderedSubCount) {
    m_selectedFilePath.clear();
  } else {
    if (scrollMgr()) {
      QString path = scrollMgr()->filePathForVisualIndex(index);
      m_selectedFilePath = path;
    }
  }

  if (detailsPaneMgr()) {
    ItemWidget *safeWidget = nullptr;
    if (scrollMgr()) {
      const auto &activeWidgets = scrollMgr()->getActiveWidgets();
      safeWidget = activeWidgets.value(index, nullptr);
    }
    if (safeWidget) {
      if (detailsPaneMgr()->isSidebarVisible()) {
        detailsPaneMgr()->updateSidebarMetadata(safeWidget);
      }
    } else if (!m_selectedFilePath.isEmpty()) {
      // No materialized ItemWidget (Cover Flow renders its own cards) — keep
      // m_currentItemContext fresh so the info-page overlay still works.
      detailsPaneMgr()->updateSidebarMetadata(m_selectedFilePath,
                                                  QFileInfo(m_selectedFilePath).completeBaseName());
    }
  }
}

void SelectionManager::persistSelection(int collectionIndex, int itemIndex, const QString &title) {
  if (!m_collections || collectionIndex < 0 || collectionIndex >= m_collections->size()) {
    return;
  }

  if (settingsMgr()) {
    settingsMgr()->setLastSelectedItem(collectionIndex, itemIndex);
  }

  // Use a stable session key that also scopes by virtual subfolder (if active).
  QString sessionKey =
      CollectionUtils::selectionSessionKeyFor((*m_collections)[collectionIndex], *m_collections);
  debugLog("[SelectionRestore] persistSelection: sessionKey=" << sessionKey << "itemIndex="
                                                              << itemIndex << "title=" << title);
  if (sessionMgr()) {
    sessionMgr()->setLastSelected(sessionKey, itemIndex, title);
  }
}

QString SelectionManager::titleForIndex(int index, const QList<int> & /*subcollections*/) const {
  const int actualIndex = scrollMgr() ? scrollMgr()->getFilteredIndex(index) : index;
  const int renderedSubCount = scrollMgr() ? scrollMgr()->getSubcollectionCount() : 0;
  if (actualIndex >= 0 && actualIndex < renderedSubCount) {
    int subIdx = scrollMgr() ? scrollMgr()->subcollectionIndexFromActual(actualIndex) : -1;
    if (m_collections && subIdx >= 0 && subIdx < m_collections->size()) {
      return (*m_collections)[subIdx].name;
    }
    return {};
  }

  QString path = m_selectedFilePath;
  if (path.isEmpty() && scrollMgr()) {
    path = scrollMgr()->filePathForVisualIndex(index);
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
  if (!scrollMgr()) {
    return nullptr;
  }
  const auto &activeWidgets = scrollMgr()->getActiveWidgets();
  return activeWidgets.value(index, nullptr);
}

bool SelectionManager::shouldTreatAsNewRow(int targetIndex, int gridWidth) const {
  if (!CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return false;
  }
  return SelectionHelpers::shouldTreatAsNewRow(targetIndex, m_lastSelectedRow, gridWidth);
}

bool SelectionManager::shouldAnimateHorizontalHop(int fromIndex, int toIndex, int gridWidth) {
  return SelectionHelpers::shouldAnimateHorizontalHop(fromIndex, toIndex, gridWidth);
}

bool SelectionManager::isNewRow(int currentSelection, int newSelection, int gridWidth) {
  return SelectionHelpers::isNewRow(currentSelection, newSelection, gridWidth);
}

int SelectionManager::getCurrentGridWidth() const {
  // Prefer ScrollManager's value for filtered/nested views
  if (scrollMgr()) {
    int width = scrollMgr()->getCurrentGridWidth();
    if (width > 0) {
      return width;
    }
  }
  return CollectionUtils::getGridWidth(m_currentCollectionIndex, m_collections);
}

void SelectionManager::selectItemByIndex(int index, bool allowHorizontalScroll) {
  Q_UNUSED(allowHorizontalScroll);
  if (!scrollMgr() || !m_itemScrollArea ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return;
  }

  const QStringList &filePaths = scrollMgr()->getFilePaths();
  QList<int> subcollections = getSubcollections(*m_currentCollectionIndex);
  int virtualFolderCount = scrollMgr()->getVirtualFolderCount();
  int totalItems = subcollections.size() + virtualFolderCount + filePaths.size();
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

  if (selectionChangedLocal && state()) {
    state()->scroll().userFreeScroll = false;
  }

  ItemWidget *widget = widgetForIndex(index);
  bool suppressed = state() && state()->click().selectionSuppressed &&
                    state()->click().pendingSelectionIndex == index;
  bool skipCenter = state() && state()->click().suppressInitialClickCenter;

  if (widget) {
    m_selectedMediaItem = widget;
    updateFilePathForSelection(index, subcollections);
    if (!suppressed) {
      handleSuccessfulSelection(index);
    }
  } else {
    trySelectWidget(index, subcollections, 0);
  }

  if (scrollMgr()) {
    scrollMgr()->updateSelectionForIndex(m_selectedItemIndex);
    if (state() && state()->scroll().clickHoldAdvancing) {
      scrollMgr()->refreshSelectionOverlayState();
    }
  }
  emit selectionChanged(m_selectedItemIndex);

  if (suppressed) {
    persistSuppressedSelectionAndMaybeCenter(index, subcollections, skipCenter);
  }

  if (skipCenter && state()) {
    state()->click().suppressInitialClickCenter = false;
  }
}

void SelectionManager::selectItemByHover(int index) {
  if (!scrollMgr() || !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return;
  }

  const int totalItems = scrollMgr()->getTotalItems();
  if (index < 0 || index >= totalItems || index == m_selectedItemIndex) {
    return;
  }

  m_selectedItemIndex = index;
  const int gridWidth = getCurrentGridWidth();
  if (gridWidth > 0) {
    m_lastSelectedRow = index / gridWidth;
  }
  if (state()) {
    state()->scroll().userFreeScroll = false;
  }

  const QList<int> subcollections = getSubcollections(*m_currentCollectionIndex);
  m_selectedMediaItem = widgetForIndex(index);
  updateFilePathForSelection(index, subcollections);
  persistSelectionForIndex(*m_currentCollectionIndex, index);

  scrollMgr()->updateSelectionForIndex(index);
  emit selectionChanged(index);
}

void SelectionManager::persistSuppressedSelectionAndMaybeCenter(int index,
                                                                const QList<int> &subcollections,
                                                                bool skipCenter) {
  bool deferCenter = state() && state()->click().deferCenterOnClick &&
                     state()->click().deferredCenterIndex == index;
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
    if (!QApplication::closingDown() && artworkMgr()) {
      artworkMgr()->updateViewportArtwork();
    }
  });
}

void SelectionManager::handleSuccessfulSelection(int index) {
  int gridWidth = getCurrentGridWidth();
  if (gridWidth > 0) {
    m_lastSelectedRow = index / gridWidth;
  }

  const auto &restoreState = state()->selectionRestore();
  bool restoringMatch = restoreState.restoring && (index == restoreState.targetIndex);

  int currentColl = ((m_currentCollectionIndex) ? *m_currentCollectionIndex : -1);
  if ((m_collections) && currentColl >= 0 && index >= 0) {
    persistSelectionForIndex(currentColl, index);
  }
  if (QApplication::closingDown()) {
    return;
  }

  bool immediate = restoreState.forceImmediateCenter || restoringMatch;
  emit requestCenterVertically(index, immediate);
  if (scrollMgr()) {
    scrollMgr()->updateVirtualView();
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
  const bool isCurrent = m_currentCollectionIndex && coll == *m_currentCollectionIndex;
  if (isCurrent && scrollMgr()) {
    const int actualIdx = scrollMgr()->getFilteredIndex(idx);
    const int renderedSubCount = scrollMgr()->getSubcollectionCount();
    if (actualIdx >= 0 && actualIdx < renderedSubCount) {
      int subIdx = scrollMgr()->subcollectionIndexFromActual(actualIdx);
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
  return QFileInfo(m_selectedFilePath).completeBaseName().replace('_', ' ').simplified();
}

void SelectionManager::persistSelectionForIndex(int coll, int idx) {
  if (!m_collections || coll < 0 || coll >= m_collections->size()) {
    return;
  }
  QString title = titleForIndexInColl(coll, idx);
  persistSelection(coll, idx, title);
}

void SelectionManager::trySelectWidget(int index, const QList<int> &subcollections, int attempt) {
  if (!scrollMgr()) {
    return;
  }
  constexpr int kMaxAttempts = 5;
  if (attempt >= kMaxAttempts) {
    return;
  }

  const auto &activeWidgets = scrollMgr()->getActiveWidgets();
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
