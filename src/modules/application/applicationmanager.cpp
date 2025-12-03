// Owns and coordinates all manager lifecycles with controlled destruction order.
#include "applicationmanager.h"

#include "artworkmanager.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "interactionmanager.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "sidebarmanager.h"

#include <QThreadPool>

#ifdef KARTEND_DEBUG_LOGGING
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcApplicationManager, "kartend.applicationmanager")
#define debugLog(msg) qCDebug(lcApplicationManager) << msg
#else
#define debugLog(msg) do {} while(0)
#endif

ApplicationManager::ApplicationManager(QObject *parent) : QObject(parent) {}

ApplicationManager::~ApplicationManager() = default;

void ApplicationManager::initialize() {
  // 1. CacheManager
  m_cacheManager = std::make_unique<CacheManager>();
  m_cacheManager->initialize();

  // 2. SessionManager
  m_sessionManager = std::make_unique<SessionManager>(this);

  // 3. ArtworkManager (needs CacheManager)
  m_artworkManager =
      std::make_unique<ArtworkManager>(m_cacheManager.get(), this);

  // 4. SettingsManager (needs SessionManager, ArtworkManager, CacheManager)
  m_settingsManager = std::make_unique<SettingsManager>(
      m_sessionManager.get(), m_artworkManager.get(), m_cacheManager.get(), this);

  // 5. DatabaseManager (needs SessionManager)
  m_databaseManager =
      std::make_unique<DatabaseManager>(m_sessionManager.get(), this);

  // 6. ScrollManager
  m_scrollManager = std::make_unique<ScrollManager>(this);
  m_scrollManager->setDatabaseManager(m_databaseManager.get());

  // 7. SidebarManager
  m_sidebarManager = std::make_unique<SidebarManager>(this);

  // 8. NavigationManager
  m_navigationManager = std::make_unique<NavigationManager>(this);

  // 9. InteractionManager
  m_interactionManager = std::make_unique<InteractionManager>(this);
}

void ApplicationManager::shutdown(const QList<CollectionConfig> &collections) {
  // 1. Cancel artwork loading first (non-blocking) to stop in-flight operations
  if (m_artworkManager) {
    m_artworkManager->cancelAllArtworkLoading();
  }

  // 2. Cleanup ScrollManager (release widgets back to pool)
  if (m_scrollManager) {
    m_scrollManager->cleanup();
  }

  // 3. Save settings synchronously (fast INI write)
  if (m_settingsManager) {
    m_settingsManager->saveCollections(collections);
  }

  // 4. Release GUI resources from cache (clears pixmaps from memory)
  if (m_cacheManager) {
    m_cacheManager->releaseGuiResources();
  }

  // 5. Persist cache and session data to disk in background threads
  //    These operations can take time but don't block the UI
  if (m_cacheManager) {
    QThreadPool::globalInstance()->start([cache = m_cacheManager.get()]() {
      cache->saveToDiskForShutdown();
    });
  }

  if (m_sessionManager) {
    QThreadPool::globalInstance()->start([session = m_sessionManager.get()]() {
      session->saveToDiskForShutdown();
    });
  }
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

InteractionManager *ApplicationManager::getInteractionManager() const {
  return m_interactionManager.get();
}

NavigationManager *ApplicationManager::getNavigationManager() const {
  return m_navigationManager.get();
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

SidebarManager *ApplicationManager::getSidebarManager() const {
  return m_sidebarManager.get();
}
