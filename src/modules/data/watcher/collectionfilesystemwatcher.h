#ifndef KARTEND_MODULES_DATA_WATCHER_COLLECTIONFILESYSTEMWATCHER_H
#define KARTEND_MODULES_DATA_WATCHER_COLLECTIONFILESYSTEMWATCHER_H

#include <functional>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "collection/collectionconfig.h"

QT_BEGIN_NAMESPACE
class QFileSystemWatcher;
class QTimer;
QT_END_NAMESPACE

/// Watches every collection whose `watchFilesystem` flag is true and triggers
/// a debounced rescan whenever a watched directory fires a change event.
///
/// QFileSystemWatcher is not recursive on Linux/macOS, so configure() walks
/// each collection's media directory once at registration time and registers
/// every subdirectory it finds. When a `directoryChanged` event fires, the
/// per-collection debounce timer is (re)started; when it fires, the
/// collection's tree is re-walked on a QtConcurrent worker and the watch set
/// reconciled (on this object's thread) so newly-created subdirectories are
/// picked up automatically — one walk per event burst, off the GUI thread.
///
/// The rescan callback runs on the GUI thread (the watcher's QObject lives on
/// it), after the reconcile walk for that burst has been applied. Callers are
/// expected to forward the collectionIndex argument to
/// NavigationManager::forceRescanCollection or equivalent.
class CollectionFilesystemWatcher : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CollectionFilesystemWatcher)

public:
  using RescanCallback = std::function<void(int collectionIndex)>;

  explicit CollectionFilesystemWatcher(QObject *parent = nullptr);
  ~CollectionFilesystemWatcher() override;

  /// Installs the callback fired (on the GUI thread) when a watched
  /// directory's change event passes the debounce window.
  void setRescanCallback(RescanCallback callback);

  /// Reconciles the watch set with the supplied collections list. Collections
  /// whose `watchFilesystem` flag is false are unwatched; the remaining
  /// collections have their media-directory trees walked and (re)registered.
  /// Safe to call repeatedly — used at startup, after settings save, and on
  /// collection add/remove.
  void configure(const QList<CollectionConfig> &collections);

  /// Debounce window in milliseconds before a watched directory's change is
  /// forwarded to the rescan callback. Exposed for tests; production code
  /// uses the default.
  void setDebounceMs(int ms);

  /// Returns the absolute paths currently being watched, sorted. For tests.
  [[nodiscard]] QStringList watchedPaths() const;

  /// Pure helper — enumerates every readable subdirectory of @p rootPath
  /// (including @p rootPath itself) suitable for installing into a
  /// QFileSystemWatcher. Symlink loops are short-circuited via a canonical-
  /// path seen-set so the walk always terminates. Unreadable / inaccessible
  /// directories are skipped silently. Returned list is sorted ascending.
  /// Exposed for unit tests.
  [[nodiscard]] static QStringList enumerateWatchableSubdirs(const QString &rootPath);

private:
  struct WatchEntry {
    int collectionIndex = -1;
    QString rootPath;
    QSet<QString> watchedSubdirs;
  };

  void onDirectoryChanged(const QString &path);
  void startReconcileWalk(int collectionIndex);
  void onWalkFinished(int collectionIndex, int generation, const QStringList &freshDirs);
  void emitRescan(int collectionIndex);

  QFileSystemWatcher *m_watcher = nullptr;
  QHash<int, WatchEntry> m_entries;
  QHash<QString, int> m_pathToCollection;
  QHash<int, QTimer *> m_debounceTimers;
  RescanCallback m_callback;
  int m_debounceMs = 2000;
  // Bumped by configure(); in-flight reconcile walks carry the generation
  // they were launched under and are discarded when it no longer matches.
  int m_configGeneration = 0;
  // Collections with a reconcile walk currently running on the worker pool,
  // and those that received another debounce expiry mid-walk and need the
  // walk re-run when the current one lands (coalescing).
  QSet<int> m_walksInFlight;
  QSet<int> m_walkRerunPending;
};

#endif // KARTEND_MODULES_DATA_WATCHER_COLLECTIONFILESYSTEMWATCHER_H
