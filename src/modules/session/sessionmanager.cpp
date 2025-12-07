// Persists and restores selection state and item counts across application sessions.
#include "sessionmanager.h"
#include "collectionutils.h"
#include "uiconstants.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>

#ifdef KARTEND_DEBUG_LOGGING
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcSessionManager, "kartend.sessionmanager")
#define debugLog(msg) qCDebug(lcSessionManager) << msg
#else
#define debugLog(msg) do {} while(0)
#endif

SessionManager::SessionManager(QObject *parent) : QObject(parent) {}

SessionManager::~SessionManager() = default;

QString SessionManager::getCacheDirectory() {
  return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

void SessionManager::initialize() {
  QString metadataPath = getCacheDirectory() + "/metadata/session.json";
  QFile metadataFile(metadataPath);
  if (!metadataFile.exists()) {
    // Fallback to old cache.json if session.json doesn't exist
    metadataPath = getCacheDirectory() + "/metadata/cache.json";
    metadataFile.setFileName(metadataPath);
    if (!metadataFile.exists()) {
        return;
    }
  }

  if (metadataFile.open(QIODevice::ReadOnly)) {
    QByteArray data = metadataFile.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isNull() && doc.isObject()) {
      QJsonObject root = doc.object();
      readCollectionsData(root);
      readGlobalData(root);
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
      collectionNameItemCountCache[name] =
          collObj["itemCount"].toVariant().toLongLong();
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

void SessionManager::saveToDisk() {
  if (QApplication::closingDown()) {
    return;
  }

  QJsonObject root;
  QJsonObject collections;

  QMutexLocker locker(&m_mutex);
  
  // Merge all keys from counts and lastSelected
  QSet<QString> allKeys;
  allKeys.unite(QSet<QString>(collectionNameItemCountCache.keyBegin(), collectionNameItemCountCache.keyEnd()));
  allKeys.unite(QSet<QString>(collectionNameRecursiveCountCache.keyBegin(), collectionNameRecursiveCountCache.keyEnd()));
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
  locker.unlock();

  QString metadataPath = getCacheDirectory() + "/metadata/session.json";
  QDir().mkpath(QFileInfo(metadataPath).absolutePath());
  QFile metadataFile(metadataPath);
  if (metadataFile.open(QIODevice::WriteOnly)) {
    metadataFile.write(QJsonDocument(root).toJson());
    metadataFile.close();
  }
}

// Saves session data to disk during shutdown - skips QApplication::closingDown
// check since we're intentionally saving during app close
void SessionManager::saveToDiskForShutdown() {
  QJsonObject root;
  QJsonObject collections;

  QMutexLocker locker(&m_mutex);
  
  // Merge all keys from counts and lastSelected
  QSet<QString> allKeys;
  allKeys.unite(QSet<QString>(collectionNameItemCountCache.keyBegin(), collectionNameItemCountCache.keyEnd()));
  allKeys.unite(QSet<QString>(collectionNameRecursiveCountCache.keyBegin(), collectionNameRecursiveCountCache.keyEnd()));
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
  locker.unlock();

  QString metadataPath = getCacheDirectory() + "/metadata/session.json";
  QDir().mkpath(QFileInfo(metadataPath).absolutePath());
  QFile metadataFile(metadataPath);
  if (metadataFile.open(QIODevice::WriteOnly)) {
    metadataFile.write(QJsonDocument(root).toJson());
    metadataFile.close();
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
    for (auto it = lastSelectedByName.constBegin();
         it != lastSelectedByName.constEnd(); ++it) {
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
  for (auto it = lastSelectedByName.constBegin();
       it != lastSelectedByName.constEnd(); ++it) {
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

void SessionManager::setCollectionCounts(
    const CollectionConfig &collection,
    const QList<CollectionConfig> &allCollections, qint64 itemCount,
    qint64 recursiveCount) {
  QString hierarchicalName = CollectionUtils::hierarchicalNameFor(collection, allCollections);
  QMutexLocker locker(&m_mutex);
  collectionNameRecursiveCountCache[hierarchicalName] = recursiveCount;
  collectionNameItemCountCache[hierarchicalName] = itemCount;
}

bool SessionManager::getCollectionCounts(
    const CollectionConfig &collection,
    const QList<CollectionConfig> &allCollections, qint64 &itemCount,
    qint64 &recursiveCount) const {
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
  return hasDirect;
}

void SessionManager::clearStaleCollections(
    const QList<CollectionConfig> &currentCollections) {
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
  for (auto it = lastSelectedByName.begin(); it != lastSelectedByName.end();
       ++it) {
    const QString &key = it.key();
    if (validHierNames.contains(key)) {
      continue;
    }
    if (!key.contains('/')) {
      const QString suffix = "/" + key;
      bool hasHier = std::ranges::any_of(validHierNames,
                                         [&suffix](const QString &hierarchy) {
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
