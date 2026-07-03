// Coordinates SQLite database access via a worker thread for collection
// metadata queries. This TU holds the lifecycle + worker-thread coordination
// core: construction/teardown of the query + scan worker threads, the
// request* dispatch surface, and the worker-callback slots. The remaining
// DatabaseManager methods are split across sibling TUs:
//   - databasemanager_collections.cpp — path resolution, collection-level
//     maintenance (clear / migrate / purge / cached counts / last-scanned),
//     and per-collection item-path / state enumeration.
//   - databasemanager_items.cpp — per-item store facades (metadata, artwork,
//     usage stats, launch history) and the queued worker-write primitive.
// The class also composes three helpers: DatabaseSchema, FileMapCache,
// CachedCountsService.
#include "databasemanager.h"

#include <atomic>
#include <memory>

#include <QMetaType>
#include <QSemaphore>
#include <QStandardPaths>
#include <QtGlobal>
#include <QThread>
#include <QTimer>

#include "applicationcontext.h"
#include "cachedcountsservice.h"
#include "collection/collectioncontext.h"
#include "databaseschema.h"
#include "isessionmanager.h"
#include "loggingcategories.h"
#include "querymanager.h"
#include "titlefilter.h"
#include "uiconstants/timing.h"

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcDatabaseManager, "kartend.databasemanager")

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
  // Kartend-4p8o: ItemDetailData crosses the query-worker -> main-thread queued
  // connection (itemDetailLoaded), so it needs an explicit meta-type.
  qRegisterMetaType<ItemDetailData>("ItemDetailData");

  initDatabase();

  // Kartend-bbubn: bring-up-order guard. ApplicationManager registers
  // SessionManager into ctx BEFORE it constructs DatabaseManager, so if a ctx is
  // supplied its sessionManager() must be non-null here. A null sibling means a
  // manager was reordered or added above its dependency in
  // ApplicationManager::initialize() — that used to be a silent null store; now
  // it fails loudly in debug builds. Null ctx is still tolerated (headless /
  // test construction). Debug-only: Q_ASSERT_X compiles out in release.
  Q_ASSERT_X(!m_ctx || m_ctx->sessionManager(), "DatabaseManager::DatabaseManager",
             "ctx supplied but SessionManager not yet registered — check manager "
             "construction order in ApplicationManager::initialize()");
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
  connect(this, &DatabaseManager::requestLoadItemDetail, m_worker, &QueryManager::loadItemDetail);
  connect(this, &DatabaseManager::requestInvalidateCache, m_worker,
          &QueryManager::invalidateCollectionCache);
  connect(this, &DatabaseManager::requestInvalidateUsageSensitiveCaches, m_worker,
          &QueryManager::invalidateUsageSensitiveCaches);

  // Background scanning is handled by the scan worker.
  connect(this, &DatabaseManager::requestEnsureScannedForContext, m_scanWorker,
          &QueryManager::ensureScannedForContext);
  connect(this, &DatabaseManager::requestEnsureItemsFtsReady, m_scanWorker,
          &QueryManager::ensureItemsFtsReady);

  // Kartend-4i5e4: retry the FTS readiness pass (one-shot index rebuild) after
  // every collection scan commits. The startup pass can lose its write lock to
  // a long first-scan transaction on the OTHER worker connection and bail
  // (search then falls back to LIKE); a scan completing is exactly the moment
  // that contention has cleared. Once ready, the slot early-returns after a
  // cheap meta read, so this wiring is effectively free in steady state. Both
  // workers can run scans (loadOrScanCollection on m_worker, background scans
  // on m_scanWorker); the readiness pass itself always runs on m_scanWorker.
  // Explicitly queued: the same-object connect would otherwise run DIRECT,
  // i.e. inside the emitting scan slot — potentially within its transaction
  // scope, where the rebuild's nested guard could stamp readiness that the
  // outer transaction later rolls back.
  connect(m_worker, &QueryManager::collectionScanCompleted, m_scanWorker,
          &QueryManager::ensureItemsFtsReady, Qt::QueuedConnection);
  connect(m_scanWorker, &QueryManager::collectionScanCompleted, m_scanWorker,
          &QueryManager::ensureItemsFtsReady, Qt::QueuedConnection);

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
  // Forward the worker's detail-load result straight out as our own signal.
  connect(m_worker, &QueryManager::itemDetailLoaded, this, &IDatabaseManager::itemDetailLoaded);
  connect(m_worker, &QueryManager::errorOccurred, this, &DatabaseManager::errorOccurred);
  connect(m_worker, &QueryManager::cacheInvalidated, this, &DatabaseManager::cacheInvalidated);

  connect(m_scanWorker, &QueryManager::errorOccurred, this, &DatabaseManager::errorOccurred);
  connect(m_scanWorker, &QueryManager::scanProgress, this, &DatabaseManager::scanProgress);
  connect(m_scanWorker, &QueryManager::scanStarting, this, &DatabaseManager::scanStarting);
  connect(m_scanWorker, &QueryManager::scanItemsProgress, this,
          &DatabaseManager::scanItemsProgress);
  connect(m_scanWorker, &QueryManager::collectionScanCompleted, this,
          &DatabaseManager::collectionScanCompleted);
  // A background scan (on m_scanWorker) commits item changes the query worker's
  // caches can't see — the sorted-items cache keys on the collection uuid list,
  // not item contents, so it would keep serving stale ranges/counts after a
  // rescan adds/removes items. Drop the query worker's caches (non-destructive)
  // on scan completion so the next fetch rebuilds from fresh data
  // (Kartend-6r4g2). Runs on the query worker's thread via the queued context.
  connect(
      m_scanWorker, &QueryManager::collectionScanCompleted, m_worker,
      [w = m_worker](const QString &) { w->invalidateQueryCaches(); }, Qt::QueuedConnection);
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

  // Defer the FTS readiness pass (one-shot index rebuild, Kartend-4i5e4)
  // until after the event loop starts so startup UI is not blocked by any
  // optional maintenance work.
  QTimer::singleShot(0, this, [this] { emit requestEnsureItemsFtsReady(); });
}

DatabaseManager::~DatabaseManager() {
  // Cancel any in-flight scans so the workers can return promptly.
  //
  // Kartend-mkm4u: this is the ONE place that calls requestCancelScan() DIRECTLY
  // across threads instead of via the queued cancelScanRequested signal, and it
  // must stay that way. requestCancelScan() flips a mutex-guarded cancel flag
  // (ScanWorkController), which is safe to set from this (GUI) thread. The
  // queued signal would be posted behind the worker's currently-running scan
  // slot and could not run until that slot yields — but the slot only returns
  // because it polls this very flag, so a queued cancel during an active scan
  // would never flip it, the scan would not stop, and the bounded wait() below
  // would time out and leak the thread. The synchronous flag flip is what lets
  // teardown interrupt a busy scan. INVARIANT: any cancellation state added to
  // QueryManager/ScanService must stay thread-safe (mutex/atomic) so this
  // cross-thread call remains race-free.
  if (m_worker) {
    m_worker->requestCancelScan();
    // Kartend-kfnv7: also interrupt the heavy NON-scan slots (sorted-cache
    // build streaming, the reconnect retry ladder) — they were the only
    // worker work with no cancellation polling, so a 100k-item build
    // straddling teardown blew the wait(2000) below (debug abort /
    // release thread+connection leak). Same cross-thread atomic-flip
    // rationale as requestCancelScan (Kartend-mkm4u).
    m_worker->requestTeardown();
  }
  if (m_scanWorker) {
    m_scanWorker->requestCancelScan();
    m_scanWorker->requestTeardown();
  }

  // Kartend-juvb7: flush queued fire-and-forget writes (launch stats /
  // history rows / the orphan purge — everything queueWorkerWrite posted)
  // BEFORE quit(). Events still pending in the worker's queue when its loop
  // exits are never executed, so "launch an item, then quit Kartend" used
  // to silently drop the final play_count / last_played / history write —
  // precisely the most common moment those writes happen. The sentinel is
  // queued BEHIND every pending write (single worker thread, FIFO), so its
  // release proves the queue drained. Bounded: a wedged worker (busy slot
  // that never yields) just falls through to the existing quit + bounded
  // wait + leak path below. The semaphore is shared_ptr-owned because on
  // timeout the still-queued lambda must not dangle a stack reference.
  if (m_worker && m_workerThread && m_workerThread->isRunning()) {
    auto flushed = std::make_shared<QSemaphore>();
    QMetaObject::invokeMethod(m_worker, [flushed]() { flushed->release(); }, Qt::QueuedConnection);
    constexpr int FLUSH_WAIT_MS = 1500;
    (void)flushed->tryAcquire(1, FLUSH_WAIT_MS); // best-effort; timeout = proceed to quit
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
  emit requestLoadAllCollections(allCollections, ++m_loadGeneration);
}

void DatabaseManager::loadItemsWithSubcollections(const CollectionContext &context,
                                                  const QList<CollectionConfig> &allCollections) {
  emit requestLoadItemsWithSubcollections(context, allCollections, ++m_loadGeneration);
}

void DatabaseManager::loadItems(const CollectionContext &context,
                                const QList<CollectionConfig> &allCollections) {
  emit requestLoadItems(context, allCollections, ++m_loadGeneration);
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

void DatabaseManager::loadItemDetailAsync(int requestToken, const QString &collectionUuid,
                                          const QString &filePath, const QString &artworkDir,
                                          const QString &videoDir, const QString &manualDir) {
  emit requestLoadItemDetail(requestToken, collectionUuid, filePath, artworkDir, videoDir,
                             manualDir);
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
                                          const QHash<QString, int> &fileToCollectionIndex,
                                          quint64 loadGeneration) {
  // Drop a superseded load: a newer loadItems* bumped m_loadGeneration after
  // this delivery was dispatched, so these items belong to a collection the
  // user has already navigated away from. Suppressing it here keeps every
  // downstream consumer (the grid widgets AND the state-flag registry) from
  // briefly painting the wrong collection (Kartend-jgj9t). gen 0 = an untracked
  // load path, never dropped. Runs on the main thread, same as the bump.
  if (loadGeneration != 0 && loadGeneration != m_loadGeneration) {
    return;
  }
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
