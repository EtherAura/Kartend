// Manages search filtering and subcollection filtering for scroll view
#include "filtermanager.h"
#include "databasemanager.h"
#include "filterhelpers.h"
#include <QDir>
#include <QFileInfo>

FilterManager::FilterManager(QObject *parent) : QObject(parent) {}

void FilterManager::setDatabaseManager(DatabaseManager *manager) {
  m_databaseManager = manager;
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

  // Filter media items by collection ownership
  for (int mediaIndex = 0; mediaIndex < m_filePaths->size(); ++mediaIndex) {
    const QString &entry = (*m_filePaths)[mediaIndex];
    if (itemBelongsToTargetCollections(entry, targetCollections)) {
      m_filteredIndices.append(subcollectionStartIndex + mediaIndex);
    }
  }

  emit filterChanged(m_filteredIndices.size(), m_subcollections->size() + m_filePaths->size());
}

void FilterManager::clearFilter() {
  if (!m_isFiltered) {
    if (m_filePaths) {
      emit filterChanged(m_filePaths->size(), m_filePaths->size());
    }
    return;
  }
  m_isFiltered = false;
  m_currentFilter.clear();
  m_filteredIndices.clear();

  if (m_filePaths) {
    emit filterChanged(m_filePaths->size(), m_filePaths->size());
  }
}

auto FilterManager::getActualIndex(int visualIndex) const -> int {
  return FilterHelpers::mapVisualToActualIndex(visualIndex, m_isFiltered, m_filteredIndices);
}

void FilterManager::rebuildFilteredIndices() {
  m_filteredIndices.clear();
  QString needle = m_currentFilter.toLower();
  if (needle.isEmpty()) {
    return;
  }
  if (!m_filePaths || !m_subcollections) {
    return;
  }

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
  return display.toLower().contains(needle);
}

auto FilterManager::getDisplayNameForMediaItem(const QString &rawEntry) const -> QString {
  return FilterHelpers::displayNameForMediaEntry(
      rawEntry, m_context.config.showAllSubcollectionItems, m_context.config.mediaDirectory,
      m_filePathToDisplayName, m_fileNames);
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
  if (m_databaseManager) {
    int collectionIndexForEntry = m_databaseManager->getCollectionIndexForFile(entry);
    if (collectionIndexForEntry >= 0 && targetCollections.contains(collectionIndexForEntry)) {
      return true;
    }

    // Try alternate path resolution
    if (collectionIndexForEntry < 0 && m_fileNames) {
      for (auto it = m_fileNames->constBegin(); it != m_fileNames->constEnd(); ++it) {
        const QString &key = it.key();
        if (key.endsWith("/" + entry) || key.endsWith(QDir::separator() + entry) || key == entry) {
          int altCollectionIndex = m_databaseManager->getCollectionIndexForFile(it.key());
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
