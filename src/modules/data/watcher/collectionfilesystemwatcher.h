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
/// QFileSystemWatcher is not recursive on Linux/macOS, so configure()
/// registers each collection's media-directory root synchronously (cheap) and
/// dispatches a per-collection tree walk to a QtConcurrent worker that
/// registers every subdirectory when it lands — the BFS stat of a large or
/// network-mounted library never runs on the GUI thread. When a
/// `directoryChanged` event fires, the per-collection debounce timer is
/// (re)started; when it fires, the collection's tree is re-walked on the same
/// worker mechanism and the watch set reconciled (on this object's thread) so
/// newly-created subdirectories are picked up automatically — one walk per
/// event burst, off the GUI thread.
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
  /// collections get their media-directory root registered synchronously and
  /// the rest of the tree walked + registered asynchronously on a QtConcurrent
  /// worker (guarded by the config generation, so a superseding configure()
  /// drops any in-flight walk results when they land). Safe to call
  /// repeatedly — used at startup, after settings save, and on collection
  /// add/remove.
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
  /// @p emitRescanWhenDone distinguishes debounce-driven walks (a filesystem
  /// event is pending, so the rescan callback must fire once the reconciled
  /// watch set is applied) from configure()-driven registration walks (no
  /// event happened — firing the callback would force a spurious rescan of
  /// every watched collection at startup).
  void startReconcileWalk(int collectionIndex, bool emitRescanWhenDone);
  void onWalkFinished(int collectionIndex, int generation, bool emitRescanWhenDone,
                      const QStringList &freshDirs);
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
