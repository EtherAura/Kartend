#include "scrolldatamanager.h"
#include <QFileInfo>
#include <algorithm>

ScrollDataManager::ScrollDataManager(QObject *parent) : QObject(parent) {}

void ScrollDataManager::initializeStorage(int totalCount) {
  m_filePaths.clear();
  m_fileNames.clear();
  
  if (totalCount > 0) {
    m_filePaths.resize(totalCount);
  }
}

void ScrollDataManager::initializeSubcollections(
    const CollectionContext &context,
    const QList<CollectionConfig> *collections,
    const CollectionHierarchyCache *hierarchyCache) {
  m_subcollections.clear();
  
  if (!collections || context.currentIndex < 0) {
    return;
  }
  
  // Use cache for O(1) lookup if available
  if (hierarchyCache && hierarchyCache->isValid()) {
    m_subcollections = hierarchyCache->directChildren(context.currentIndex);
  } else {
    // Fallback to O(n) scan
    m_subcollections = CollectionUtils::directChildrenOf(context.currentIndex, *collections);
  }
  
  // Sort subcollections: A-Z when excluded from main sort, otherwise use sort mode
  if (m_subcollections.size() > 1) {
    if (context.excludeSubfoldersFromSort) {
      // Always sort A-Z when excluded from main sorting
      std::sort(m_subcollections.begin(), m_subcollections.end(),
                [collections](int a, int b) {
                  return QString::compare((*collections)[a].name,
                                          (*collections)[b].name,
                                          Qt::CaseInsensitive) < 0;
                });
    } else {
      switch (context.sortMode) {
        case SortMode::NameAscending:
          std::sort(m_subcollections.begin(), m_subcollections.end(),
                    [collections](int a, int b) {
                      return QString::compare((*collections)[a].name,
                                              (*collections)[b].name,
                                              Qt::CaseInsensitive) < 0;
                    });
          break;
        case SortMode::NameDescending:
          std::sort(m_subcollections.begin(), m_subcollections.end(),
                    [collections](int a, int b) {
                      return QString::compare((*collections)[a].name,
                                              (*collections)[b].name,
                                              Qt::CaseInsensitive) > 0;
                    });
          break;
        case SortMode::Random: {
          std::random_device rd;
          std::mt19937 g(rd());
          std::shuffle(m_subcollections.begin(), m_subcollections.end(), g);
          break;
        }
      }
    }
  }
}

void ScrollDataManager::initializeVirtualFolders(const CollectionContext &context) {
  m_virtualFolders.clear();
  
  // Only show virtual folders if includeContentSubfolders is enabled
  // AND showAllSubfolderItems is false (otherwise items are flattened)
  if (!context.config.includeContentSubfolders || context.config.showAllSubfolderItems) {
    return;
  }
  
  // Determine the effective directory to scan
  QString scanDir = context.config.mediaDirectory;
  if (!context.config.currentSubfolder.isEmpty()) {
    scanDir = QDir(scanDir).absoluteFilePath(context.config.currentSubfolder);
  }
  
  QDir dir(scanDir);
  if (!dir.exists()) {
    return;
  }
  
  // Get list of subdirectories, optionally including hidden folders
  QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot;
  if (context.config.showHiddenFolders) {
    filters |= QDir::Hidden;
  }
  // Get unsorted list first, then apply sort mode
  QStringList subdirs = dir.entryList(filters, QDir::Unsorted);
  for (const QString &subdir : subdirs) {
    // Store path relative to mediaDirectory for navigation
    if (context.config.currentSubfolder.isEmpty()) {
      m_virtualFolders.append(subdir);
    } else {
      m_virtualFolders.append(context.config.currentSubfolder + "/" + subdir);
    }
  }
  
  // Sort virtual folders: A-Z when excluded from main sort, otherwise use sort mode
  if (m_virtualFolders.size() > 1) {
    if (context.excludeSubfoldersFromSort) {
      // Always sort A-Z when excluded from main sorting
      std::sort(m_virtualFolders.begin(), m_virtualFolders.end(),
                [](const QString &a, const QString &b) {
                  return QString::compare(QFileInfo(a).fileName(),
                                          QFileInfo(b).fileName(),
                                          Qt::CaseInsensitive) < 0;
                });
    } else {
      switch (context.sortMode) {
        case SortMode::NameAscending:
          std::sort(m_virtualFolders.begin(), m_virtualFolders.end(),
                    [](const QString &a, const QString &b) {
                      return QString::compare(QFileInfo(a).fileName(),
                                              QFileInfo(b).fileName(),
                                              Qt::CaseInsensitive) < 0;
                    });
          break;
        case SortMode::NameDescending:
          std::sort(m_virtualFolders.begin(), m_virtualFolders.end(),
                    [](const QString &a, const QString &b) {
                      return QString::compare(QFileInfo(a).fileName(),
                                              QFileInfo(b).fileName(),
                                              Qt::CaseInsensitive) > 0;
                    });
          break;
        case SortMode::Random: {
          std::random_device rd;
          std::mt19937 g(rd());
          std::shuffle(m_virtualFolders.begin(), m_virtualFolders.end(), g);
          break;
        }
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
      
    case SortMode::Random: {
      std::random_device rd;
      std::mt19937 g(rd());
      std::shuffle(m_unifiedItems.begin(), m_unifiedItems.end(), g);
      break;
    }
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

QList<int> ScrollDataManager::receiveItemsRange(
    int offset, const QStringList &paths,
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
