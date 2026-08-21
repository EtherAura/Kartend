// Search/filter/pre-search-state methods extracted from scrollmanager.cpp.
// Operate on raw aliases (m_filterManager, m_dataManager,
// m_preSearchStateManager) into m_dataSource.
#include "applicationcontext.h"
#include "artworkutils.h"
#include "coverflowcontroller.h"
#include "datasourcemanager.h"
#include "filtermanager.h"
#include "loggingcategories.h"
#include "presearchstatemanager.h"
#include "scrolldatamanager.h"
#include "scrollmanager.h"
#include "timerutils.h"
#include "uiconstants/scroll.h"
#include "widgetpoolmanager.h"
#include <QLoggingCategory>
#include <QScrollArea>
#include <QScrollBar>

Q_DECLARE_LOGGING_CATEGORY(lcScrollManager)

void ScrollManager::applyFilter(const QString &searchText) {
  if (!m_filterManager) {
    return;
  }

  // Update FilterManager's source data before applying filter
  m_filterManager->setSourceData(&m_dataManager->filePaths(), &m_dataManager->fileNames(),
                                 &m_dataManager->filePathToDisplayName(),
                                 &m_dataManager->subcollections(), &m_dataManager->virtualFolders(),
                                 m_dataManager->unifiedConcatToActualMap());
  m_filterManager->setContext(m_context);
  m_filterManager->applyFilter(searchText);

  // Update local state from FilterManager. The unfiltered fallback is the
  // store's full count — virtual folders included.
  m_totalItems = m_filterManager->isFiltered() ? m_filterManager->filteredCount()
                                               : m_dataManager->totalItemCount();

  calculateVirtualMetrics();
  positionVirtualContainer();

  // Release widgets back to pool for reuse instead of just hiding
  qCDebug(lcScrollManager).nospace()
      << "WIDGETREL reason=filterChange-release-ALL n=" << m_activeWidgets.size();
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    if (ItemWidget *widget = it.value()) {
      releaseWidget(widget);
    }
  }
  clearActiveWidgets();

  if (m_mediaScrollArea && m_mediaScrollArea->verticalScrollBar()) {
    m_mediaScrollArea->verticalScrollBar()->setValue(0);
  }

  updateVirtualView();
}

void ScrollManager::refreshHideMissingArtworkBaseline() {
  if (m_destroying || !m_filterManager || !m_dataManager) {
    return;
  }
  // Only the artwork-only baseline (isFiltered with no filter text): an
  // explicit search / subcollection filter owns m_filteredIndices and
  // re-applies through its own entry points.
  if (!m_filterManager->isFiltered() || !m_filterManager->currentFilter().isEmpty()) {
    return;
  }
  if (m_isMutating) {
    // A collection mutation is mid-flight — relayouting under it would race
    // the mutation's own rebuild. The refresh isn't lost, just re-queued.
    m_baselineRefilterTimer->trigger();
    return;
  }
  // Re-push source pointers + context so the recompute sees the current
  // store, then rebuild the artwork-only baseline against the now-loaded
  // paths (unloaded rows passed as "unknown" — Kartend-l66sn).
  m_filterManager->setSourceData(&m_dataManager->filePaths(), &m_dataManager->fileNames(),
                                 &m_dataManager->filePathToDisplayName(),
                                 &m_dataManager->subcollections(), &m_dataManager->virtualFolders(),
                                 m_dataManager->unifiedConcatToActualMap());
  m_filterManager->setContext(m_context);
  // Sample settledness BEFORE the pass. The prewarm runs concurrently, so a
  // post-pass sample can say "settled" for a pass that actually ran cold —
  // which read as authoritative and left every fail-open row visible for
  // good. Sampling first is race-safe in the direction that matters: the
  // cascade only ever gets warmer, so a pre-pass "settled" can't be
  // invalidated mid-pass (Kartend-l66sn).
  const bool passIsAuthoritative = m_filterManager->artworkKeySetSettled();
  m_filterManager->clearFilter();

  qCDebug(lcPerfTrace) << "HideMissing baseline refresh: authoritative=" << passIsAuthoritative
                       << "filteredCount=" << m_filterManager->filteredCount()
                       << "storeCount=" << m_dataManager->totalItemCount()
                       << "retries=" << m_baselineRefilterRetries;

  // While the artwork lookup cascade is cold, the pass ran fail-open (every
  // row visible — mediaItemHasArtwork's unsettled stance), so it is not the
  // authoritative prune yet. Warm the cascade and re-arm; the settled pass
  // then hides the genuinely artless items. Bounded so a wedged prewarm
  // degrades to "filter off" instead of polling forever.
  if (!passIsAuthoritative) {
    ArtworkUtils::DirectoryCache::instance().schedulePrewarm(
        ArtworkUtils::artworkLookupDirectories(m_filterManager->hideMissingArtworkDirectory()));
    if (++m_baselineRefilterRetries <= UIConstants::Scroll::HIDE_MISSING_REFILTER_MAX_RETRIES) {
      m_baselineRefilterTimer->trigger();
    }
  } else {
    m_baselineRefilterRetries = 0;
  }

  m_totalItems = m_filterManager->isFiltered() ? m_filterManager->filteredCount()
                                               : m_dataManager->totalItemCount();
  calculateVirtualMetrics();
  positionVirtualContainer();
  // A newly-hidden row shifts every visual index after it, and active
  // widgets are keyed by visual index — rebind wholesale rather than patch.
  // Deliberately NO scrollbar reset: this fires mid-load, not on a user
  // action, and yanking the viewport to the top would be visible.
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    if (ItemWidget *widget = it.value()) {
      releaseWidget(widget);
    }
  }
  clearActiveWidgets();
  updateVirtualView();
  // The filtered index space changed wholesale; cover flow's incremental
  // patch path cannot map it (no actual→visual reverse map when filtered).
  if (m_coverFlow) {
    m_coverFlow->rebuildCardsIfActive();
  }
}

void ScrollManager::cleanupActiveWidgets() {
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    if (ItemWidget *widget = it.value()) {
      releaseWidget(widget);
    }
  }
  clearActiveWidgets();
}

void ScrollManager::savePreSearchState() {
  if (m_preSearchStateManager) {
    m_preSearchStateManager->saveState(m_activeWidgets);
  }
}

void ScrollManager::restorePreSearchState() {
  if (!m_preSearchStateManager || !m_preSearchStateManager->hasSavedState()) {
    return;
  }

  // Create position callback for widget repositioning
  auto getPositionFunc = [this](int index) -> QPoint { return getItemPosition(index); };

  m_preSearchStateManager->restoreState(m_activeWidgets, m_virtualContainer, m_widgetPool.get(),
                                        m_ctx ? m_ctx->artworkManager() : nullptr, getPositionFunc,
                                        m_metrics.itemWidth, m_metrics.itemHeight);
}

auto ScrollManager::hasPreSearchState() const -> bool {
  return m_dataSource && m_dataSource->hasPreSearchState();
}

void ScrollManager::showSearchLoadingOverlay() {
  if (m_dataSource) {
    m_dataSource->showSearchLoadingOverlay();
  }
}

void ScrollManager::hideSearchLoadingOverlay() {
  if (m_dataSource) {
    m_dataSource->hideSearchLoadingOverlay();
  }
}

void ScrollManager::clearFilter() {
  if (!m_filterManager) {
    emit filterChanged(m_dataManager->fileCount(), m_dataManager->fileCount());
    return;
  }

  bool hasPreSearch = hasPreSearchState();

  if (!m_filterManager->isFiltered()) {
    // Filter not active, but we may still have pre-search widgets to restore
    if (hasPreSearch) {
      restorePreSearchState();
    }
    emit filterChanged(m_dataManager->fileCount(), m_dataManager->fileCount());
    return;
  }

  m_filterManager->clearFilter();
  m_totalItems = m_dataManager->totalItemCount();

  calculateVirtualMetrics();

  // Pre-set scroll position BEFORE positionVirtualContainer to prevent visual
  // jump where list briefly shows at position 0 then jumps to remembered
  // position
  if (hasPreSearch && m_mediaScrollArea && m_preSearchStateManager) {
    if (QScrollBar *scrollbar = m_mediaScrollArea->verticalScrollBar()) {
      int viewportHeight = m_mediaScrollArea->viewport()->height();
      // Use clamped totalHeight for scrollbar range
      int scrollMax = qMax(0, m_metrics.totalHeight - viewportHeight);
      scrollbar->setRange(0, scrollMax);
      scrollbar->setValue(m_preSearchStateManager->savedScrollPosition());
    }
  }

  positionVirtualContainer();

  // Try to restore saved pre-search state for instant recovery
  if (hasPreSearch) {
    restorePreSearchState();
  } else {
    // Fallback: release widgets and rebuild view
    for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
      if (ItemWidget *widget = it.value()) {
        releaseWidget(widget);
      }
    }
    clearActiveWidgets();
    updateVirtualView();
  }
}

auto ScrollManager::getFilteredIndex(int visualIndex) const -> int {
  return m_dataSource ? m_dataSource->getFilteredIndex(visualIndex) : visualIndex;
}
