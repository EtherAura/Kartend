// Handles search bar logic, search modes, and query debouncing for item
// filtering.
#include "searchmanager.h"
#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/hierarchyhelpers.h"
#include "collection/validationhelpers.h"
#include "extensionutils.h"
#include "idatabasemanager.h"
#include "inavigationmanager.h"
#include "interactionstateholder.h"
#include "isettingsmanager.h"
#include "loggingcategories.h"
#include "scrollmanager.h"
#include "searchhelpers.h"
#include "settingsutils.h"
#include "uiconstants/icons.h"
#include "uiconstants/search.h"
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QtGlobal>

#include <algorithm>
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcSearchManager, "kartend.searchmanager")
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcSearchManager().isDebugEnabled()) {                                                      \
      qCDebug(lcSearchManager) << msg;                                                             \
    }                                                                                              \
  } while (0)

// SearchManagerSetup getter definitions (non-manager fields only).
SETUP_GETTER_DEF_UI_SAME(SearchManagerSetup, QLineEdit *, SearchBar, searchBar)
SETUP_GETTER_DEF_UI_SAME(SearchManagerSetup, QAction *, SearchModeAction, searchModeAction)
SETUP_GETTER_DEF_UI_SAME(SearchManagerSetup, QScrollArea *, ItemScrollArea, itemScrollArea)
SETUP_GETTER_DEF_UI_SAME(SearchManagerSetup, QStackedWidget *, StackedWidget, stackedWidget)
SETUP_GETTER_DEF_UI_SAME(SearchManagerSetup, QWidget *, ItemsPage, itemsPage)
SETUP_GETTER_DEF_COL_SAME(SearchManagerSetup, QList<CollectionConfig> *, Collections, collections)
SETUP_GETTER_DEF_COL_SAME(SearchManagerSetup, const int *, CurrentCollectionIndex,
                          currentCollectionIndex)
SETUP_GETTER_DEF_COL_SAME(SearchManagerSetup, const CollectionHierarchyCache *, HierarchyCache,
                          hierarchyCache)

SearchManager::SearchManager(QObject *parent) : QObject(parent) {
  m_searchDebounceTimer =
      new TimerUtils::DebouncedTimer(UIConstants::Search::DEBOUNCE_DELAY_MS, this);
  connect(m_searchDebounceTimer, &TimerUtils::DebouncedTimer::triggered, this,
          &SearchManager::performDebouncedSearch);
}

SearchManager::~SearchManager() = default;

void SearchManager::setupReferences(const SearchManagerSetup &setup) {
  m_ctx = setup.ctx;
  m_hierarchyCache = setup.getHierarchyCache();
  if (setup.ctx) {
    m_generalSettings = setup.ctx->collection.generalSettings;
  }
  m_searchBar = setup.getSearchBar();
  m_searchModeAction = setup.getSearchModeAction();
  m_itemScrollArea = setup.getItemScrollArea();
  m_stackedWidget = setup.getStackedWidget();
  m_collectionPage = setup.collectionPage;
  m_itemsPage = setup.getItemsPage();
  m_collections = setup.getCollections();
  m_currentCollectionIndex = setup.getCurrentCollectionIndex();

  // Drop the direct-items memo whenever collections are added/removed/reordered
  // — the coarse collectionsModified() signal is the same trigger that rebuilds
  // the hierarchy cache, so cached "has direct items" answers and their index
  // keys can no longer be trusted past it.
  m_hasDirectItemsCache.clear();
  QObject::disconnect(m_collectionsModifiedConn);
  if (auto *settings = settingsMgr()) {
    m_collectionsModifiedConn = connect(settings, &ISettingsManager::collectionsModified, this,
                                        [this]() { m_hasDirectItemsCache.clear(); });
  }
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
  if (!m_searchModeAction) {
    return;
  }

  // Always render the same Breeze "search" glyph regardless of mode — the
  // tooltip carries the mode-specific text. Keeps the in-field action visually
  // stable across CurrentCollection / CurrentAndSubcollections / AllCollections.
  const QIcon icon = UIConstants::Icons::fromTheme(UIConstants::Icons::SEARCH);

  QString tip;
  switch (m_currentSearchMode) {
  case SearchMode::CurrentCollection:
    tip = tr("Search: Current collection (click to cycle modes)");
    break;
  case SearchMode::CurrentAndSubcollections:
    tip = tr("Search: Current + subcollections (click to cycle modes)");
    break;
  case SearchMode::AllCollections:
    tip = tr("Search: All collections (click to cycle modes)");
    break;
  }

  m_searchModeAction->setIcon(icon);
  m_searchModeAction->setToolTip(tip);
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
  QColor placeholderColor = QApplication::palette(m_searchBar).color(QPalette::PlaceholderText);
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

  SearchMode desired = (mustReset || invalid) ? defaultMode : m_currentSearchMode;

  if (desired != m_currentSearchMode) {
    m_currentSearchMode = desired;
    emit searchModeChanged(m_currentSearchMode);
  }

  updateSearchModeButton();
  updateSearchBarPlaceholder();
}

SearchContext SearchManager::computeSearchContext() const {
  SearchContext ctx{};

  const int collIndex = (m_currentCollectionIndex) ? *m_currentCollectionIndex : -1;
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

QVector<SearchMode> SearchManager::buildSearchModeCycle(const SearchContext &ctx) const {
  const int collIndex = ((m_currentCollectionIndex) ? *m_currentCollectionIndex : -1);
  // Synthetic Home view: only the cross-collection scope is meaningful.
  // Pin the cycle to a single AllCollections entry so the mode toggle
  // doesn't cycle into modes that need a host collection.
  if (collIndex < 0 && navMgr() && navMgr()->isInRootView()) {
    return {SearchMode::AllCollections};
  }
  const bool valid = ((m_collections) && collIndex >= 0 && collIndex < m_collections->size());
  const bool isRoot = valid ? ((*m_collections)[collIndex].parentCollectionIndex == -1) : false;
  return SearchHelpers::buildSearchModeCycle(ctx, isRoot);
}

bool SearchManager::hasDirectItemsForIndex(int idx) const {
  if (!CollectionUtils::isValidIndex(idx, m_collections)) {
    return false;
  }

  const auto cached = m_hasDirectItemsCache.constFind(idx);
  if (cached != m_hasDirectItemsCache.constEnd()) {
    return cached.value();
  }

  const CollectionConfig &collCfg = (*m_collections)[idx];

  const auto compute = [&collCfg]() -> bool {
    QString mediaDir = SettingsUtils::expandConfigVariables(collCfg.mediaDirectory, collCfg.name);
    if (mediaDir.trimmed().isEmpty()) {
      return false;
    }
    QDir dir(mediaDir);
    if (!dir.exists()) {
      return false;
    }
    // Kartend-z6rnd: bounded existence probe instead of a full entryList()
    // enumeration — this runs on the GUI thread on every collection switch /
    // search-mode toggle, and a full readdir of a huge directory over
    // NFS/SMB stalled the UI. QDirIterator::hasNext() stops at the FIRST
    // matching entry. Glob composition goes through
    // ExtensionUtils::toNameFilters (Kartend-693zb): extensions are stored
    // bare ("bin") and the helper is the one place that prepends "*.",
    // mirroring ScanService::buildNameFilters.
    const QStringList nameFilters = ExtensionUtils::toNameFilters(collCfg.extensions);
    QDirIterator it(mediaDir, nameFilters, QDir::Files | QDir::System);
    return it.hasNext();
  };

  const bool result = compute();
  m_hasDirectItemsCache.insert(idx, result);
  return result;
}

bool SearchManager::allowAllFor(const CollectionConfig &cfg, int collIndex, bool hasSubs) const {
  if (!m_collections) {
    return false;
  }
  return SearchHelpers::allowAllFor(cfg, collIndex, hasSubs, *m_collections,
                                    [this](int i) { return hasDirectItemsForIndex(i); });
}
