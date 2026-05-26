#ifndef APPLICATIONMANAGER_H
#define APPLICATIONMANAGER_H

#include <functional>
#include <memory>
#include <QFuture>
#include <QObject>

class ArtworkManager;
class CacheManager;
class IDatabaseManager;
class DetailPageManager;
class InteractionManager;
class NavigationManager;
class PlaylistManager;
class ScrollManager;
class SessionManager;
class ISettingsManager;
class DetailsPaneManager;
class MainWindow;

namespace kart {
class KartManager;
}

struct CollectionConfig;
#include "applicationcontext_fwd.h"
template <typename T> class QList;

class ApplicationManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ApplicationManager)

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
  [[nodiscard]] IDatabaseManager *getDatabaseManager() const;
  [[nodiscard]] DetailPageManager *getDetailPageManager() const;
  [[nodiscard]] InteractionManager *getInteractionManager() const;
  [[nodiscard]] NavigationManager *getNavigationManager() const;
  [[nodiscard]] PlaylistManager *getPlaylistManager() const;
  [[nodiscard]] ScrollManager *getScrollManager() const;
  [[nodiscard]] SessionManager *getSessionManager() const;
  [[nodiscard]] ISettingsManager *getSettingsManager() const;
  [[nodiscard]] DetailsPaneManager *getDetailsPaneManager() const;
  [[nodiscard]] kart::KartManager *getKartManager() const;

  // ───────────────────────────────────────────────────────────────────────────
  // Test injection seam: factory hooks for swappable manager construction.
  //
  // initialize() consults these before falling back to building the real
  // SQLite-backed DatabaseManager / QSettings-backed SettingsManager. Tests
  // install hooks before the MainWindow that owns this ApplicationManager is
  // constructed; production code leaves them unset.
  //
  // Process-wide static state is intentional — the integration test runner
  // executes test groups serially within one QApplication, and MainWindow's
  // constructor does not expose a per-instance injection point. Callers must
  // reset hooks (pass nullptr) at fixture teardown to avoid leaking mocks
  // into later tests.
  // ───────────────────────────────────────────────────────────────────────────
  using DatabaseManagerFactory =
      std::function<std::unique_ptr<IDatabaseManager>(const ApplicationContext *, QObject *)>;
  using SettingsManagerFactory =
      std::function<std::unique_ptr<ISettingsManager>(const ApplicationContext *, QObject *)>;

  static void setDatabaseManagerFactory(DatabaseManagerFactory factory);
  static void setSettingsManagerFactory(SettingsManagerFactory factory);

private:
  // Order of declaration determines order of destruction (reverse).
  // Dependencies must be declared BEFORE dependents so they are destroyed
  // AFTER.

  std::unique_ptr<CacheManager> m_cacheManager;
  std::unique_ptr<SessionManager> m_sessionManager;
  std::unique_ptr<ArtworkManager> m_artworkManager;
  std::unique_ptr<IDatabaseManager> m_databaseManager;
  // PlaylistManager owns its own SQLite connection on the main thread; declared
  // after DatabaseManager so it tears down first (Qt destroys SQL connections
  // in reverse-declaration order).
  std::unique_ptr<PlaylistManager> m_playlistManager;
  std::unique_ptr<ISettingsManager> m_settingsManager;
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
