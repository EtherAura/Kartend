// Sibling translation unit for InteractionManager.
// Extracted from interactionmanager.cpp during LOC-reduction refactor.
// These remain InteractionManager members; this is a translation-unit split.
#include "interactionmanager.h"

#include <algorithm>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPoint>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>

#include "alphabeticnavigationhandler.h"
#include "animationmanager.h"
#include "arrownavigationhandler.h"
#include "eventmanager.h"
#include "gamepadmanager.h"
#include "keyboardmanager.h"
#include "launchmanager.h"
#include "mousemanager.h"
#include "searchmanager.h"
#include "selectionmanager.h"
#include "viewportmanager.h"

#include "artworkmanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "gridutils.h"
#include "itemwidget.h"
#include "metadatasidebar.h"
#include "navigationmanager.h"
#include "navigationstackmanager.h"
#include "scrolldatamanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "sidebarmanager.h"
#include "timerutils.h"
#include "uiconstants.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcInteractionManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcInteractionManager().isDebugEnabled()) {                                                 \
      qCDebug(lcInteractionManager) << msg;                                                        \
    }                                                                                              \
  } while (0)

auto InteractionManager::processEnterOrReturnKey(int totalItems) -> bool {
  const int currentSelection = std::max(0, currentSelectedIndex());
  if (currentSelection < 0 || currentSelection >= totalItems) {
    return true;
  }
  // Use the *rendered* subcollection count from the scroll data, not the
  // hierarchy cache. During search the rendered list contains only matching
  // subcollections (or none), while getSubcollections() still returns the full
  // unfiltered parent's children. Using the latter caused media items to be
  // misclassified as subcollections, navigating into the wrong child and
  // "clearing the search and breaking the view."
  const int actualIndex =
      m_scrollManager ? m_scrollManager->getFilteredIndex(currentSelection) : currentSelection;
  const int renderedSubCount = m_scrollManager ? m_scrollManager->getSubcollectionCount() : 0;
  if (actualIndex >= 0 && actualIndex < renderedSubCount) {
    const int subCollIdx =
        m_scrollManager && m_scrollManager->getDataManager()
            ? m_scrollManager->getDataManager()->subcollectionIndexFromActual(actualIndex)
            : -1;
    if (subCollIdx >= 0) {
      return handleEnterOnSubcollection(actualIndex, subCollIdx);
    }
  }

  // Check if this is a virtual folder
  if (m_scrollManager) {
    QString folderPath = m_scrollManager->virtualFolderPathForVisualIndex(currentSelection);
    if (!folderPath.isEmpty()) {
      return handleEnterOnVirtualFolder(folderPath);
    }
  }

  return handleEnterOnItem(currentSelection, totalItems);
}

auto InteractionManager::handleEnterOnSubcollection(int subActualIndex, int subCollIdx) -> bool {
  saveCurrentSelection();
  const int subIdx = subCollIdx;
  if (m_navigationManager) {
    if (*m_currentCollectionIndex >= 0 && *m_currentCollectionIndex < m_collections->size()) {
      m_navigationManager->stackManager()->push(*m_currentCollectionIndex);
    }
    clearSelectionAndFocus();
    if (m_sidebarManager) {
      m_sidebarManager->updateSidebarMetadata(nullptr);
    }
    const bool success = m_navigationManager->showCollectionItems(subIdx);
    if (!success) {
      // Undo the push if navigation failed
      (void)m_navigationManager->stackManager()->pop();
      selectItemByIndex(subActualIndex, true);
      if (m_itemsPage) {
        m_itemsPage->setFocus();
      }
    } else {
      // Delay horizontal centering until after subcollection navigation
      // animations complete and layout is stable
      constexpr int kHorizontalCenterDelayMs = 600;
      QTimer::singleShot(kHorizontalCenterDelayMs, this, [this]() {
        if (!QApplication::closingDown() && m_scrollManager) {
          m_scrollManager->centerHorizontalScrollbar(*m_currentCollectionIndex, *m_collections);
        }
      });
    }
  }
  return true;
}

auto InteractionManager::handleEnterOnVirtualFolder(const QString &folderPath) -> bool {
  if (m_navigationManager) {
    m_navigationManager->onVirtualFolderEntered(folderPath);
  }
  return true;
}

auto InteractionManager::handleEnterOnItem(int currentSelection, int /*totalItems*/) -> bool {
  QString path = m_selectionManager ? m_selectionManager->selectedFilePath() : QString();
  if (path.isEmpty() && (m_scrollManager)) {
    path = m_scrollManager->filePathForVisualIndex(currentSelection);
  }
  if (!path.isEmpty()) {
    saveCurrentSelection();
    const int cIdx =
        ((m_databaseManager) ? m_databaseManager->getCollectionIndexForFile(path) : -1);
    const int ownerIdx = (cIdx >= 0 ? cIdx : *m_currentCollectionIndex);
    launchItemWithCollection(path, ownerIdx);
  }
  return true;
}

auto InteractionManager::isItemOffscreen(int selection, int gridWidth) const -> bool {
  if (!m_itemScrollArea || selection < 0 || gridWidth <= 0 ||
      !CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections)) {
    return false;
  }
  const CollectionConfig &collection = (*m_collections)[*m_currentCollectionIndex];
  QScrollBar *vbar = m_itemScrollArea->verticalScrollBar();
  if (!vbar) {
    return false;
  }
  const int viewportH = m_itemScrollArea->viewport()->height();
  if (viewportH <= 0) {
    return false;
  }
  int logicalItemY =
      GridUtils::computeItemY(selection, gridWidth, collection.itemHeight,
                              collection.verticalSpacing, UIConstants::Grid::MARGINS);

  // Convert widget scroll position to logical for visibility check in clipped
  // grids
  int logicalVisibleTop = vbar->value();
  if (m_scrollManager) {
    const auto &metrics = m_scrollManager->getMetrics();
    if (metrics.isClipped) {
      logicalVisibleTop = metrics.toLogicalScrollY(vbar->value(), viewportH);
    }
  }
  const int logicalVisibleBottom = logicalVisibleTop + viewportH;
  return (logicalItemY + collection.itemHeight) <= logicalVisibleTop ||
         logicalItemY >= logicalVisibleBottom;
}
