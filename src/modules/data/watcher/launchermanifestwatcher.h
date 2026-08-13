#ifndef KARTEND_MODULES_DATA_WATCHER_LAUNCHERMANIFESTWATCHER_H
#define KARTEND_MODULES_DATA_WATCHER_LAUNCHERMANIFESTWATCHER_H

#include <functional>
#include <QObject>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QFileSystemWatcher;
class QTimer;
QT_END_NAMESPACE

/// Watches the directories a launcher writes its manifests into, so a game
/// installed while Kartend is running turns up without waiting for the next
/// startup sync (Kartend-5vuqy).
///
/// This is deliberately NOT the per-collection CollectionFilesystemWatcher.
/// That one walks a collection's own media tree; these paths are external and
/// belong to another application — Steam's steamapps/, Heroic's store_cache/,
/// the flatpak exports roots — and they churn on a completely different
/// rhythm.
///
/// TWO THINGS DRIVE THE DESIGN, both learned from how Steam behaves:
///
///  1. DEBOUNCE GENEROUSLY. A Steam download rewrites its appmanifest_*.acf
///     repeatedly — on start, on progress, on completion — so an eager
///     watcher would kick off a full re-sync of every launcher collection
///     several times per install. The default interval is measured in seconds,
///     not milliseconds, because being late costs nothing here: the sync is
///     idempotent and the user is not waiting on it.
///
///  2. RE-ADD PATHS THAT VANISH. QFileSystemWatcher drops a path the moment it
///     stops existing and never takes it back on its own. Launchers delete and
///     recreate these directories routinely (an uninstall can empty a library
///     folder), so every debounce tick reconciles the desired set against what
///     is actually registered. Without that the watch dies silently after the
///     first uninstall and everything still LOOKS wired up.
class LauncherManifestWatcher : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(LauncherManifestWatcher)

public:
  /// Called on this object's thread once a burst of changes has settled.
  using SyncCallback = std::function<void()>;

  explicit LauncherManifestWatcher(QObject *parent = nullptr);
  ~LauncherManifestWatcher() override;

  void setSyncCallback(SyncCallback callback);

  /// Resolve and watch the directories for each source id, skipping sources
  /// that are not installed (their path list comes back empty).
  void configureForSources(const QStringList &sourceIds);

  /// Watch exactly these directories. configureForSources() funnels here;
  /// tests use it directly to avoid depending on what is installed.
  void setWatchedDirectories(const QStringList &directories);

  /// What is currently registered with the underlying watcher — which can be
  /// a subset of the requested set when a directory does not exist yet.
  [[nodiscard]] QStringList activeDirectories() const;

  /// Everything setWatchedDirectories() was asked to watch, existing or not.
  [[nodiscard]] QStringList requestedDirectories() const { return m_requested; }

  void setDebounceInterval(int milliseconds);
  [[nodiscard]] int debounceInterval() const;

  /// Debounce and fire as though a watched directory had changed. Exists for
  /// tests, which cannot rely on filesystem event delivery timing.
  void notifyChangeForTesting(const QString &path) { onDirectoryChanged(path); }

private:
  void onDirectoryChanged(const QString &path);
  void onDebounceElapsed();
  /// Register anything requested that exists and is not already watched.
  void reconcileWatchSet();

  QFileSystemWatcher *m_watcher = nullptr;
  QTimer *m_debounce = nullptr;
  SyncCallback m_callback;
  QStringList m_requested;
};

#endif
