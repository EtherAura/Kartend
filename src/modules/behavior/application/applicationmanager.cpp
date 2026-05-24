// Owns and coordinates all manager lifecycles with controlled destruction
// order.
#include "applicationmanager.h"

#include "applicationcontext.h"
#include "artworkmanager.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "detailpagemanager.h"
#include "detailspanemanager.h"
#include "idatabasemanager.h"
#include "interactionmanager.h"
#include "isettingsmanager.h"
#include "kartmanager.h"
#include "navigationmanager.h"
#include "playlistmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"

#include <QtConcurrent>

namespace {
ApplicationManager::DatabaseManagerFactory g_databaseManagerFactory;
ApplicationManager::SettingsManagerFactory g_settingsManagerFactory;
} // namespace

void ApplicationManager::setDatabaseManagerFactory(DatabaseManagerFactory factory) {
  g_databaseManagerFactory = std::move(factory);
}

void ApplicationManager::setSettingsManagerFactory(SettingsManagerFactory factory) {
  g_settingsManagerFactory = std::move(factory);
}

ApplicationManager::ApplicationManager(QObject *parent) : QObject(parent) {}

ApplicationManager::~ApplicationManager() {
  // Defensive: ensure the deferred cache initialize() task is finished before
  // m_cacheManager is destroyed. shutdown() handles this normally; this guard
  // covers paths where shutdown() is skipped (e.g. abnormal teardown).
  //
  // Signal cancellation FIRST so an in-flight initialize() short-circuits
  // its 50MB timestamps parse rather than stalling the destructor for
  // multiple seconds (Kartend-bu5n). cancelPendingIo is idempotent and
  // safe to call repeatedly.
  if (m_cacheManager) {
    m_cacheManager->cancelPendingIo();
  }
  // We unconditionally call waitForFinished() rather than gating on
  // isRunning(): the latter is a plain atomic state read with no acquire
  // fence, so even when the worker has logically finished, the main thread
  // may not yet observe the worker's final QMutexLocker unlock inside the
  // CacheManager being destroyed (TSan flags this as a race). waitForFinished
  // takes the future's internal mutex, providing the happens-before edge
  // that publishes the worker's last writes. On a default-constructed or
  // already-finished future it is effectively a no-op.
  m_cacheInitFuture.waitForFinished();
}

void ApplicationManager::initialize(ApplicationContext *ctx) {
  // Sub-managers are owned solely by their std::unique_ptr members; the
  // QObject parent stays null so destruction is driven purely by
  // member order and Qt's children sweep can never double-delete one if
  // a future refactor reparents a manager elsewhere (Kartend-d70s,
  // re-attempted after Kartend-3v92 replaced NavigationManager's
  // parent()-based lifetime guards with the isAlive() helper).

  // 1. Create CacheManager and SessionManager instances (fast, no I/O)
  m_cacheManager = std::make_unique<CacheManager>();
  m_sessionManager = std::make_unique<SessionManager>(nullptr);

  // Register early so subsequent managers can read siblings through ctx.
  if (ctx) {
    ctx->managers.cacheManager = m_cacheManager.get();
    ctx->managers.sessionManager = m_sessionManager.get();
  }

  // 2. Initialize session synchronously (needed for selection restore at
  // startup). Session file is small (~160KB) and loads quickly.
  m_sessionManager->initialize();

  // 3. Defer cache metadata loading to background - the 51MB+ timestamps file
  // is only needed for artwork cache validation, not for initial UI display.
  // This avoids blocking startup for 2-3 seconds on large artwork caches.
  // The QFuture is retained so shutdown() can wait for completion before
  // destroying m_cacheManager (avoids use-after-free if the app exits
  // before this task finishes).
  CacheManager *cachePtr = m_cacheManager.get();
  m_cacheInitFuture = QtConcurrent::run([cachePtr]() { cachePtr->initialize(); });

  // 4. ArtworkManager. Kartend-davi: the cache pointer is read through
  // ApplicationContext during setupReferences (which mainwindow_setup.cpp
  // calls before initializeCache) — the old explicit CacheManager arg is
  // gone now.
  m_artworkManager = std::make_unique<ArtworkManager>(nullptr);
  if (ctx) {
    ctx->managers.artworkManager = m_artworkManager.get();
  }

  // 5. SettingsManager — reads SessionManager, ArtworkManager, CacheManager
  // through ctx (which is already populated above).
  m_settingsManager = g_settingsManagerFactory ? g_settingsManagerFactory(ctx, nullptr)
                                               : std::make_unique<SettingsManager>(ctx, nullptr);
  if (ctx) {
    ctx->managers.settingsManager = m_settingsManager.get();
  }

  // 6. DatabaseManager — reads SessionManager through ctx.
  m_databaseManager = g_databaseManagerFactory ? g_databaseManagerFactory(ctx, nullptr)
                                               : std::make_unique<DatabaseManager>(ctx, nullptr);
  if (ctx) {
    ctx->managers.databaseManager = m_databaseManager.get();
  }

  // 6b. PlaylistManager — opens its own main-thread connection
  // on the same media.db. Construction is fast; initialize() does the I/O and
  // is called by MainWindow before loadCollections() so synthesized playlist
  // CollectionConfigs can be appended to m_collections in the same setup pass.
  m_playlistManager = std::make_unique<PlaylistManager>(nullptr);

  // 7. ScrollManager — DatabaseManager is now resolved through ApplicationContext
  // during ScrollManager::setupReferences().
  m_scrollManager = std::make_unique<ScrollManager>(nullptr);

  // 8. DetailsPaneManager
  m_detailsPaneManager = std::make_unique<DetailsPaneManager>(nullptr);

  // 9. NavigationManager
  m_navigationManager = std::make_unique<NavigationManager>(nullptr);

  // 10. InteractionManager
  m_interactionManager = std::make_unique<InteractionManager>(nullptr);

  // 11. DetailPageManager. Standalone — only depends on the
  // overlay widget + DetailsPaneManager + DatabaseManager, all of which are
  // wired in MainWindow::setupManagerConnections via the setup struct.
  m_detailPageManager = std::make_unique<DetailPageManager>(nullptr);

  // 12. KartManager. Coordinates Kart import/export. Wired in
  // MainWindow::setupManagerConnections with SettingsManager + collection list
  // accessors via the setup struct.
  m_kartManager = std::make_unique<kart::KartManager>(nullptr);
}

void ApplicationManager::shutdown(const QList<CollectionConfig> &collections) {
  // 0a. Signal cancellation for any in-flight cache I/O BEFORE we wait for
  // the cache-init future to finish. CacheManager::initialize() runs on
  // QtConcurrent and parses a metadata file that can reach ~50 MB on
  // large libraries — without this early signal, closeEvent stalls for
  // multiple seconds while the worker drains an already-irrelevant load
  // (Kartend-bu5n). The cancellation flag is polled inside
  // CacheDiskStorage::readTimestampsInto so the worker bails on its next
  // 1024-entry sample. Safe to call before the cache manager has finished
  // initialising — cancelPendingIo only touches m_diskStorage and timer
  // state, both constructed in the CacheManager ctor.
  if (m_cacheManager) {
    m_cacheManager->cancelPendingIo();
  }

  // 0b. Wait for the deferred cache initialize() task to complete before
  // touching the cache - prevents use-after-free if shutdown begins while
  // CacheManager::initialize() is still parsing the metadata file.
  //
  // Same reasoning as in the destructor: unconditionally call
  // waitForFinished() so the future's internal mutex provides the
  // happens-before edge into the worker's last writes (the timestamp
  // hash inside CacheManager). isRunning() is a relaxed read and would
  // let snapshotTimestampsForShutdown() below race with the worker's
  // QHash::insert, surfacing as a memmove-vs-memmove TSan race in the
  // implicitly-shared QArrayData buffer.
  m_cacheInitFuture.waitForFinished();

  // 1. Cancel artwork loading first (non-blocking) to stop in-flight operations
  if (m_artworkManager) {
    m_artworkManager->cancelAllArtworkLoading();
  }

  // 2. Cleanup ScrollManager (release widgets back to pool)
  if (m_scrollManager) {
    m_scrollManager->cleanup();
  }

  // 3. Snapshot state for persistence BEFORE releasing GUI resources.
  // Take snapshots while managers are still valid, then write asynchronously.
  QByteArray sessionSnapshot;
  QHash<QString, qint64> cacheTimestamps;
  if (m_sessionManager) {
    sessionSnapshot = m_sessionManager->snapshotSessionJsonBytesForShutdown();
  }
  if (m_cacheManager) {
    cacheTimestamps = m_cacheManager->snapshotTimestampsForShutdown();
  }

  // 4. Save settings synchronously (fast INI write, typically <1ms)
  if (m_settingsManager) {
    m_settingsManager->saveCollections(collections);
  }

  // 5. Release GUI resources from cache (clears pixmaps from memory)
  if (m_cacheManager) {
    m_cacheManager->releaseGuiResources();
  }

  // 6. Cancel any in-flight cache I/O and wait for completion
  if (m_cacheManager) {
    m_cacheManager->cancelPendingIo();
  }

  // 7. Persist cache and session data synchronously. The prior implementation
  // dispatched these via QtConcurrent::run and main.cpp slept 200 ms before
  // std::_Exit — on a slow disk or a large session JSON the write was
  // routinely dropped without any indication (Kartend-x2by). The snapshot
  // payloads are bytes-on-the-stack already, so blocking the shutdown thread
  // for the actual write is the only way to guarantee durability before
  // process exit.
  CacheManager::saveTimestampsSnapshotToDiskForShutdown(cacheTimestamps);
  SessionManager::saveSessionBytesToDiskForShutdown(sessionSnapshot);
}

ArtworkManager *ApplicationManager::getArtworkManager() const {
  return m_artworkManager.get();
}

CacheManager *ApplicationManager::getCacheManager() const {
  return m_cacheManager.get();
}

IDatabaseManager *ApplicationManager::getDatabaseManager() const {
  return m_databaseManager.get();
}

DetailPageManager *ApplicationManager::getDetailPageManager() const {
  return m_detailPageManager.get();
}

InteractionManager *ApplicationManager::getInteractionManager() const {
  return m_interactionManager.get();
}

NavigationManager *ApplicationManager::getNavigationManager() const {
  return m_navigationManager.get();
}

PlaylistManager *ApplicationManager::getPlaylistManager() const {
  return m_playlistManager.get();
}

ScrollManager *ApplicationManager::getScrollManager() const {
  return m_scrollManager.get();
}

SessionManager *ApplicationManager::getSessionManager() const {
  return m_sessionManager.get();
}

ISettingsManager *ApplicationManager::getSettingsManager() const {
  return m_settingsManager.get();
}

DetailsPaneManager *ApplicationManager::getDetailsPaneManager() const {
  return m_detailsPaneManager.get();
}

kart::KartManager *ApplicationManager::getKartManager() const {
  return m_kartManager.get();
}
