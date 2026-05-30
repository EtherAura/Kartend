// Manages search filtering and subcollection filtering for scroll view
#include "filtermanager.h"
#include "applicationcontext.h"
#include "artworkutils.h"
#include "collection/hierarchyhelpers.h"
#include "filterhelpers.h"
#include "idatabasemanager.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>

FilterManager::FilterManager(QObject *parent) : QObject(parent) {}

void FilterManager::setApplicationContext(const ApplicationContext *ctx) {
  m_ctx = ctx;
}

IDatabaseManager *FilterManager::dbMgr() const {
  return m_ctx ? m_ctx->databaseManager() : nullptr;
}

void FilterManager::setCollections(const QList<CollectionConfig> *collections) {
  m_collections = collections;
}

void FilterManager::setHierarchyCache(const CollectionHierarchyCache *cache) {
  m_hierarchyCache = cache;
}

void FilterManager::setSourceData(const QStringList &filePaths,
                                  const QHash<QString, QString> &fileNames,
                                  const QHash<QString, QString> &filePathToDisplayName,
                                  const QList<int> &subcollections) {
  m_filePaths = &filePaths;
  m_fileNames = &fileNames;
  m_filePathToDisplayName = &filePathToDisplayName;
  m_subcollections = &subcollections;
}

void FilterManager::setContext(const CollectionContext &context) {
  m_context = context;
  // keep the per-collection "hide missing artwork" predicate in
  // sync with the active collection's config so callers don't have to push the
  // flag separately at each entry point.
  m_hideMissingArtwork = m_context.config.hideMissingArtwork;
  m_hideMissingArtworkDirectory = m_context.artworkDirectory.isEmpty()
                                      ? m_context.config.artworkDirectory
                                      : m_context.artworkDirectory;
}

void FilterManager::setHideMissingArtworkFilter(bool enabled, const QString &artworkDirectory) {
  m_hideMissingArtwork = enabled;
  m_hideMissingArtworkDirectory = artworkDirectory;
}

void FilterManager::applyFilter(const QString &searchText) {
  QString trimmedQuery = searchText.trimmed();
  if (trimmedQuery.isEmpty()) {
    clearFilter();
    return;
  }

  m_currentFilter = trimmedQuery;
  m_isFiltered = true;
  rebuildFilteredIndices();

  // prune media items that have no artwork after the search
  // pass. Subcollection rows in m_filteredIndices are preserved as-is.
  if (m_hideMissingArtwork && m_subcollections) {
    int subCount = m_subcollections->size();
    QList<int> kept;
    kept.reserve(m_filteredIndices.size());
    for (int originalIndex : m_filteredIndices) {
      if (originalIndex < subCount || mediaItemHasArtwork(originalIndex - subCount)) {
        kept.append(originalIndex);
      }
    }
    m_filteredIndices = std::move(kept);
  }

  if (!m_filePaths || !m_subcollections) {
    return;
  }

  int subcollectionCount = m_subcollections->size();
  int visibleFiles = 0;
  for (int actualIndex : m_filteredIndices) {
    if (actualIndex >= subcollectionCount) {
      ++visibleFiles;
    }
  }
  int totalFiles = m_filePaths->size();

  emit filterChanged(visibleFiles, totalFiles);
}

void FilterManager::applySubcollectionFilter(int subcollectionIndex) {
  if (!m_collections || subcollectionIndex < 0 || subcollectionIndex >= m_collections->size()) {
    return;
  }
  if (!m_filePaths || !m_subcollections) {
    return;
  }
  if (m_filePaths->isEmpty() && m_subcollections->isEmpty()) {
    return;
  }

  m_isFiltered = true;
  m_currentFilter = (*m_collections)[subcollectionIndex].name;
  m_filteredIndices.clear();

  QSet<int> targetCollections;
  determineTargetCollections(subcollectionIndex, targetCollections);

  // Include all direct subcollections
  for (int index = 0; index < m_subcollections->size(); ++index) {
    m_filteredIndices.append(index);
  }

  int subcollectionStartIndex = m_subcollections->size();

  // Filter media items by collection ownership; honor hideMissingArtwork
  // as an additional predicate.
  for (int mediaIndex = 0; mediaIndex < m_filePaths->size(); ++mediaIndex) {
    const QString &entry = (*m_filePaths)[mediaIndex];
    if (!itemBelongsToTargetCollections(entry, targetCollections)) {
      continue;
    }
    if (m_hideMissingArtwork && !mediaItemHasArtwork(mediaIndex)) {
      continue;
    }
    m_filteredIndices.append(subcollectionStartIndex + mediaIndex);
  }

  emit filterChanged(m_filteredIndices.size(), m_subcollections->size() + m_filePaths->size());
}

void FilterManager::clearFilter() {
  m_currentFilter.clear();
  m_filteredIndices.clear();

  // when the per-collection hideMissingArtwork toggle is on,
  // "clearing the filter" really means transitioning to the artwork-only
  // baseline filter. We populate m_filteredIndices with every subcollection
  // plus the media items that resolve to artwork, and keep m_isFiltered = true
  // so the visual→actual index map runs through m_filteredIndices.
  if (m_hideMissingArtwork && m_subcollections && m_filePaths) {
    int subCount = m_subcollections->size();
    m_filteredIndices.reserve(subCount + m_filePaths->size());
    for (int subIndex = 0; subIndex < subCount; ++subIndex) {
      m_filteredIndices.append(subIndex);
    }
    int visibleFiles = 0;
    for (int mediaIndex = 0; mediaIndex < m_filePaths->size(); ++mediaIndex) {
      if (mediaItemHasArtwork(mediaIndex)) {
        m_filteredIndices.append(subCount + mediaIndex);
        ++visibleFiles;
      }
    }
    m_isFiltered = true;
    emit filterChanged(visibleFiles, m_filePaths->size());
    return;
  }

  m_isFiltered = false;
  if (m_filePaths) {
    emit filterChanged(m_filePaths->size(), m_filePaths->size());
  }
}

auto FilterManager::getActualIndex(int visualIndex) const -> int {
  return FilterHelpers::mapVisualToActualIndex(visualIndex, m_isFiltered, m_filteredIndices);
}

void FilterManager::rebuildFilteredIndices() {
  m_filteredIndices.clear();
  if (m_currentFilter.isEmpty()) {
    return;
  }
  if (!m_filePaths || !m_subcollections) {
    return;
  }

  // Match helpers use Qt::CaseInsensitive directly — no per-item toLower()
  // allocation in the loop body.
  const QString &needle = m_currentFilter;

  int subCount = m_subcollections->size();
  int totalOriginal = subCount + m_filePaths->size();

  for (int originalIndex = 0; originalIndex < totalOriginal; ++originalIndex) {
    bool match = false;
    if (originalIndex < subCount) {
      match = matchesSubcollectionFilter(originalIndex, needle);
    } else {
      int mediaIndex = originalIndex - subCount;
      match = matchesMediaItemFilter(mediaIndex, needle);
    }
    if (match) {
      m_filteredIndices.append(originalIndex);
    }
  }
}

auto FilterManager::matchesSubcollectionFilter(int subcollectionIndex, const QString &needle) const
    -> bool {
  if (!m_subcollections || !m_collections) {
    return false;
  }
  int actualSubcollectionIndex = (*m_subcollections)[subcollectionIndex];
  if (actualSubcollectionIndex < 0 || actualSubcollectionIndex >= m_collections->size()) {
    return false;
  }
  return FilterHelpers::subcollectionNameMatches((*m_collections)[actualSubcollectionIndex].name,
                                                 needle);
}

auto FilterManager::matchesMediaItemFilter(int mediaIndex, const QString &needle) const -> bool {
  if (!m_filePaths) {
    return false;
  }
  QString rawEntry = m_filePaths->value(mediaIndex);
  QString display = getDisplayNameForMediaItem(rawEntry);
  return display.contains(needle, Qt::CaseInsensitive);
}

auto FilterManager::getDisplayNameForMediaItem(const QString &rawEntry) const -> QString {
  return FilterHelpers::displayNameForMediaEntry(
      rawEntry, m_context.config.showAllSubcollectionItems, m_context.config.mediaDirectory,
      m_filePathToDisplayName, m_fileNames);
}

auto FilterManager::mediaItemHasArtwork(int mediaIndex) const -> bool {
  if (m_hideMissingArtworkDirectory.isEmpty() || !m_filePaths) {
    return true;
  }
  if (mediaIndex < 0 || mediaIndex >= m_filePaths->size()) {
    return true;
  }
  const QString rawEntry = (*m_filePaths)[mediaIndex];
  if (rawEntry.isEmpty()) {
    return false;
  }
  // Mirrors the artwork resolution used by ItemWidgetFactory: prefer the
  // directory-scan cache (cheap, hits while artwork is being populated) over
  // a fresh stat-loop each rebuild.
  const QString lookup = QFileInfo(rawEntry).fileName();
  return !ArtworkUtils::findArtworkForFileCached(lookup, m_hideMissingArtworkDirectory).isEmpty();
}

void FilterManager::determineTargetCollections(int subcollectionIndex,
                                               QSet<int> &targetCollections) {
  targetCollections.insert(subcollectionIndex);

  QList<int> descendants;
  if (m_hierarchyCache && m_hierarchyCache->isValid()) {
    // O(1) cache lookup
    descendants = m_hierarchyCache->allDescendants(subcollectionIndex);
  } else if (m_collections) {
    // Fallback to O(n) recursive scan
    descendants = CollectionUtils::collectDescendantIndices(subcollectionIndex, *m_collections);
  }

  for (int descendant : descendants) {
    targetCollections.insert(descendant);
  }
}

auto FilterManager::itemBelongsToTargetCollections(const QString &entry,
                                                   const QSet<int> &targetCollections) const
    -> bool {
  if (auto *db = dbMgr()) {
    int collectionIndexForEntry = db->getCollectionIndexForFile(entry);
    if (collectionIndexForEntry >= 0 && targetCollections.contains(collectionIndexForEntry)) {
      return true;
    }

    // Try alternate path resolution
    if (collectionIndexForEntry < 0 && m_fileNames) {
      for (auto it = m_fileNames->constBegin(); it != m_fileNames->constEnd(); ++it) {
        const QString &key = it.key();
        if (key.endsWith("/" + entry) || key.endsWith(QDir::separator() + entry) || key == entry) {
          int altCollectionIndex = db->getCollectionIndexForFile(it.key());
          if (altCollectionIndex >= 0 && targetCollections.contains(altCollectionIndex)) {
            return true;
          }
          break;
        }
      }
    }
  } else if (m_filePathToDisplayName) {
    // Fallback to name-based matching
    QString display = m_filePathToDisplayName->value(entry);
    if (display.isEmpty()) {
      display = QFileInfo(entry).completeBaseName();
    }
    return display.contains(m_currentFilter, Qt::CaseInsensitive);
  }
  return false;
}
