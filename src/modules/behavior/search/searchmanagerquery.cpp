// Sibling TU: query handling + debounce for SearchManager.
#include "applicationcontext.h"
#include "databasemanager.h"
#include "interactionstateholder.h"
#include "loggingcategories.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "searchhelpers.h"
#include "searchmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "uiconstants.h"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QtGlobal>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcSearchManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcSearchManager().isDebugEnabled()) {                                                      \
      qCDebug(lcSearchManager) << msg;                                                             \
    }                                                                                              \
  } while (0)

void SearchManager::onSearchTextChanged(const QString &text, int currentSelectedIndex) {
  if (!navMgr()) {
    return;
  }

  const QString trimmed = text.trimmed();
  const bool hasSearch = !trimmed.isEmpty();
  const int collIndex = (m_currentCollectionIndex) ? *m_currentCollectionIndex : -1;

  qCDebug(lcSearchDiag) << QString("onSearchTextChanged: hasSearch=%1 mode=%2 collIndex=%3 "
                                   "text='%4' sel=%5")
                               .arg(hasSearch)
                               .arg(static_cast<int>(m_currentSearchMode))
                               .arg(collIndex)
                               .arg(trimmed)
                               .arg(currentSelectedIndex);

  if (m_searchBar) {
    QFont searchFont = m_searchBar->font();
    searchFont.setItalic(!hasSearch);
    m_searchBar->setFont(searchFont);
  }

  if (!hasSearch) {
    if (m_searchDebounceTimer) {
      m_searchDebounceTimer->cancel();
    }
    // Reset adaptive debounce state when search is cleared
    m_lastKeystrokeTime = 0;
    m_adaptiveDebounceMs = 0;

    if (m_searchItemsLoadedConn != QMetaObject::Connection()) {
      QObject::disconnect(m_searchItemsLoadedConn);
      m_searchItemsLoadedConn = QMetaObject::Connection();
    }

    if (collIndex >= 0) {
      // If we have saved pre-search state, just restore it instead of reloading
      if (scrollMgr() && scrollMgr()->hasPreSearchState()) {
        if (m_preSearchMode == SearchMode::CurrentCollection) {
          // CurrentCollection searches are DB-backed (count + on-demand
          // ranges), so the scroll data backing the search view is different
          // from the pre-search view. Rebuild the pre-search view and then
          // restore the cached widgets/scroll position for instant recovery.
          CollectionContext context;
          context.currentIndex = collIndex;
          context.config = (*m_collections)[collIndex];
          context.config.mediaDirectory = SettingsUtils::expandConfigVariables(
              context.config.mediaDirectory, context.config.name);
          context.config.artworkDirectory = SettingsUtils::expandConfigVariables(
              context.config.artworkDirectory, context.config.name);
          context.artworkDirectory = context.config.artworkDirectory;
          if (m_generalSettings) {
            context.sortMode = m_generalSettings->sortMode;
            context.excludeSubfoldersFromSort = m_generalSettings->excludeSubfoldersFromSort;
          }

          const int totalItems = (m_preSearchTotalItems >= 0) ? m_preSearchTotalItems
                                                              : scrollMgr()->getTotalItems();
          scrollMgr()->setupVirtualScrolling(totalItems, context);
          scrollMgr()->restorePreSearchState();

          // Restore selection manually since NavigationManager isn't driving
          // this restoration path.
          int sel = m_preSearchSelectedIndex;
          if (sel < 0 && settingsMgr() && collIndex >= 0) {
            sel = settingsMgr()->getLastSelectedItem(collIndex);
          }
          if (sel >= 0) {
            emit requestSelectionRestore(sel);
          }
        } else {
          // Other modes use in-memory filter, so clearFilter restores directly.
          scrollMgr()->clearFilter();
          // For pre-search state restoration, we need to restore selection
          // manually since onItemsLoaded won't be called.
          int sel = m_preSearchSelectedIndex;
          if (sel < 0 && settingsMgr() && collIndex >= 0) {
            sel = settingsMgr()->getLastSelectedItem(collIndex);
          }
          if (sel >= 0) {
            emit requestSelectionRestore(sel);
          }
        }
      } else {
        // For other modes, safeReloadCollection triggers onItemsLoaded which
        // handles selection restore via calculateSelectionIndex - don't emit
        // requestSelectionRestore here to avoid duplicate/racing restores
        navMgr()->filterItems(QString());
      }
      initializeSearchModeForCurrentCollection();
    }

    emit requestScrollbarRecovery();
    m_searchActive = false;
    m_preSearchTotalItems = -1;
    return;
  }

  if (!m_searchActive) {
    m_searchActive = true;
    m_preSearchCollectionIndex = collIndex;
    m_preSearchMode = m_currentSearchMode;
    m_preSearchSelectedIndex = currentSelectedIndex;
    if (m_preSearchSelectedIndex < 0 && settingsMgr() && collIndex >= 0) {
      m_preSearchSelectedIndex = settingsMgr()->getLastSelectedItem(collIndex);
    }
    // Save scroll view state for fast restoration when search is cleared.
    // Only CurrentCollection uses the pre-search restore path: it reloads via
    // setupVirtualScrolling on the same collection context. CurrentAndSub-
    // collections and AllCollections are DB-backed and can change the visible
    // data backing (and the subcollection/virtual-folder tile composition), so
    // they take the full reload path on clear instead. See bd.
    const bool canUsePreSearchState = (m_currentSearchMode == SearchMode::CurrentCollection);
    if (scrollMgr() && canUsePreSearchState) {
      m_preSearchTotalItems = scrollMgr()->getTotalItems();
      scrollMgr()->savePreSearchState();
    }
    emit requestClearSelection();
  }

  if (m_searchDebounceTimer) {
    updateAdaptiveDebounce();
    int debounceMs =
        (m_adaptiveDebounceMs > 0) ? m_adaptiveDebounceMs : UIConstants::Search::TYPING_DEBOUNCE_MS;
    m_searchDebounceTimer->setInterval(debounceMs);
    m_searchDebounceTimer->trigger();
  }
}

void SearchManager::performDebouncedSearch() {
  if (!navMgr() || !m_searchBar) {
    return;
  }

  const QString trimmed = m_searchBar->text().trimmed();
  if (trimmed.isEmpty()) {
    return;
  }

  qCDebug(lcSearchDiag) << QString("performDebouncedSearch: mode=%1 collIndex=%2 query='%3'")
                               .arg(static_cast<int>(m_currentSearchMode))
                               .arg((m_currentCollectionIndex ? *m_currentCollectionIndex : -1))
                               .arg(trimmed);

  if (m_searchItemsLoadedConn != QMetaObject::Connection()) {
    QObject::disconnect(m_searchItemsLoadedConn);
    m_searchItemsLoadedConn = QMetaObject::Connection();
  }

  const int collIndex = (m_currentCollectionIndex ? *m_currentCollectionIndex : -1);
  if (collIndex < 0 || !m_collections || collIndex >= m_collections->size()) {
    return;
  }

  CollectionContext context;
  context.currentIndex = collIndex;
  context.config = (*m_collections)[collIndex];
  context.config.mediaDirectory =
      SettingsUtils::expandConfigVariables(context.config.mediaDirectory, context.config.name);
  context.config.artworkDirectory =
      SettingsUtils::expandConfigVariables(context.config.artworkDirectory, context.config.name);
  context.artworkDirectory = context.config.artworkDirectory;
  if (m_generalSettings) {
    context.sortMode = m_generalSettings->sortMode;
    context.excludeSubfoldersFromSort = m_generalSettings->excludeSubfoldersFromSort;
  }

  switch (m_currentSearchMode) {
  case SearchMode::CurrentCollection: {
    // CurrentCollection uses on-demand range loading; in-memory filtering can
    // be incomplete. Use the DB-backed count + range pipeline.
    qCDebug(lcSearchDiag) << "dispatch filterItems(CurrentCollection)";
    // Show loading overlay while DB query is processing
    if (scrollMgr()) {
      scrollMgr()->showSearchLoadingOverlay();
    }
    navMgr()->filterItems(trimmed);
    break;
  }
  case SearchMode::CurrentAndSubcollections: {
    // Always DB-backed: items are loaded on-demand even when
    // showAllSubcollectionItems is true, so the in-memory file list is mostly
    // empty placeholders. Filtering it would silently drop unloaded items.
    // The DB-backed path uses FTS/LIKE and returns the full result set.
    // See bd.
    qCDebug(lcSearchDiag) << "dispatch filterItemsCurrentAndSubcollections";
    if (scrollMgr()) {
      scrollMgr()->showSearchLoadingOverlay();
    }
    navMgr()->filterItemsCurrentAndSubcollections(trimmed);
    break;
  }
  case SearchMode::AllCollections: {
    // DB-backed: query across all collections without loading everything into
    // memory.
    qCDebug(lcSearchDiag) << "dispatch filterItemsAllCollections";
    // Show loading overlay while DB query is processing
    if (scrollMgr()) {
      scrollMgr()->showSearchLoadingOverlay();
    }
    navMgr()->filterItemsAllCollections(trimmed);
    break;
  }
  }
}

void SearchManager::scheduleSearchBarRefocusIfNeeded() {
  if (!m_searchBar) {
    return;
  }
  // Don't refocus if user intentionally cleared search with Escape key
  if (state() && state()->search().clearedByEscape) {
    return;
  }
  // Attempt refocus at multiple intervals to handle race conditions where
  // other widgets briefly steal focus during search result updates.
  // 0ms: Try immediately after current event processing completes
  QTimer::singleShot(0, this, [this]() {
    if (m_searchBar && m_searchBar->isVisible()) {
      m_searchBar->setFocus(Qt::OtherFocusReason);
    }
  });
  // Short delay: Retry after widgets have finished their initial layout
  QTimer::singleShot(UIConstants::Search::REFOCUS_DELAY_SHORT_MS, this, [this]() {
    if (m_searchBar && m_searchBar->isVisible()) {
      m_searchBar->setFocus(Qt::OtherFocusReason);
    }
  });
  // Long delay: Final retry to catch slow focus changes from animations
  QTimer::singleShot(UIConstants::Search::REFOCUS_DELAY_LONG_MS, this, [this]() {
    if (m_searchBar && m_searchBar->isVisible()) {
      m_searchBar->setFocus(Qt::OtherFocusReason);
    }
  });
}

// Updates debounce interval based on typing speed
// Fast typing (< 100ms between keystrokes) = shorter debounce for
// responsiveness Slow typing (> 300ms between keystrokes) = longer debounce to
// avoid premature searches
void SearchManager::updateAdaptiveDebounce() {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const qint64 gap = (m_lastKeystrokeTime > 0) ? (now - m_lastKeystrokeTime) : 0;
  m_adaptiveDebounceMs = SearchHelpers::computeAdaptiveDebounceMs(
      gap, MIN_ADAPTIVE_DEBOUNCE_MS, MAX_ADAPTIVE_DEBOUNCE_MS,
      UIConstants::Search::TYPING_DEBOUNCE_MS);
  m_lastKeystrokeTime = now;
}
