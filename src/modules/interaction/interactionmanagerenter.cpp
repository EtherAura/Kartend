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
    const int cIdx =
        ((m_databaseManager) ? m_databaseManager->getCollectionIndexForFile(path) : -1);
    // Kartend-xbwa: when the user is browsing a playlist, getCollectionIndexForFile
    // returns the *source* collection (the one whose items table row holds this
    // path), not the playlist's synthetic index — so the launcher chooser and
    // per-item override (Kartend-dnx4) downstream key off the source's UUID +
    // launcher list, not the empty/single-launcher playlist config. The
    // fallback to the current collection only matters for ordinary collections
    // where the file→collection map hasn't been populated yet.
    const int ownerIdx = (cIdx >= 0 ? cIdx : *m_currentCollectionIndex);
    // Expand-mode: first activation expands the artwork preview; only the
    // second activation on the same selection launches.
    if (maybeExpandInsteadOfLaunch(path, ownerIdx, currentSelection)) {
      return true;
    }
    saveCurrentSelection();
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

// ─────────────────────────────────────────────────────────────────────────
// Expand-mode (two-stage activation)
// ─────────────────────────────────────────────────────────────────────────
//
// When a collection has CollectionConfig::expandMode enabled, the first
// activation (Enter or double-click) on a selected item shows the artwork
// preview overlay instead of launching the item. A second activation on the
// same item (no selection change in between) falls through to launch.
//
// State is tracked in InteractionStateHolder::expandedItemIndex(); the value
// is cleared whenever selection changes (see connectSelectionManagerSignals)
// or when the overlay is dismissed via Escape (see handleEscapeKey).
auto InteractionManager::maybeExpandInsteadOfLaunch(const QString &filePath, int collectionIndex,
                                                    int activationIndex) -> bool {
  if (filePath.isEmpty() || !m_collections) {
    return false;
  }
  // Expand-mode is a property of the *viewing* collection (what the user is
  // currently browsing), not the file's owning collection. When a parent
  // collection aggregates subcollection items via showAllSubcollectionItems,
  // the activated file's owner is the subcollection, but the setting the
  // user toggled lives on the parent they're viewing. Prefer the current
  // view; fall back to the resolved owner if no view is set.
  int effectiveIdx = (m_currentCollectionIndex && *m_currentCollectionIndex >= 0)
                         ? *m_currentCollectionIndex
                         : collectionIndex;
  if (effectiveIdx < 0 || effectiveIdx >= m_collections->size()) {
    return false;
  }
  const CollectionConfig &collection = (*m_collections)[effectiveIdx];
  if (!collection.expandMode) {
    return false;
  }
  // Use the owning collection's artwork directory so the preview matches
  // the actual file (fall back to the viewing collection if the owner is
  // unknown).
  int artworkOwnerIdx = (collectionIndex >= 0 && collectionIndex < m_collections->size())
                            ? collectionIndex
                            : effectiveIdx;
  const CollectionConfig &artworkOwner = (*m_collections)[artworkOwnerIdx];
  // Already expanded for this exact item AND the overlay is still visible
  // → fall through to launch and clear. If the user dismissed the overlay
  // by clicking outside (without changing selection), treat the next
  // activation as a fresh first-stage expand.
  if (m_state.expandedItemIndex() == activationIndex && activationIndex >= 0 && m_scrollManager &&
      m_scrollManager->isArtworkPreviewVisible()) {
    m_state.clearExpandedItem();
    m_scrollManager->hideArtworkPreview();
    return false;
  }
  // First activation: expand into a video-first preview (Kartend-ljey).
  // The overlay falls back to artwork when no video is found, preserving
  // the original expand-mode behavior for collections without
  // videoDirectory configured.
  if (!m_scrollManager) {
    return false;
  }
  const QString artworkDir =
      SettingsUtils::expandConfigVariables(artworkOwner.artworkDirectory, artworkOwner.name);
  const QString videoDir =
      SettingsUtils::expandConfigVariables(artworkOwner.videoDirectory, artworkOwner.name);
  m_scrollManager->showMediaPreview(filePath, artworkDir, videoDir);
  m_state.setExpandedItemIndex(activationIndex);
  return true;
}

void InteractionManager::onArtworkPreviewLaunchRequested(const QString &filePath) {
  // Second-stage expand-mode activation OR Enter/double-click on a
  // middle-click peek overlay: hide the overlay, clear expand state, and
  // launch the previewed item. Prefers the path the overlay was showing
  // (so a middle-click peek on a non-selected item launches the *clicked*
  // item, not whatever happens to be selected) and falls back to the
  // current selection for gallery thumbnail previews that don't carry a
  // media path.
  QString path = filePath;
  if (path.isEmpty() && m_selectionManager) {
    path = m_selectionManager->selectedFilePath();
  }
  if (path.isEmpty() && m_scrollManager) {
    path = m_scrollManager->filePathForVisualIndex(currentSelectedIndex());
  }
  if (m_scrollManager) {
    m_scrollManager->hideArtworkPreview();
  }
  m_state.clearExpandedItem();
  if (path.isEmpty()) {
    return;
  }
  const int cIdx = (m_databaseManager ? m_databaseManager->getCollectionIndexForFile(path) : -1);
  int ownerIdx = cIdx;
  if (ownerIdx < 0 && m_currentCollectionIndex) {
    ownerIdx = *m_currentCollectionIndex;
  }
  if (ownerIdx < 0) {
    return;
  }
  saveCurrentSelection();
  launchItemWithCollection(path, ownerIdx);
}

void InteractionManager::onMediaPreviewRequested(ItemWidget *widget, int visualIndex) {
  // Middle-click peek (Kartend-ljey). Resolves the clicked item's path and
  // its owning collection, then opens a video-first preview overlay. Does
  // *not* set m_state.expandedItemIndex — middle-click is a peek that
  // dismisses on Escape / click-outside, not a first-stage launch.
  if (!widget || !m_collections || !m_scrollManager) {
    return;
  }

  QString filePath = widget->getFilePath();
  if (filePath.isEmpty()) {
    filePath = m_scrollManager->filePathForVisualIndex(visualIndex);
  }
  if (filePath.isEmpty()) {
    return;
  }

  const int cIdx = m_databaseManager ? m_databaseManager->getCollectionIndexForFile(filePath) : -1;
  int ownerIdx = cIdx;
  if (ownerIdx < 0 && m_currentCollectionIndex) {
    ownerIdx = *m_currentCollectionIndex;
  }
  if (ownerIdx < 0 || ownerIdx >= m_collections->size()) {
    return;
  }

  const CollectionConfig &owner = (*m_collections)[ownerIdx];
  // Resolve directories from the owning collection so a subcollection's
  // configured paths win over the viewing parent in
  // showAllSubcollectionItems mode (mirrors expand-mode's behavior).
  const QString artworkDir =
      SettingsUtils::expandConfigVariables(owner.artworkDirectory, owner.name);
  const QString videoDir = SettingsUtils::expandConfigVariables(owner.videoDirectory, owner.name);

  m_scrollManager->showMediaPreview(filePath, artworkDir, videoDir);
}

void InteractionManager::onArtworkTypeCycleRequested(ItemWidget *widget, int visualIndex) {
  // Kartend-1v6: shift+middle-click (modifier configurable via
  // GeneralSettings::artworkCycleModifier) cycles the clicked item's grid
  // tile through the artwork types that exist on disk. Mirrors the path
  // resolution pattern from onMediaPreviewRequested so a subcollection's
  // configured artwork directory wins over the viewing parent in
  // showAllSubcollectionItems mode.
  if (!widget || !m_collections || !m_artworkManager || !m_scrollManager) {
    return;
  }

  QString filePath = widget->getFilePath();
  if (filePath.isEmpty()) {
    filePath = m_scrollManager->filePathForVisualIndex(visualIndex);
  }
  if (filePath.isEmpty()) {
    return;
  }

  const int cIdx = m_databaseManager ? m_databaseManager->getCollectionIndexForFile(filePath) : -1;
  int ownerIdx = cIdx;
  if (ownerIdx < 0 && m_currentCollectionIndex) {
    ownerIdx = *m_currentCollectionIndex;
  }
  if (ownerIdx < 0 || ownerIdx >= m_collections->size()) {
    return;
  }

  m_artworkManager->cycleArtworkType(widget, filePath, ownerIdx);
}
