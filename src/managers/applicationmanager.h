#ifndef APPLICATIONMANAGER_H
#define APPLICATIONMANAGER_H

#include <QObject>
#include <memory>

class ArtworkManager;
class CacheManager;
class DatabaseManager;
class InteractionManager;
class NavigationManager;
class ScrollManager;
class SessionManager;
class SettingsManager;
class SidebarManager;
class MainWindow;

class ApplicationManager : public QObject {
  Q_OBJECT

public:
  explicit ApplicationManager(QObject *parent = nullptr);
  ~ApplicationManager() override;

  void initialize();

  // Getters
  ArtworkManager *getArtworkManager() const;
  CacheManager *getCacheManager() const;
  DatabaseManager *getDatabaseManager() const;
  InteractionManager *getInteractionManager() const;
  NavigationManager *getNavigationManager() const;
  ScrollManager *getScrollManager() const;
  SessionManager *getSessionManager() const;
  SettingsManager *getSettingsManager() const;
  SidebarManager *getSidebarManager() const;

private:
  // Order of declaration determines order of destruction (reverse).
  // Dependencies must be declared BEFORE dependents so they are destroyed AFTER.
  
  std::unique_ptr<CacheManager> m_cacheManager;
  std::unique_ptr<SessionManager> m_sessionManager;
  std::unique_ptr<ArtworkManager> m_artworkManager;
  std::unique_ptr<DatabaseManager> m_databaseManager;
  std::unique_ptr<SettingsManager> m_settingsManager;
  std::unique_ptr<ScrollManager> m_scrollManager;
  std::unique_ptr<SidebarManager> m_sidebarManager;
  std::unique_ptr<NavigationManager> m_navigationManager;
  std::unique_ptr<InteractionManager> m_interactionManager;
};

#endif // APPLICATIONMANAGER_H
