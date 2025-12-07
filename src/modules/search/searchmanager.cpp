// Handles search bar logic, search modes, and query debouncing for item filtering.
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
#include <QDir>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>

#ifdef KARTEND_DEBUG_LOGGING
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcSearchManager, "kartend.searchmanager")
#define debugLog(msg) qCDebug(lcSearchManager) << msg
#else
#define debugLog(msg) do {} while(0)
#endif

// SearchManagerSetup getter definitions
SETUP_GETTER_DEF_SAME(SearchManagerSetup, DatabaseManager*, DatabaseManager, databaseManager)
SETUP_GETTER_DEF_CTX_ONLY(SearchManagerSetup, InteractionStateHolder*, InteractionState, interactionState)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, NavigationManager*, NavigationManager, navigationManager)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, ScrollManager*, ScrollManager, scrollManager)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, SettingsManager*, SettingsManager, settingsManager)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, QLineEdit*, SearchBar, searchBar)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, QPushButton*, SearchModeButton, searchModeButton)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, QScrollArea*, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, QStackedWidget*, StackedWidget, stackedWidget)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, QWidget*, ItemsPage, itemsPage)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, QList<CollectionConfig>*, Collections, collections)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, int*, CurrentCollectionIndex, currentCollectionIndex)
SETUP_GETTER_DEF_SAME(SearchManagerSetup, const CollectionHierarchyCache*, HierarchyCache, hierarchyCache)

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

  auto themed = [](std::initializer_list<const char *> names) -> QIcon {
    for (const auto *name : names) {
      QIcon iconCandidate = QIcon::fromTheme(QString::fromUtf8(name));
      if (!iconCandidate.isNull()) {
        return iconCandidate;
      }
    }
    return {};
  };

  QIcon icon;
  QString tip;
  switch (m_currentSearchMode) {
  case SearchMode::CurrentCollection:
    icon = themed({"search"});
    tip = "Search: Current collection";
    break;
  case SearchMode::CurrentAndSubcollections:
    icon = themed({"folder-stash-symbolic"});
    tip = "Search: Current + subcollections";
    break;
  case SearchMode::AllCollections:
    icon = themed({"emblem-shared-symbolic"});
    tip = "Search: All collections";
    break;
  }

  m_searchModeButton->setText(QString());
  if (!icon.isNull()) {
    m_searchModeButton->setIcon(icon);
    m_searchModeButton->setIconSize(QSize(kSearchIconSizePx, kSearchIconSizePx));
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
  if (!m_collections || collIndex < 0 ||
      collIndex >= m_collections->size()) {
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
  const bool valid = ((m_collections) && collIndex >= 0 &&
                      collIndex < m_collections->size());
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

  QString mediaDir = SettingsUtils::expandConfigVariables(collCfg.mediaDirectory,
                                                          collCfg.name);
  if (mediaDir.trimmed().isEmpty()) {
    return false;
  }
  QDir dir(mediaDir);
  if (!dir.exists()) {
    return false;
  }
  const QStringList filters =
      collCfg.extensions.isEmpty() ? QStringList() : collCfg.extensions;
  const QStringList files =
      filters.isEmpty() ? dir.entryList(QDir::Files)
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

void SearchManager::onSearchTextChanged(const QString &text, int currentSelectedIndex) {
  if (!m_navigationManager) {
    return;
  }

  const QString trimmed = text.trimmed();
  const bool hasSearch = !trimmed.isEmpty();
  const int collIndex =
      (m_currentCollectionIndex) ? *m_currentCollectionIndex : -1;

  if (m_searchBar) {
    QFont searchFont = m_searchBar->font();
    searchFont.setItalic(!hasSearch);
    m_searchBar->setFont(searchFont);
  }

  if (!hasSearch) {
    if (m_searchDebounceTimer) {
      m_searchDebounceTimer->cancel();
    }
    if (m_searchItemsLoadedConn != QMetaObject::Connection()) {
      QObject::disconnect(m_searchItemsLoadedConn);
      m_searchItemsLoadedConn = QMetaObject::Connection();
    }

    if (collIndex >= 0) {
      // If we have saved pre-search state, just restore it instead of reloading
      if (m_scrollManager && m_scrollManager->hasPreSearchState()) {
        m_scrollManager->clearFilter();
        // For CurrentCollection mode with pre-search state, we need to restore
        // selection manually since onItemsLoaded won't be called
        int sel = m_preSearchSelectedIndex;
        if (sel < 0 && m_settingsManager && collIndex >= 0) {
          sel = m_settingsManager->getLastSelectedItem(collIndex);
        }
        if (sel >= 0) {
          emit requestSelectionRestore(sel);
        }
      } else {
        // For other modes, safeReloadCollection triggers onItemsLoaded which
        // handles selection restore via calculateSelectionIndex - don't emit
        // requestSelectionRestore here to avoid duplicate/racing restores
        m_navigationManager->filterItems(QString());
        m_navigationManager->safeReloadCollection(collIndex);
      }
      initializeSearchModeForCurrentCollection();
    }

    emit requestScrollbarRecovery();
    m_searchActive = false;
    return;
  }

  if (!m_searchActive) {
    m_searchActive = true;
    m_preSearchCollectionIndex = collIndex;
    m_preSearchMode = m_currentSearchMode;
    m_preSearchSelectedIndex = currentSelectedIndex;
    if (m_preSearchSelectedIndex < 0 && m_settingsManager &&
        collIndex >= 0) {
      m_preSearchSelectedIndex =
          m_settingsManager->getLastSelectedItem(collIndex);
    }
    // Save scroll view state for fast restoration when search is cleared
    // For CurrentCollection mode, items are already loaded
    // For CurrentAndSubcollections with showAllSubcollectionItems, items are also already loaded
    bool canUsePreSearchState = (m_currentSearchMode == SearchMode::CurrentCollection);
    if (m_currentSearchMode == SearchMode::CurrentAndSubcollections &&
        m_collections && collIndex >= 0 && collIndex < m_collections->size()) {
      canUsePreSearchState = (*m_collections)[collIndex].showAllSubcollectionItems;
    }
    if (m_scrollManager && canUsePreSearchState) {
      m_scrollManager->savePreSearchState();
    }
    emit requestClearSelection();
  }

  if (m_searchDebounceTimer) {
    m_searchDebounceTimer->setInterval(UIConstants::Search::TYPING_DEBOUNCE_MS);
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

  if (m_searchItemsLoadedConn != QMetaObject::Connection()) {
    QObject::disconnect(m_searchItemsLoadedConn);
    m_searchItemsLoadedConn = QMetaObject::Connection();
  }

  const int collIndex =
      (m_currentCollectionIndex ? *m_currentCollectionIndex : -1);
  if (collIndex < 0 || !m_collections ||
      collIndex >= m_collections->size()) {
    m_navigationManager->filterItems(trimmed);
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

  switch (m_currentSearchMode) {
  case SearchMode::CurrentCollection: {
    // For current collection, apply filter directly without reloading
    // The items are already loaded, just filter the existing view
    if (m_scrollManager) {
      m_scrollManager->applyFilter(trimmed);
    }
    break;
  }
  case SearchMode::CurrentAndSubcollections: {
    // If showAllSubcollectionItems is enabled, items are already loaded
    // so we can filter directly like CurrentCollection mode
    if (context.config.showAllSubcollectionItems && m_scrollManager) {
      m_scrollManager->applyFilter(trimmed);
    } else {
      // Otherwise, need to load items from all subcollections
      if (m_databaseManager) {
        m_databaseManager->loadItemsWithSubcollections(context, *m_collections);
      }
      m_navigationManager->filterItems(trimmed);
    }
    break;
  }
  case SearchMode::AllCollections: {
    m_navigationManager->loadAllCollectionsView();
    m_navigationManager->filterItems(trimmed);
    break;
  }
  }

  // Only connect for async loading cases (not when using applyFilter)
  bool needsAsyncLoad = (m_currentSearchMode == SearchMode::AllCollections) ||
                        (m_currentSearchMode == SearchMode::CurrentAndSubcollections &&
                         !context.config.showAllSubcollectionItems);
  if (needsAsyncLoad && m_databaseManager) {
    m_searchItemsLoadedConn = connect(
        m_databaseManager, &DatabaseManager::itemsLoaded, this,
        [this, trimmed]() {
          if (!m_navigationManager || !m_searchBar) {
            return;
          }
          if (m_searchBar->text().trimmed() != trimmed) {
            return;
          }
          m_navigationManager->filterItems(trimmed);
        });
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
  QTimer::singleShot(UIConstants::Search::REFOCUS_DELAY_LONG_MS, this, [this]() {
    if (m_searchBar && m_searchBar->isVisible()) {
      m_searchBar->setFocus(Qt::OtherFocusReason);
    }
  });
}
