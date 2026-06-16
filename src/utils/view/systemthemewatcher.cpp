#include "systemthemewatcher.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QStandardPaths>
#include <QTimer>

SystemThemeWatcher::SystemThemeWatcher(QObject *parent) : QObject(parent) {
#ifdef Q_OS_LINUX
  // KDE writes the active accent colour + colour scheme into kdeglobals; any
  // runtime change (incl. a wallpaper-derived accent recomputed on a Plasma
  // activity switch) rewrites it. Resolve its path under $XDG_CONFIG_HOME.
  const QString cfgDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  if (cfgDir.isEmpty()) {
    return;
  }
  m_kdeglobalsPath = cfgDir + QStringLiteral("/kdeglobals");

  m_watcher = new QFileSystemWatcher(this);
  if (QFileInfo::exists(m_kdeglobalsPath)) {
    m_watcher->addPath(m_kdeglobalsPath);
  }
  // Also watch the config directory: KConfig saves atomically (temp file +
  // rename-over), which drops a file-only watch and would otherwise make us
  // deaf after the first change. The directory signal lets us notice that the
  // file was replaced and re-arm — and catches a kdeglobals created for the
  // very first time.
  if (QFileInfo::exists(cfgDir)) {
    m_watcher->addPath(cfgDir);
  }

  m_debounce = new QTimer(this);
  m_debounce->setSingleShot(true);
  // Coalesce the burst of writes KDE emits during a single scheme/accent
  // change into one themeChanged().
  m_debounce->setInterval(250);
  connect(m_debounce, &QTimer::timeout, this, &SystemThemeWatcher::themeChanged);

  connect(m_watcher, &QFileSystemWatcher::fileChanged, this,
          &SystemThemeWatcher::onConfigPathChanged);
  connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
          &SystemThemeWatcher::onConfigPathChanged);
#endif
}

bool SystemThemeWatcher::rearmKdeglobalsWatch() {
  if (!m_watcher || m_kdeglobalsPath.isEmpty()) {
    return false;
  }
  // Already watched → nothing to do (and not a "file was replaced" signal).
  if (m_watcher->files().contains(m_kdeglobalsPath)) {
    return false;
  }
  // Not watched: either it was just replaced by an atomic save, or it didn't
  // exist before and has now appeared. Re-add it and treat that as a change.
  if (QFileInfo::exists(m_kdeglobalsPath)) {
    m_watcher->addPath(m_kdeglobalsPath);
    return true;
  }
  return false;
}

void SystemThemeWatcher::onConfigPathChanged(const QString &path) {
#ifdef Q_OS_LINUX
  // A direct modify of kdeglobals (in-place write) keeps it in the watch set
  // and fires fileChanged for it — that is unambiguously a change.
  const bool directFileChange = (path == m_kdeglobalsPath);
  // An atomic save replaces the inode, dropping kdeglobals from the watch; the
  // directory signal then lets us notice + re-arm. Only treat the directory
  // signal as a change when kdeglobals actually needed re-arming, so unrelated
  // writes elsewhere under $XDG_CONFIG_HOME don't trigger spurious re-themes.
  const bool replaced = rearmKdeglobalsWatch();
  if ((directFileChange || replaced) && m_debounce) {
    m_debounce->start();
  }
#else
  Q_UNUSED(path);
#endif
}
