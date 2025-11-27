#include "selectionmanager.h"

#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QWidget>

#include "animationmanager.h"
#include "artworkmanager.h"
#include "collectionutils.h"
#include "itemwidget.h"
#include "mainwindow.h"
#include "metadatasidebar.h"
#include "mousemanager.h"
#include "navigationmanager.h"
#include "propertyutils.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "sidebarmanager.h"
#include "uiconstants.h"
#include "viewportmanager.h"

SelectionManager::SelectionManager(QObject *parent) : QObject(parent) {}

SelectionManager::~SelectionManager() = default;

void SelectionManager::setupReferences(const SelectionManagerSetup &setup) {
  m_scrollManager = setup.scrollManager;
  m_sidebarManager = setup.sidebarManager;
  m_sessionManager = setup.sessionManager;
  m_settingsManager = setup.settingsManager;
  m_navigationManager = setup.navigationManager;
  m_animationManager = setup.animationManager;
  m_viewportManager = setup.viewportManager;
  m_artworkManager = setup.artworkManager;
  m_mainWindow = setup.mainWindow;
  m_metadataSidebar = setup.sidebar;
  m_itemsPage = setup.itemsPage;
  m_gridContainer = setup.gridContainer;
  m_itemScrollArea = setup.itemScrollArea;
  m_collections = setup.collections;
  m_currentCollectionIndex = setup.currentCollectionIndex;
}

void SelectionManager::setSelectedIndex(int index) {
  m_selectedItemIndex = index;
}

void SelectionManager::setSelectedFilePath(const QString &path) {
  m_selectedFilePath = path;
}

void SelectionManager::setSelectedWidget(MediaItemWidget *widget) {
  m_selectedMediaItem = widget;
}

void SelectionManager::clearWidgetSelectionStates() {
  if (m_scrollManager == nullptr) {
    return;
  }
  const auto &activeWidgets = m_scrollManager->getActiveWidgets();
  for (auto it = activeWidgets.begin(); it != activeWidgets.end(); ++it) {
    if ((it.value() != nullptr) && it.value()->isSelected()) {
      it.value()->setSelected(false);
    }
  }
}

void SelectionManager::clearMetadataSidebar() {
  if (m_metadataSidebar != nullptr) {
    m_metadataSidebar->clearMetadata();
  }
}

void SelectionManager::notifyScrollManagerOfSelection(int index) {
  if (m_scrollManager != nullptr) {
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
  if (m_collections == nullptr) {
    return {};
  }
  return CollectionUtils::directChildrenOf(parentIndex, *m_collections);
}

void SelectionManager::updateFilePathForSelection(
    int index, const QList<int> &subcollections) {
  if (index < subcollections.size()) {
    m_selectedFilePath.clear();
  } else {
    if (m_scrollManager != nullptr) {
      QString path = m_scrollManager->filePathForVisualIndex(index);
      m_selectedFilePath = path;
    }
  }

  if ((m_sidebarManager != nullptr) && m_sidebarManager->isSidebarVisible()) {
    MediaItemWidget *safeWidget = nullptr;
    if (m_scrollManager != nullptr) {
      const auto &activeWidgets = m_scrollManager->getActiveWidgets();
      safeWidget = activeWidgets.value(index, nullptr);
    }
    m_sidebarManager->updateSidebarMetadata(safeWidget);
  }
}

void SelectionManager::persistSelection(int collectionIndex, int itemIndex,
                                         const QString &title) {
  if (m_mainWindow == nullptr || collectionIndex < 0 ||
      collectionIndex >= m_mainWindow->m_collections.size()) {
    return;
  }

  if (m_settingsManager != nullptr) {
    m_settingsManager->setLastSelectedItem(collectionIndex, itemIndex);
  }

  QString collectionName = m_mainWindow->m_collections[collectionIndex].name;
  if (m_sessionManager) {
    m_sessionManager->setLastSelected(collectionName, itemIndex, title);
  }
}

QString SelectionManager::titleForIndex(int index,
                                         const QList<int> &subcollections) const {
  if (m_mainWindow == nullptr) {
    return {};
  }

  if (index < subcollections.size()) {
    int subIdx = subcollections[index];
    if (subIdx >= 0 && subIdx < m_mainWindow->m_collections.size()) {
      return m_mainWindow->m_collections[subIdx].name;
    }
    return {};
  }

  QString path = m_selectedFilePath;
  if (path.isEmpty() && m_scrollManager != nullptr) {
    path = m_scrollManager->filePathForVisualIndex(index);
  }
  if (!path.isEmpty()) {
    return QFileInfo(path).completeBaseName().replace('_', ' ').simplified();
  }
  return {};
}

void SelectionManager::applyWidgetSelection(MediaItemWidget *widget) {
  if (widget != nullptr) {
    widget->setSelected(true);
  }
}

void SelectionManager::clearWidgetSelection(MediaItemWidget *widget) {
  if (widget != nullptr) {
    widget->setSelected(false);
  }
}

MediaItemWidget *SelectionManager::widgetForIndex(int index) const {
  if (m_scrollManager == nullptr) {
    return nullptr;
  }
  const auto &activeWidgets = m_scrollManager->getActiveWidgets();
  return activeWidgets.value(index, nullptr);
}

bool SelectionManager::checkAndFinalizeRestore(int index) {
  if (m_restoringSelection && index == m_targetRestoreIndex) {
    m_restoringSelection = false;
    m_targetRestoreIndex = -1;
    return true;
  }
  return false;
}

void SelectionManager::prepareForRestore(int targetIndex) {
  clearSelection();

  m_restoringSelection = true;
  m_targetRestoreIndex = targetIndex;
  m_forceImmediateCenter = true;

  // Request InteractionManager to configure scroll area properties
  if (m_itemScrollArea) {
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenter, true);
    qint64 until = QDateTime::currentMSecsSinceEpoch() +
                   UIConstants::ARROW_KEY_ANIMATION_SETTLE_MS +
                   UIConstants::ARROW_CENTER_EXTRA_SUPPRESS_AFTER_RESTORE_MS;
    m_itemScrollArea->setProperty(PropertyKeys::SuppressArrowCenterUntilMs,
                                  until);
  }

  emit requestStopScrollAnimations();
}

void SelectionManager::finalizeRestore() {
  m_restoringSelection = false;
  m_targetRestoreIndex = -1;
  m_forceImmediateCenter = false;

  if (parent() != nullptr) {
    parent()->setProperty(PropertyKeys::SelectionSuppressed, false);
    parent()->setProperty(PropertyKeys::PendingSelectionIndex, -1);
  }
}

void SelectionManager::cancelPendingSelectionRestore() {
  m_selectionRestoreToken++;
  m_selectionRestorePending = false;
  if (parent() != nullptr) {
    int token =
        parent()->property(PropertyKeys::SelectionRestoreToken).toInt() + 1;
    parent()->setProperty(PropertyKeys::SelectionRestoreToken, token);
    parent()->setProperty(PropertyKeys::SelectionRestorePending, false);
  }
}

bool SelectionManager::shouldTreatAsNewRow(int targetIndex,
                                           int gridWidth) const {
  if (m_currentCollectionIndex == nullptr || m_collections == nullptr ||
      gridWidth <= 0) {
    return false;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return false;
  }
  int targetRow = targetIndex / gridWidth;
  return (m_lastSelectedRow < 0) || (targetRow != m_lastSelectedRow);
}

bool SelectionManager::shouldAnimateHorizontalHop(int fromIndex, int toIndex,
                                                  int gridWidth) {
  if (fromIndex < 0 || gridWidth <= 0) {
    return false;
  }
  return (fromIndex / gridWidth) == (toIndex / gridWidth) &&
         qAbs(toIndex - fromIndex) > 1;
}

bool SelectionManager::isNewRow(int currentSelection, int newSelection,
                                int gridWidth) {
  if (gridWidth <= 0) {
    return false;
  }
  const int currentRow = (currentSelection >= 0) ? currentSelection / gridWidth : -1;
  const int targetRow = newSelection / gridWidth;
  return currentRow != targetRow;
}

int SelectionManager::getCurrentGridWidth() const {
  if (m_collections == nullptr || m_currentCollectionIndex == nullptr) {
    return 0;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return 0;
  }
  return (*m_collections)[*m_currentCollectionIndex].gridWidth;
}

void SelectionManager::processSingleClickSelection(
    int visualIndex, const QString &filePath) {
  if ((m_scrollManager == nullptr) || (m_collections == nullptr) ||
      (m_currentCollectionIndex == nullptr)) {
    return;
  }
  int gridWidth = getCurrentGridWidth();
  if (gridWidth <= 0) {
    return;
  }

  // Clear user scroll state so centering isn't blocked
  if (m_itemScrollArea) {
    m_itemScrollArea->setProperty(PropertyKeys::UserScrollActive, false);
  }

  qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  int dcInterval = QApplication::doubleClickInterval();

  if (parent()) {
    parent()->setProperty(PropertyKeys::HorizAnimActive, false);
    parent()->setProperty(PropertyKeys::HorizAnimGen,
                parent()->property(PropertyKeys::HorizAnimGen).toInt() + 1);
  }

  if (filePath.isEmpty()) {
    m_selectedFilePath.clear();
  } else {
    m_selectedFilePath = filePath;
  }
  if (m_viewportManager) {
    m_viewportManager->setPhysicalKeyDown(false);
    m_viewportManager->setRepeating(false);
    m_viewportManager->setWrapSequenceActive(false);
  }
  emit requestStopRepeat();

  const int pendingIndex =
      parent() ? parent()->property(PropertyKeys::RowChangeFirstClickIndex).toInt() : -1;
  const qint64 pendingMs =
      parent() ? parent()->property(PropertyKeys::RowChangeFirstClickMs).toLongLong() : 0;
  const bool pendingValid =
      (pendingIndex >= 0 && (nowMs - pendingMs) <= dcInterval);

  const int fromIndex = m_selectedItemIndex;
  const bool canAnimateHoriz = shouldAnimateHorizontalHop(fromIndex, visualIndex, gridWidth);

  if (canAnimateHoriz) {
    runHorizontalHopAnimation(fromIndex, visualIndex, nowMs);
    return;
  }

  const bool treatAsNewRow = shouldTreatAsNewRow(visualIndex, gridWidth);
  if (treatAsNewRow) {
    handleNewRowClickSelection(visualIndex, nowMs);
  } else {
    const bool skipCenter = (pendingValid && pendingIndex == visualIndex);
    handleSameRowClickSelection(visualIndex, skipCenter, nowMs);
  }

  if (parent()) {
    parent()->setProperty(PropertyKeys::ClickSeriesLastMs, nowMs);
  }
  emit requestFocusItemsPage();
}

void SelectionManager::runHorizontalHopAnimation(int start, int target,
                                                 qint64 nowMs) {
  if (!parent()) {
    return;
  }
  const int gen = parent()->property(PropertyKeys::HorizAnimGen).toInt() + 1;
  parent()->setProperty(PropertyKeys::HorizAnimGen, gen);
  parent()->setProperty(PropertyKeys::HorizAnimActive, true);
  const int step = (target > start) ? 1 : -1;
  const int steps = qAbs(target - start);
  constexpr int kPerHopMs = 12;
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateSelectionForIndex(start);
  }
  for (int i = 1; i <= steps; ++i) {
    QTimer::singleShot(
        i * kPerHopMs, this, [this, gen, i, step, start, target]() {
          if (!parent() || parent()->property(PropertyKeys::HorizAnimGen).toInt() != gen) {
            return;
          }
          if (!m_scrollManager) {
            return;
          }
          int nextIdx = start + (i * step);
          if (nextIdx != target) {
            m_selectedItemIndex = nextIdx;
            m_scrollManager->updateSelectionForIndex(nextIdx);
          } else {
            if (parent()) {
              parent()->setProperty(PropertyKeys::HorizAnimActive, false);
            }
            m_selectedItemIndex = target;
            selectItemByIndex(target, true);
            emit requestCenterVertically(target, false);
          }
        });
  }
  if (parent()) {
    parent()->setProperty(PropertyKeys::ClickSeriesLastMs, nowMs);
    parent()->setProperty(PropertyKeys::RowChangeFirstClickIndex, -1);
    parent()->setProperty(PropertyKeys::RowChangeFirstClickMs, 0);
  }
  emit requestFocusItemsPage();
}

void SelectionManager::handleNewRowClickSelection(int visualIndex,
                                                  qint64 nowMs) {
  if (parent()) {
    parent()->setProperty(PropertyKeys::SelectionSuppressed, true);
    parent()->setProperty(PropertyKeys::PendingSelectionIndex, visualIndex);
    parent()->setProperty(PropertyKeys::DeferCenterOnClick, false);
    parent()->setProperty(PropertyKeys::DeferredCenterIndex, -1);
  }
  m_selectedItemIndex = visualIndex;
  QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  updateFilePathForSelection(visualIndex, subs);
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateSelectionForIndex(visualIndex);
  }
  selectItemByIndex(visualIndex, true);
  emit requestCenterVertically(visualIndex, false);
  if (parent()) {
    parent()->setProperty(PropertyKeys::RowChangeFirstClickIndex, visualIndex);
    parent()->setProperty(PropertyKeys::RowChangeFirstClickMs, nowMs);
  }
}

void SelectionManager::handleSameRowClickSelection(int visualIndex,
                                                   bool skipCenter,
                                                   qint64 /*nowMs*/) {
  if (parent()) {
    parent()->setProperty(PropertyKeys::DeferCenterOnClick, false);
    parent()->setProperty(PropertyKeys::DeferredCenterIndex, -1);
  }
  selectItemByIndex(visualIndex, true);
  if (!skipCenter) {
    emit requestCenterVertically(visualIndex, false);
  }
  if (parent()) {
    parent()->setProperty(PropertyKeys::RowChangeFirstClickIndex, -1);
    parent()->setProperty(PropertyKeys::RowChangeFirstClickMs, 0);
  }
}

int SelectionManager::handleWidgetSelection(MediaItemWidget *widget,
                                            const QPoint &clickPos,
                                            QMouseEvent *originalEvent) {
  if (widget == nullptr || m_scrollManager == nullptr) {
    return -1;
  }

  // Send synthetic mouse press event to widget for proper visual state
  if (m_gridContainer != nullptr && originalEvent != nullptr) {
    QPoint localPos = widget->mapFrom(m_gridContainer, clickPos);
    QPoint globalPos = widget->mapToGlobal(localPos);
    QMouseEvent synthetic(QEvent::MouseButtonPress, localPos, globalPos,
                          Qt::LeftButton, Qt::LeftButton,
                          originalEvent->modifiers());
    widget->mousePressEvent(&synthetic);
  }

  int visualIndex = -1;
  const auto &activeWidgets = m_scrollManager->getActiveWidgets();
  for (auto it = activeWidgets.constBegin(); it != activeWidgets.constEnd();
       ++it) {
    if (it.value() == widget) {
      visualIndex = it.key();
      break;
    }
  }
  if (visualIndex < 0) {
    return -1;
  }

  QString filePath = widget->getFilePath();
  processSingleClickSelection(visualIndex, filePath);
  return visualIndex;
}

void SelectionManager::handleWidgetClicked(MediaItemWidget *widget,
                                           const QString &filePath) {
  cancelPendingSelectionRestore();

  if ((widget == nullptr) || (m_scrollManager == nullptr) ||
      (m_collections == nullptr) || (m_currentCollectionIndex == nullptr)) {
    return;
  }
  int gridWidth = getCurrentGridWidth();
  if (gridWidth <= 0) {
    return;
  }

  int visualIndex = -1;
  const auto &active = m_scrollManager->getActiveWidgets();
  for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
    if (it.value() == widget) {
      visualIndex = it.key();
      break;
    }
  }
  if (visualIndex < 0) {
    return;
  }

  processSingleClickSelection(visualIndex, filePath);
}

void SelectionManager::selectItemByIndex(int index,
                                         bool allowHorizontalScroll) {
  Q_UNUSED(allowHorizontalScroll);
  if (m_scrollManager == nullptr || m_collections == nullptr ||
      m_currentCollectionIndex == nullptr || m_itemScrollArea == nullptr) {
    return;
  }
  if (*m_currentCollectionIndex < 0 ||
      *m_currentCollectionIndex >= m_collections->size()) {
    return;
  }

  const QStringList &filePaths = m_scrollManager->getFilePaths();
  QList<int> subcollections = getSubcollections(*m_currentCollectionIndex);
  int totalItems = subcollections.size() + filePaths.size();
  if (index < 0 || index >= totalItems) {
    return;
  }

  bool selectionChangedLocal = (index != m_selectedItemIndex);
  m_selectedItemIndex = index;
  if (selectionChangedLocal && parent()) {
    parent()->setProperty(PropertyKeys::UserFreeScroll, false);
  }

  MediaItemWidget *widget = widgetForIndex(index);
  bool suppressed = parent() &&
      parent()->property(PropertyKeys::SelectionSuppressed).toBool() &&
      parent()->property(PropertyKeys::PendingSelectionIndex).toInt() == index;
  bool skipCenter = parent() &&
      parent()->property(PropertyKeys::SuppressInitialClickCenter).toBool();

  if (widget != nullptr) {
    m_selectedMediaItem = widget;
    updateFilePathForSelection(index, subcollections);
    if (!suppressed) {
      handleSuccessfulSelection(index);
    }
  } else {
    trySelectWidget(index, subcollections, 0);
  }

  if (m_scrollManager != nullptr) {
    m_scrollManager->updateSelectionForIndex(m_selectedItemIndex);
    if (parent() && parent()->property(PropertyKeys::ClickHoldAdvancing).toBool()) {
      m_scrollManager->refreshSelectionOverlayState();
    }
  }
  emit selectionChanged(m_selectedItemIndex);

  if (suppressed) {
    persistSuppressedSelectionAndMaybeCenter(index, subcollections, skipCenter);
  }

  if (skipCenter && parent()) {
    parent()->setProperty(PropertyKeys::SuppressInitialClickCenter, false);
  }
}

void SelectionManager::persistSuppressedSelectionAndMaybeCenter(
    int index, const QList<int> &subcollections, bool skipCenter) {
  bool deferCenter = parent() &&
      parent()->property(PropertyKeys::DeferCenterOnClick).toBool() &&
      parent()->property(PropertyKeys::DeferredCenterIndex).toInt() == index;
  if (!deferCenter && !skipCenter) {
    emit requestCenterVertically(index, false);
  }
  int curColl =
      ((m_currentCollectionIndex != nullptr) ? *m_currentCollectionIndex : -1);
  if ((m_mainWindow != nullptr) && curColl >= 0 &&
      curColl < m_mainWindow->m_collections.size()) {
    QString title = titleForIndex(index, subcollections);
    persistSelection(curColl, index, title);
  }
  QTimer::singleShot(UIConstants::SHORT_TIMER_DELAY, this, [this]() {
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
      ((m_currentCollectionIndex != nullptr) ? *m_currentCollectionIndex : -1);
  if ((m_mainWindow != nullptr) && currentColl >= 0 && index >= 0) {
    persistSelectionForIndex(currentColl, index);
  }
  if (QApplication::closingDown()) {
    return;
  }

  bool immediate = m_forceImmediateCenter || restoringMatch;
  emit requestCenterVertically(index, immediate);
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateVirtualView();
  }
}

QString SelectionManager::titleForIndexInColl(int coll, int idx) const {
  if (m_mainWindow == nullptr || m_collections == nullptr ||
      coll < 0 || coll >= m_collections->size()) {
    return {};
  }

  QList<int> subs = getSubcollections(coll);
  if (idx < subs.size()) {
    int subIdx = subs[idx];
    if (subIdx >= 0 && subIdx < m_collections->size()) {
      return (*m_collections)[subIdx].name;
    }
    return {};
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
  if (m_mainWindow == nullptr || coll < 0 ||
      coll >= m_mainWindow->m_collections.size()) {
    return;
  }
  QString title = titleForIndexInColl(coll, idx);
  persistSelection(coll, idx, title);
}

void SelectionManager::trySelectWidget(int index,
                                       const QList<int> &subcollections,
                                       int attempt) {
  if (m_scrollManager == nullptr) {
    return;
  }
  constexpr int kMaxAttempts = 5;
  if (attempt >= kMaxAttempts) {
    return;
  }

  const auto &activeWidgets = m_scrollManager->getActiveWidgets();
  MediaItemWidget *widget = activeWidgets.value(index, nullptr);
  if (widget != nullptr) {
    m_selectedMediaItem = widget;
    updateFilePathForSelection(index, subcollections);
    handleSuccessfulSelection(index);
    return;
  }

  constexpr int kSelectRetryBaseMs = 30;
  constexpr int kSelectRetryStepMs = 30;
  int delay = kSelectRetryBaseMs + (attempt * kSelectRetryStepMs);
  QTimer::singleShot(delay, this, [this, index, subcollections, attempt]() {
    trySelectWidget(index, subcollections, attempt + 1);
  });
}

void SelectionManager::beginFullSelectionRestore(int targetIndex) {
  if (targetIndex < 0) {
    return;
  }

  prepareForRestore(targetIndex);

  // Stop any running scroll animations
  emit requestStopScrollAnimations();

  applySelectionStateForIndex(targetIndex);
  if (m_viewportManager) {
    m_viewportManager->applyImmediateViewportPositioningForSelection(targetIndex);
  }
  selectItemByIndex(targetIndex, false);

  if (m_selectedItemIndex == targetIndex) {
    finalizeRestoreFlagsAndFocus();
    emit selectionChanged(targetIndex);
  }

  // Finalize restore state
  finalizeRestore();

  if ((m_sidebarManager != nullptr) && m_sidebarManager->isSidebarVisible()) {
    MediaItemWidget *widget = widgetForIndex(targetIndex);
    if (widget != nullptr) {
      m_sidebarManager->updateSidebarMetadata(widget);
    }
    constexpr int kMetadataSidebarUpdateDelayMs = 120;
    scheduleSidebarMetadataUpdateIfVisible(targetIndex, 0,
                                           kMetadataSidebarUpdateDelayMs);
  }
}

void SelectionManager::applySelectionStateForIndex(int idx) {
  m_selectedItemIndex = idx;
  QList<int> subs = getSubcollections(*m_currentCollectionIndex);
  updateFilePathForSelection(idx, subs);
  if (m_scrollManager != nullptr) {
    m_scrollManager->updateVirtualView();
    m_scrollManager->updateSelectionForIndex(idx);
  }
}

void SelectionManager::finalizeRestoreFlagsAndFocus() {
  if (m_viewportManager) {
    m_viewportManager->setPhysicalKeyDown(false);
    m_viewportManager->setRepeating(false);
    m_viewportManager->setWrapSequenceActive(false);
  }
  // Only set focus to items page if search bar doesn't currently have focus
  bool searchBarHasFocus = false;
  if (m_mainWindow) {
    QLineEdit *searchBar = m_mainWindow->findChild<QLineEdit *>();
    searchBarHasFocus = searchBar && searchBar->hasFocus();
  }
  if ((m_itemsPage != nullptr) && !m_itemsPage->hasFocus() && !searchBarHasFocus) {
    emit requestFocusItemsPage();
  }
  QTimer::singleShot(UIConstants::ARROW_CENTER_CLEAR_AFTER_RESTORE_MS, this,
                     [this]() {
                       if (m_itemScrollArea) {
                         m_itemScrollArea->setProperty(
                             PropertyKeys::SuppressArrowCenter, false);
                       }
                     });
}

void SelectionManager::scheduleSidebarMetadataUpdateIfVisible(
    int targetIndex, int initialDelayMs, int secondaryDelayMs) {
  if (m_sidebarManager == nullptr || !m_sidebarManager->isSidebarVisible()) {
    return;
  }

  auto updateSidebar = [this, targetIndex]() {
    if (m_sidebarManager == nullptr || !m_sidebarManager->isSidebarVisible()) {
      return;
    }
    MediaItemWidget *widget = widgetForIndex(targetIndex);
    if (widget != nullptr) {
      m_sidebarManager->updateSidebarMetadata(widget);
    }
  };

  if (initialDelayMs > 0) {
    QTimer::singleShot(initialDelayMs, this, updateSidebar);
  } else {
    updateSidebar();
  }

  if (secondaryDelayMs > 0) {
    QTimer::singleShot(secondaryDelayMs, this, updateSidebar);
  }
}
