#include "collectionfilesystemwatcher.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QQueue>
#include <QTimer>

#include "settingsutils.h"

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

  for (int i = 0; i < collections.size(); ++i) {
    const CollectionConfig &cfg = collections.at(i);
    if (!cfg.watchFilesystem) continue;
    if (cfg.isPlaylist) continue;
    const QString mediaDir = SettingsUtils::expandConfigVariables(cfg.mediaDirectory, cfg.name);
    if (mediaDir.trimmed().isEmpty()) continue;

    const QStringList dirs = enumerateWatchableSubdirs(mediaDir);
    if (dirs.isEmpty()) continue;

    WatchEntry entry;
    entry.collectionIndex = i;
    entry.rootPath = mediaDir;
    for (const QString &d : dirs) entry.watchedSubdirs.insert(d);
    m_entries.insert(i, entry);
    for (const QString &d : dirs) m_pathToCollection.insert(d, i);

    if (m_watcher) m_watcher->addPaths(dirs);
  }
}

void CollectionFilesystemWatcher::onDirectoryChanged(const QString &path) {
  const auto it = m_pathToCollection.constFind(path);
  if (it == m_pathToCollection.cend()) return;
  const int collectionIndex = it.value();

  // Reconcile the watch set for this collection — directoryChanged can fire
  // because a subfolder was created or removed; the QFSW only sees the
  // mutation event, not the resulting tree. Re-walking from the root is
  // O(n) in directory count which is fine for a debounced path.
  auto entryIt = m_entries.find(collectionIndex);
  if (entryIt != m_entries.end()) {
    const QStringList freshDirs = enumerateWatchableSubdirs(entryIt->rootPath);
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
      if (!toAdd.isEmpty()) m_watcher->addPaths(toAdd);
      if (!toRemove.isEmpty()) m_watcher->removePaths(toRemove);
    }
    for (const QString &d : toRemove) m_pathToCollection.remove(d);
    for (const QString &d : toAdd) m_pathToCollection.insert(d, collectionIndex);
    entryIt->watchedSubdirs = freshSet;
  }

  // Debounce the rescan: bursts of filesystem events (e.g. copying a folder)
  // should coalesce into a single rescan. QTimer::singleShot won't work
  // because we need to reset the deadline on each new event for the same
  // collection.
  QTimer *&timer = m_debounceTimers[collectionIndex];
  if (!timer) {
    timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this,
            [this, collectionIndex]() { emitRescan(collectionIndex); });
  }
  timer->start(m_debounceMs);
}

void CollectionFilesystemWatcher::emitRescan(int collectionIndex) {
  if (m_callback) m_callback(collectionIndex);
}
