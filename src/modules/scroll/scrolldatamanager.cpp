#include "scrolldatamanager.h"

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
  QStringList subdirs = dir.entryList(filters, QDir::Name);
  for (const QString &subdir : subdirs) {
    // Store path relative to mediaDirectory for navigation
    if (context.config.currentSubfolder.isEmpty()) {
      m_virtualFolders.append(subdir);
    } else {
      m_virtualFolders.append(context.config.currentSubfolder + "/" + subdir);
    }
  }
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
