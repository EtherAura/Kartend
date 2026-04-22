#ifndef APPLICATIONMANAGER_H
#define APPLICATIONMANAGER_H

#include <QFuture>
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

struct CollectionConfig;
template <typename T> class QList;

class ApplicationManager : public QObject {
  Q_OBJECT

public:
  explicit ApplicationManager(QObject *parent = nullptr);
  ~ApplicationManager() override;

  void initialize();
  void shutdown(const QList<CollectionConfig> &collections);

  // Getters
  [[nodiscard]] ArtworkManager *getArtworkManager() const;
  [[nodiscard]] CacheManager *getCacheManager() const;
  [[nodiscard]] DatabaseManager *getDatabaseManager() const;
  [[nodiscard]] InteractionManager *getInteractionManager() const;
  [[nodiscard]] NavigationManager *getNavigationManager() const;
  [[nodiscard]] ScrollManager *getScrollManager() const;
  [[nodiscard]] SessionManager *getSessionManager() const;
  [[nodiscard]] SettingsManager *getSettingsManager() const;
  [[nodiscard]] SidebarManager *getSidebarManager() const;

private:
  // Order of declaration determines order of destruction (reverse).
  // Dependencies must be declared BEFORE dependents so they are destroyed
  // AFTER.

  std::unique_ptr<CacheManager> m_cacheManager;
  std::unique_ptr<SessionManager> m_sessionManager;
  std::unique_ptr<ArtworkManager> m_artworkManager;
  std::unique_ptr<DatabaseManager> m_databaseManager;
  std::unique_ptr<SettingsManager> m_settingsManager;
  std::unique_ptr<ScrollManager> m_scrollManager;
  std::unique_ptr<SidebarManager> m_sidebarManager;
  std::unique_ptr<NavigationManager> m_navigationManager;
  std::unique_ptr<InteractionManager> m_interactionManager;

  // Tracks the background CacheManager::initialize() task so shutdown can
  // wait for it before destroying the cache (prevents use-after-free of
  // m_cacheManager when the app exits before initialize() completes).
  QFuture<void> m_cacheInitFuture;
};

#endif // APPLICATIONMANAGER_H
