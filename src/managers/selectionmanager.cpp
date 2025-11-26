#include "selectionmanager.h"

#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QScrollArea>
#include <QTimer>
#include <QWidget>

#include "collectionutils.h"
#include "itemwidget.h"
#include "mainwindow.h"
#include "metadatasidebar.h"
#include "navigationmanager.h"
#include "propertyutils.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "sidebarmanager.h"
#include "uiconstants.h"

SelectionManager::SelectionManager(QObject *parent) : QObject(parent) {}

SelectionManager::~SelectionManager() = default;

void SelectionManager::setupReferences(const SelectionManagerSetup &setup) {
  m_scrollManager = setup.scrollManager;
  m_sidebarManager = setup.sidebarManager;
  m_sessionManager = setup.sessionManager;
  m_settingsManager = setup.settingsManager;
  m_navigationManager = setup.navigationManager;
  m_mainWindow = setup.mainWindow;
  m_metadataSidebar = setup.metadataSidebar;
  m_itemsPage = setup.itemsPage;
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
