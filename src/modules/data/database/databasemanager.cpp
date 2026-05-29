// Coordinates SQLite database access via worker thread for collection metadata
// queries. Composes three helpers — DatabaseSchema, FileMapCache,
// CachedCountsService — that own the responsibilities the partial-class file
// split (databasemanager{,paths,worker}.cpp) used to scatter.
#include "databasemanager.h"

#include <atomic>
#include <QMetaType>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QtGlobal>
#include <QThread>
#include <QTimer>

#include "applicationcontext.h"
#include "cachedcountsservice.h"
#include "collection/collectioncontext.h"
#include "collection/helpers.h"
#include "databaseschema.h"
#include "errorutils.h"
#include "isessionmanager.h"
#include "loggingcategories.h"
#include "pathutils.h"
#include "querymanager.h"
#include "titlefilter.h"
#include "uiconstants/timing.h"

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcDatabaseManager, "kartend.databasemanager")

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace {
/// Monotonic counter that suffixes Qt SQL connection names so multiple
/// DatabaseManager instances (e.g. one per integration-test fixture) don't
/// collide in QSqlDatabase's process-global connection registry.
std::atomic<quint64> g_connectionInstanceId{0};
} // namespace

DatabaseManager::DatabaseManager(const ApplicationContext *ctx, QObject *parent)
    : IDatabaseManager(parent), m_ctx(ctx) {
  // Per-instance suffix on every Qt SQL connection name so concurrent
  // DatabaseManagers (e.g. parallel test fixtures, future split DB modes)
  // don't collide in QSqlDatabase's process-global connection registry.
  const quint64 instanceId = g_connectionInstanceId.fetch_add(1, std::memory_order_relaxed);
  m_connectionName = QStringLiteral("kartend_main_%1").arg(instanceId);
  const QString queryWorkerName = QStringLiteral("kartend_query_worker_%1").arg(instanceId);
  const QString scanWorkerName = QStringLiteral("kartend_scan_worker_%1").arg(instanceId);

  qRegisterMetaType<CollectionConfig>("CollectionConfig");
  qRegisterMetaType<CollectionContext>("CollectionContext");
  qRegisterMetaType<QList<CollectionConfig>>("QList<CollectionConfig>");
  qRegisterMetaType<QHash<QString, qint64>>("QHash<QString, qint64>");

  initDatabase();

  ISessionManager *session = m_ctx ? m_ctx->sessionManager() : nullptr;

  // NOTE: QThreads are intentionally NOT parented to DatabaseManager. If they
  // were, ~QObject would auto-delete them mid-run during shutdown, and
  // ~QThread qFatals when destroyed while running. The destructor handles
  // bounded shutdown explicitly.
  m_workerThread = new QThread();
  m_worker = new QueryManager(session, queryWorkerName);
  m_worker->moveToThread(m_workerThread);
  connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

  // Dedicated scan worker (separate thread + separate DB connection) so
  // long scans don't block query operations and UI updates.
  m_scanThread = new QThread();
  m_scanWorker = new QueryManager(session, scanWorkerName);
  m_scanWorker->moveToThread(m_scanThread);
  connect(m_scanThread, &QThread::finished, m_scanWorker, &QObject::deleteLater);

  connect(this, &DatabaseManager::requestLoadAllCollections, m_worker,
          &QueryManager::loadAllCollections);
  connect(this, &DatabaseManager::requestLoadItems, m_worker, &QueryManager::loadItems);
  connect(this, &DatabaseManager::requestLoadItemsWithSubcollections, m_worker,
          &QueryManager::loadItemsWithSubcollections);
  connect(this, &DatabaseManager::requestFetchItemCount, m_worker,
          &QueryManager::fetchItemCountWithToken);
  connect(this, &DatabaseManager::requestFetchItemsRange, m_worker, &QueryManager::fetchItemsRange);
  connect(this, &DatabaseManager::requestFetchVisualIndexForPath, m_worker,
          &QueryManager::fetchVisualIndexForPath);
  connect(this, &DatabaseManager::requestInvalidateCache, m_worker,
          &QueryManager::invalidateCollectionCache);

  // Background scanning is handled by the scan worker.
  connect(this, &DatabaseManager::requestEnsureScannedForContext, m_scanWorker,
          &QueryManager::ensureScannedForContext);
  connect(this, &DatabaseManager::requestEnsureItemsFtsReady, m_scanWorker,
          &QueryManager::ensureItemsFtsReady);

  // Kartend-fvye: route cancelScan through the same queued-signal
  // pattern as the other request* signals, so the worker translates
  // the atomic flip on its own thread instead of the GUI thread
  // touching the worker directly. Future-proofs against a maintainer
  // adding non-atomic cancellation state to QueryManager.
  connect(this, &DatabaseManager::cancelScanRequested, m_worker, &QueryManager::requestCancelScan);
  connect(this, &DatabaseManager::cancelScanRequested, m_scanWorker,
          &QueryManager::requestCancelScan);

  connect(m_worker, &QueryManager::itemsLoaded, this, &DatabaseManager::onWorkerItemsLoaded);
  connect(m_worker, &QueryManager::itemCountLoaded, this,
          &DatabaseManager::onWorkerItemCountLoaded);
  connect(m_worker, &QueryManager::itemCountLoadedWithToken, this,
          &DatabaseManager::onWorkerItemCountLoadedWithToken);
  connect(m_worker, &QueryManager::itemsRangeLoaded, this,
          &DatabaseManager::onWorkerItemsRangeLoaded);
  connect(m_worker, &QueryManager::visualIndexForPathLoaded, this,
          &DatabaseManager::visualIndexForPathLoaded);
  connect(m_worker, &QueryManager::errorOccurred, this, &DatabaseManager::errorOccurred);
  connect(m_worker, &QueryManager::cacheInvalidated, this, &DatabaseManager::cacheInvalidated);

  connect(m_scanWorker, &QueryManager::errorOccurred, this, &DatabaseManager::errorOccurred);
  connect(m_scanWorker, &QueryManager::scanProgress, this, &DatabaseManager::scanProgress);
  connect(m_scanWorker, &QueryManager::scanStarting, this, &DatabaseManager::scanStarting);
  connect(m_scanWorker, &QueryManager::scanItemsProgress, this,
          &DatabaseManager::scanItemsProgress);
  connect(m_scanWorker, &QueryManager::collectionScanCompleted, this,
          &DatabaseManager::collectionScanCompleted);
  connect(m_scanWorker, &QueryManager::collectionScanSummary, this,
          &DatabaseManager::collectionScanSummary);

  // Cached-counts service: bridges a debounced main-thread refresh request to
  // QueryManager::updateCachedCounts and back to SessionManager + UI.
  m_cachedCounts = new CachedCountsService(session, UIConstants::Timing::SHORT_DELAY_MS, this);
  connect(m_cachedCounts, &CachedCountsService::dispatchToWorker, m_worker,
          &QueryManager::updateCachedCounts);
  connect(m_worker, &QueryManager::cachedCountsComputed, m_cachedCounts,
          &CachedCountsService::onWorkerComputed);
  connect(m_cachedCounts, &CachedCountsService::updated, this,
          &DatabaseManager::cachedCountsUpdated);

  m_workerThread->start();
  m_scanThread->start();

  // Defer FTS backfill until after the event loop starts so startup UI is not
  // blocked by any optional maintenance work.
  QTimer::singleShot(0, this, [this] { emit requestEnsureItemsFtsReady(); });
}

DatabaseManager::~DatabaseManager() {
  // Cancel any in-flight scans so the workers can return promptly.
  if (m_worker) {
    m_worker->requestCancelScan();
  }
  if (m_scanWorker) {
    m_scanWorker->requestCancelScan();
  }

  // Quit + bounded wait. If the worker thread doesn't return within the
  // budget, intentionally leak it (set pointer to nullptr) rather than let
  // ~QThread qFatal on a still-running thread. The OS will reclaim threads
  // and SQLite handles at process exit (we use std::_Exit in main).
  constexpr int SHUTDOWN_WAIT_MS = 2000;
  // The leak paths below (timed-out wait → forget the thread pointer) are
  // safe in production because main.cpp std::_Exits and the OS reaps
  // the thread + its SQLite handle on process exit. They're NOT safe in
  // tests, which run thousands of MainWindow ctor/dtor cycles inside one
  // QApplication and would accumulate leaked threads. Surface a debug-only
  // qFatal here so a regression that makes 2s actually insufficient
  // (e.g. a deadlock in the worker) is caught immediately in CI rather
  // than silently leaking. Release builds keep the leak-and-move-on
  // semantics because qFatal mid-shutdown isn't useful to end users.
  if (m_workerThread) {
    m_workerThread->quit();
    if (m_workerThread->wait(SHUTDOWN_WAIT_MS)) {
      delete m_workerThread;
    } else {
      Q_ASSERT_X(false, "DatabaseManager::~DatabaseManager",
                 "m_workerThread did not finish within SHUTDOWN_WAIT_MS — leaked. "
                 "Likely a worker-side deadlock or a slot blocking on the GUI thread.");
    }
    m_workerThread = nullptr;
  }
  if (m_scanThread) {
    m_scanThread->quit();
    if (m_scanThread->wait(SHUTDOWN_WAIT_MS)) {
      delete m_scanThread;
    } else {
      Q_ASSERT_X(false, "DatabaseManager::~DatabaseManager",
                 "m_scanThread did not finish within SHUTDOWN_WAIT_MS — leaked. "
                 "Likely a worker-side deadlock or a slot blocking on the GUI thread.");
    }
    m_scanThread = nullptr;
  }

  if (m_db.isValid()) {
    QString connectionName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
  }
}

void DatabaseManager::initDatabase() {
  if (m_db.isValid()) {
    QString connectionName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
  }

  // Reconnecting can re-open the same file with a different on-disk
  // state (recovered transaction, manual restore, fresh database). The
  // per-item cache from the previous connection is no longer guaranteed
  // to reflect what we're about to read, so drop it.
  m_metadataCache.clear();

  m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
  const QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (!DatabaseSchema::openConnection(m_db, dbPath)) {
    return;
  }
  DatabaseSchema::applyConnectionPragmas(m_db);
  DatabaseSchema::createTables(m_db);
  DatabaseSchema::createIndexes(m_db);
}

void DatabaseManager::loadAllCollections(const QList<CollectionConfig> &allCollections) {
  emit requestLoadAllCollections(allCollections);
}

void DatabaseManager::loadItemsWithSubcollections(const CollectionContext &context,
                                                  const QList<CollectionConfig> &allCollections) {
  emit requestLoadItemsWithSubcollections(context, allCollections);
}

void DatabaseManager::loadItems(const CollectionContext &context,
                                const QList<CollectionConfig> &allCollections) {
  emit requestLoadItems(context, allCollections);
}

void DatabaseManager::fetchItemCount(const CollectionContext &context,
                                     const QList<CollectionConfig> &allCollections,
                                     const QString &filter, int requestToken) {
  qCDebug(lcSearchDiag) << "[DatabaseManager] fetchItemCount: collIndex=" << context.currentIndex
                        << "filter='" << filter << "'"
                        << "token=" << requestToken;
  // Avoid scheduling scans on every search keystroke — scan completion can
  // invalidate in-flight paginated range loads (blank views + repeated CPU
  // rebuilds). Playlists have no filesystem to scan, so skip them too.
  if (filter.trimmed().isEmpty() && !context.config.isPlaylist) {
    emit requestEnsureScannedForContext(context, allCollections);
  }
  emit requestFetchItemCount(context, allCollections, filter, requestToken);
}

void DatabaseManager::fetchItemsRange(const CollectionContext &context,
                                      const QList<CollectionConfig> &allCollections, int offset,
                                      int limit, const QString &filter) {
  qCDebug(lcSearchDiag) << "[DatabaseManager] fetchItemsRange: collIndex=" << context.currentIndex
                        << "offset=" << offset << "limit=" << limit << "filter='" << filter << "'";
  emit requestFetchItemsRange(context, allCollections, offset, limit, filter);
}

void DatabaseManager::fetchVisualIndexForPath(const CollectionContext &context,
                                              const QList<CollectionConfig> &allCollections,
                                              const QString &filePath) {
  emit requestFetchVisualIndexForPath(context, allCollections, filePath);
}

void DatabaseManager::invalidateCollectionCache(const QString &collectionUuid) {
  // Drop the per-item LRU entries before the worker invalidation lands —
  // a rescan / re-import could change any row's contents under us, so
  // serving cached metadata until the worker reply arrives would risk
  // showing stale data.
  m_metadataCache.invalidateCollection(collectionUuid);
  emit requestInvalidateCache(collectionUuid);
}

void DatabaseManager::cancelScan() {
  // Kartend-fvye: queue the cancel through a Qt::QueuedConnection signal
  // (wired in the ctor) so the workers translate the request on their
  // own threads instead of the GUI thread touching worker-owned state.
  // The destructor still calls requestCancelScan() directly because the
  // worker event loop is being torn down anyway — see ~DatabaseManager.
  emit cancelScanRequested();
}

// ─── Worker callbacks ─────────────────────────────────────────────────────────

void DatabaseManager::onWorkerItemsLoaded(const QStringList &filePaths,
                                          const QHash<QString, QString> &fileNames,
                                          const QHash<QString, QString> &fileToArtworkDir,
                                          const QHash<QString, QString> &fileToMediaDir,
                                          const QHash<QString, int> &fileToCollectionIndex) {
  // Apply per-collection title-exclusion patterns on the main thread, after
  // the worker has produced the underscore-cleaned base names. Doing it here
  // (rather than inside QueryManager) keeps the worker free of settings-
  // dependent state and lets a single TitleFilter registry serve all
  // downstream consumers (alpha jump, search, sidebar, list view).
  QHash<QString, QString> filteredNames = fileNames;
  for (auto it = filteredNames.begin(); it != filteredNames.end(); ++it) {
    const int collectionIdx = fileToCollectionIndex.value(it.key(), -1);
    it.value() = TitleFilter::apply(collectionIdx, it.value());
  }

  m_fileMapCache.replaceFromItemsLoaded(fileToArtworkDir, fileToMediaDir, fileToCollectionIndex,
                                        filteredNames);
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
  // Keep legacy listeners working (e.g. MainWindow overlay suppression).
  emit itemCountLoaded(count);
}

void DatabaseManager::onWorkerItemsRangeLoaded(int offset, const QStringList &filePaths,
                                               const QHash<QString, QString> &fileNames,
                                               const QHash<QString, QString> &fileToArtworkDir,
                                               const QHash<QString, QString> &fileToMediaDir,
                                               const QHash<QString, int> &fileToCollectionIndex,
                                               int requestedCollectionIndex) {
  qCDebug(lcSearchDiag) << "[DatabaseManager] onWorkerItemsRangeLoaded: offset=" << offset
                        << "paths=" << filePaths.size();

  QHash<QString, QString> filteredNames = fileNames;
  for (auto it = filteredNames.begin(); it != filteredNames.end(); ++it) {
    const int collectionIdx = fileToCollectionIndex.value(it.key(), -1);
    it.value() = TitleFilter::apply(collectionIdx, it.value());
  }

  m_fileMapCache.mergeRangeLoaded(fileToArtworkDir, fileToMediaDir, fileToCollectionIndex);
  emit itemsRangeLoaded(offset, filePaths, filteredNames, fileToArtworkDir, fileToMediaDir,
                        fileToCollectionIndex, requestedCollectionIndex);
}

// ─── Path resolution (delegated to FileMapCache) ──────────────────────────────

int DatabaseManager::getCollectionIndexForFile(const QString &filePath) const {
  return m_fileMapCache.collectionIndexForFile(filePath);
}

QString DatabaseManager::findArtworkDirectoryForFile(const QString &filePath) const {
  return m_fileMapCache.artworkDirForFile(filePath);
}

QString DatabaseManager::resolveFilePath(const QString &rawEntry,
                                         const CollectionContext &context) const {
  return m_fileMapCache.resolveFilePath(rawEntry, context);
}

QString DatabaseManager::resolveRelativeFilePath(const QString &rawFileName,
                                                 const QHash<QString, QString> &fileNames) const {
  return m_fileMapCache.resolveRelativeFilePath(rawFileName, fileNames);
}

// ─── Counts ───────────────────────────────────────────────────────────────────

qint64 DatabaseManager::countCollectionByUuid(const QString &collectionUuid) const {
  if (!m_db.isOpen()) {
    return 0;
  }
  QSqlQuery countQuery(m_db);
  countQuery.prepare("SELECT COUNT(DISTINCT path) FROM items WHERE collection_uuid=?");
  countQuery.addBindValue(collectionUuid);
  if (!countQuery.exec() || !countQuery.next()) {
    return 0;
  }
  return countQuery.value(0).toLongLong();
}

qint64
DatabaseManager::countCollectionRecursive(int collectionIndex,
                                          const QList<CollectionConfig> &allCollections) const {
  if (!CollectionUtils::isValidIndex(collectionIndex, &allCollections)) {
    return 0;
  }
  const QString expandedMediaDir = PathUtils::validateAndExpandPath(
      allCollections[collectionIndex].mediaDirectory, allCollections[collectionIndex].name);
  const QString uuid = CollectionUtils::computeCollectionUuid(allCollections[collectionIndex].name,
                                                              expandedMediaDir);
  qint64 total = countCollectionByUuid(uuid);
  const QList<int> descendants =
      CollectionUtils::collectDescendantIndices(collectionIndex, allCollections);
  for (int descendantIndex : descendants) {
    const QString descExpandedMediaDir = PathUtils::validateAndExpandPath(
        allCollections[descendantIndex].mediaDirectory, allCollections[descendantIndex].name);
    const QString descendantUuid = CollectionUtils::computeCollectionUuid(
        allCollections[descendantIndex].name, descExpandedMediaDir);
    total += countCollectionByUuid(descendantUuid);
  }
  return total;
}

void DatabaseManager::clearCollectionFromDatabaseByUuid(const QString &collectionUuid) {
  if (!m_db.isOpen()) {
    return;
  }
  if (!m_db.transaction()) {
    auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                      "Failed to start database transaction",
                                      "DatabaseManager::clearCollectionFromDatabaseByUuid")
                   .withDetails(m_db.lastError().text());
    ErrorUtils::logError(err);
    return;
  }
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
    if (!delc.exec()) {
      throw std::runtime_error(delc.lastError().text().toStdString());
    }
    if (!m_db.commit()) {
      throw std::runtime_error(m_db.lastError().text().toStdString());
    }
    m_metadataCache.invalidateCollection(collectionUuid);
  } catch (const std::exception &e) {
    m_db.rollback();
    auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                      "Failed to clear collection from database",
                                      "DatabaseManager::clearCollectionFromDatabaseByUuid")
                   .withDetails(ErrorUtils::exceptionMessage(e));
    ErrorUtils::logError(err);
  }
}

void DatabaseManager::migrateCollectionUuid(const QString &oldUuid, const QString &newUuid) {
  if (!m_db.isOpen() || oldUuid.isEmpty() || newUuid.isEmpty() || oldUuid == newUuid) {
    return;
  }
  if (!m_db.transaction()) {
    ErrorUtils::logError(ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                                "Failed to start database transaction",
                                                "DatabaseManager::migrateCollectionUuid")
                             .withDetails(m_db.lastError().text()));
    return;
  }
  try {
    // OR REPLACE: should a row already exist under newUuid (e.g. a
    // rescan ran before the migration), the migrated row — which
    // carries the real play history — wins over the fresh one.
    QSqlQuery items(m_db);
    items.prepare("UPDATE OR REPLACE items SET collection_uuid = ? WHERE collection_uuid = ?");
    items.addBindValue(newUuid);
    items.addBindValue(oldUuid);
    if (!items.exec()) {
      throw std::runtime_error(items.lastError().text().toStdString());
    }
    QSqlQuery cols(m_db);
    cols.prepare("UPDATE OR REPLACE collections SET uuid = ? WHERE uuid = ?");
    cols.addBindValue(newUuid);
    cols.addBindValue(oldUuid);
    if (!cols.exec()) {
      throw std::runtime_error(cols.lastError().text().toStdString());
    }
    if (!m_db.commit()) {
      throw std::runtime_error(m_db.lastError().text().toStdString());
    }
    m_metadataCache.invalidateCollection(oldUuid);
    m_metadataCache.invalidateCollection(newUuid);
  } catch (const std::exception &e) {
    m_db.rollback();
    ErrorUtils::logError(ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                                "Failed to migrate collection uuid",
                                                "DatabaseManager::migrateCollectionUuid")
                             .withDetails(ErrorUtils::exceptionMessage(e)));
  }
}

QList<IDatabaseManager::ItemPathRow>
DatabaseManager::loadAllItemPathsForCollection(const QString &collectionUuid) const {
  QList<IDatabaseManager::ItemPathRow> out;
  if (!m_db.isOpen() || collectionUuid.isEmpty()) {
    return out;
  }
  QSqlQuery q(const_cast<QSqlDatabase &>(m_db));
  if (!q.prepare("SELECT path, artwork_path FROM items WHERE collection_uuid = ? "
                 "ORDER BY name COLLATE NOCASE ASC")) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to prepare item-path enumeration query",
                                             "DatabaseManager::loadAllItemPathsForCollection")
                             .withDetails(q.lastError().text()));
    return out;
  }
  q.addBindValue(collectionUuid);
  if (!q.exec()) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to enumerate item paths",
                                             "DatabaseManager::loadAllItemPathsForCollection")
                             .withDetails(q.lastError().text()));
    return out;
  }
  while (q.next()) {
    IDatabaseManager::ItemPathRow row;
    row.path = q.value(0).toString();
    row.artworkPath = q.value(1).toString();
    out.append(row);
  }
  return out;
}

QHash<QString, IDatabaseManager::ItemStateFlags>
DatabaseManager::loadItemStateFlagsForCollection(const QString &collectionUuid) const {
  QHash<QString, IDatabaseManager::ItemStateFlags> out;
  if (!m_db.isOpen() || collectionUuid.isEmpty()) {
    return out;
  }
  QSqlQuery q(const_cast<QSqlDatabase &>(m_db));
  if (!q.prepare("SELECT path, is_pinned, is_hidden, continue_later "
                 "FROM item_metadata "
                 "WHERE collection_uuid = ? "
                 "AND (is_pinned = 1 OR is_hidden = 1 OR continue_later = 1)")) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to prepare item-state-flags query",
                                             "DatabaseManager::loadItemStateFlagsForCollection")
                             .withDetails(q.lastError().text()));
    return out;
  }
  q.addBindValue(collectionUuid);
  if (!q.exec()) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to enumerate item state flags",
                                             "DatabaseManager::loadItemStateFlagsForCollection")
                             .withDetails(q.lastError().text()));
    return out;
  }
  while (q.next()) {
    IDatabaseManager::ItemStateFlags flags;
    flags.isPinned = q.value(1).toInt() != 0;
    flags.isHidden = q.value(2).toInt() != 0;
    flags.continueLater = q.value(3).toInt() != 0;
    out.insert(q.value(0).toString(), flags);
  }
  return out;
}

void DatabaseManager::purgeOrphanCollectionData(const QList<CollectionConfig> &liveCollections) {
  // Derive the uuid set to keep. A collection's uuid is
  // hash(name, mediaDir); different scan paths feed either the raw or
  // the path-variable-expanded media dir, so keep BOTH forms — that
  // way a collection scanned under either is never purged as an
  // orphan. (For a plain media path the two forms are identical.)
  QSet<QString> liveUuids;
  liveUuids.reserve(liveCollections.size() * 2);
  for (const CollectionConfig &c : liveCollections) {
    liveUuids.insert(CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory));
    const QString expanded = PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
    if (expanded != c.mediaDirectory) {
      liveUuids.insert(CollectionUtils::computeCollectionUuid(c.name, expanded));
    }
  }

  // An empty live set means "no collections" — but that is also the
  // transient state during early startup, so refuse to nuke every
  // row on it. A genuinely empty library has nothing to purge anyway.
  if (!m_db.isOpen() || liveUuids.isEmpty()) {
    return;
  }
  // Build a placeholder list for the NOT IN clause; collection counts
  // are small, so a bound IN clause is fine (no temp table needed).
  QStringList placeholders;
  placeholders.reserve(liveUuids.size());
  for (int i = 0; i < liveUuids.size(); ++i) {
    placeholders << QStringLiteral("?");
  }
  const QString inClause =
      QStringLiteral("(") + placeholders.join(QStringLiteral(", ")) + QStringLiteral(")");

  if (!m_db.transaction()) {
    ErrorUtils::logError(ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                                "Failed to start database transaction",
                                                "DatabaseManager::purgeOrphanCollectionData")
                             .withDetails(m_db.lastError().text()));
    return;
  }
  try {
    // items keys on `collection_uuid`, collections on `uuid`.
    const QList<QPair<QString, QString>> targets = {
        {QStringLiteral("items"), QStringLiteral("collection_uuid")},
        {QStringLiteral("collections"), QStringLiteral("uuid")}};
    for (const auto &target : targets) {
      QSqlQuery del(m_db);
      del.prepare(QStringLiteral("DELETE FROM %1 WHERE %2 NOT IN %3")
                      .arg(target.first, target.second, inClause));
      for (const QString &uuid : liveUuids) {
        del.addBindValue(uuid);
      }
      if (!del.exec()) {
        throw std::runtime_error(del.lastError().text().toStdString());
      }
    }
    if (!m_db.commit()) {
      throw std::runtime_error(m_db.lastError().text().toStdString());
    }
  } catch (const std::exception &e) {
    m_db.rollback();
    ErrorUtils::logError(ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                                "Failed to purge orphan collection data",
                                                "DatabaseManager::purgeOrphanCollectionData")
                             .withDetails(ErrorUtils::exceptionMessage(e)));
  }
}

void DatabaseManager::updateCachedCounts(const QList<CollectionConfig> &allCollections) {
  ISessionManager *session = m_ctx ? m_ctx->sessionManager() : nullptr;
  if (!m_db.isOpen() || !session || !m_cachedCounts) {
    return;
  }
  session->clearStaleCollections(allCollections);

  const int collectionCount = allCollections.size();
  QStringList uuids;
  uuids.reserve(collectionCount);
  for (int i = 0; i < collectionCount; ++i) {
    const QString expandedMediaDir =
        PathUtils::validateAndExpandPath(allCollections[i].mediaDirectory, allCollections[i].name);
    const QString uuid =
        CollectionUtils::computeCollectionUuid(allCollections[i].name, expandedMediaDir);
    uuids.append(uuid);
    if (expandedMediaDir.trimmed().isEmpty()) {
      clearCollectionFromDatabaseByUuid(uuid);
    }
  }
  m_cachedCounts->requestUpdate(allCollections, uuids);
}

// ─── Last-scanned ─────────────────────────────────────────────────────────────

QDateTime DatabaseManager::loadCollectionLastScanned(const QString &collectionUuid) const {
  if (collectionUuid.isEmpty() || !m_db.isValid() || !m_db.isOpen()) {
    return {};
  }
  QSqlQuery query(m_db);
  query.prepare(QStringLiteral("SELECT last_scanned FROM collections WHERE uuid = ?"));
  query.bindValue(0, collectionUuid);
  if (!query.exec() || !query.next()) {
    return {};
  }
  return QDateTime::fromString(query.value(0).toString(), Qt::ISODate);
}

// ─── ItemMetadataStore facades ────────────────────────────────────────────────

ItemMetadataStore::ItemMetadata DatabaseManager::loadItemMetadata(const QString &collectionUuid,
                                                                  const QString &path) const {
  if (auto cached = m_metadataCache.tryGetMetadata(collectionUuid, path); cached) {
    return *cached;
  }
  auto result = ItemMetadataStore::load(const_cast<QSqlDatabase &>(m_db), collectionUuid, path);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    ItemMetadataStore::ItemMetadata empty;
    empty.collectionUuid = collectionUuid;
    empty.path = path;
    // Don't cache DB-failure stubs — a transient connection blip
    // shouldn't pin an empty row that masks the real data after
    // reconnection.
    return empty;
  }
  m_metadataCache.putMetadata(collectionUuid, path, result.value());
  return result.value();
}

QHash<QString, ItemMetadataStore::ItemMetadata>
DatabaseManager::loadItemMetadataBatch(const QString &collectionUuid,
                                       const QStringList &paths) const {
  QHash<QString, ItemMetadataStore::ItemMetadata> out;
  out.reserve(paths.size());

  // Walk cache first so a warm BatchScrapeRunner pre-flight doesn't issue
  // any SQL when the same paths were touched earlier in the session.
  QStringList missingPaths;
  missingPaths.reserve(paths.size());
  for (const QString &path : paths) {
    if (auto cached = m_metadataCache.tryGetMetadata(collectionUuid, path); cached) {
      out.insert(path, *cached);
    } else {
      missingPaths.append(path);
    }
  }

  if (missingPaths.isEmpty()) return out;

  auto result =
      ItemMetadataStore::loadBatch(const_cast<QSqlDatabase &>(m_db), collectionUuid, missingPaths);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    // Mirror single-load's graceful degradation: hand back empty stubs so
    // the caller can keep iterating without nullopt checks.
    for (const QString &p : missingPaths) {
      ItemMetadataStore::ItemMetadata empty;
      empty.collectionUuid = collectionUuid;
      empty.path = p;
      out.insert(p, empty);
    }
    return out;
  }

  // Merge in the freshly-loaded rows and back-populate the cache for any
  // row that actually had data — empty stubs (no row in item_metadata)
  // are intentionally NOT cached to match loadItemMetadata's policy.
  const auto &loaded = result.value();
  for (auto it = loaded.constBegin(); it != loaded.constEnd(); ++it) {
    out.insert(it.key(), it.value());
    if (!it.value().source.isEmpty()) {
      m_metadataCache.putMetadata(collectionUuid, it.key(), it.value());
    }
  }

  return out;
}

bool DatabaseManager::saveItemMetadata(const ItemMetadataStore::ItemMetadata &metadata) {
  auto result = ItemMetadataStore::save(m_db, metadata);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    emit errorOccurred(result.error());
    return false;
  }
  m_metadataCache.invalidateItem(metadata.collectionUuid, metadata.path);
  return true;
}

// ─── ItemArtworkStore facades ─────────────────────────────────────────────────

QList<ItemArtworkStore::ItemArtwork> DatabaseManager::loadItemArtwork(const QString &collectionUuid,
                                                                      const QString &path) const {
  if (auto cached = m_metadataCache.tryGetArtwork(collectionUuid, path); cached) {
    return *cached;
  }
  auto result =
      ItemArtworkStore::loadAllForItem(const_cast<QSqlDatabase &>(m_db), collectionUuid, path);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  m_metadataCache.putArtwork(collectionUuid, path, result.value());
  return result.value();
}

bool DatabaseManager::saveItemArtwork(const ItemArtworkStore::ItemArtwork &artwork) {
  auto result = ItemArtworkStore::save(m_db, artwork);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    emit errorOccurred(result.error());
    return false;
  }
  m_metadataCache.invalidateItem(artwork.collectionUuid, artwork.path);
  return true;
}

bool DatabaseManager::removeItemArtwork(const QString &collectionUuid, const QString &path,
                                        const QString &artworkType) {
  auto result = ItemArtworkStore::remove(m_db, collectionUuid, path, artworkType);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    emit errorOccurred(result.error());
    return false;
  }
  m_metadataCache.invalidateItem(collectionUuid, path);
  return true;
}

void DatabaseManager::invalidateMetadataCacheItem(const QString &collectionUuid,
                                                  const QString &path) {
  m_metadataCache.invalidateItem(collectionUuid, path);
}

// ─── UsageStatsStore facades ──────────────────────────────────────────────────

UsageStatsStore::ItemUsageStats DatabaseManager::loadItemUsageStats(const QString &collectionUuid,
                                                                    const QString &path) const {
  if (auto cached = m_metadataCache.tryGetUsageStats(collectionUuid, path); cached) {
    return *cached;
  }
  auto result =
      UsageStatsStore::loadForItem(const_cast<QSqlDatabase &>(m_db), collectionUuid, path);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    UsageStatsStore::ItemUsageStats empty;
    empty.collectionUuid = collectionUuid;
    empty.path = path;
    return empty;
  }
  m_metadataCache.putUsageStats(collectionUuid, path, result.value());
  return result.value();
}

void DatabaseManager::recordItemLaunch(const QString &collectionUuid, const QString &path) {
  auto result = UsageStatsStore::recordLaunch(m_db, collectionUuid, path);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return;
  }
  m_metadataCache.invalidateUsage(collectionUuid, path);
}

void DatabaseManager::recordItemPlaySession(const QString &collectionUuid, const QString &path,
                                            qint64 seconds) {
  auto result = UsageStatsStore::recordPlaySession(m_db, collectionUuid, path, seconds);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return;
  }
  m_metadataCache.invalidateUsage(collectionUuid, path);
}

UsageStatsStore::AggregateStats DatabaseManager::loadAggregateUsageStats() const {
  auto result = UsageStatsStore::loadAggregate(const_cast<QSqlDatabase &>(m_db));
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

QList<UsageStatsStore::ItemUsageRow> DatabaseManager::loadTopPlayedItems(int limit) const {
  auto result = UsageStatsStore::loadTopPlayed(const_cast<QSqlDatabase &>(m_db), limit);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

QList<UsageStatsStore::ItemUsageRow> DatabaseManager::loadRecentlyPlayedItems(int limit) const {
  auto result = UsageStatsStore::loadRecentlyPlayed(const_cast<QSqlDatabase &>(m_db), limit);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

QList<UsageStatsStore::ItemUsageRow> DatabaseManager::loadNeverPlayedItems(int limit) const {
  auto result = UsageStatsStore::loadNeverPlayed(const_cast<QSqlDatabase &>(m_db), limit);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

qint64 DatabaseManager::countItemsPlayedSince(const QString &isoCutoffUtc) const {
  auto result = UsageStatsStore::countPlayedSince(const_cast<QSqlDatabase &>(m_db), isoCutoffUtc);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return 0;
  }
  return result.value();
}

QHash<QString, UsageStatsStore::CollectionUsage> DatabaseManager::loadUsageByCollection() const {
  auto result = UsageStatsStore::loadCollectionBreakdown(const_cast<QSqlDatabase &>(m_db));
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

bool DatabaseManager::resetAllUsageStats() {
  auto result = UsageStatsStore::resetAll(m_db);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    emit errorOccurred(result.error());
    return false;
  }
  // The reset only touches the usage column, but enumerating every
  // cached entry to scrub one slot costs more than the simple clear.
  // resetAllUsageStats is a deliberate user action — flushing the
  // whole per-item cache here is fine; the next selection refills it.
  m_metadataCache.clear();
  return true;
}

// ─── HistoryStore facades ─────────────────────────────────────────────────────

void DatabaseManager::recordHistoryEntry(const QString &collectionUuid, const QString &path,
                                         const QString &name, int maxEntries) {
  auto result = HistoryStore::recordLaunch(m_db, collectionUuid, path, name);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return;
  }
  if (maxEntries > 0) {
    auto trim = HistoryStore::trimToMaxEntries(m_db, maxEntries);
    if (trim.isError()) {
      ErrorUtils::logError(trim.error());
    }
  }
}

QList<HistoryStore::HistoryEntry> DatabaseManager::loadRecentHistory(int limit) const {
  auto result = HistoryStore::loadRecent(const_cast<QSqlDatabase &>(m_db), limit);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

qint64 DatabaseManager::historyEntryCount() const {
  auto result = HistoryStore::count(const_cast<QSqlDatabase &>(m_db));
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return 0;
  }
  return result.value();
}

bool DatabaseManager::clearHistory() {
  auto result = HistoryStore::clearAll(m_db);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    emit errorOccurred(result.error());
    return false;
  }
  return true;
}
