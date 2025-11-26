#include "searchmanager.h"
#include "databasemanager.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsutils.h"
#include "uiconstants.h"
#include <QApplication>
#include <QDir>

SearchManager::SearchManager(QObject *parent) : QObject(parent) {}

SearchManager::~SearchManager() = default;

void SearchManager::setupReferences(const SearchManagerSetup &setup) {
  m_databaseManager = setup.databaseManager;
  m_navigationManager = setup.navigationManager;
  m_scrollManager = setup.scrollManager;
  m_mainWindow = setup.mainWindow;
  m_searchBar = setup.searchBar;
  m_searchModeButton = setup.searchModeButton;
  m_collections = setup.collections;
  m_currentCollectionIndex = setup.currentCollectionIndex;
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
  if (m_searchModeButton == nullptr) {
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
  if (m_searchBar == nullptr) {
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
      (m_currentCollectionIndex != nullptr) ? *m_currentCollectionIndex : -1;
  if (m_collections == nullptr || collIndex < 0 ||
      collIndex >= m_collections->size()) {
    return ctx;
  }

  const CollectionConfig &cfg = (*m_collections)[collIndex];
  const QList<int> subs =
      CollectionUtils::directChildrenOf(collIndex, *m_collections);
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
      ((m_currentCollectionIndex != nullptr) ? *m_currentCollectionIndex : -1);
  const bool valid = ((m_collections != nullptr) && collIndex >= 0 &&
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
  if (m_collections == nullptr || idx < 0 || idx >= m_collections->size()) {
    return false;
  }

  const CollectionConfig &collCfg = (*m_collections)[idx];

  // Check database via ScrollManager's reference if available
  if (m_scrollManager != nullptr) {
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
    const int total = (m_collections != nullptr) ? m_collections->size() : 0;
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
