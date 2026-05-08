// Owns and coordinates all manager lifecycles with controlled destruction
// order.
#include "applicationmanager.h"

#include "artworkmanager.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "detailpagemanager.h"
#include "detailspanemanager.h"
#include "interactionmanager.h"
#include "kartmanager.h"
#include "navigationmanager.h"
#include "playlistmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"

#include <QtConcurrent>

ApplicationManager::ApplicationManager(QObject *parent) : QObject(parent) {}

ApplicationManager::~ApplicationManager() {
  // Defensive: ensure the deferred cache initialize() task is finished before
  // m_cacheManager is destroyed. shutdown() handles this normally; this guard
  // covers paths where shutdown() is skipped (e.g. abnormal teardown).
  if (m_cacheInitFuture.isRunning()) {
    m_cacheInitFuture.waitForFinished();
  }
}

void ApplicationManager::initialize() {
  // 1. Create CacheManager and SessionManager instances (fast, no I/O)
  m_cacheManager = std::make_unique<CacheManager>();
  m_sessionManager = std::make_unique<SessionManager>(this);

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

  // 4. ArtworkManager (needs CacheManager - but can work without timestamps
  // loaded)
  m_artworkManager = std::make_unique<ArtworkManager>(m_cacheManager.get(), this);

  // 5. SettingsManager (needs SessionManager, ArtworkManager, CacheManager)
  m_settingsManager = std::make_unique<SettingsManager>(
      m_sessionManager.get(), m_artworkManager.get(), m_cacheManager.get(), this);

  // 6. DatabaseManager (needs SessionManager)
  m_databaseManager = std::make_unique<DatabaseManager>(m_sessionManager.get(), this);

  // 6b. PlaylistManager — opens its own main-thread connection
  // on the same media.db. Construction is fast; initialize() does the I/O and
  // is called by MainWindow before loadCollections() so synthesized playlist
  // CollectionConfigs can be appended to m_collections in the same setup pass.
  m_playlistManager = std::make_unique<PlaylistManager>(this);

  // 7. ScrollManager
  m_scrollManager = std::make_unique<ScrollManager>(this);
  m_scrollManager->setDatabaseManager(m_databaseManager.get());

  // 8. DetailsPaneManager
  m_detailsPaneManager = std::make_unique<DetailsPaneManager>(this);

  // 9. NavigationManager
  m_navigationManager = std::make_unique<NavigationManager>(this);

  // 10. InteractionManager
  m_interactionManager = std::make_unique<InteractionManager>(this);

  // 11. DetailPageManager. Standalone — only depends on the
  // overlay widget + DetailsPaneManager + DatabaseManager, all of which are
  // wired in MainWindow::setupManagerConnections via the setup struct.
  m_detailPageManager = std::make_unique<DetailPageManager>(this);

  // 12. KartManager. Coordinates Kart import/export. Wired in
  // MainWindow::setupManagerConnections with SettingsManager + collection list
  // accessors via the setup struct.
  m_kartManager = std::make_unique<kart::KartManager>(this);
}

void ApplicationManager::shutdown(const QList<CollectionConfig> &collections) {
  // 0. Wait for the deferred cache initialize() task to complete before
  // touching the cache - prevents use-after-free if shutdown begins while
  // CacheManager::initialize() is still parsing the metadata file.
  if (m_cacheInitFuture.isRunning()) {
    m_cacheInitFuture.waitForFinished();
  }

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

  // 7. Persist cache and session data in parallel.
  // Fire and forget - the writes are fast (just JSON) and the process
  // will stay alive long enough for them to complete.
  // Using snapshots avoids needing the manager instances during write.
  (void)QtConcurrent::run([cacheTimestamps]() {
    CacheManager::saveTimestampsSnapshotToDiskForShutdown(cacheTimestamps);
  });

  (void)QtConcurrent::run(
      [sessionSnapshot]() { SessionManager::saveSessionBytesToDiskForShutdown(sessionSnapshot); });

  // DON'T wait for saves - they complete quickly and we need fast shutdown.
  // The global thread pool will finish these before process exit.
}

ArtworkManager *ApplicationManager::getArtworkManager() const {
  return m_artworkManager.get();
}

CacheManager *ApplicationManager::getCacheManager() const {
  return m_cacheManager.get();
}

DatabaseManager *ApplicationManager::getDatabaseManager() const {
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

SettingsManager *ApplicationManager::getSettingsManager() const {
  return m_settingsManager.get();
}

DetailsPaneManager *ApplicationManager::getDetailsPaneManager() const {
  return m_detailsPaneManager.get();
}

kart::KartManager *ApplicationManager::getKartManager() const {
  return m_kartManager.get();
}
