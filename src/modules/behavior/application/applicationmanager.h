#ifndef APPLICATIONMANAGER_H
#define APPLICATIONMANAGER_H

#include <memory>
#include <QFuture>
#include <QObject>

class ArtworkManager;
class CacheManager;
class DatabaseManager;
class DetailPageManager;
class InteractionManager;
class NavigationManager;
class PlaylistManager;
class ScrollManager;
class SessionManager;
class SettingsManager;
class DetailsPaneManager;
class MainWindow;

namespace kart {
class KartManager;
}

struct CollectionConfig;
struct ApplicationContext;
template <typename T> class QList;

class ApplicationManager : public QObject {
  Q_OBJECT

public:
  explicit ApplicationManager(QObject *parent = nullptr);
  ~ApplicationManager() override;

  /// Constructs all managers and registers each into ctx as it is created so
  /// later managers (which read siblings through ctx) see a fully-populated
  /// context when their constructors run.
  void initialize(ApplicationContext *ctx);
  void shutdown(const QList<CollectionConfig> &collections);

  // Getters
  [[nodiscard]] ArtworkManager *getArtworkManager() const;
  [[nodiscard]] CacheManager *getCacheManager() const;
  [[nodiscard]] DatabaseManager *getDatabaseManager() const;
  [[nodiscard]] DetailPageManager *getDetailPageManager() const;
  [[nodiscard]] InteractionManager *getInteractionManager() const;
  [[nodiscard]] NavigationManager *getNavigationManager() const;
  [[nodiscard]] PlaylistManager *getPlaylistManager() const;
  [[nodiscard]] ScrollManager *getScrollManager() const;
  [[nodiscard]] SessionManager *getSessionManager() const;
  [[nodiscard]] SettingsManager *getSettingsManager() const;
  [[nodiscard]] DetailsPaneManager *getDetailsPaneManager() const;
  [[nodiscard]] kart::KartManager *getKartManager() const;

private:
  // Order of declaration determines order of destruction (reverse).
  // Dependencies must be declared BEFORE dependents so they are destroyed
  // AFTER.

  std::unique_ptr<CacheManager> m_cacheManager;
  std::unique_ptr<SessionManager> m_sessionManager;
  std::unique_ptr<ArtworkManager> m_artworkManager;
  std::unique_ptr<DatabaseManager> m_databaseManager;
  // PlaylistManager owns its own SQLite connection on the main thread; declared
  // after DatabaseManager so it tears down first (Qt destroys SQL connections
  // in reverse-declaration order).
  std::unique_ptr<PlaylistManager> m_playlistManager;
  std::unique_ptr<SettingsManager> m_settingsManager;
  std::unique_ptr<ScrollManager> m_scrollManager;
  std::unique_ptr<DetailsPaneManager> m_detailsPaneManager;
  std::unique_ptr<NavigationManager> m_navigationManager;
  std::unique_ptr<InteractionManager> m_interactionManager;
  //. DetailPageManager has no destructors-during-shutdown
  // dependencies on other managers; declared last so it tears down first
  // (its only owned member is the QObject parent link).
  std::unique_ptr<DetailPageManager> m_detailPageManager;

  //. KartManager coordinates Kart import/export and depends only
  // on SettingsManager (for collection registration). Has no shutdown ordering
  // requirements beyond being a QObject child.
  std::unique_ptr<kart::KartManager> m_kartManager;

  // Tracks the background CacheManager::initialize() task so shutdown can
  // wait for it before destroying the cache (prevents use-after-free of
  // m_cacheManager when the app exits before initialize() completes).
  QFuture<void> m_cacheInitFuture;
};

#endif // APPLICATIONMANAGER_H
