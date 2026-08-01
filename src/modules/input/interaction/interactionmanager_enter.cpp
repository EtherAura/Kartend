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
#include "arrownavigationhandler.h"
#include "artworkpreviewoverlay.h"
#include "eventmanager.h"
#include "gamepadmanager.h"
#include "ianimationmanager.h"
#include "ikeyboardmanager.h"
#include "imousemanager.h"
#include "interactionhelpers.h"
#include "iviewportmanager.h"
#include "launchmanager.h"
#include "searchmanager.h"
#include "selectionmanager.h"

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "collection/validationhelpers.h"
#include "gridutils.h"
#include "iartworkmanager.h"
#include "idatabasemanager.h"
#include "idetailspane.h"
#include "idetailspanemanager.h"
#include "inavigationmanager.h"
#include "isessionmanager.h"
#include "isettingsmanager.h"
#include "itemwidget.h"
#include "navigationstackmanager.h"
#include "scrollmanager.h"
#include "settingsutils.h"
#include "timerutils.h"
#include "uiconstants/grid.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcInteractionManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcInteractionManager().isDebugEnabled()) {                                                 \
      qCDebug(lcInteractionManager) << msg;                                                        \
    }                                                                                              \
  } while (0)

auto InteractionManager::processEnterOrReturnKey(int totalItems) -> bool {
  // Kartend-l06g6: capture UNCLAMPED — the old std::max(0, …) made the < 0
  // guard unsatisfiable, so Enter/Confirm with nothing selected (e.g. right
  // after entering a collection with rememberSelection off) activated item 0
  // instead of no-op'ing as the guard intended.
  const int currentSelection = currentSelectedIndex();
  if (currentSelection < 0 || currentSelection >= totalItems) {
    return true;
  }
  // Classify via the rendered scroll data, not the hierarchy cache. During
  // search the rendered list contains only matching subcollections (or none),
  // while getSubcollections() still returns the full unfiltered parent's
  // children. Using the latter caused media items to be misclassified as
  // subcollections, navigating into the wrong child and "clearing the search
  // and breaking the view." subcollectionIndexFromActual routes through the
  // store's unified-aware mapping and returns -1 for non-subcollection
  // indices — a raw `actualIndex < subCount` band check would skip
  // subcollections that unified sort permutes past the subcollection band
  // (mirrors VirtualScrollEngine::ensureWidgetForIndex).
  const int actualIndex =
      scrollMgr() ? scrollMgr()->getFilteredIndex(currentSelection) : currentSelection;
  if (scrollMgr() && actualIndex >= 0) {
    const int subCollIdx = scrollMgr()->subcollectionIndexFromActual(actualIndex);
    if (subCollIdx >= 0) {
      return handleEnterOnSubcollection(actualIndex, subCollIdx);
    }
  }

  // Check if this is a virtual folder
  if (scrollMgr()) {
    QString folderPath = scrollMgr()->virtualFolderPathForVisualIndex(currentSelection);
    if (!folderPath.isEmpty()) {
      return handleEnterOnVirtualFolder(folderPath);
    }
  }

  return handleEnterOnItem(currentSelection, totalItems);
}

auto InteractionManager::handleEnterOnSubcollection(int subActualIndex, int subCollIdx) -> bool {
  saveCurrentSelection();
  const int subIdx = subCollIdx;
  if (navMgr()) {
    if (*m_currentCollectionIndex >= 0 && *m_currentCollectionIndex < m_collections->size()) {
      navMgr()->stackManager()->push(*m_currentCollectionIndex);
    }
    clearSelectionAndFocus();
    if (detailsPaneMgr()) {
      detailsPaneMgr()->updateSidebarMetadata(nullptr);
    }
    const bool success = navMgr()->showCollectionItems(subIdx);
    if (!success) {
      // Undo the push if navigation failed
      (void)navMgr()->stackManager()->pop();
      selectItemByIndex(subActualIndex, true);
      if (m_itemsPage) {
        m_itemsPage->setFocus();
      }
    } else {
      constexpr int kHorizontalCenterDelayMs = 600;
      // Delay horizontal centering until after the subcollection navigation
      // animation completes and the layout has settled — centering against an
      // in-flight scroll fights the animation and leaves the viewport
      // visibly off by the animation's last few frames.
      QTimer::singleShot(kHorizontalCenterDelayMs, this, [this]() {
        if (!QApplication::closingDown() && scrollMgr()) {
          scrollMgr()->centerHorizontalScrollbar(*m_currentCollectionIndex, *m_collections);
        }
      });
    }
  }
  return true;
}

auto InteractionManager::handleEnterOnVirtualFolder(const QString &folderPath) -> bool {
  if (navMgr()) {
    navMgr()->onVirtualFolderEntered(folderPath);
  }
  return true;
}

auto InteractionManager::handleEnterOnItem(int currentSelection, int /*totalItems*/) -> bool {
  QString path = m_selectionManager ? m_selectionManager->selectedFilePath() : QString();
  if (path.isEmpty() && (scrollMgr())) {
    path = scrollMgr()->filePathForVisualIndex(currentSelection);
  }
  if (!path.isEmpty()) {
    const int cIdx = ((databaseMgr()) ? databaseMgr()->getCollectionIndexForFile(path) : -1);
    // when the user is browsing a playlist, getCollectionIndexForFile
    // returns the *source* collection (the one whose items table row holds this
    // path), not the playlist's synthetic index — so the launcher chooser and
    // per-item override downstream key off the source's UUID +
    // launcher list, not the empty/single-launcher playlist config. The
    // fallback to the current collection only matters for ordinary collections
    // where the file→collection map hasn't been populated yet.
    const int ownerIdx = InteractionHelpers::resolveOwnerIndex(
        cIdx, m_currentCollectionIndex ? *m_currentCollectionIndex : -1,
        m_collections ? m_collections->size() : 0);
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
      GridUtils::computeItemY(selection, gridWidth, collection.gridLayout.itemHeight,
                              collection.gridLayout.verticalSpacing, UIConstants::Grid::MARGINS);

  // Convert widget scroll position to logical for visibility check in clipped
  // grids
  int logicalVisibleTop = vbar->value();
  if (scrollMgr()) {
    const auto &metrics = scrollMgr()->getMetrics();
    if (metrics.isClipped) {
      logicalVisibleTop = metrics.toLogicalScrollY(vbar->value(), viewportH);
    }
  }
  const int logicalVisibleBottom = logicalVisibleTop + viewportH;
  return (logicalItemY + collection.gridLayout.itemHeight) <= logicalVisibleTop ||
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
  // The three-way routing (launch directly / collapse-then-launch / try to
  // expand) is a pure decision on the expand-mode flag + overlay state —
  // extracted to InteractionHelpers so the two-stage activation contract is
  // unit-testable. "Already expanded for this exact item AND the overlay is
  // still visible" falls through to launch and clears; if the user dismissed
  // the overlay by clicking outside (without changing selection), the next
  // activation is a fresh first-stage expand.
  const auto activation = InteractionHelpers::classifyExpandActivation(
      collection.expandMode, m_state.expandedItemIndex(), activationIndex,
      scrollMgr() && scrollMgr()->isArtworkPreviewVisible());
  if (activation == InteractionHelpers::ExpandActivation::LaunchDirectly) {
    return false;
  }
  // Use the owning collection's artwork directory so the preview matches
  // the actual file (fall back to the viewing collection if the owner is
  // unknown).
  int artworkOwnerIdx = (collectionIndex >= 0 && collectionIndex < m_collections->size())
                            ? collectionIndex
                            : effectiveIdx;
  const CollectionConfig &artworkOwner = (*m_collections)[artworkOwnerIdx];
  if (activation == InteractionHelpers::ExpandActivation::CollapseThenLaunch) {
    m_state.clearExpandedItem();
    scrollMgr()->hideArtworkPreview();
    return false;
  }
  // First activation: expand into a video-first preview.
  // The overlay falls back to artwork when no video is found, preserving
  // the original expand-mode behavior for collections without
  // videoDirectory configured.
  if (!scrollMgr()) {
    return false;
  }
  const QString artworkDir =
      SettingsUtils::expandConfigVariables(artworkOwner.artworkDirectory, artworkOwner.name);
  const QString videoDir =
      SettingsUtils::expandConfigVariables(artworkOwner.videoDirectory, artworkOwner.name);
  // When the item has neither a video nor artwork the overlay stays
  // hidden. Don't enter the expand state in that case: the second-stage
  // launch gate above requires a *visible* overlay, so a hidden overlay
  // would trap the item — every activation re-expands and it can never
  // launch. Fall through to a normal launch instead.
  if (!scrollMgr()->showMediaPreview(filePath, artworkDir, videoDir)) {
    m_state.clearExpandedItem();
    return false;
  }
  // Mirror the sidebar's gallery into the overlay's bottom thumb strip so
  // Left/Right + thumb click can cycle the main preview between the
  // item's scraped artwork types. DetailsPane already has the entries
  // built for the currently-selected item; pull from there rather than
  // re-running the DB/resolve cycle.
  if (auto *details = detailsPaneMgr() ? detailsPaneMgr()->sidebarWidget() : nullptr) {
    const auto sidebarEntries = details->currentGalleryEntries();
    QList<ArtworkPreviewOverlay::GalleryEntry> overlayEntries;
    overlayEntries.reserve(sidebarEntries.size());
    for (const auto &e : sidebarEntries) {
      overlayEntries.append({e.label, e.path, e.isVideo});
    }
    static_cast<ScrollManager *>(scrollMgr())->setArtworkPreviewGallery(overlayEntries);
  }
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
  if (path.isEmpty() && scrollMgr()) {
    path = scrollMgr()->filePathForVisualIndex(currentSelectedIndex());
  }
  if (scrollMgr()) {
    scrollMgr()->hideArtworkPreview();
  }
  m_state.clearExpandedItem();
  if (path.isEmpty()) {
    return;
  }
  const int cIdx = (databaseMgr() ? databaseMgr()->getCollectionIndexForFile(path) : -1);
  const int ownerIdx = InteractionHelpers::resolveOwnerIndex(
      cIdx, m_currentCollectionIndex ? *m_currentCollectionIndex : -1,
      m_collections ? m_collections->size() : 0);
  if (ownerIdx < 0) {
    return;
  }
  saveCurrentSelection();
  launchItemWithCollection(path, ownerIdx);
}

void InteractionManager::onMediaPreviewRequested(ItemWidget *widget, int visualIndex) {
  // Middle-click peek. Resolves the clicked item's path and
  // its owning collection, then opens a video-first preview overlay. Does
  // *not* set m_state.expandedItemIndex — middle-click is a peek that
  // dismisses on Escape / click-outside, not a first-stage launch.
  if (!widget || !m_collections || !scrollMgr()) {
    return;
  }

  QString filePath = widget->getFilePath();
  if (filePath.isEmpty()) {
    filePath = scrollMgr()->filePathForVisualIndex(visualIndex);
  }
  if (filePath.isEmpty()) {
    return;
  }

  const int cIdx = databaseMgr() ? databaseMgr()->getCollectionIndexForFile(filePath) : -1;
  const int ownerIdx = InteractionHelpers::resolveOwnerIndex(
      cIdx, m_currentCollectionIndex ? *m_currentCollectionIndex : -1, m_collections->size());
  if (ownerIdx < 0) {
    return;
  }

  const CollectionConfig &owner = (*m_collections)[ownerIdx];
  // Resolve directories from the owning collection so a subcollection's
  // configured paths win over the viewing parent in
  // showAllSubcollectionItems mode (mirrors expand-mode's behavior).
  const QString artworkDir =
      SettingsUtils::expandConfigVariables(owner.artworkDirectory, owner.name);
  const QString videoDir = SettingsUtils::expandConfigVariables(owner.videoDirectory, owner.name);

  scrollMgr()->showMediaPreview(filePath, artworkDir, videoDir);
  // Same rationale as maybeExpandInsteadOfLaunch — keep the overlay's
  // thumb strip in lockstep with the sidebar's gallery so middle-click
  // peek gets the same cycle affordance.
  if (auto *details = detailsPaneMgr() ? detailsPaneMgr()->sidebarWidget() : nullptr) {
    const auto sidebarEntries = details->currentGalleryEntries();
    QList<ArtworkPreviewOverlay::GalleryEntry> overlayEntries;
    overlayEntries.reserve(sidebarEntries.size());
    for (const auto &e : sidebarEntries) {
      overlayEntries.append({e.label, e.path, e.isVideo});
    }
    static_cast<ScrollManager *>(scrollMgr())->setArtworkPreviewGallery(overlayEntries);
  }
}

void InteractionManager::onArtworkTypeCycleRequested(ItemWidget *widget, int visualIndex) {
  // shift+middle-click (modifier configurable via
  // GeneralSettings::artworkCycleModifier) cycles the clicked item's grid
  // tile through the artwork types that exist on disk. Mirrors the path
  // resolution pattern from onMediaPreviewRequested so a subcollection's
  // configured artwork directory wins over the viewing parent in
  // showAllSubcollectionItems mode.
  if (!widget || !m_collections || !artworkMgr() || !scrollMgr()) {
    return;
  }

  QString filePath = widget->getFilePath();
  if (filePath.isEmpty()) {
    filePath = scrollMgr()->filePathForVisualIndex(visualIndex);
  }
  if (filePath.isEmpty()) {
    return;
  }

  const int cIdx = databaseMgr() ? databaseMgr()->getCollectionIndexForFile(filePath) : -1;
  const int ownerIdx = InteractionHelpers::resolveOwnerIndex(
      cIdx, m_currentCollectionIndex ? *m_currentCollectionIndex : -1, m_collections->size());
  if (ownerIdx < 0) {
    return;
  }

  artworkMgr()->cycleArtworkType(widget, filePath, ownerIdx);
}
