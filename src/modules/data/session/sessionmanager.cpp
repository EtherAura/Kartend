// Persists and restores selection state and item counts across application
// sessions.
#include "sessionmanager.h"
#include "collectionutils.h"
#include "errorutils.h"
#include "pathutils.h"
#include "uiconstants.h"

#include <algorithm>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcSessionManager, "kartend.sessionmanager")
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcSessionManager().isDebugEnabled()) {                                                     \
      qCDebug(lcSessionManager) << msg;                                                            \
    }                                                                                              \
  } while (0)

SessionManager::SessionManager(QObject *parent) : QObject(parent) {}

SessionManager::~SessionManager() = default;

auto SessionManager::snapshotSessionJsonBytesForShutdown() const -> QByteArray {
  QMutexLocker locker(&m_mutex);
  QJsonObject root = buildSessionJson();
  return QJsonDocument(root).toJson();
}

void SessionManager::saveSessionBytesToDiskForShutdown(const QByteArray &data) {
  const QString metadataPath = getCacheDirectory() + QStringLiteral("/metadata/session.json");
  if (!atomicWriteFile(metadataPath, data)) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                          "Failed to persist session metadata during shutdown",
                                          "SessionManager::saveSessionBytesToDiskForShutdown")
            .withDetails(QString("Path: %1").arg(metadataPath)));
  }
}

QString SessionManager::getCacheDirectory() {
  // Use GenericCacheLocation + app name for consistent ~/.cache/kartend path
  return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
         QStringLiteral("/kartend");
}

void SessionManager::initialize() {
  QString metadataPath = getCacheDirectory() + QStringLiteral("/metadata/session.json");
  QFile metadataFile(metadataPath);
  if (!metadataFile.exists()) {
    // Fallback to old cache.json if session.json doesn't exist
    metadataPath = getCacheDirectory() + QStringLiteral("/metadata/cache.json");
    metadataFile.setFileName(metadataPath);
    if (!metadataFile.exists()) {
      return;
    }
  }

  if (metadataFile.open(QIODevice::ReadOnly)) {
    QByteArray data = metadataFile.readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (doc.isNull() || !doc.isObject()) {
      qCWarning(lcSessionManager) << "Failed to parse session metadata at" << metadataPath
                                  << "- error:" << parseError.errorString()
                                  << "offset:" << parseError.offset
                                  << "(session state will reset)";
    } else {
      QJsonObject root = doc.object();
      readCollectionsData(root);
      readGlobalData(root);
      readCachedViewports(root);
    }
    metadataFile.close();
  }
}

void SessionManager::readCollectionsData(const QJsonObject &root) {
  if (!root.contains("collections")) {
    return;
  }
  QJsonObject collections = root["collections"].toObject();
  QMutexLocker locker(&m_mutex);

  for (auto it = collections.begin(); it != collections.end(); ++it) {
    QString name = it.key();
    QJsonObject collObj = it.value().toObject();

    if (collObj.contains("itemCount")) {
      collectionNameItemCountCache[name] = collObj["itemCount"].toVariant().toLongLong();
    }
    if (collObj.contains("itemRecursiveCount")) {
      collectionNameRecursiveCountCache[name] =
          collObj["itemRecursiveCount"].toVariant().toLongLong();
    }
    if (collObj.contains("lastSelected")) {
      QJsonObject lastSelObj = collObj["lastSelected"].toObject();
      LastSelectedInfo info;
      info.index = lastSelObj["index"].toInt(-1);
      info.title = lastSelObj["title"].toString();
      lastSelectedByName[name] = info;
    }
  }
}

void SessionManager::readGlobalData(const QJsonObject &root) {
  if (root.contains("global")) {
    QMutexLocker locker(&m_mutex);
    globalItemCount = root["global"].toVariant().toLongLong();
  }
}

// Builds JSON object from current session state
// Caller must hold m_mutex
auto SessionManager::buildSessionJson() const -> QJsonObject {
  QJsonObject root;
  QJsonObject collections;

  // Merge all keys from counts and lastSelected
  QSet<QString> allKeys;
  allKeys.unite(QSet<QString>(collectionNameItemCountCache.keyBegin(),
                              collectionNameItemCountCache.keyEnd()));
  allKeys.unite(QSet<QString>(collectionNameRecursiveCountCache.keyBegin(),
                              collectionNameRecursiveCountCache.keyEnd()));
  allKeys.unite(QSet<QString>(lastSelectedByName.keyBegin(), lastSelectedByName.keyEnd()));

  for (const QString &name : allKeys) {
    QJsonObject coll;
    if (collectionNameItemCountCache.contains(name)) {
      coll["itemCount"] = static_cast<double>(collectionNameItemCountCache[name]);
    }
    if (collectionNameRecursiveCountCache.contains(name)) {
      coll["itemRecursiveCount"] = static_cast<double>(collectionNameRecursiveCountCache[name]);
    }
    if (lastSelectedByName.contains(name)) {
      const LastSelectedInfo &info = lastSelectedByName[name];
      QJsonObject sel;
      sel["index"] = info.index;
      sel["title"] = info.title;
      coll["lastSelected"] = sel;
    }
    collections[name] = coll;
  }

  root["collections"] = collections;
  root["global"] = static_cast<double>(globalItemCount);

  // Serialize cached viewports for instant startup
  QJsonObject viewportsObj;
  for (auto it = cachedViewports.begin(); it != cachedViewports.end(); ++it) {
    const CachedViewport &vp = it.value();
    if (!vp.isValid()) continue;

    QJsonObject vpObj;
    vpObj["startIndex"] = vp.startIndex;
    vpObj["totalItems"] = vp.totalItems;

    QJsonArray pathsArray;
    for (const QString &path : vp.filePaths) {
      pathsArray.append(path);
    }
    vpObj["filePaths"] = pathsArray;

    QJsonObject namesObj;
    for (auto nameIt = vp.fileNames.begin(); nameIt != vp.fileNames.end(); ++nameIt) {
      namesObj[nameIt.key()] = nameIt.value();
    }
    vpObj["fileNames"] = namesObj;

    QJsonObject artworkObj;
    for (auto artIt = vp.artworkPaths.begin(); artIt != vp.artworkPaths.end(); ++artIt) {
      artworkObj[artIt.key()] = artIt.value();
    }
    vpObj["artworkPaths"] = artworkObj;

    viewportsObj[it.key()] = vpObj;
  }
  root["viewports"] = viewportsObj;

  return root;
}

// Atomically writes data to file using temp file + rename pattern
// This prevents data loss if the application crashes during write
auto SessionManager::atomicWriteFile(const QString &filePath, const QByteArray &data) -> bool {
  if (filePath.isEmpty()) {
    return false;
  }

  const QString parentDir = QFileInfo(filePath).absolutePath();
  if (!parentDir.isEmpty() && !QDir().mkpath(parentDir)) {
    return false;
  }

  QSaveFile file(filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }

  const qint64 written = file.write(data);
  if (written != data.size()) {
    file.cancelWriting();
    return false;
  }

  if (!file.commit()) {
    return false;
  }

  // fsync the parent directory so the rename is durable across crash/power loss
  PathUtils::syncDirectory(parentDir);
  return true;
}

void SessionManager::saveToDisk() {
  if (QApplication::closingDown()) {
    return;
  }

  QMutexLocker locker(&m_mutex);
  QJsonObject root = buildSessionJson();
  locker.unlock();

  QString metadataPath = getCacheDirectory() + QStringLiteral("/metadata/session.json");
  if (!atomicWriteFile(metadataPath, QJsonDocument(root).toJson())) {
    ErrorUtils::logError(ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                                           "Failed to persist session metadata",
                                                           "SessionManager::saveToDisk")
                             .withDetails(QString("Path: %1").arg(metadataPath)));
  }
}

// Saves session data to disk during shutdown - skips QApplication::closingDown
// check since we're intentionally saving during app close
void SessionManager::saveToDiskForShutdown() {
  QMutexLocker locker(&m_mutex);
  QJsonObject root = buildSessionJson();
  locker.unlock();

  QString metadataPath = getCacheDirectory() + QStringLiteral("/metadata/session.json");
  if (!atomicWriteFile(metadataPath, QJsonDocument(root).toJson())) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                          "Failed to persist session metadata during shutdown",
                                          "SessionManager::saveToDiskForShutdown")
            .withDetails(QString("Path: %1").arg(metadataPath)));
  }
}

void SessionManager::setLastSelected(const QString &collectionName, int index,
                                     const QString &title) {
  QMutexLocker locker(&m_mutex);

  QString key = collectionName;
  const QString suffix = "/" + collectionName;

  auto longestMatchFrom = [&](const QHash<QString, qint64> &map) -> QString {
    QString best;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
      const QString &keyName = it.key();
      if (keyName.endsWith(suffix)) {
        if (best.isEmpty() || keyName.length() > best.length()) {
          best = keyName;
        }
      }
    }
    return best;
  };

  QString candidate = longestMatchFrom(collectionNameItemCountCache);
  if (candidate.isEmpty()) {
    candidate = longestMatchFrom(collectionNameRecursiveCountCache);
  }
  if (candidate.isEmpty()) {
    for (auto it = lastSelectedByName.constBegin(); it != lastSelectedByName.constEnd(); ++it) {
      const QString &keyName = it.key();
      if (keyName.endsWith(suffix)) {
        if (candidate.isEmpty() || keyName.length() > candidate.length()) {
          candidate = keyName;
        }
      }
    }
  }
  if (!candidate.isEmpty()) {
    key = candidate;
  }

  LastSelectedInfo info{.index = index, .title = title};
  lastSelectedByName.insert(key, info);

  if (key != collectionName) {
    lastSelectedByName.remove(collectionName);
  }
}

int SessionManager::getLastSelectedIndex(const QString &collectionName) const {
  QMutexLocker locker(&m_mutex);
  if (lastSelectedByName.contains(collectionName)) {
    return lastSelectedByName[collectionName].index;
  }
  // Try suffix match
  const QString suffix = "/" + collectionName;
  for (auto it = lastSelectedByName.constBegin(); it != lastSelectedByName.constEnd(); ++it) {
    if (it.key().endsWith(suffix)) {
      return it.value().index;
    }
  }
  return -1;
}

void SessionManager::setGlobalItemCount(qint64 count) {
  QMutexLocker locker(&m_mutex);
  globalItemCount = count;
}

void SessionManager::setCollectionCounts(const CollectionConfig &collection,
                                         const QList<CollectionConfig> &allCollections,
                                         qint64 itemCount, qint64 recursiveCount) {
  QString hierarchicalName = CollectionUtils::hierarchicalNameFor(collection, allCollections);
  QMutexLocker locker(&m_mutex);
  collectionNameRecursiveCountCache[hierarchicalName] = recursiveCount;
  collectionNameItemCountCache[hierarchicalName] = itemCount;
}

bool SessionManager::getCollectionCounts(const CollectionConfig &collection,
                                         const QList<CollectionConfig> &allCollections,
                                         qint64 &itemCount, qint64 &recursiveCount) const {
  const QString key = CollectionUtils::hierarchicalNameFor(collection, allCollections);
  QMutexLocker locker(&m_mutex);
  const bool hasDirect = collectionNameItemCountCache.contains(key);
  const bool hasRec = collectionNameRecursiveCountCache.contains(key);
  if (!hasDirect && !hasRec) {
    return false;
  }
  if (hasDirect) {
    itemCount = collectionNameItemCountCache.value(key);
  } else {
    itemCount = -1;
  }
  if (hasRec) {
    recursiveCount = collectionNameRecursiveCountCache.value(key);
  } else {
    recursiveCount = (itemCount >= 0) ? itemCount : -1;
  }
  return true;
}

void SessionManager::clearStaleCollections(const QList<CollectionConfig> &currentCollections) {
  QMutexLocker locker(&m_mutex);

  QSet<QString> validHierNames;
  for (const CollectionConfig &config : currentCollections) {
    QString hierarchicalName = CollectionUtils::hierarchicalNameFor(config, currentCollections);
    validHierNames.insert(hierarchicalName);
  }

  QStringList keysToRemove;
  for (auto countsIt = collectionNameRecursiveCountCache.begin();
       countsIt != collectionNameRecursiveCountCache.end(); ++countsIt) {
    if (!validHierNames.contains(countsIt.key())) {
      keysToRemove.append(countsIt.key());
    }
  }
  for (const QString &key : keysToRemove) {
    collectionNameRecursiveCountCache.remove(key);
    collectionNameItemCountCache.remove(key);
  }

  QStringList selToRemove;
  for (auto it = lastSelectedByName.begin(); it != lastSelectedByName.end(); ++it) {
    const QString &key = it.key();
    if (validHierNames.contains(key)) {
      continue;
    }
    if (!key.contains('/')) {
      const QString suffix = "/" + key;
      bool hasHier = std::ranges::any_of(validHierNames, [&suffix](const QString &hierarchy) {
        return hierarchy.endsWith(suffix);
      });
      if (hasHier) {
        selToRemove.append(key);
      }
    }
  }
  for (const QString &nameKey : selToRemove) {
    lastSelectedByName.remove(nameKey);
  }
}

void SessionManager::readCachedViewports(const QJsonObject &root) {
  if (!root.contains("viewports")) {
    return;
  }
  QJsonObject viewportsObj = root["viewports"].toObject();
  QMutexLocker locker(&m_mutex);

  for (auto it = viewportsObj.begin(); it != viewportsObj.end(); ++it) {
    QString key = it.key();
    QJsonObject vpObj = it.value().toObject();

    CachedViewport vp;
    vp.startIndex = vpObj["startIndex"].toInt(0);
    vp.totalItems = vpObj["totalItems"].toInt(0);

    QJsonArray pathsArray = vpObj["filePaths"].toArray();
    vp.filePaths.reserve(pathsArray.size());
    for (const QJsonValue &val : pathsArray) {
      vp.filePaths.append(val.toString());
    }

    QJsonObject namesObj = vpObj["fileNames"].toObject();
    for (auto nameIt = namesObj.begin(); nameIt != namesObj.end(); ++nameIt) {
      vp.fileNames[nameIt.key()] = nameIt.value().toString();
    }

    QJsonObject artworkObj = vpObj["artworkPaths"].toObject();
    for (auto artIt = artworkObj.begin(); artIt != artworkObj.end(); ++artIt) {
      vp.artworkPaths[artIt.key()] = artIt.value().toString();
    }

    if (vp.isValid()) {
      cachedViewports[key] = vp;
    }
  }
}

void SessionManager::setCachedViewport(const QString &collectionKey, int startIndex, int totalItems,
                                       const QStringList &filePaths,
                                       const QHash<QString, QString> &fileNames,
                                       const QHash<QString, QString> &artworkPaths) {
  QMutexLocker locker(&m_mutex);
  CachedViewport vp;
  vp.startIndex = startIndex;
  vp.totalItems = totalItems;
  vp.filePaths = filePaths;
  vp.fileNames = fileNames;
  vp.artworkPaths = artworkPaths;
  cachedViewports[collectionKey] = vp;
}

SessionManager::CachedViewport
SessionManager::getCachedViewport(const QString &collectionKey) const {
  QMutexLocker locker(&m_mutex);
  return cachedViewports.value(collectionKey);
}
