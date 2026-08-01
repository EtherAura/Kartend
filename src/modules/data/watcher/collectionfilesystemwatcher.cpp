#include "collectionfilesystemwatcher.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QLoggingCategory>
#include <QQueue>
#include <QtConcurrent/QtConcurrentRun>
#include <QTimer>

#include "settingsutils.h"

Q_LOGGING_CATEGORY(lcCollectionWatcher, "kartend.collectionwatcher")

namespace {
// addPaths returns the subset it could NOT register (e.g. inotify watch
// exhaustion). Directories on that list get no change events — mutations under
// them silently stop triggering rescans — so leave a loud breadcrumb. Returns
// the failed subset so callers can keep their bookkeeping honest (a dir we
// don't actually watch must not be recorded as watched, or a later reconcile
// would never retry it).
QStringList addPathsLogged(QFileSystemWatcher *watcher, const QStringList &dirs,
                           const QString &rootPath) {
  const QStringList failed = watcher->addPaths(dirs);
  if (!failed.isEmpty()) {
    qCWarning(lcCollectionWatcher)
        << "Failed to watch" << failed.size() << "of" << dirs.size() << "directories under"
        << rootPath << "(watch descriptor limit?); changes there will not trigger rescans."
        << "First failed paths:" << failed.mid(0, 5);
  }
  return failed;
}
} // namespace

CollectionFilesystemWatcher::CollectionFilesystemWatcher(QObject *parent) : QObject(parent) {
  m_watcher = new QFileSystemWatcher(this);
  connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
          &CollectionFilesystemWatcher::onDirectoryChanged);
}

CollectionFilesystemWatcher::~CollectionFilesystemWatcher() = default;

void CollectionFilesystemWatcher::setRescanCallback(RescanCallback callback) {
  m_callback = std::move(callback);
}

void CollectionFilesystemWatcher::setDebounceMs(int ms) {
  m_debounceMs = ms > 0 ? ms : 0;
}

QStringList CollectionFilesystemWatcher::watchedPaths() const {
  QStringList paths = m_watcher ? m_watcher->directories() : QStringList{};
  std::sort(paths.begin(), paths.end());
  return paths;
}

QStringList CollectionFilesystemWatcher::enumerateWatchableSubdirs(const QString &rootPath) {
  QStringList out;
  if (rootPath.trimmed().isEmpty()) return out;
  const QFileInfo rootInfo(rootPath);
  if (!rootInfo.exists() || !rootInfo.isDir() || !rootInfo.isReadable()) {
    return out;
  }

  QSet<QString> seen;
  QQueue<QString> pending;
  pending.enqueue(rootInfo.absoluteFilePath());

  while (!pending.isEmpty()) {
    const QString current = pending.dequeue();
    const QFileInfo info(current);
    if (!info.exists() || !info.isDir() || !info.isReadable()) continue;

    // Canonical path collapses symlink loops; fall back to absolute when
    // canonicalFilePath() returns empty (broken symlink, restricted dir).
    const QString canonical = info.canonicalFilePath();
    const QString key = canonical.isEmpty() ? info.absoluteFilePath() : canonical;
    if (seen.contains(key)) continue;
    seen.insert(key);

    out.append(info.absoluteFilePath());

    QDir dir(info.absoluteFilePath());
    const QFileInfoList children =
        dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo &child : children) {
      pending.enqueue(child.absoluteFilePath());
    }
  }

  std::sort(out.begin(), out.end());
  return out;
}

void CollectionFilesystemWatcher::configure(const QList<CollectionConfig> &collections) {
  // Tear down the existing watch set entirely; re-walk reconciles below. This
  // is simpler than diffing watched paths against the new desired set and
  // avoids subtle bugs when a collection's mediaDirectory was renamed.
  if (m_watcher) {
    const QStringList existing = m_watcher->directories();
    if (!existing.isEmpty()) m_watcher->removePaths(existing);
    const QStringList files = m_watcher->files();
    if (!files.isEmpty()) m_watcher->removePaths(files);
  }
  m_entries.clear();
  m_pathToCollection.clear();
  // Cancel any in-flight debounce timers — their bound collection index may
  // no longer match the new layout.
  for (auto it = m_debounceTimers.begin(); it != m_debounceTimers.end(); ++it) {
    if (it.value()) it.value()->stop();
  }
  // Invalidate in-flight reconcile walks the same way: their results describe
  // the layout being torn down and must be dropped when they land.
  ++m_configGeneration;
  m_walksInFlight.clear();
  m_walkRerunPending.clear();

  for (int i = 0; i < collections.size(); ++i) {
    const CollectionConfig &cfg = collections.at(i);
    if (!cfg.watchFilesystem) continue;
    if (cfg.isPlaylist) continue;
    const QString mediaDir = SettingsUtils::expandConfigVariables(cfg.mediaDirectory, cfg.name);
    if (mediaDir.trimmed().isEmpty()) continue;
    const QFileInfo rootInfo(mediaDir);
    if (!rootInfo.exists() || !rootInfo.isDir() || !rootInfo.isReadable()) continue;

    // Register only the root synchronously (one stat + one watch — cheap), so
    // change events at the top of the tree are live immediately. The rest of
    // the tree is a BFS stat of every subdirectory — too slow for the GUI
    // thread on a large or network-mounted library — so it rides the same
    // async reconcile mechanism the debounce path uses: the walk runs on the
    // worker pool, its result is applied on this thread when the future
    // lands, and the generation guard above drops it if a newer configure()
    // superseded this layout in the meantime. No rescan is emitted for these
    // registration walks — no filesystem event happened.
    WatchEntry entry;
    entry.collectionIndex = i;
    entry.rootPath = rootInfo.absoluteFilePath();
    if (m_watcher) {
      const QStringList failed = addPathsLogged(m_watcher, {entry.rootPath}, entry.rootPath);
      if (failed.isEmpty()) {
        entry.watchedSubdirs.insert(entry.rootPath);
        m_pathToCollection.insert(entry.rootPath, i);
      }
    }
    m_entries.insert(i, entry);
    startReconcileWalk(i, /*emitRescanWhenDone=*/false);
  }
}

void CollectionFilesystemWatcher::onDirectoryChanged(const QString &path) {
  const auto it = m_pathToCollection.constFind(path);
  if (it == m_pathToCollection.cend()) return;
  const int collectionIndex = it.value();

  // Debounce the reconcile walk + rescan: bursts of filesystem events (e.g.
  // copying a folder) should coalesce into a single tree walk and a single
  // rescan, instead of one O(all-subdirs) walk per mutation on the GUI thread.
  // QTimer::singleShot won't work because we need to reset the deadline on
  // each new event for the same collection.
  QTimer *&timer = m_debounceTimers[collectionIndex];
  if (!timer) {
    timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, collectionIndex]() {
      startReconcileWalk(collectionIndex, /*emitRescanWhenDone=*/true);
    });
  }
  timer->start(m_debounceMs);
}

void CollectionFilesystemWatcher::startReconcileWalk(int collectionIndex,
                                                     bool emitRescanWhenDone) {
  const auto entryIt = m_entries.constFind(collectionIndex);
  if (entryIt == m_entries.cend()) {
    // No watch bookkeeping for this collection (shouldn't happen while a
    // debounce timer for it is live) — still honor the pending rescan.
    if (emitRescanWhenDone) emitRescan(collectionIndex);
    return;
  }
  if (m_walksInFlight.contains(collectionIndex)) {
    // A walk for this collection is already on the pool. Coalesce: re-run it
    // once the current one lands so events that arrived mid-walk still get
    // their reconcile + rescan, without ever stacking concurrent walks.
    m_walkRerunPending.insert(collectionIndex);
    return;
  }
  m_walksInFlight.insert(collectionIndex);

  // The re-walk is needed because directoryChanged only reports the mutated
  // directory, not the resulting tree — but a BFS stat of every subdirectory
  // is too slow for the GUI thread on a large or network-mounted library, so
  // it runs on the global pool. Only the pure static helper (capturing a copy
  // of rootPath) runs there; QFileSystemWatcher is touched exclusively from
  // this object's (GUI) thread when the future lands. The QFutureWatcher is
  // parented to `this`, so destruction mid-walk just drops the result.
  const int generation = m_configGeneration;
  const QString rootPath = entryIt->rootPath;
  auto *walkWatcher = new QFutureWatcher<QStringList>(this);
  connect(walkWatcher, &QFutureWatcher<QStringList>::finished, this,
          [this, walkWatcher, collectionIndex, generation, emitRescanWhenDone]() {
            walkWatcher->deleteLater();
            onWalkFinished(collectionIndex, generation, emitRescanWhenDone,
                           walkWatcher->result());
          });
  walkWatcher->setFuture(
      QtConcurrent::run(&CollectionFilesystemWatcher::enumerateWatchableSubdirs, rootPath));
}

void CollectionFilesystemWatcher::onWalkFinished(int collectionIndex, int generation,
                                                 bool emitRescanWhenDone,
                                                 const QStringList &freshDirs) {
  // configure() ran while the walk was in flight: the result describes a torn
  // down layout (and the in-flight/rerun sets were already cleared) — drop it.
  if (generation != m_configGeneration) return;
  m_walksInFlight.remove(collectionIndex);

  auto entryIt = m_entries.find(collectionIndex);
  if (entryIt != m_entries.end()) {
    QSet<QString> freshSet;
    for (const QString &d : freshDirs) freshSet.insert(d);

    QStringList toAdd;
    for (const QString &d : freshDirs) {
      if (!entryIt->watchedSubdirs.contains(d)) toAdd.append(d);
    }
    QStringList toRemove;
    for (const QString &d : entryIt->watchedSubdirs) {
      if (!freshSet.contains(d)) toRemove.append(d);
    }

    if (m_watcher) {
      if (!toAdd.isEmpty()) {
        const QStringList failed = addPathsLogged(m_watcher, toAdd, entryIt->rootPath);
        for (const QString &f : failed) {
          toAdd.removeOne(f);
          freshSet.remove(f);
        }
      }
      if (!toRemove.isEmpty()) m_watcher->removePaths(toRemove);
    }
    for (const QString &d : toRemove) m_pathToCollection.remove(d);
    for (const QString &d : toAdd) m_pathToCollection.insert(d, collectionIndex);
    entryIt->watchedSubdirs = freshSet;
  }

  if (emitRescanWhenDone) emitRescan(collectionIndex);

  // A debounce window elapsed while we were walking — run the deferred walk
  // now so those later events get reconciled and rescanned too. Reruns always
  // come from the debounce path (a real filesystem event), so they rescan
  // when they land, even when the walk they piggybacked on was a silent
  // configure() registration walk.
  if (m_walkRerunPending.remove(collectionIndex)) {
    startReconcileWalk(collectionIndex, /*emitRescanWhenDone=*/true);
  }
}

void CollectionFilesystemWatcher::emitRescan(int collectionIndex) {
  if (m_callback) m_callback(collectionIndex);
}
