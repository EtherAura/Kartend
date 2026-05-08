// Worker thread callback methods split out from databasemanager.cpp.
#include <QSqlError>
#include <QSqlQuery>
#include <QtGlobal>

#include "databasemanager.h"
#include "errorutils.h"
#include "loggingcategories.h"
#include "querymanager.h"
#include "titlefilter.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcDatabaseManager)
#define debugLog(msg) qCDebug(lcDatabaseManager) << msg

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

void DatabaseManager::onWorkerItemsLoaded(const QStringList &filePaths,
                                          const QHash<QString, QString> &fileNames,
                                          const QHash<QString, QString> &fileToArtworkDir,
                                          const QHash<QString, QString> &fileToMediaDir,
                                          const QHash<QString, int> &fileToCollectionIndex) {
  // apply per-collection title-exclusion patterns on the main
  // thread, after the worker has produced the underscore-cleaned base names.
  // Doing it here (rather than inside QueryManager) keeps the worker free of
  // settings-dependent state and lets a single TitleFilter registry serve all
  // downstream consumers (alpha jump, search, sidebar, list view).
  QHash<QString, QString> filteredNames = fileNames;
  for (auto it = filteredNames.begin(); it != filteredNames.end(); ++it) {
    const int collectionIdx = fileToCollectionIndex.value(it.key(), -1);
    it.value() = TitleFilter::apply(collectionIdx, it.value());
  }

  QHash<QString, QString> relativeToFullPath;
  relativeToFullPath.reserve(filteredNames.size() * 2);

  // Build a fast lookup cache for resolveRelativeFilePath().
  // Keep "first seen" semantics to match the previous linear scan behavior
  // over fileNames (which returns the first match it encounters).
  for (auto it = filteredNames.constBegin(); it != filteredNames.constEnd(); ++it) {
    const QString &fullPath = it.key();
    if (fullPath.isEmpty()) {
      continue;
    }

    if (!relativeToFullPath.contains(fullPath)) {
      relativeToFullPath.insert(fullPath, fullPath);
    }

    const QString leafName = QFileInfo(fullPath).fileName();
    if (!leafName.isEmpty() && !relativeToFullPath.contains(leafName)) {
      relativeToFullPath.insert(leafName, fullPath);
    }

    const QString mediaDir = fileToMediaDir.value(fullPath);
    if (!mediaDir.trimmed().isEmpty()) {
      const QString relativePath = QDir(mediaDir).relativeFilePath(fullPath);
      if (!relativePath.isEmpty() && !relativeToFullPath.contains(relativePath)) {
        relativeToFullPath.insert(relativePath, fullPath);
      }
    }
  }

  {
    QMutexLocker locker(&m_dataMutex);
    m_fileToArtworkDir = fileToArtworkDir;
    m_fileToMediaDir = fileToMediaDir;
    m_fileToCollectionIndex = fileToCollectionIndex;
    m_relativeToFullPath = std::move(relativeToFullPath);
  }
  emit itemsLoaded(filePaths, filteredNames);
}

void DatabaseManager::onWorkerItemCountLoaded(int count) {
  qCDebug(lcSearchDiag) << "[DatabaseManager] onWorkerItemCountLoaded:" << count;
  emit itemCountLoaded(count);
}

void DatabaseManager::onWorkerItemCountLoadedWithToken(int count, int requestToken) {
  qCDebug(lcSearchDiag) << "[DatabaseManager] onWorkerItemCountLoadedWithToken:" << count
                        << "token=" << requestToken;
  emit itemCountLoadedWithToken(count, requestToken);

  // Keep legacy listeners working (e.g., MainWindow overlay suppression).
  emit itemCountLoaded(count);
}

void DatabaseManager::onWorkerItemsRangeLoaded(int offset, const QStringList &filePaths,
                                               const QHash<QString, QString> &fileNames,
                                               const QHash<QString, QString> &fileToArtworkDir,
                                               const QHash<QString, QString> &fileToMediaDir,
                                               const QHash<QString, int> &fileToCollectionIndex) {
  qCDebug(lcSearchDiag) << "[DatabaseManager] onWorkerItemsRangeLoaded: offset=" << offset
                        << "paths=" << filePaths.size();

  // mirror the main-loaded path — strip per-collection title
  // patterns before downstream consumers see the names.
  QHash<QString, QString> filteredNames = fileNames;
  for (auto it = filteredNames.begin(); it != filteredNames.end(); ++it) {
    const int collectionIdx = fileToCollectionIndex.value(it.key(), -1);
    it.value() = TitleFilter::apply(collectionIdx, it.value());
  }
  // Merge the directory and collection index mappings from range query into our
  // cache. This enables findArtworkDirectoryForFile() and
  // getCollectionIndexForFile() to work for range-loaded items.
  if (!fileToArtworkDir.isEmpty() || !fileToMediaDir.isEmpty() ||
      !fileToCollectionIndex.isEmpty()) {
    QMutexLocker locker(&m_dataMutex);
    m_fileToArtworkDir.insert(fileToArtworkDir);
    m_fileToMediaDir.insert(fileToMediaDir);
    m_fileToCollectionIndex.insert(fileToCollectionIndex);
  }

  emit itemsRangeLoaded(offset, filePaths, filteredNames, fileToArtworkDir, fileToMediaDir,
                        fileToCollectionIndex);
}

void DatabaseManager::cancelScan() {
  if (m_worker) {
    m_worker->requestCancelScan();
  }
  if (m_scanWorker) {
    m_scanWorker->requestCancelScan();
  }
}

// Count items in collection and descendants using uuid identity

// Get owning collection index for a file based on built maps

// Public wrapper to invalidate collection cache asynchronously on worker thread
