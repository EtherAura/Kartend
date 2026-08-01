// Sibling TU: query handling + debounce for SearchManager.
#include "applicationcontext.h"
#include "idatabasemanager.h"
#include "inavigationmanager.h"
#include "interactionstateholder.h"
#include "isettingsmanager.h"
#include "loggingcategories.h"
#include "scrollmanager.h"
#include "searchhelpers.h"
#include "searchmanager.h"
#include "settingsutils.h"
#include "uiconstants/search.h"
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

  // Only touch the font on the empty↔non-empty transition: setFont triggers
  // a style/layout pass on the line edit, which is wasted work on every
  // keystroke where the italic state doesn't actually change.
  if (m_searchBar) {
    const bool wantItalic = !hasSearch;
    QFont searchFont = m_searchBar->font();
    if (searchFont.italic() != wantItalic) {
      searchFont.setItalic(wantItalic);
      m_searchBar->setFont(searchFont);
    }
  }

  if (!hasSearch) {
    if (m_searchDebounceTimer) {
      m_searchDebounceTimer->cancel();
    }
    // Kartend-8uoe1: the debounce cancel above only stops queries that have
    // not fired yet. A count query already in flight for the abandoned text
    // would land AFTER the restore below, pass the stale-token guard (no
    // newer request exists on this path), and rebuild the restored view as
    // search results. Invalidate it before restoring. (filterItems-based
    // branches bump the token again themselves — harmless.)
    if (navMgr()) {
      navMgr()->invalidatePendingItemCount();
    }
    // Reset adaptive debounce state when search is cleared
    m_lastKeystrokeTime = 0;
    m_adaptiveDebounceMs = 0;

    if (m_searchItemsLoadedConn != QMetaObject::Connection()) {
      QObject::disconnect(m_searchItemsLoadedConn);
      m_searchItemsLoadedConn = QMetaObject::Connection();
    }

    // The clear-path routing (root-view rebuild vs pre-search restore vs
    // full reload) is a pure decision over the saved pre-search state —
    // extracted to SearchHelpers so the state machine is unit-testable.
    const SearchHelpers::SearchClearAction clearAction = SearchHelpers::classifySearchClearAction(
        collIndex, m_preSearchInRootView, scrollMgr() && scrollMgr()->hasPreSearchState(),
        m_preSearchMode);

    // Root / Home view clear: rebuild the synthetic Home tile view rather
    // than trying to restore pre-search state (none was saved — there was
    // no host collection to snapshot).
    if (clearAction == SearchHelpers::SearchClearAction::RestoreRootView && navMgr()) {
      navMgr()->loadRootView();
      m_preSearchInRootView = false;
      emit requestScrollbarRecovery();
      m_searchActive = false;
      m_preSearchTotalItems = -1;
      return;
    }

    if (collIndex >= 0) {
      // If we have saved pre-search state, just restore it instead of reloading
      if (clearAction != SearchHelpers::SearchClearAction::ReloadCollection) {
        if (clearAction == SearchHelpers::SearchClearAction::RebuildAndRestorePreSearch) {
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
            context.sortMode = m_generalSettings->view.sortMode;
            context.excludeSubfoldersFromSort = m_generalSettings->view.excludeSubfoldersFromSort;
            // mirror toolbar filters so subcollection tile visibility honors
            // the active type filter / hide-subs toggle after the restore
            // (matches NavigationManager's canonical context builders —
            // omitting these rebuilt the pre-search view with tiles the
            // toolbar had hidden).
            context.collectionTypeFilter = m_generalSettings->view.collectionTypeFilter;
            context.hideSubcollectionTiles = m_generalSettings->view.hideSubcollectionTiles;
          }

          const int totalItems =
              (m_preSearchTotalItems >= 0) ? m_preSearchTotalItems : scrollMgr()->getTotalItems();
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
    m_preSearchInRootView = (collIndex < 0 && navMgr() && navMgr()->isInRootView());
    if (m_preSearchSelectedIndex < 0 && settingsMgr() && collIndex >= 0) {
      m_preSearchSelectedIndex = settingsMgr()->getLastSelectedItem(collIndex);
    }
    // Save scroll view state for fast restoration when search is cleared.
    // Only CurrentCollection qualifies — see SearchHelpers::shouldSavePreSearchState.
    const bool canUsePreSearchState = SearchHelpers::shouldSavePreSearchState(m_currentSearchMode);
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

  // Pipeline routing is a pure decision over (host collection, root-view
  // flag, active mode) — extracted to SearchHelpers so the dispatch table is
  // unit-testable with explicit state.
  const SearchHelpers::SearchDispatch dispatch = SearchHelpers::classifySearchDispatch(
      collIndex, m_collections ? m_collections->size() : 0, navMgr() && navMgr()->isInRootView(),
      m_currentSearchMode);

  // Root / Home view: no collection is selected (collIndex == -1) but
  // search must still work. Route directly to the cross-collection query
  // pipeline — the only mode that makes sense without a host collection.
  if (dispatch == SearchHelpers::SearchDispatch::RootAllCollections) {
    qCDebug(lcSearchDiag) << "dispatch filterItemsAllCollections (root view)";
    if (scrollMgr()) {
      scrollMgr()->showSearchLoadingOverlay();
    }
    navMgr()->filterItemsAllCollections(trimmed);
    return;
  }

  if (dispatch == SearchHelpers::SearchDispatch::None) {
    return;
  }

  // No CollectionContext is built here: every dispatch below hands off to a
  // NavigationManager pipeline that constructs its own canonical context
  // (the block this comment replaces built one nothing consumed —
  // Kartend-a4ccd).
  switch (dispatch) {
  case SearchHelpers::SearchDispatch::CurrentCollection: {
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
  case SearchHelpers::SearchDispatch::CurrentAndSubcollections: {
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
  case SearchHelpers::SearchDispatch::AllCollections: {
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
  case SearchHelpers::SearchDispatch::None:
  case SearchHelpers::SearchDispatch::RootAllCollections:
    // Both handled by the early returns above; listed so the switch stays
    // exhaustive without a default.
    break;
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
  // One restartable timer instead of three uncancellable singleShots. The old
  // trio forced focus regardless of where it had moved, so a click into another
  // field within the 400ms window had focus yanked back (Kartend-8oau). All
  // reclaim attempts now route through refocusSearchBarUnlessDeliberate, which
  // leaves a deliberate text target alone. The immediate attempt runs queued
  // (after the results-update event that may steal focus settles, like the old
  // 0ms singleShot); the timer catches slow focus changes from result-list
  // animations (the old 400ms retry).
  QMetaObject::invokeMethod(
      this, [this]() { refocusSearchBarUnlessDeliberate(); }, Qt::QueuedConnection);
  if (!m_searchBarRefocusTimer) {
    m_searchBarRefocusTimer = new QTimer(this);
    m_searchBarRefocusTimer->setSingleShot(true);
    connect(m_searchBarRefocusTimer, &QTimer::timeout, this,
            &SearchManager::refocusSearchBarUnlessDeliberate);
  }
  m_searchBarRefocusTimer->start(UIConstants::Search::REFOCUS_DELAY_LONG_MS);
}

void SearchManager::refocusSearchBarUnlessDeliberate() {
  if (!m_searchBar) {
    return;
  }
  QWidget *focused = QApplication::focusWidget();
  // The decision itself (visible / already-focused / deliberate-text-target)
  // is pure and lives in SearchHelpers; this slot only samples the live
  // widget state. A QLineEdit other than the search bar means the user
  // deliberately moved focus into another field — don't steal it back.
  // Transient grabbers during a results update (result tiles, etc.) aren't
  // text inputs, so we still reclaim focus from those (Kartend-8oau).
  const bool focusIsOtherTextInput =
      focused != m_searchBar && qobject_cast<QLineEdit *>(focused) != nullptr;
  if (!SearchHelpers::shouldRefocusSearchBar(m_searchBar->isVisible(), focused == m_searchBar,
                                             focusIsOtherTextInput)) {
    return;
  }
  m_searchBar->setFocus(Qt::OtherFocusReason);
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
