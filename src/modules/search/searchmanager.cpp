// Handles search bar logic, search modes, and query debouncing for item
// filtering.
#include "searchmanager.h"
#include "applicationcontext.h"
#include "databasemanager.h"
#include "interactionstateholder.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
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
Q_LOGGING_CATEGORY(lcSearchManager, "kartend.searchmanager")
#define debugLog(msg)                                                          \
  do {                                                                         \
    if (lcSearchManager().isDebugEnabled()) {                                  \
      qCDebug(lcSearchManager) << msg;                                         \
    }                                                                          \
  } while (0)

// Temporary diagnostic logging (release-safe) gated by env var.
// Enable with: `KARTEND_SEARCH_DIAG=1 kartend`
static inline bool searchDiagEnabled() {
  return qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG");
}

#define diagLog(msg)                                                           \
  do {                                                                         \
    if (searchDiagEnabled()) {                                                 \
      qWarning() << "[SearchDiag][SearchManager]" << msg;                      \
    }                                                                          \
  } while (0)

// SearchManagerSetup getter definitions
SETUP_GETTER_DEF_SAME(SearchManagerSetup, DatabaseManager *, DatabaseManager,
                      databaseManager)
SETUP_GETTER_DEF_CTX_ONLY(SearchManagerSetup, InteractionStateHolder *,
                          InteractionState, interactionState)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, NavigationManager *,
                      NavigationManager, navigationManager)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, ScrollManager *, ScrollManager,
                      scrollManager)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, SettingsManager *, SettingsManager,
                      settingsManager)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, QLineEdit *, SearchBar, searchBar)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, QPushButton *, SearchModeButton,
                      searchModeButton)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, QScrollArea *, ItemScrollArea,
                      itemScrollArea)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, QStackedWidget *, StackedWidget,
                      stackedWidget)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, QWidget *, ItemsPage, itemsPage)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, QList<CollectionConfig> *,
                      Collections, collections)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, int *, CurrentCollectionIndex,
                      currentCollectionIndex)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, const CollectionHierarchyCache *,
                      HierarchyCache, hierarchyCache)

SearchManager::SearchManager(QObject *parent) : QObject(parent) {
  m_searchDebounceTimer = new TimerUtils::DebouncedTimer(
      UIConstants::Search::DEBOUNCE_DELAY_MS, this);
  connect(m_searchDebounceTimer, &TimerUtils::DebouncedTimer::triggered, this,
          &SearchManager::performDebouncedSearch);
}

SearchManager::~SearchManager() = default;

void SearchManager::setupReferences(const SearchManagerSetup &setup) {
  m_state = setup.getInteractionState();
  m_databaseManager = setup.getDatabaseManager();
  m_navigationManager = setup.getNavigationManager();
  m_scrollManager = setup.getScrollManager();
  m_settingsManager = setup.getSettingsManager();
  m_hierarchyCache = setup.getHierarchyCache();
  if (setup.ctx) {
    m_generalSettings = setup.ctx->generalSettings;
  }
  m_searchBar = setup.getSearchBar();
  m_searchModeButton = setup.getSearchModeButton();
  m_itemScrollArea = setup.getItemScrollArea();
  m_stackedWidget = setup.getStackedWidget();
  m_collectionPage = setup.collectionPage;
  m_itemsPage = setup.getItemsPage();
  m_collections = setup.getCollections();
  m_currentCollectionIndex = setup.getCurrentCollectionIndex();
}

void SearchManager::toggleSearchMode() {
  const auto ctx = computeSearchContext();
  QVector<SearchMode> cycle = buildSearchModeCycle(ctx);
  if (cycle.isEmpty()) {
    return;
  }

  if (!cycle.contains(m_currentSearchMode)) {
    m_currentSearchMode = cycle.first();
    emit searchModeChanged(m_currentSearchMode);
  } else {
    int pos = cycle.indexOf(m_currentSearchMode);
    pos = std::max(pos, 0);
    SearchMode next = cycle[(pos + 1) % cycle.size()];
    if (next != m_currentSearchMode) {
      m_currentSearchMode = next;
      emit searchModeChanged(m_currentSearchMode);
    }
  }

  updateSearchModeButton();
  updateSearchBarPlaceholder();
}

void SearchManager::updateSearchModeButton() {
  if (!m_searchModeButton) {
    return;
  }

  constexpr int kSearchIconSizePx = 18;

  QIcon icon;
  QString tip;
  switch (m_currentSearchMode) {
  case SearchMode::CurrentCollection:
    icon = UIConstants::Icons::fromTheme(UIConstants::Icons::SEARCH_LOCAL);
    tip = "Search: Current collection";
    break;
  case SearchMode::CurrentAndSubcollections:
    icon = UIConstants::Icons::fromTheme(
        UIConstants::Icons::SEARCH_SUBCOLLECTIONS);
    tip = "Search: Current + subcollections";
    break;
  case SearchMode::AllCollections:
    icon = UIConstants::Icons::fromTheme(UIConstants::Icons::SEARCH_GLOBAL);
    tip = "Search: All collections";
    break;
  }

  m_searchModeButton->setText(QString());
  if (!icon.isNull()) {
    m_searchModeButton->setIcon(icon);
    m_searchModeButton->setIconSize(
        QSize(kSearchIconSizePx, kSearchIconSizePx));
  } else {
    m_searchModeButton->setIcon(QIcon());
  }
  m_searchModeButton->setToolTip(tip);
  m_searchModeButton->setStyleSheet(QString());
}

void SearchManager::updateSearchBarPlaceholder() {
  if (!m_searchBar) {
    return;
  }

  switch (m_currentSearchMode) {
  case SearchMode::CurrentCollection:
    m_searchBar->setPlaceholderText("Search current collection...");
    break;
  case SearchMode::CurrentAndSubcollections:
    m_searchBar->setPlaceholderText("Search current + subcollections...");
    break;
  case SearchMode::AllCollections:
    m_searchBar->setPlaceholderText("Search all collections...");
    break;
  }

  const bool emptyNow = m_searchBar->text().trimmed().isEmpty();
  QFont searchFont = m_searchBar->font();
  searchFont.setItalic(emptyNow);
  m_searchBar->setFont(searchFont);

  QPalette pal = m_searchBar->palette();
  QColor placeholderColor =
      QApplication::palette(m_searchBar).color(QPalette::PlaceholderText);
  constexpr qreal kPlaceholderAlpha = 0.6;
  placeholderColor.setAlphaF(kPlaceholderAlpha);
  pal.setColor(QPalette::PlaceholderText, placeholderColor);
  m_searchBar->setPalette(pal);
}

void SearchManager::initializeSearchModeForCurrentCollection() {
  const auto ctx = computeSearchContext();
  const QVector<SearchMode> allowed = buildSearchModeCycle(ctx);

  SearchMode defaultMode = m_currentSearchMode;
  if (!allowed.isEmpty()) {
    defaultMode = allowed.first();
  }

  bool mustReset = (m_currentSearchMode == SearchMode::AllCollections);
  bool invalid = !allowed.contains(m_currentSearchMode);

  SearchMode desired =
      (mustReset || invalid) ? defaultMode : m_currentSearchMode;

  if (desired != m_currentSearchMode) {
    m_currentSearchMode = desired;
    emit searchModeChanged(m_currentSearchMode);
  }

  updateSearchModeButton();
  updateSearchBarPlaceholder();
}

SearchContext SearchManager::computeSearchContext() const {
  SearchContext ctx{};

  const int collIndex =
      (m_currentCollectionIndex) ? *m_currentCollectionIndex : -1;
  if (!m_collections || collIndex < 0 || collIndex >= m_collections->size()) {
    return ctx;
  }

  const CollectionConfig &cfg = (*m_collections)[collIndex];
  QList<int> subs;
  // Use cache for O(1) lookup if available
  if (m_hierarchyCache && m_hierarchyCache->isValid()) {
    subs = m_hierarchyCache->directChildren(collIndex);
  } else {
    // Fallback to O(n) scan
    subs = CollectionUtils::directChildrenOf(collIndex, *m_collections);
  }
  ctx.hasSubs = !subs.isEmpty();

  ctx.realDirectItems = hasDirectItemsForIndex(collIndex);

  ctx.allowAll = allowAllFor(cfg, collIndex, ctx.hasSubs);

  ctx.isContainer = ctx.hasSubs && !ctx.realDirectItems;
  return ctx;
}

QVector<SearchMode>
SearchManager::buildSearchModeCycle(const SearchContext &ctx) const {
  QVector<SearchMode> cycle;
  cycle.reserve(3);

  const int collIndex =
      ((m_currentCollectionIndex) ? *m_currentCollectionIndex : -1);
  const bool valid =
      ((m_collections) && collIndex >= 0 && collIndex < m_collections->size());
  bool isRoot = false;
  if (valid) {
    isRoot = ((*m_collections)[collIndex].parentCollectionIndex == -1);
  }

  if (isRoot) {
    cycle << (ctx.hasSubs ? SearchMode::CurrentAndSubcollections
                          : SearchMode::CurrentCollection);
    if (ctx.allowAll) {
      cycle << SearchMode::AllCollections;
    }
    return cycle;
  }

  if (ctx.hasSubs) {
    cycle << SearchMode::CurrentAndSubcollections;
    if (ctx.realDirectItems) {
      cycle << SearchMode::CurrentCollection;
    }
    if (ctx.allowAll) {
      cycle << SearchMode::AllCollections;
    }
    return cycle;
  }

  cycle << SearchMode::CurrentCollection;
  if (ctx.allowAll) {
    cycle << SearchMode::AllCollections;
  }
  return cycle;
}

bool SearchManager::hasDirectItemsForIndex(int idx) const {
  if (!CollectionUtils::isValidIndex(idx, m_collections)) {
    return false;
  }

  const CollectionConfig &collCfg = (*m_collections)[idx];

  // Check database via ScrollManager's reference if available
  if (m_scrollManager) {
    // For now, use filesystem check as fallback
  }

  QString mediaDir = SettingsUtils::expandConfigVariables(
      collCfg.mediaDirectory, collCfg.name);
  if (mediaDir.trimmed().isEmpty()) {
    return false;
  }
  QDir dir(mediaDir);
  if (!dir.exists()) {
    return false;
  }
  const QStringList filters =
      collCfg.extensions.isEmpty() ? QStringList() : collCfg.extensions;
  const QStringList files = filters.isEmpty()
                                ? dir.entryList(QDir::Files)
                                : dir.entryList(filters, QDir::Files);
  return !files.isEmpty();
}

bool SearchManager::allowAllFor(const CollectionConfig &cfg, int collIndex,
                                bool hasSubs) const {
  const bool isRoot = (cfg.parentCollectionIndex == -1);
  const bool isLeaf = !hasSubs;

  if (isRoot) {
    const int total = (m_collections) ? m_collections->size() : 0;
    for (int i = 0; i < total; ++i) {
      if (i == collIndex) {
        continue;
      }
      const CollectionConfig &rootCandidate = (*m_collections)[i];
      if (rootCandidate.parentCollectionIndex == -1 &&
          hasDirectItemsForIndex(i)) {
        return true;
      }
    }
    return false;
  }

  if (hasSubs || isLeaf) {
    return true;
  }
  return false;
}

void SearchManager::onSearchTextChanged(const QString &text,
                                        int currentSelectedIndex) {
  if (!m_navigationManager) {
    return;
  }

  const QString trimmed = text.trimmed();
  const bool hasSearch = !trimmed.isEmpty();
  const int collIndex =
      (m_currentCollectionIndex) ? *m_currentCollectionIndex : -1;

  diagLog(QString("onSearchTextChanged: hasSearch=%1 mode=%2 collIndex=%3 "
                  "text='%4' sel=%5")
              .arg(hasSearch)
              .arg(static_cast<int>(m_currentSearchMode))
              .arg(collIndex)
              .arg(trimmed)
              .arg(currentSelectedIndex));

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
      if (m_scrollManager && m_scrollManager->hasPreSearchState()) {
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
          context.config.artworkDirectory =
              SettingsUtils::expandConfigVariables(
                  context.config.artworkDirectory, context.config.name);
          context.artworkDirectory = context.config.artworkDirectory;
          if (m_generalSettings) {
            context.sortMode = m_generalSettings->sortMode;
            context.excludeSubfoldersFromSort =
                m_generalSettings->excludeSubfoldersFromSort;
          }

          const int totalItems = (m_preSearchTotalItems >= 0)
                                     ? m_preSearchTotalItems
                                     : m_scrollManager->getTotalItems();
          m_scrollManager->setupVirtualScrolling(totalItems, context);
          m_scrollManager->restorePreSearchState();

          // Restore selection manually since NavigationManager isn't driving
          // this restoration path.
          int sel = m_preSearchSelectedIndex;
          if (sel < 0 && m_settingsManager && collIndex >= 0) {
            sel = m_settingsManager->getLastSelectedItem(collIndex);
          }
          if (sel >= 0) {
            emit requestSelectionRestore(sel);
          }
        } else {
          // Other modes use in-memory filter, so clearFilter restores directly.
          m_scrollManager->clearFilter();
          // For pre-search state restoration, we need to restore selection
          // manually since onItemsLoaded won't be called.
          int sel = m_preSearchSelectedIndex;
          if (sel < 0 && m_settingsManager && collIndex >= 0) {
            sel = m_settingsManager->getLastSelectedItem(collIndex);
          }
          if (sel >= 0) {
            emit requestSelectionRestore(sel);
          }
        }
      } else {
        // For other modes, safeReloadCollection triggers onItemsLoaded which
        // handles selection restore via calculateSelectionIndex - don't emit
        // requestSelectionRestore here to avoid duplicate/racing restores
        m_navigationManager->filterItems(QString());
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
    if (m_preSearchSelectedIndex < 0 && m_settingsManager && collIndex >= 0) {
      m_preSearchSelectedIndex =
          m_settingsManager->getLastSelectedItem(collIndex);
    }
    // Save scroll view state for fast restoration when search is cleared
    // For CurrentCollection mode, items are already loaded
    // For CurrentAndSubcollections with showAllSubcollectionItems, items are
    // also already loaded
    bool canUsePreSearchState =
        (m_currentSearchMode == SearchMode::CurrentCollection);
    if (m_currentSearchMode == SearchMode::CurrentAndSubcollections &&
        m_collections && collIndex >= 0 && collIndex < m_collections->size()) {
      canUsePreSearchState =
          (*m_collections)[collIndex].showAllSubcollectionItems;
    }
    if (m_scrollManager && canUsePreSearchState) {
      m_preSearchTotalItems = m_scrollManager->getTotalItems();
      m_scrollManager->savePreSearchState();
    }
    emit requestClearSelection();
  }

  if (m_searchDebounceTimer) {
    updateAdaptiveDebounce();
    int debounceMs = (m_adaptiveDebounceMs > 0)
                         ? m_adaptiveDebounceMs
                         : UIConstants::Search::TYPING_DEBOUNCE_MS;
    m_searchDebounceTimer->setInterval(debounceMs);
    m_searchDebounceTimer->trigger();
  }
}

void SearchManager::performDebouncedSearch() {
  if (!m_navigationManager || !m_searchBar) {
    return;
  }

  const QString trimmed = m_searchBar->text().trimmed();
  if (trimmed.isEmpty()) {
    return;
  }

  diagLog(QString("performDebouncedSearch: mode=%1 collIndex=%2 query='%3'")
              .arg(static_cast<int>(m_currentSearchMode))
              .arg((m_currentCollectionIndex ? *m_currentCollectionIndex : -1))
              .arg(trimmed));

  if (m_searchItemsLoadedConn != QMetaObject::Connection()) {
    QObject::disconnect(m_searchItemsLoadedConn);
    m_searchItemsLoadedConn = QMetaObject::Connection();
  }

  const int collIndex =
      (m_currentCollectionIndex ? *m_currentCollectionIndex : -1);
  if (collIndex < 0 || !m_collections || collIndex >= m_collections->size()) {
    return;
  }

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
    context.excludeSubfoldersFromSort =
        m_generalSettings->excludeSubfoldersFromSort;
  }

  switch (m_currentSearchMode) {
  case SearchMode::CurrentCollection: {
    // CurrentCollection uses on-demand range loading; in-memory filtering can
    // be incomplete. Use the DB-backed count + range pipeline.
    diagLog("dispatch filterItems(CurrentCollection)");
    // Show loading overlay while DB query is processing
    if (m_scrollManager) {
      m_scrollManager->showSearchLoadingOverlay();
    }
    m_navigationManager->filterItems(trimmed);
    break;
  }
  case SearchMode::CurrentAndSubcollections: {
    // When showAllSubcollectionItems is true, all items are already loaded
    // in memory - use fast in-memory filtering instead of DB queries.
    // This provides major performance improvement for large hierarchies.
    if (context.config.showAllSubcollectionItems && m_scrollManager) {
      diagLog("dispatch applyFilter(CurrentAndSubcollections, in-memory)");
      m_scrollManager->applyFilter(trimmed);
    } else {
      // DB-backed: include descendants even when showAllSubcollectionItems is
      // false.
      diagLog("dispatch filterItemsCurrentAndSubcollections");
      // Show loading overlay while DB query is processing
      if (m_scrollManager) {
        m_scrollManager->showSearchLoadingOverlay();
      }
      m_navigationManager->filterItemsCurrentAndSubcollections(trimmed);
    }
    break;
  }
  case SearchMode::AllCollections: {
    // DB-backed: query across all collections without loading everything into
    // memory.
    diagLog("dispatch filterItemsAllCollections");
    // Show loading overlay while DB query is processing
    if (m_scrollManager) {
      m_scrollManager->showSearchLoadingOverlay();
    }
    m_navigationManager->filterItemsAllCollections(trimmed);
    break;
  }
  }
}

void SearchManager::scheduleSearchBarRefocusIfNeeded() {
  if (!m_searchBar) {
    return;
  }
  // Don't refocus if user intentionally cleared search with Escape key
  if (m_state && m_state->search().clearedByEscape) {
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
  QTimer::singleShot(UIConstants::Search::REFOCUS_DELAY_SHORT_MS, this,
                     [this]() {
                       if (m_searchBar && m_searchBar->isVisible()) {
                         m_searchBar->setFocus(Qt::OtherFocusReason);
                       }
                     });
  // Long delay: Final retry to catch slow focus changes from animations
  QTimer::singleShot(UIConstants::Search::REFOCUS_DELAY_LONG_MS, this,
                     [this]() {
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
  qint64 now = QDateTime::currentMSecsSinceEpoch();

  if (m_lastKeystrokeTime > 0) {
    qint64 timeSinceLastKeystroke = now - m_lastKeystrokeTime;

    // Clamp to reasonable range for calculation
    int keystrokeInterval = static_cast<int>(
        std::clamp(timeSinceLastKeystroke, static_cast<qint64>(50),
                   static_cast<qint64>(500)));

    // Map keystroke interval to debounce delay:
    // Fast typing (50-100ms between keys) -> short debounce (80-120ms)
    // Slow typing (300-500ms between keys) -> long debounce (180-250ms)
    // Formula: debounce = MIN + (interval - 50) * (MAX - MIN) / (500 - 50)
    int range = MAX_ADAPTIVE_DEBOUNCE_MS - MIN_ADAPTIVE_DEBOUNCE_MS;
    int debounce =
        MIN_ADAPTIVE_DEBOUNCE_MS + ((keystrokeInterval - 50) * range) / 450;

    m_adaptiveDebounceMs = std::clamp(debounce, MIN_ADAPTIVE_DEBOUNCE_MS,
                                      MAX_ADAPTIVE_DEBOUNCE_MS);
  } else {
    // First keystroke - use default
    m_adaptiveDebounceMs = UIConstants::Search::TYPING_DEBOUNCE_MS;
  }

  m_lastKeystrokeTime = now;
}
