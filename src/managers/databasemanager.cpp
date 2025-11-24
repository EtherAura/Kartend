#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <stdexcept>
#include <QThread>

#include "artworkmanager.h"
#include "databasemanager.h"
#include "databaseworker.h"
#include "pathutils.h"
#include "sessionmanager.h"

// Construct the database manager and initialize the database
DatabaseManager::DatabaseManager(SessionManager *sessionManager, QObject *parent)
    : QObject(parent), m_sessionManager(sessionManager) {
  qRegisterMetaType<CollectionConfig>("CollectionConfig");
  qRegisterMetaType<CollectionContext>("CollectionContext");
  qRegisterMetaType<QList<CollectionConfig>>("QList<CollectionConfig>");

  m_connectionName = "kartend_main";
  initDatabase();

  m_workerThread = new QThread(this);
  m_worker = new DatabaseWorker(m_sessionManager);
  m_worker->moveToThread(m_workerThread);

  connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
  
  connect(this, &DatabaseManager::requestLoadAllCollections, m_worker, &DatabaseWorker::loadAllCollections);
  connect(this, &DatabaseManager::requestLoadItems, m_worker, &DatabaseWorker::loadItems);
  connect(this, &DatabaseManager::requestLoadItemsWithSubcollections, m_worker, &DatabaseWorker::loadItemsWithSubcollections);
  connect(this, &DatabaseManager::requestFetchItemCount, m_worker, &DatabaseWorker::fetchItemCount);
  connect(this, &DatabaseManager::requestFetchItemsRange, m_worker, &DatabaseWorker::fetchItemsRange);
  
  connect(m_worker, &DatabaseWorker::itemsLoaded, this, &DatabaseManager::onWorkerItemsLoaded);
  connect(m_worker, &DatabaseWorker::itemCountLoaded, this, &DatabaseManager::onWorkerItemCountLoaded);
  connect(m_worker, &DatabaseWorker::itemsRangeLoaded, this, &DatabaseManager::onWorkerItemsRangeLoaded);
  connect(m_worker, &DatabaseWorker::errorOccurred, this, &DatabaseManager::errorOccurred);

  m_workerThread->start();
}

// Destroy the database manager and close/remove the connection
DatabaseManager::~DatabaseManager() {
  if (m_workerThread) {
    m_workerThread->quit();
    m_workerThread->wait();
  }

  if (m_db.isValid()) {
    QString connectionName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
  }
}

// Initialize the sqlite database and ensure uuid-based identity with
// de-duplication
void DatabaseManager::initDatabase() {
  if (m_db.isValid()) {
    QString connectionName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
  }

  m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
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

  QString collectionsTable = "CREATE TABLE IF NOT EXISTS collections ("
                             "id INTEGER PRIMARY KEY, "
                             "name TEXT NOT NULL, "
                             "last_scanned TEXT NOT NULL, "
                             "ext_signature TEXT DEFAULT '', "
                             "uuid TEXT DEFAULT ''"
                             ")";
  if (!query.exec(collectionsTable)) {
    qCritical() << "Failed to create collections table:" << query.lastError();
  }

  QString itemsTable =
      "CREATE TABLE IF NOT EXISTS items ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
      "collection_id INTEGER NOT NULL, "
      "path TEXT NOT NULL, "
      "name TEXT NOT NULL, "
      "artwork_path TEXT, "
      "last_modified TEXT NOT NULL, "
      "play_count INTEGER DEFAULT 0, "
      "last_played TEXT, "
      "rating INTEGER DEFAULT 0, "
      "collection_uuid TEXT DEFAULT '', "
      "UNIQUE(collection_id, path), "
      "FOREIGN KEY(collection_id) REFERENCES collections(id) ON DELETE CASCADE"
      ")";
  if (!query.exec(itemsTable)) {
    qCritical() << "Failed to create items table:" << query.lastError();
  }

  QSqlQuery idx(m_db);
  idx.prepare("CREATE INDEX IF NOT EXISTS idx_collections_uuid ON collections(uuid)");
  idx.exec();

  idx.prepare("CREATE INDEX IF NOT EXISTS idx_items_collection_uuid ON items(collection_uuid)");
  idx.exec();

  idx.prepare("CREATE UNIQUE INDEX IF NOT EXISTS uniq_items_uuid_path ON items(collection_uuid, path)");
  idx.exec();
}







void DatabaseManager::loadAllCollections(const QList<CollectionConfig> &allCollections) {
  emit requestLoadAllCollections(allCollections);
}

void DatabaseManager::loadItemsWithSubcollections(const CollectionContext &context,
                                                  const QList<CollectionConfig> &allCollections) {
  emit requestLoadItemsWithSubcollections(context, allCollections);
}

void DatabaseManager::loadItems(const CollectionContext &context) {
  emit requestLoadItems(context);
}

void DatabaseManager::fetchItemCount(const CollectionContext &context, const QList<CollectionConfig> &allCollections, const QString &filter) {
  emit requestFetchItemCount(context, allCollections, filter);
}

void DatabaseManager::fetchItemsRange(const CollectionContext &context, const QList<CollectionConfig> &allCollections, int offset, int limit, const QString &filter) {
  emit requestFetchItemsRange(context, allCollections, offset, limit, filter);
}

void DatabaseManager::onWorkerItemsLoaded(const QStringList &filePaths,
                                          const QHash<QString, QString> &fileNames,
                                          const QHash<QString, QString> &fileToArtworkDir,
                                          const QHash<QString, int> &fileToCollectionIndex) {
  m_fileToArtworkDir = fileToArtworkDir;
  m_fileToCollectionIndex = fileToCollectionIndex;
  emit itemsLoaded(filePaths, fileNames);
}

void DatabaseManager::onWorkerItemCountLoaded(int count) {
  emit itemCountLoaded(count);
}

void DatabaseManager::onWorkerItemsRangeLoaded(int offset, const QStringList &filePaths, const QHash<QString, QString> &fileNames) {
  emit itemsRangeLoaded(offset, filePaths, fileNames);
}

// Count items in collection and descendants using uuid identity
auto DatabaseManager::countCollectionRecursive(
    int collectionIndex, const QList<CollectionConfig> &allCollections)
    -> qint64 {
  if (collectionIndex < 0 || collectionIndex >= allCollections.size()) {
    return 0;
  }
  const QString uuid =
      computeCollectionUuid(allCollections[collectionIndex].name);
  qint64 total = countCollectionByUuid(uuid);
  QList<int> descendants =
      collectDescendantIndices(collectionIndex, allCollections);
  for (int descendantIndex : descendants) {
    const QString descendantUuid =
        computeCollectionUuid(allCollections[descendantIndex].name);
    total += countCollectionByUuid(descendantUuid);
  }
  return total;
}

// Count items globally across all collections
auto DatabaseManager::countGlobal(const QList<CollectionConfig> &allCollections)
    -> qint64 {
  Q_UNUSED(allCollections)
  if (!m_db.isOpen()) {
    return 0;
  }
  QSqlQuery query("SELECT COUNT(*) FROM items", m_db);
  if (!query.next()) {
    return 0;
  }
  return query.value(0).toLongLong();
}



// Recomputes and persists direct and recursive item counts for all collections
void DatabaseManager::updateCachedCounts(
    const QList<CollectionConfig> &allCollections) {
  if (!m_db.isOpen() || !m_sessionManager) {
    return;
  }

  m_sessionManager->clearStaleCollections(allCollections);

  for (const auto &config : allCollections) {
    if (config.mediaDirectory.trimmed().isEmpty()) {
      const QString uuid = computeCollectionUuid(config.name);
      clearCollectionFromDatabaseByUuid(uuid);
    }
  }

  qint64 global = countGlobal(allCollections);
  m_sessionManager->setGlobalItemCount(global);

  for (int i = 0; i < allCollections.size(); ++i) {
    const QString uuid = computeCollectionUuid(allCollections[i].name);
    qint64 direct = countCollectionByUuid(uuid);
    qint64 recursive = countCollectionRecursive(i, allCollections);
    m_sessionManager->setCollectionCounts(allCollections[i], allCollections,
                                          direct, recursive);
  }

  m_sessionManager->saveToDisk();
}

// Get owning collection index for a file based on built maps
auto DatabaseManager::getCollectionIndexForFile(const QString &filePath) const
    -> int {
  return m_fileToCollectionIndex.value(filePath, -1);
}

// Resolve artwork directory for a file using best-available mapping
auto DatabaseManager::findArtworkDirectoryForFile(const QString &filePath) const
    -> QString {
  if (m_fileToArtworkDir.contains(filePath)) {
    return m_fileToArtworkDir.value(filePath);
  }
  QString fileName = QFileInfo(filePath).fileName();
  QString baseName = QFileInfo(filePath).completeBaseName();

  if (m_fileToArtworkDir.contains(fileName)) {
    return m_fileToArtworkDir.value(fileName);
  }
  if (m_fileToArtworkDir.contains(baseName)) {
    return m_fileToArtworkDir.value(baseName);
  }
  return {};
}

// Compute a deterministic uuid from collection name
auto DatabaseManager::computeCollectionUuid(const QString &name) -> QString {
  QByteArray norm = name.trimmed().toLower().toUtf8();
  QByteArray digest =
      QCryptographicHash::hash(norm, QCryptographicHash::Sha1).toHex();
  return QString::fromLatin1(digest);
}



// Clear a collection's data by uuid
void DatabaseManager::clearCollectionFromDatabaseByUuid(
    const QString &collectionUuid) {
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

// Count items in a single collection by uuid
auto DatabaseManager::countCollectionByUuid(const QString &collectionUuid)
    -> qint64 {
  if (!m_db.isOpen()) {
    return 0;
  }
  QSqlQuery countQuery(m_db);
  countQuery.prepare(
      "SELECT COUNT(DISTINCT path) FROM items WHERE collection_uuid=?");
  countQuery.addBindValue(collectionUuid);
  if (!countQuery.exec() || !countQuery.next()) {
    return 0;
  }
  return countQuery.value(0).toLongLong();
}