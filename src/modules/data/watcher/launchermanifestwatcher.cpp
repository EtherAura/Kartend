#include "launchermanifestwatcher.h"

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

#include "launcherimportservice.h"

namespace {
/// Long enough to sit out a Steam download's manifest churn, short enough that
/// an install shows up while the user is still looking for it. The sync this
/// triggers is idempotent and runs in the background, so erring long is free
/// and erring short costs a burst of redundant full syncs.
constexpr int kDefaultDebounceMs = 15000;
} // namespace

LauncherManifestWatcher::LauncherManifestWatcher(QObject *parent)
    : QObject(parent), m_watcher(new QFileSystemWatcher(this)), m_debounce(new QTimer(this)) {
  m_debounce->setSingleShot(true);
  m_debounce->setInterval(kDefaultDebounceMs);
  connect(m_debounce, &QTimer::timeout, this, &LauncherManifestWatcher::onDebounceElapsed);
  connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
          &LauncherManifestWatcher::onDirectoryChanged);
}

LauncherManifestWatcher::~LauncherManifestWatcher() = default;

void LauncherManifestWatcher::setSyncCallback(SyncCallback callback) {
  m_callback = std::move(callback);
}

void LauncherManifestWatcher::setDebounceInterval(int milliseconds) {
  m_debounce->setInterval(milliseconds);
}

auto LauncherManifestWatcher::debounceInterval() const -> int { return m_debounce->interval(); }

void LauncherManifestWatcher::configureForSources(const QStringList &sourceIds) {
  QStringList directories;
  for (const QString &sourceId : sourceIds) {
    // Empty for a source that is not installed, so callers can pass every
    // source id and get back only what exists.
    for (const QString &path : LauncherImportService::watchPaths(sourceId)) {
      if (!directories.contains(path)) {
        directories.append(path);
      }
    }
  }
  setWatchedDirectories(directories);
}

void LauncherManifestWatcher::setWatchedDirectories(const QStringList &directories) {
  m_requested = directories;
  const QStringList registered = m_watcher->directories();
  if (!registered.isEmpty()) {
    m_watcher->removePaths(registered);
  }
  // A pending burst belongs to the old watch set; dropping it avoids firing a
  // sync for directories nobody is watching any more.
  m_debounce->stop();
  reconcileWatchSet();
}

auto LauncherManifestWatcher::activeDirectories() const -> QStringList {
  return m_watcher->directories();
}

void LauncherManifestWatcher::reconcileWatchSet() {
  const QStringList registered = m_watcher->directories();
  for (const QString &path : m_requested) {
    if (registered.contains(path)) {
      continue;
    }
    // Only add what exists: QFileSystemWatcher warns and refuses otherwise,
    // and a launcher's directory legitimately appears later (Heroic writes
    // store_cache/ the first time it syncs a store).
    if (QFileInfo(path).isDir()) {
      m_watcher->addPath(path);
    }
  }
}

void LauncherManifestWatcher::onDirectoryChanged(const QString & /*path*/) {
  // Which directory changed does not matter: the sync this drives reconciles
  // every launcher collection anyway, so a burst spanning several sources
  // still costs one pass.
  m_debounce->start();
}

void LauncherManifestWatcher::onDebounceElapsed() {
  // Reconcile BEFORE the callback. A path dropped because it was deleted mid
  // burst is re-registered here if it came back, which is the normal shape of
  // an uninstall followed by a reinstall.
  reconcileWatchSet();
  if (m_callback) {
    m_callback();
  }
}
