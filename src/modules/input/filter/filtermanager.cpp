// Manages search filtering and subcollection filtering for scroll view
#include "filtermanager.h"
#include "applicationcontext.h"
#include "artworkutils.h"
#include "collection/hierarchyhelpers.h"
#include "filterhelpers.h"
#include "idatabasemanager.h"
#include <algorithm>
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

void FilterManager::setSourceData(const QStringList *filePaths,
                                  const QHash<QString, QString> *fileNames,
                                  const QHash<QString, QString> *filePathToDisplayName,
                                  const QList<int> *subcollections,
                                  const QStringList *virtualFolders,
                                  const QList<int> &unifiedConcatToActual) {
  // Pointer parameters by design (Kartend-tnyff): these are stored, and the
  // const-ref version silently bound (and dangled) temporaries.
  m_filePaths = filePaths;
  m_fileNames = fileNames;
  m_filePathToDisplayName = filePathToDisplayName;
  m_subcollections = subcollections;
  m_virtualFolders = virtualFolders;
  m_concatToActual = unifiedConcatToActual;
  m_displayNameCache.clear(); // source data changed — drop precomputed names
}

auto FilterManager::prefixItemCount() const -> int {
  int count = m_subcollections ? m_subcollections->size() : 0;
  if (m_virtualFolders) {
    count += m_virtualFolders->size();
  }
  return count;
}

void FilterManager::remapFilteredIndicesToStoreSpace() {
  if (m_concatToActual.isEmpty()) {
    return;
  }
  for (int &index : m_filteredIndices) {
    if (index >= 0 && index < m_concatToActual.size()) {
      index = m_concatToActual[index];
    }
  }
  // Ascending actual order IS the unified display order, so sorting keeps the
  // filtered view's relative ordering identical to the unfiltered view.
  std::sort(m_filteredIndices.begin(), m_filteredIndices.end());
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
  // showAllSubcollectionItems / mediaDirectory feed the media display name, so
  // drop the precomputed cache when the context changes.
  m_displayNameCache.clear();
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

  // Single pass over the search results: prune media items without artwork
  // (subcollection and virtual-folder rows are preserved as-is) while
  // counting the surviving media. Counting happens in concat space BEFORE
  // the unified-sort remap — post-remap indices are permuted positions and
  // can't be band-classified by value.
  const int prefixCount = prefixItemCount();
  int visibleFiles = 0;
  if (m_hideMissingArtwork && m_subcollections) {
    QList<int> kept;
    kept.reserve(m_filteredIndices.size());
    for (int concatIndex : m_filteredIndices) {
      if (concatIndex < prefixCount) {
        kept.append(concatIndex);
      } else if (mediaItemHasArtwork(concatIndex - prefixCount)) {
        kept.append(concatIndex);
        ++visibleFiles;
      }
    }
    m_filteredIndices = std::move(kept);
  } else {
    for (int concatIndex : m_filteredIndices) {
      if (concatIndex >= prefixCount) {
        ++visibleFiles;
      }
    }
  }

  if (!m_filePaths || !m_subcollections) {
    return;
  }
  int totalFiles = m_filePaths->size();

  remapFilteredIndicesToStoreSpace();
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

  // Include all direct subcollections and virtual folders — the prefix bands
  // are the current view's navigation affordances and are never hidden by a
  // subcollection filter.
  const int prefixCount = prefixItemCount();
  m_filteredIndices.reserve(prefixCount + m_filePaths->size());
  for (int index = 0; index < prefixCount; ++index) {
    m_filteredIndices.append(index);
  }

  // Filter media items by collection ownership; honor hideMissingArtwork
  // as an additional predicate. Media actual indices start after the
  // subcollection + virtual folder bands.
  int visibleMedia = 0;
  for (int mediaIndex = 0; mediaIndex < m_filePaths->size(); ++mediaIndex) {
    const QString &entry = (*m_filePaths)[mediaIndex];
    if (!itemBelongsToTargetCollections(entry, targetCollections)) {
      continue;
    }
    if (m_hideMissingArtwork && !mediaItemHasArtwork(mediaIndex)) {
      continue;
    }
    m_filteredIndices.append(prefixCount + mediaIndex);
    ++visibleMedia;
  }

  remapFilteredIndicesToStoreSpace();
  // media-only counts, matching the filterChanged contract (the prefix bands
  // are structural, not part of the "N of M items" readout).
  emit filterChanged(visibleMedia, m_filePaths->size());
}

void FilterManager::clearFilter() {
  m_currentFilter.clear();
  m_filteredIndices.clear();

  // when the per-collection hideMissingArtwork toggle is on,
  // "clearing the filter" really means transitioning to the artwork-only
  // baseline filter. We populate m_filteredIndices with every subcollection
  // and virtual folder plus the media items that resolve to artwork, and keep
  // m_isFiltered = true so the visual→actual index map runs through
  // m_filteredIndices.
  if (m_hideMissingArtwork && m_subcollections && m_filePaths) {
    int prefixCount = prefixItemCount();
    m_filteredIndices.reserve(prefixCount + m_filePaths->size());
    for (int prefixIndex = 0; prefixIndex < prefixCount; ++prefixIndex) {
      m_filteredIndices.append(prefixIndex);
    }
    int visibleFiles = 0;
    for (int mediaIndex = 0; mediaIndex < m_filePaths->size(); ++mediaIndex) {
      if (mediaItemHasArtwork(mediaIndex)) {
        m_filteredIndices.append(prefixCount + mediaIndex);
        ++visibleFiles;
      }
    }
    m_isFiltered = true;
    remapFilteredIndicesToStoreSpace();
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
  int prefixCount = prefixItemCount();
  int totalOriginal = prefixCount + m_filePaths->size();

  // Upper bound (every row matches) — trades transient over-reservation for
  // zero mid-loop reallocations on the per-keystroke rebuild.
  m_filteredIndices.reserve(totalOriginal);
  for (int concatIndex = 0; concatIndex < totalOriginal; ++concatIndex) {
    bool match = false;
    if (concatIndex < subCount) {
      match = matchesSubcollectionFilter(concatIndex, needle);
    } else if (concatIndex < prefixCount) {
      match = matchesVirtualFolderFilter(concatIndex - subCount, needle);
    } else {
      int mediaIndex = concatIndex - prefixCount;
      match = matchesMediaItemFilter(mediaIndex, needle);
    }
    if (match) {
      m_filteredIndices.append(concatIndex);
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

auto FilterManager::matchesVirtualFolderFilter(int folderIndex, const QString &needle) const
    -> bool {
  if (!m_virtualFolders || folderIndex < 0 || folderIndex >= m_virtualFolders->size()) {
    return false;
  }
  // Folders match on their display name (the last path component), mirroring
  // how subcollections match on their configured name.
  const QString displayName = QFileInfo((*m_virtualFolders)[folderIndex]).fileName();
  return FilterHelpers::subcollectionNameMatches(displayName, needle);
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
  // Precompute-once cache: displayNameForMediaEntry builds a QDir +
  // absoluteFilePath + QFileInfo per call on the non-showAll path, and the
  // rebuild loop calls this for every media item on every keystroke. Cache by
  // rawEntry; invalidated in setSourceData/setContext (its only other inputs).
  const auto it = m_displayNameCache.constFind(rawEntry);
  if (it != m_displayNameCache.constEnd()) {
    return it.value();
  }
  QString display = FilterHelpers::displayNameForMediaEntry(
      rawEntry, m_context.config.showAllSubcollectionItems, m_context.config.mediaDirectory,
      m_filePathToDisplayName, m_fileNames);
  m_displayNameCache.insert(rawEntry, display);
  return display;
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
  // Membership test against the precomputed artwork key set instead of the
  // per-item findArtworkForFileCached cascade (20 lock-guarded probes plus
  // potential first-miss stat sweeps, for EVERY item on EVERY filter pass).
  // Both name-key variants the cascade probes are tested: the extension-
  // stripped stem and the full filename (an artwork file "Title.iso.png"
  // backs item "Title.iso" through the second key).
  ensureArtworkKeySet();
  const QString fileName = QFileInfo(rawEntry).fileName();
  const QString baseName = QFileInfo(fileName).completeBaseName();
  if (m_artworkKeySet.contains(ArtworkUtils::baseMatchKey(baseName))) {
    return true;
  }
  return fileName != baseName && m_artworkKeySet.contains(ArtworkUtils::baseMatchKey(fileName));
}

void FilterManager::ensureArtworkKeySet() const {
  // Generation is read BEFORE the build: if the DirectoryCache mutates while
  // we enumerate it, the stored generation is already stale and the next pass
  // rebuilds — conservative, never serves a set newer-tagged than its data.
  const quint64 generation = ArtworkUtils::DirectoryCache::instance().contentsGeneration();
  if (m_artworkKeySetValid && m_artworkKeySetGeneration == generation &&
      m_artworkKeySetDirectory == m_hideMissingArtworkDirectory) {
    return;
  }
  m_artworkKeySet = ArtworkUtils::buildArtworkKeySet(m_hideMissingArtworkDirectory);
  m_artworkKeySetGeneration = generation;
  m_artworkKeySetDirectory = m_hideMissingArtworkDirectory;
  m_artworkKeySetValid = true;
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

    // Try alternate path resolution.
    // Kartend-ardm7: this used to linear-scan every m_fileNames key with
    // endsWith — running inside applySubcollectionFilter's per-item loop that
    // is O(items x map) = multi-second GUI-thread freezes on large flattened
    // views. resolveRelativeFilePath answers the same question (which full
    // path does this leaf/relative entry belong to?) from the FileMapCache
    // reverse index in O(1). Like the old scan, it yields ONE candidate; an
    // entry it cannot resolve stays filtered out, exactly as a scan miss did.
    if (collectionIndexForEntry < 0 && m_fileNames) {
      const QString resolved = db->resolveRelativeFilePath(entry, *m_fileNames);
      // resolved == entry means an exact m_fileNames hit — the primary
      // getCollectionIndexForFile(entry) above already ruled that key out.
      if (!resolved.isEmpty() && resolved != entry) {
        int altCollectionIndex = db->getCollectionIndexForFile(resolved);
        if (altCollectionIndex >= 0 && targetCollections.contains(altCollectionIndex)) {
          return true;
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
