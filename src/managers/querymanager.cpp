#include "querymanager.h"
#include "pathutils.h"
#include "sessionmanager.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <stdexcept>
#include <QDebug>



// Forward declarations of static helpers
static auto canonicalKeyPath(const QString &absPath, bool dedup) -> QString;
static auto displayNameForBase(const QString &baseName) -> QString;

QueryManager::QueryManager(SessionManager *sessionManager, QObject *parent)
    : QObject(parent), m_sessionManager(sessionManager) {
  m_connectionName = "kartend_worker";
}

QueryManager::~QueryManager() {
  if (m_db.isValid()) {
    QString connectionName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
  }
}

void QueryManager::initDatabase() {
  if (QSqlDatabase::contains(m_connectionName)) {
    m_db = QSqlDatabase::database(m_connectionName);
  } else {
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
  }

  QString dbPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dbPath);
  m_db.setDatabaseName(dbPath + "/media.db");

  if (!m_db.open()) {
    qCritical() << "Database error:" << m_db.lastError().text();
    return;
  }

  QSqlQuery query(m_db);
  if (!query.exec("PRAGMA foreign_keys = ON")) {
    qWarning() << "Failed to enable foreign keys:" << query.lastError();
  }
  if (!query.exec("PRAGMA journal_mode = WAL")) {
    qWarning() << "Failed to enable WAL mode:" << query.lastError();
  }
  // Tables are created by the main DatabaseManager or assumed to exist.
  // But for safety, we can ensure they exist here too, or rely on main thread init.
  // Since main thread runs initDatabase first, we should be fine.
}

void QueryManager::loadAllCollections(const QList<CollectionConfig> &allCollections) {
  if (!m_db.isOpen()) initDatabase();

  QStringList allFilePaths;
  QHash<QString, QString> allFileNames;
  QHash<QString, QString> fileToArtworkDir;
  QHash<QString, QString> fileToMediaDir;
  QHash<QString, int> fileToCollectionIndex;

  for (int collectionIndex = 0; collectionIndex < allCollections.size();
       ++collectionIndex) {
    CollectionConfig collection = allCollections[collectionIndex];
    collection.mediaDirectory = PathUtils::validateAndExpandPath(
        collection.mediaDirectory, collection.name);
    collection.artworkDirectory = PathUtils::validateAndExpandPath(
        collection.artworkDirectory, collection.name);

    if (collection.mediaDirectory.trimmed().isEmpty()) {
      continue;
    }

    QHash<QString, QDateTime> timestamps;
    QStringList filePaths =
        loadOrScanCollection(collectionIndex, collection, timestamps);

    appendFileMapsAndListCanonical(
        collectionIndex, collection,
        allCollections[collectionIndex].artworkDirectory, filePaths,
        allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir,
        fileToCollectionIndex, false);
  }

  sortFiles(allFilePaths);

  emit itemsLoaded(allFilePaths, allFileNames, fileToArtworkDir, fileToCollectionIndex);
}

void QueryManager::loadItems(const CollectionContext &context) {
  if (!m_db.isOpen()) initDatabase();

  if (!context.isValid()) {
    emit errorOccurred("Invalid collection context");
    return;
  }

  CollectionContext ctx = context;
  ctx.config.mediaDirectory = PathUtils::validateAndExpandPath(
      ctx.config.mediaDirectory, ctx.config.name);
  ctx.config.artworkDirectory = PathUtils::validateAndExpandPath(
      ctx.config.artworkDirectory, ctx.config.name);

  if (ctx.config.mediaDirectory.trimmed().isEmpty()) {
    emit itemsLoaded(QStringList(), QHash<QString, QString>(), QHash<QString, QString>(), QHash<QString, int>());
    return;
  }

  QHash<QString, QDateTime> timestamps;
  QStringList filePaths =
      loadOrScanCollection(ctx.currentIndex, ctx.config, timestamps);

  QStringList allFilePaths;
  QHash<QString, QString> allFileNames;
  QHash<QString, QString> fileToArtworkDir;
  QHash<QString, QString> fileToMediaDir;
  QHash<QString, int> fileToCollectionIndex;

  appendFileMapsAndListCanonical(ctx.currentIndex, ctx.config,
                                 ctx.config.artworkDirectory, filePaths,
                                 allFilePaths, allFileNames, fileToArtworkDir,
                                 fileToMediaDir, fileToCollectionIndex, false);

  sortFiles(allFilePaths);

  emit itemsLoaded(allFilePaths, allFileNames, fileToArtworkDir, fileToCollectionIndex);
}

void QueryManager::loadItemsWithSubcollections(const CollectionContext &context,
                                                 const QList<CollectionConfig> &allCollections) {
  if (!m_db.isOpen()) initDatabase();

  if (!context.isValid()) {
    emit errorOccurred("Invalid collection context");
    return;
  }

  CollectionContext mainCtx = context;
  mainCtx.config.mediaDirectory = PathUtils::validateAndExpandPath(
      mainCtx.config.mediaDirectory, mainCtx.config.name);
  mainCtx.config.artworkDirectory = PathUtils::validateAndExpandPath(
      mainCtx.config.artworkDirectory, mainCtx.config.name);

  QStringList allFilePaths;
  QHash<QString, QString> allFileNames;
  QHash<QString, QString> fileToArtworkDir;
  QHash<QString, QString> fileToMediaDir;
  QHash<QString, int> fileToCollectionIndex;

  bool hasMainMediaDirectory =
      !mainCtx.config.mediaDirectory.trimmed().isEmpty();
  if (hasMainMediaDirectory) {
    QHash<QString, QDateTime> timestamps;
    QStringList mainFilePaths =
        loadOrScanCollection(mainCtx.currentIndex, mainCtx.config, timestamps);

    appendFileMapsAndListCanonical(
        mainCtx.currentIndex, mainCtx.config, mainCtx.config.artworkDirectory,
        mainFilePaths, allFilePaths, allFileNames, fileToArtworkDir,
        fileToMediaDir, fileToCollectionIndex, true);
  }

  QList<int> rawDescendants =
      collectDescendantIndices(mainCtx.currentIndex, allCollections);
  QSet<int> seenDesc;
  QList<int> descendants;
  descendants.reserve(rawDescendants.size());
  for (int descendantIndex : rawDescendants) {
    if (descendantIndex == mainCtx.currentIndex) {
      continue;
    }
    if (descendantIndex < 0 || descendantIndex >= allCollections.size()) {
      continue;
    }
    if (!seenDesc.contains(descendantIndex)) {
      seenDesc.insert(descendantIndex);
      descendants.append(descendantIndex);
    }
  }

  for (int collectionIndex : descendants) {
    CollectionConfig collection = allCollections[collectionIndex];
    collection.mediaDirectory = PathUtils::validateAndExpandPath(
        collection.mediaDirectory, collection.name);
    collection.artworkDirectory = PathUtils::validateAndExpandPath(
        collection.artworkDirectory, collection.name);

    if (collection.mediaDirectory.trimmed().isEmpty()) {
      continue;
    }

    QHash<QString, QDateTime> subTimestamps;
    QStringList subFilePaths =
        loadOrScanCollection(collectionIndex, collection, subTimestamps);

    appendFileMapsAndListCanonical(
        collectionIndex, collection,
        allCollections[collectionIndex].artworkDirectory, subFilePaths,
        allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir,
        fileToCollectionIndex, true);
  }

  {
    QSet<QString> seen;
    QStringList unique;
    unique.reserve(allFilePaths.size());
    for (const QString &path : allFilePaths) {
      if (!seen.contains(path)) {
        seen.insert(path);
        unique.append(path);
      }
    }
    allFilePaths.swap(unique);
  }

  sortFiles(allFilePaths);
  emit itemsLoaded(allFilePaths, allFileNames, fileToArtworkDir, fileToCollectionIndex);
}

void QueryManager::updateCachedCounts(const QList<CollectionConfig> &allCollections) {
  if (!m_db.isOpen()) initDatabase();

  // Note: SessionManager usage here was removed because it's not thread-safe.
  // The logic for updating cached counts should be handled in the main thread
  // (e.g., in DatabaseManager) or SessionManager should be made thread-safe.
  // Currently, DatabaseManager::updateCachedCounts handles the SessionManager updates.
}

void QueryManager::ensureCollectionScanned(int collectionIndex, const CollectionConfig &collection) {
  if (collection.mediaDirectory.trimmed().isEmpty()) return;
  if (needsRescan(collectionIndex, collection)) {
    QHash<QString, QDateTime> timestamps;
    QStringList filePaths = scanMediaDirectory(collection, timestamps);
    if (!filePaths.isEmpty()) {
      saveItemsToDatabase(collectionIndex, filePaths, timestamps, collection);
    }
  }
}

void QueryManager::fetchItemCount(const CollectionContext &context, const QList<CollectionConfig> &allCollections, const QString &filter) {
  if (!m_db.isOpen()) initDatabase();

  if (!context.isValid()) {
    emit errorOccurred("Invalid collection context");
    return;
  }

  CollectionContext ctx = context;
  ctx.config.mediaDirectory = PathUtils::validateAndExpandPath(
      ctx.config.mediaDirectory, ctx.config.name);
  
  ensureCollectionScanned(ctx.currentIndex, ctx.config);

  QStringList uuids;
  uuids << computeCollectionUuid(ctx.config.name, ctx.config.mediaDirectory);

  if (ctx.config.showAllSubcollectionItems) {
      QList<int> rawDescendants = collectDescendantIndices(ctx.currentIndex, allCollections);
      for (int descendantIndex : rawDescendants) {
          if (descendantIndex == ctx.currentIndex || descendantIndex < 0 || descendantIndex >= allCollections.size()) continue;
          CollectionConfig subCol = allCollections[descendantIndex];
          subCol.mediaDirectory = PathUtils::validateAndExpandPath(subCol.mediaDirectory, subCol.name);
          ensureCollectionScanned(descendantIndex, subCol);
          uuids << computeCollectionUuid(subCol.name, subCol.mediaDirectory);
      }
  }

  QSqlQuery query(m_db);
  QString sql = "SELECT COUNT(DISTINCT path) FROM items WHERE collection_uuid IN (";
  for (int i = 0; i < uuids.size(); ++i) {
      sql += (i == 0 ? "?" : ", ?");
  }
  sql += ")";
  
  if (!filter.isEmpty()) {
      sql += " AND name LIKE ?";
  }
  
  query.prepare(sql);
  for (const QString &uuid : uuids) {
      query.addBindValue(uuid);
  }
  if (!filter.isEmpty()) {
      query.addBindValue("%" + filter + "%");
  }

  if (query.exec() && query.next()) {
      emit itemCountLoaded(query.value(0).toInt());
  } else {
      emit itemCountLoaded(0);
  }
}

void QueryManager::fetchItemsRange(const CollectionContext &context, const QList<CollectionConfig> &allCollections, int offset, int limit, const QString &filter) {
  if (!m_db.isOpen()) initDatabase();

  if (!context.isValid()) {
    emit errorOccurred("Invalid collection context");
    return;
  }

  // We assume ensureCollectionScanned was called during fetchItemCount, 
  // but for safety we could call it again or assume the DB is up to date.
  // To avoid double scanning, we'll assume fetchItemCount is called first or the user accepts slight staleness until next scan.
  // But strictly speaking, we should ensure correctness.
  // For performance, let's assume fetchItemCount handled the scanning.

  QStringList uuids;
  CollectionContext ctx = context;
  ctx.config.mediaDirectory = PathUtils::validateAndExpandPath(ctx.config.mediaDirectory, ctx.config.name);
  uuids << computeCollectionUuid(ctx.config.name, ctx.config.mediaDirectory);

  if (ctx.config.showAllSubcollectionItems) {
      QList<int> rawDescendants = collectDescendantIndices(ctx.currentIndex, allCollections);
      for (int descendantIndex : rawDescendants) {
          if (descendantIndex == ctx.currentIndex || descendantIndex < 0 || descendantIndex >= allCollections.size()) continue;
          CollectionConfig subCol = allCollections[descendantIndex];
          subCol.mediaDirectory = PathUtils::validateAndExpandPath(subCol.mediaDirectory, subCol.name);
          uuids << computeCollectionUuid(subCol.name, subCol.mediaDirectory);
      }
  }

  QSqlQuery query(m_db);
  QString sql = "SELECT DISTINCT path, collection_uuid FROM items WHERE collection_uuid IN (";
  for (int i = 0; i < uuids.size(); ++i) {
      sql += (i == 0 ? "?" : ", ?");
  }
  sql += ")";
  
  if (!filter.isEmpty()) {
      sql += " AND name LIKE ?";
  }
  
  sql += " ORDER BY name COLLATE NOCASE LIMIT ? OFFSET ?";
  
  query.prepare(sql);
  for (const QString &uuid : uuids) {
      query.addBindValue(uuid);
  }
  if (!filter.isEmpty()) {
      query.addBindValue("%" + filter + "%");
  }
  query.addBindValue(limit);
  query.addBindValue(offset);

  QStringList filePaths;
  QHash<QString, QString> fileNames;
  
  // We need to reconstruct full paths because DB stores relative paths (mostly) or absolute?
  // The DB stores "path" which is relative to collection media directory.
  // So we need to map back to absolute paths if needed, or just return what we have.
  // The original loadItems logic does a lot of mapping (appendFileMapsAndListCanonical).
  // We need to replicate that logic but for a subset.
  
  // To do this efficiently, we need a map of UUID -> CollectionConfig/MediaDir
  QHash<QString, QString> uuidToMediaDir;
  QHash<QString, QString> uuidToArtworkDir;
  
  // Populate maps
  auto addMap = [&](const CollectionConfig &c) {
      QString u = computeCollectionUuid(c.name, c.mediaDirectory);
      uuidToMediaDir[u] = c.mediaDirectory;
      uuidToArtworkDir[u] = c.artworkDirectory;
  };
  
  addMap(ctx.config);
  if (ctx.config.showAllSubcollectionItems) {
      QList<int> rawDescendants = collectDescendantIndices(ctx.currentIndex, allCollections);
      for (int descendantIndex : rawDescendants) {
          if (descendantIndex == ctx.currentIndex || descendantIndex < 0 || descendantIndex >= allCollections.size()) continue;
          CollectionConfig subCol = allCollections[descendantIndex];
          subCol.mediaDirectory = PathUtils::validateAndExpandPath(subCol.mediaDirectory, subCol.name);
          subCol.artworkDirectory = PathUtils::validateAndExpandPath(subCol.artworkDirectory, subCol.name);
          addMap(subCol);
      }
  }

  if (query.exec()) {
      while (query.next()) {
          QString relPath = query.value(0).toString();
          QString uuid = query.value(1).toString();
          QString mediaDir = uuidToMediaDir.value(uuid);
          
          QString fullPath;
          if (QDir::isAbsolutePath(relPath)) {
              fullPath = relPath;
          } else {
              fullPath = QDir(mediaDir).absoluteFilePath(relPath);
          }
          
          // Canonicalize if needed? The original code does canonicalKeyPath.
          // For performance, maybe skip canonicalization if not strictly needed, or do it.
          // Let's do it to be consistent.
          QString keyPath = canonicalKeyPath(fullPath, false); // dedup=false for now
          
          filePaths.append(keyPath);
          fileNames[keyPath] = displayNameForBase(QFileInfo(keyPath).completeBaseName());
      }
  } else {
      qWarning() << "Fetch items range failed:" << query.lastError().text();
  }

  emit itemsRangeLoaded(offset, filePaths, fileNames);
}

// ... Helper implementations ...

bool QueryManager::needsRescan(int collectionIndex, const CollectionConfig &collection) {
  Q_UNUSED(collectionIndex)

  if (collection.mediaDirectory.trimmed().isEmpty()) {
    if (m_db.isOpen()) {
      const QString uuid = computeCollectionUuid(collection.name, collection.mediaDirectory);
      clearCollectionFromDatabaseByUuid(uuid);
    }
    return false;
  }

  QString currentSignature = collection.extensions.isEmpty()
                                 ? QString()
                                 : collection.extensions.join('|');
  const QString uuid = computeCollectionUuid(collection.name, collection.mediaDirectory);

  QSqlQuery query(m_db);
  query.prepare("SELECT last_scanned, name, ext_signature FROM collections "
                "WHERE uuid = ?");
  query.addBindValue(uuid);

  bool rowPresent = query.exec() && query.next();
  if (!rowPresent) {
    return true;
  }

  QString storedName = query.value(1).toString();
  QString storedSignature = query.value(2).toString();

  if (storedName != collection.name) {
    clearCollectionFromDatabaseByUuid(uuid);
    return true;
  }

  if (storedSignature != currentSignature) {
    clearCollectionFromDatabaseByUuid(uuid);
    return true;
  }

  QSqlQuery pathQuery(m_db);
  pathQuery.prepare("SELECT path FROM items WHERE collection_uuid = ? LIMIT 1");
  pathQuery.addBindValue(uuid);

  if (pathQuery.exec() && pathQuery.next()) {
    QString storedPath = pathQuery.value(0).toString();
    QString storedFullPath =
        QDir(collection.mediaDirectory).absoluteFilePath(storedPath);
    if (!QFile::exists(storedFullPath)) {
      clearCollectionFromDatabaseByUuid(uuid);
      return true;
    }
  }

  QDateTime lastScanned =
      QDateTime::fromString(query.value(0).toString(), Qt::ISODate);
  QFileInfo dirInfo(collection.mediaDirectory);

  if (!dirInfo.exists() || dirInfo.lastModified() > lastScanned) {
    return true;
  }

  QSqlQuery newer(m_db);
  newer.prepare("SELECT COUNT(*) FROM items WHERE collection_uuid = ? AND "
                "last_modified > ?");
  newer.addBindValue(uuid);
  newer.addBindValue(lastScanned.toString(Qt::ISODate));
  newer.exec();

  return newer.next() && newer.value(0).toInt() > 0;
}

QStringList QueryManager::scanMediaDirectory(const CollectionConfig &collection,
                                         QHash<QString, QDateTime> &timestamps) {
  QStringList filePaths;
  QDir dir(collection.mediaDirectory);

  if (!dir.exists()) {
    return filePaths;
  }

  QStringList nameFilters;
  if (!collection.extensions.isEmpty()) {
    for (const QString &ext : collection.extensions) {
      nameFilters << "*." + ext;
    }
  }

  QDirIterator iterator(dir.absolutePath(), nameFilters, QDir::Files,
                        QDirIterator::NoIteratorFlags);
  while (iterator.hasNext()) {
    iterator.next();
    QString relativePath = dir.relativeFilePath(iterator.filePath());
    filePaths.append(relativePath);
    timestamps[relativePath] = QFileInfo(iterator.filePath()).lastModified();
  }

  return filePaths;
}

QStringList QueryManager::loadOrScanCollection(
    int collectionIndex, const CollectionConfig &collection,
    QHash<QString, QDateTime> &timestamps) {
  QStringList filePaths;
  if (collection.mediaDirectory.trimmed().isEmpty()) {
    return filePaths;
  }

  if (needsRescan(collectionIndex, collection)) {
    filePaths = scanMediaDirectory(collection, timestamps);
    if (!filePaths.isEmpty()) {
      saveItemsToDatabase(collectionIndex, filePaths, timestamps, collection);
    }
  } else {
    const QString uuid = computeCollectionUuid(collection.name, collection.mediaDirectory);
    filePaths = loadItemsFromDatabaseByUuid(uuid);
  }

  return filePaths;
}

void QueryManager::saveItemsToDatabase(
    int collectionIndex, const QStringList &filePaths,
    const QHash<QString, QDateTime> &timestamps,
    const CollectionConfig &collection) {
  Q_UNUSED(collectionIndex)

  if (!m_db.isOpen()) {
    qWarning() << "Database is not open";
    return;
  }

  QString extSignature = collection.extensions.isEmpty()
                             ? QString()
                             : collection.extensions.join('|');
  const QString uuid = computeCollectionUuid(collection.name, collection.mediaDirectory);

  m_db.transaction();

  try {
    QSqlQuery update(m_db);
    update.prepare("UPDATE collections SET name=?, last_scanned=?, "
                   "ext_signature=? WHERE uuid=?");
    update.addBindValue(collection.name);
    update.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    update.addBindValue(extSignature);
    update.addBindValue(uuid);
    update.exec();

    QSqlQuery check(m_db);
    check.prepare("SELECT COUNT(*) FROM collections WHERE uuid=?");
    check.addBindValue(uuid);
    bool exists = (check.exec() && check.next() && check.value(0).toInt() > 0);

    if (!exists) {
      QSqlQuery insert(m_db);
      insert.prepare("INSERT INTO collections (name, last_scanned, "
                     "ext_signature, uuid) VALUES (?, ?, ?, ?)");
      insert.addBindValue(collection.name);
      insert.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
      insert.addBindValue(extSignature);
      insert.addBindValue(uuid);
      if (!insert.exec()) {
        throw std::runtime_error(insert.lastError().text().toStdString());
      }
    }

    QSqlQuery del(m_db);
    del.prepare("DELETE FROM items WHERE collection_uuid = ?");
    del.addBindValue(uuid);
    if (!del.exec()) {
      throw std::runtime_error(del.lastError().text().toStdString());
    }

    QSqlQuery ins(m_db);
    ins.prepare("INSERT OR IGNORE INTO items (collection_id, collection_uuid, "
                "path, name, last_modified) VALUES (?, ?, ?, ?, ?)");

    int legacyId = -1;
    {
      QSqlQuery idq(m_db);
      idq.prepare("SELECT id FROM collections WHERE uuid = ?");
      idq.addBindValue(uuid);
      if (idq.exec() && idq.next()) {
        legacyId = idq.value(0).toInt();
      }
    }

    for (const QString &filePath : filePaths) {
      ins.addBindValue(legacyId);
      ins.addBindValue(uuid);
      ins.addBindValue(filePath);
      ins.addBindValue(QFileInfo(filePath).completeBaseName());
      ins.addBindValue(timestamps.value(filePath).toString(Qt::ISODate));

      if (!ins.exec()) {
        qWarning() << "Failed to insert item:" << filePath
                   << "Error:" << ins.lastError().text();
      }
    }

    m_db.commit();
  } catch (const std::exception &e) {
    m_db.rollback();
    qCritical() << "Database transaction failed:" << e.what();
  }
}

QStringList QueryManager::loadItemsFromDatabaseByUuid(const QString &collectionUuid) {
  QStringList filePaths;

  if (!m_db.isOpen()) {
    qWarning() << "Database is not open, cannot load items";
    return filePaths;
  }

  QSqlQuery query(m_db);
  query.prepare("SELECT DISTINCT path FROM items WHERE collection_uuid = ? "
                "ORDER BY name COLLATE NOCASE");
  query.addBindValue(collectionUuid);

  if (!query.exec()) {
    qWarning() << "Failed to load items from database:"
               << query.lastError().text();
    return filePaths;
  }

  while (query.next()) {
    filePaths.append(query.value(0).toString());
  }

  return filePaths;
}

void QueryManager::clearCollectionFromDatabaseByUuid(const QString &collectionUuid) {
  if (!m_db.isOpen()) {
    return;
  }

  m_db.transaction();

  try {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM items WHERE collection_uuid = ?");
    query.addBindValue(collectionUuid);
    if (!query.exec()) {
      throw std::runtime_error(query.lastError().text().toStdString());
    }

    QSqlQuery delc(m_db);
    delc.prepare("DELETE FROM collections WHERE uuid = ?");
    delc.addBindValue(collectionUuid);
    delc.exec();

    m_db.commit();
  } catch (const std::exception &e) {
    m_db.rollback();
    qCritical() << "Failed to clear collection from database:" << e.what();
  }
}

QString QueryManager::computeCollectionUuid(const QString &name, const QString &mediaDir) {
  QByteArray norm = (name + "|" + mediaDir).trimmed().toLower().toUtf8();
  QByteArray digest =
      QCryptographicHash::hash(norm, QCryptographicHash::Sha1).toHex();
  return QString::fromLatin1(digest);
}

// Static helpers
static auto canonicalKeyPath(const QString &absPath, bool dedup) -> QString {
  if (!dedup) {
    return absPath;
  }
  QString canon = QFileInfo(absPath).canonicalFilePath();
  if (canon.isEmpty()) {
    canon = QDir::cleanPath(absPath);
  }
  return canon;
}

static auto displayNameForBase(const QString &baseName) -> QString {
  return QString(baseName)
      .replace(QLatin1Char('_'), QLatin1Char(' '))
      .simplified();
}

template <typename Map, typename Key, typename Value>
inline void insertIfAbsent(Map &map, const Key &key, const Value &value) {
  if (!map.contains(key)) {
    map.insert(key, value);
  }
}

void QueryManager::appendFileMapsAndListCanonical(
    int collectionIndex, const CollectionConfig &expandedCollection,
    const QString &mappingArtworkDir, const QStringList &filePaths,
    QStringList &allFilePaths, QHash<QString, QString> &allFileNames,
    QHash<QString, QString> &fileToArtworkDir,
    QHash<QString, QString> &fileToMediaDir,
    QHash<QString, int> &fileToCollectionIndex, bool dedup) {
  const QString mediaDir = expandedCollection.mediaDirectory;
  QDir mediaQDir(mediaDir);

  for (const QString &file : filePaths) {
    const QString absPath = mediaQDir.absoluteFilePath(file);
    const QString keyPath = canonicalKeyPath(absPath, dedup);

    if (dedup) {
      if (!allFilePaths.contains(keyPath)) {
        allFilePaths.append(keyPath);
      }
    } else {
      allFilePaths.append(keyPath);
    }

    const QFileInfo info(file);
    const QString fileName = info.fileName();
    const QString baseName = info.completeBaseName();
    const QString displayName = displayNameForBase(baseName);

    allFileNames[keyPath] = displayName;

    insertIfAbsent(fileToArtworkDir, keyPath, mappingArtworkDir);
    insertIfAbsent(fileToArtworkDir, file, mappingArtworkDir);
    insertIfAbsent(fileToArtworkDir, fileName, mappingArtworkDir);
    insertIfAbsent(fileToArtworkDir, baseName, mappingArtworkDir);

    insertIfAbsent(fileToMediaDir, keyPath, mediaDir);
    insertIfAbsent(fileToMediaDir, file, mediaDir);
    insertIfAbsent(fileToMediaDir, fileName, mediaDir);
    insertIfAbsent(fileToMediaDir, baseName, mediaDir);

    insertIfAbsent(fileToCollectionIndex, keyPath, collectionIndex);
    insertIfAbsent(fileToCollectionIndex, file, collectionIndex);
    insertIfAbsent(fileToCollectionIndex, fileName, collectionIndex);
    insertIfAbsent(fileToCollectionIndex, baseName, collectionIndex);
  }
}

void QueryManager::sortFiles(QStringList &allFilePaths) {
  std::ranges::sort(allFilePaths, [&](const QString &lhs, const QString &rhs) {
    QString nameA = QFileInfo(lhs).completeBaseName();
    QString nameB = QFileInfo(rhs).completeBaseName();
    QString sortKeyA = PathUtils::normalizeDisplayName(nameA);
    QString sortKeyB = PathUtils::normalizeDisplayName(nameB);

    if (nameA.startsWith('\'') && nameA.length() > 1 &&
        (nameA[1].isDigit() || nameA[1].isLetter())) {
      sortKeyA = PathUtils::normalizeDisplayName(nameA.mid(1));
    }
    if (nameB.startsWith('\'') && nameB.length() > 1 &&
        (nameB[1].isDigit() || nameB[1].isLetter())) {
      sortKeyB = PathUtils::normalizeDisplayName(nameB.mid(1));
    }

    int priorityA = getCharacterSortPriority(sortKeyA);
    int priorityB = getCharacterSortPriority(sortKeyB);
    if (priorityA != priorityB) {
      return priorityA < priorityB;
    }
    return sortKeyA.compare(sortKeyB, Qt::CaseInsensitive) < 0;
  });
}

int QueryManager::getCharacterSortPriority(const QString &text) {
  if (text.isEmpty()) {
    return 3;
  }

  QChar firstChar = text[0];
  if (firstChar == '[' || firstChar == '(') {
    return 0;
  }
  if (firstChar == '\'' && text.length() > 1 &&
      (text[1].isDigit() || text[1].isLetter())) {
    return text[1].isDigit() ? 2 : 3;
  }
  if (firstChar.isDigit()) {
    return 2;
  }
  if (firstChar.isLetter()) {
    return 3;
  }
  return 1;
}
