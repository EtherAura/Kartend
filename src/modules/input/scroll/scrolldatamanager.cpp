#include "scrolldatamanager.h"
#include "artworkutils.h"
#include <algorithm>
#include <QFileInfo>

ScrollDataManager::ScrollDataManager(QObject *parent) : QObject(parent) {}

void ScrollDataManager::initializeStorage(int totalCount) {
  m_filePaths.clear();
  m_fileNames.clear();

  if (totalCount > 0) {
    m_filePaths.resize(totalCount);
  }
}

void ScrollDataManager::resizeStorage(int totalCount) {
  totalCount = std::max(0, totalCount);
  const int oldSize = m_filePaths.size();
  if (totalCount == oldSize) {
    return;
  }

  // Unified sorting requires complete metadata; resizing an on-demand
  // placeholder list should not keep unified order.
  if (m_unifiedSortActive) {
    m_unifiedSortActive = false;
    m_unifiedItems.clear();
  }

  if (totalCount < oldSize) {
    // Remove name mappings for trimmed entries.
    for (int i = totalCount; i < oldSize; ++i) {
      const QString &path = m_filePaths[i];
      if (!path.isEmpty()) {
        m_fileNames.remove(path);
        m_filePathToDisplayName.remove(path);
      }
    }
  }

  m_filePaths.resize(totalCount);
}

void ScrollDataManager::initializeSubcollections(const CollectionContext &context,
                                                 const QList<CollectionConfig> *collections,
                                                 const CollectionHierarchyCache *hierarchyCache) {
  m_subcollections.clear();

  if (!collections) {
    return;
  }
  // The synthetic Home view has no host collection (currentIndex == -1) and
  // populates its tile list entirely from subcollectionOverride.
  if (context.currentIndex < 0 && !context.isRootView) {
    return;
  }

  if (context.hasSubcollectionOverride) {
    m_subcollections = context.subcollectionOverride;
  } else {
    // Use cache for O(1) lookup if available
    if (hierarchyCache && hierarchyCache->isValid()) {
      m_subcollections = hierarchyCache->directChildren(context.currentIndex);
    } else {
      // Fallback to O(n) scan
      m_subcollections = CollectionUtils::directChildrenOf(context.currentIndex, *collections);
    }
  }

  // apply toolbar-level subcollection visibility controls.
  // hideSubcollectionTiles short-circuits past the type filter — it's a
  // global "media items only" mode. The type filter then drops tiles whose
  // effective type (own type, or nearest tagged ancestor) doesn't match.
  // Search-mode overrides are honored as-is so a search hit doesn't get
  // hidden by a stale filter the user forgot to clear.
  if (!context.hasSubcollectionOverride && !m_subcollections.isEmpty()) {
    if (context.hideSubcollectionTiles) {
      m_subcollections.clear();
    } else if (!context.collectionTypeFilter.isEmpty()) {
      const QString filterLower = context.collectionTypeFilter.toLower();
      QList<int> filtered;
      filtered.reserve(m_subcollections.size());
      for (int idx : m_subcollections) {
        QString eff = CollectionUtils::effectiveCollectionType(idx, *collections);
        if (eff.toLower() == filterLower) {
          filtered.append(idx);
        }
      }
      m_subcollections = filtered;
    }
  }

  // Sort subcollections: A-Z when excluded from main sort, otherwise use sort
  // mode
  if (m_subcollections.size() > 1) {
    if (context.excludeSubfoldersFromSort) {
      // Always sort A-Z when excluded from main sorting
      std::sort(m_subcollections.begin(), m_subcollections.end(), [collections](int a, int b) {
        return QString::compare((*collections)[a].name, (*collections)[b].name,
                                Qt::CaseInsensitive) < 0;
      });
    } else {
      switch (context.sortMode) {
      case SortMode::NameAscending:
      case SortMode::DateAscending:
      case SortMode::DateDescending:
      case SortMode::SizeAscending:
      case SortMode::SizeDescending:
      case SortMode::ArtworkFirst:
      case SortMode::ArtworkLast:
        // Media metadata sorting doesn't apply to subcollections, use name A-Z
        std::sort(m_subcollections.begin(), m_subcollections.end(), [collections](int a, int b) {
          return QString::compare((*collections)[a].name, (*collections)[b].name,
                                  Qt::CaseInsensitive) < 0;
        });
        break;
      case SortMode::NameDescending:
        std::sort(m_subcollections.begin(), m_subcollections.end(), [collections](int a, int b) {
          return QString::compare((*collections)[a].name, (*collections)[b].name,
                                  Qt::CaseInsensitive) > 0;
        });
        break;
      case SortMode::Random: {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(m_subcollections.begin(), m_subcollections.end(), g);
        break;
      }
      case SortMode::CollectionAscending:
      case SortMode::CollectionDescending:
        // Collection sorting doesn't apply to subcollections, use name A-Z
        std::sort(m_subcollections.begin(), m_subcollections.end(), [collections](int a, int b) {
          return QString::compare((*collections)[a].name, (*collections)[b].name,
                                  Qt::CaseInsensitive) < 0;
        });
        break;
      }
    }
  }
}

void ScrollDataManager::initializeVirtualFolders(const CollectionContext &context) {
  m_virtualFolders.clear();

  if (context.suppressVirtualFolders) {
    return;
  }

  // Only show virtual folders if includeContentSubfolders is enabled
  // AND showAllSubfolderItems is false (otherwise items are flattened)
  if (!context.config.folderBrowsing.includeContentSubfolders ||
      context.config.folderBrowsing.showAllSubfolderItems) {
    return;
  }

  // Determine the effective directory to scan
  QString scanDir = context.config.mediaDirectory;
  if (!context.config.folderBrowsing.currentSubfolder.isEmpty()) {
    scanDir = QDir(scanDir).absoluteFilePath(context.config.folderBrowsing.currentSubfolder);
  }

  QDir dir(scanDir);
  if (!dir.exists()) {
    return;
  }

  // Get list of subdirectories, optionally including hidden folders
  QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot;
  if (context.config.folderBrowsing.showHiddenFolders) {
    filters |= QDir::Hidden;
  }
  // Get unsorted list first, then apply sort mode
  QStringList subdirs = dir.entryList(filters, QDir::Unsorted);
  for (const QString &subdir : subdirs) {
    // Store path relative to mediaDirectory for navigation
    if (context.config.folderBrowsing.currentSubfolder.isEmpty()) {
      m_virtualFolders.append(subdir);
    } else {
      m_virtualFolders.append(context.config.folderBrowsing.currentSubfolder + "/" + subdir);
    }
  }

  // Sort virtual folders: A-Z when excluded from main sort, otherwise use sort
  // mode
  if (m_virtualFolders.size() > 1) {
    if (context.excludeSubfoldersFromSort) {
      // Always sort A-Z when excluded from main sorting
      std::sort(m_virtualFolders.begin(), m_virtualFolders.end(),
                [](const QString &a, const QString &b) {
                  return QString::compare(QFileInfo(a).fileName(), QFileInfo(b).fileName(),
                                          Qt::CaseInsensitive) < 0;
                });
    } else {
      switch (context.sortMode) {
      case SortMode::NameAscending:
      case SortMode::DateAscending:
      case SortMode::DateDescending:
      case SortMode::SizeAscending:
      case SortMode::SizeDescending:
      case SortMode::ArtworkFirst:
      case SortMode::ArtworkLast:
        // Media metadata sorting doesn't apply to virtual folders, use name A-Z
        std::sort(m_virtualFolders.begin(), m_virtualFolders.end(),
                  [](const QString &a, const QString &b) {
                    return QString::compare(QFileInfo(a).fileName(), QFileInfo(b).fileName(),
                                            Qt::CaseInsensitive) < 0;
                  });
        break;
      case SortMode::NameDescending:
        std::sort(m_virtualFolders.begin(), m_virtualFolders.end(),
                  [](const QString &a, const QString &b) {
                    return QString::compare(QFileInfo(a).fileName(), QFileInfo(b).fileName(),
                                            Qt::CaseInsensitive) > 0;
                  });
        break;
      case SortMode::Random: {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(m_virtualFolders.begin(), m_virtualFolders.end(), g);
        break;
      }
      case SortMode::CollectionAscending:
      case SortMode::CollectionDescending:
        // Collection sorting doesn't apply to virtual folders, use name A-Z
        std::sort(m_virtualFolders.begin(), m_virtualFolders.end(),
                  [](const QString &a, const QString &b) {
                    return QString::compare(QFileInfo(a).fileName(), QFileInfo(b).fileName(),
                                            Qt::CaseInsensitive) < 0;
                  });
        break;
      }
    }
  }
}

void ScrollDataManager::applyUnifiedSort(const CollectionContext &context,
                                         const QList<CollectionConfig> *collections) {
  m_unifiedItems.clear();
  m_unifiedSortActive = false;

  // Skip unified sorting if subfolders are excluded from sort
  if (context.excludeSubfoldersFromSort) {
    return;
  }

  // Build unified items list
  // Subcollections
  for (int i = 0; i < m_subcollections.size(); ++i) {
    UnifiedItem item;
    item.type = UnifiedItem::Type::Subcollection;
    item.originalIndex = i;
    // Get display name from collections list
    int collectionIndex = m_subcollections[i];
    if (collections && collectionIndex >= 0 && collectionIndex < collections->size()) {
      item.displayName = (*collections)[collectionIndex].name;
    }
    m_unifiedItems.append(item);
  }

  // Virtual folders
  for (int i = 0; i < m_virtualFolders.size(); ++i) {
    UnifiedItem item;
    item.type = UnifiedItem::Type::VirtualFolder;
    item.originalIndex = i;
    // Use folder name (last component of path) as display name
    item.displayName = QFileInfo(m_virtualFolders[i]).fileName();
    m_unifiedItems.append(item);
  }

  // Media files
  for (int i = 0; i < m_filePaths.size(); ++i) {
    UnifiedItem item;
    item.type = UnifiedItem::Type::MediaFile;
    item.originalIndex = i;
    // Use file name hash if available, otherwise file name from path
    QString filePath = m_filePaths[i];
    item.displayName = m_fileNames.value(filePath, QFileInfo(filePath).completeBaseName());
    m_unifiedItems.append(item);
  }

  if (m_unifiedItems.isEmpty()) {
    return;
  }

  // Sort based on mode
  switch (context.sortMode) {
  case SortMode::NameAscending:
    std::sort(m_unifiedItems.begin(), m_unifiedItems.end(),
              [](const UnifiedItem &a, const UnifiedItem &b) {
                return QString::compare(a.displayName, b.displayName, Qt::CaseInsensitive) < 0;
              });
    break;

  case SortMode::NameDescending:
    std::sort(m_unifiedItems.begin(), m_unifiedItems.end(),
              [](const UnifiedItem &a, const UnifiedItem &b) {
                return QString::compare(a.displayName, b.displayName, Qt::CaseInsensitive) > 0;
              });
    break;

  case SortMode::DateAscending:
  case SortMode::DateDescending:
  case SortMode::SizeAscending:
  case SortMode::SizeDescending:
    // Media-only metadata sorting is applied by QueryManager before media
    // rows arrive here. Keep non-media items name-sorted so virtual folders
    // and subcollections remain deterministic in unified views.
    std::sort(m_unifiedItems.begin(), m_unifiedItems.end(),
              [](const UnifiedItem &a, const UnifiedItem &b) {
                return QString::compare(a.displayName, b.displayName, Qt::CaseInsensitive) < 0;
              });
    break;

  case SortMode::ArtworkFirst:
  case SortMode::ArtworkLast: {
    // For artwork sorting, we need to check if artwork exists for each media
    // file Subcollections and virtual folders are sorted by name as a secondary
    // sort
    QString artworkDir = context.config.artworkDirectory;
    bool artworkFirst = (context.sortMode == SortMode::ArtworkFirst);

    // Build a map of hasArtwork for media files
    QHash<int, bool> mediaHasArtwork;
    for (int i = 0; i < m_filePaths.size(); ++i) {
      QString filePath = m_filePaths[i];
      if (!filePath.isEmpty() && !artworkDir.isEmpty()) {
        QString artworkPath =
            ArtworkUtils::findArtworkForFileCached(QFileInfo(filePath).fileName(), artworkDir);
        mediaHasArtwork[i] = !artworkPath.isEmpty();
      } else {
        mediaHasArtwork[i] = false;
      }
    }

    std::sort(m_unifiedItems.begin(), m_unifiedItems.end(),
              [&mediaHasArtwork, artworkFirst](const UnifiedItem &a, const UnifiedItem &b) {
                // Non-media items (subcollections, virtual folders) sort by
                // name after media
                bool aIsMedia = (a.type == UnifiedItem::Type::MediaFile);
                bool bIsMedia = (b.type == UnifiedItem::Type::MediaFile);

                if (!aIsMedia && !bIsMedia) {
                  // Both non-media: sort by name
                  return QString::compare(a.displayName, b.displayName, Qt::CaseInsensitive) < 0;
                }
                if (!aIsMedia) return false; // Non-media comes after media
                if (!bIsMedia) return true;  // Media comes before non-media

                // Both are media files - sort by artwork presence
                bool aHasArtwork = mediaHasArtwork.value(a.originalIndex, false);
                bool bHasArtwork = mediaHasArtwork.value(b.originalIndex, false);

                if (aHasArtwork != bHasArtwork) {
                  // One has artwork, one doesn't
                  return artworkFirst ? (aHasArtwork && !bHasArtwork)
                                      : (!aHasArtwork && bHasArtwork);
                }
                // Same artwork status - sort by name as secondary
                return QString::compare(a.displayName, b.displayName, Qt::CaseInsensitive) < 0;
              });
    break;
  }

  case SortMode::Random: {
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(m_unifiedItems.begin(), m_unifiedItems.end(), g);
    break;
  }

  case SortMode::CollectionAscending:
  case SortMode::CollectionDescending:
    // Collection sorting is handled by the database query via JOIN
    // For the unified sort, just sort by name since subcollections/folders
    // don't have a "collection" property to sort by
    std::sort(m_unifiedItems.begin(), m_unifiedItems.end(),
              [sortMode = context.sortMode](const UnifiedItem &a, const UnifiedItem &b) {
                bool ascending = (sortMode == SortMode::CollectionAscending);
                int cmp = QString::compare(a.displayName, b.displayName, Qt::CaseInsensitive);
                return ascending ? (cmp < 0) : (cmp > 0);
              });
    break;
  }

  m_unifiedSortActive = true;
}

void ScrollDataManager::setupFilePathMappings(const CollectionContext &context) {
  m_filePathToDisplayName.clear();
  if (context.config.showAllSubcollectionItems) {
    for (auto it = m_fileNames.constBegin(); it != m_fileNames.constEnd(); ++it) {
      m_filePathToDisplayName[it.key()] = it.value();
    }
  }
}

void ScrollDataManager::clear() {
  m_filePaths.clear();
  m_fileNames.clear();
  m_subcollections.clear();
  m_virtualFolders.clear();
  m_filePathToDisplayName.clear();
  m_unifiedItems.clear();
  m_unifiedSortActive = false;
}

QList<int> ScrollDataManager::receiveItemsRange(int offset, const QStringList &paths,
                                                const QHash<QString, QString> &names) {
  QList<int> updatedVisualIndices;

  if (offset < 0 || offset >= m_filePaths.size()) {
    return updatedVisualIndices;
  }

  int subCount = m_subcollections.size();
  int folderCount = m_virtualFolders.size();

  for (int i = 0; i < paths.size(); ++i) {
    int index = offset + i;
    if (index < m_filePaths.size()) {
      m_filePaths[index] = paths[i];
      m_fileNames[paths[i]] = names.value(paths[i]);

      // Calculate visual index for this item
      int visualIndex = subCount + folderCount + index;
      updatedVisualIndices.append(visualIndex);
    }
  }

  return updatedVisualIndices;
}

int ScrollDataManager::visualIndexFromFilePath(const QString &filePath) const {
  if (filePath.isEmpty()) {
    return -1;
  }

  // Quick check: has this path been loaded yet?
  // m_fileNames is a hash keyed by path, so this is O(1)
  if (!m_fileNames.contains(filePath)) {
    return -1; // Path not yet loaded from database
  }

  int subCount = m_subcollections.size();
  int folderCount = m_virtualFolders.size();

  if (m_unifiedSortActive) {
    // Search unified items for the matching media file
    for (int i = 0; i < m_unifiedItems.size(); ++i) {
      const auto &item = m_unifiedItems[i];
      if (item.type == UnifiedItem::Type::MediaFile) {
        int mediaIndex = item.originalIndex;
        if (mediaIndex >= 0 && mediaIndex < m_filePaths.size() &&
            m_filePaths[mediaIndex] == filePath) {
          return i;
        }
      }
    }
    return -1;
  }

  // Linear search through file paths (could be optimized with a hash map if
  // needed)
  for (int i = 0; i < m_filePaths.size(); ++i) {
    if (m_filePaths[i] == filePath) {
      return subCount + folderCount + i;
    }
  }
  return -1;
}
