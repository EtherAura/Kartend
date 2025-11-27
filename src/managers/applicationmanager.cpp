// Owns and coordinates all manager lifecycles with controlled destruction order.
#include "applicationmanager.h"

#include "artworkmanager.h"
#include "cachemanager.h"
#include "databasemanager.h"
#include "interactionmanager.h"
#include "mainwindow.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "sidebarmanager.h"

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
