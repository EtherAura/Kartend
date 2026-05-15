// Utility functions for artwork file operations.
#include "artworkutils.h"
#include "extensionutils.h"
#include "loggingcategories.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QtConcurrent>

namespace ArtworkUtils {

// Singleton instance
DirectoryCache &DirectoryCache::instance() {
  static DirectoryCache cache;
  return cache;
}

void DirectoryCache::ensureDirectoryCached(const QString &directory) {
  // NOTE: This is now called WITHOUT mutex held.
  // Check if already cached first (with brief lock).
  {
    QMutexLocker locker(&m_mutex);
    if (m_cache.contains(directory)) {
      return;
    }
  }

  QElapsedTimer perfTimer;
  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
    perfTimer.start();
  }

  QDir dir(directory);
  if (!dir.exists()) {
    // Cache empty hash to avoid repeated checks
    QMutexLocker locker(&m_mutex);
    m_cache.insert(directory, QHash<QString, QString>());
    qCDebug(lcPerfTrace) << "ensureDirectoryCached: dir NOT EXISTS ms=" << perfTimer.elapsed()
                         << "dir=" << directory;
    return;
  }

  QHash<QString, QString> dirContents;
  const QStringList &imageFilters = ExtensionUtils::imageFilters();

  // Scan directory WITHOUT holding mutex - this is the slow part
  QDirIterator it(directory, imageFilters, QDir::Files);
  int fileCount = 0;
  while (it.hasNext()) {
    QString path = it.next();
    QString baseName = QFileInfo(path).completeBaseName().toLower();
    // First match wins (preserves priority of extensions in imageFilters)
    if (!dirContents.contains(baseName)) {
      dirContents.insert(baseName, path);
    }
    ++fileCount;
  }

  // Now briefly lock to insert results
  {
    QMutexLocker locker(&m_mutex);
    // Double-check another thread didn't cache it while we were scanning
    if (!m_cache.contains(directory)) {
      m_cache.insert(directory, dirContents);
    }
  }

  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") && perfTimer.elapsed() > 1) {
    qCDebug(lcPerfTrace) << "ensureDirectoryCached: SCAN ms=" << perfTimer.elapsed()
                         << "files=" << fileCount << "dir=" << directory;
  }
}

QString DirectoryCache::findInDirectory(const QString &baseName, const QString &artworkDirectory) {
  if (baseName.isEmpty() || artworkDirectory.isEmpty()) {
    return {};
  }

  QElapsedTimer timer;
  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
    timer.start();
  }

  QMutexLocker locker(&m_mutex);

  qint64 afterLock = qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") ? timer.elapsed() : 0;

  // Non-blocking: if not cached, queue for background scan and return empty
  if (!m_cache.contains(artworkDirectory)) {
    m_queuedDirectories.insert(artworkDirectory);
    if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
      static int queuedLogCount = 0;
      if (++queuedLogCount <= 10) {
        qCDebug(lcPerfTrace) << "findInDirectory: QUEUED lockMs=" << afterLock
                             << "cacheSize=" << m_cache.size()
                             << "queueSize=" << m_queuedDirectories.size()
                             << "dir=" << artworkDirectory;
      }
    }
    return {};
  }

  const QHash<QString, QString> &dirContents = m_cache.value(artworkDirectory);
  QString result = dirContents.value(baseName.toLower());

  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") && timer.elapsed() > 2) {
    qCDebug(lcPerfTrace) << "findInDirectory: CACHED lockMs=" << afterLock
                         << "totalMs=" << timer.elapsed() << "found=" << !result.isEmpty()
                         << "dirContentsSize=" << dirContents.size() << "dir=" << artworkDirectory;
  }

  if (!result.isEmpty()) {
    return result;
  }

  // Cache miss for this basename. The cache only re-scans on
  // collection switch — files dropped into the directory between
  // switches (manual artwork drops, external editors, etc.) would
  // stay invisible until the user navigated away and back. Probe
  // the filesystem directly for the expected per-extension paths
  // and patch the cache on a hit. Cost is bounded: one stat per
  // image extension per miss, and only ONE such miss-probe round
  // per (dir, baseName) since the patch makes subsequent lookups
  // a pure cache hit.
  locker.unlock();
  const QStringList &exts = ExtensionUtils::imageBaseExtensions();
  for (const QString &ext : exts) {
    QString candidate = QDir(artworkDirectory).absoluteFilePath(baseName + "." + ext);
    if (!QFile::exists(candidate)) {
      candidate = QDir(artworkDirectory).absoluteFilePath(baseName + "." + ext.toUpper());
      if (!QFile::exists(candidate)) continue;
    }
    // Found a new file on disk. Patch the cache and return. Both
    // locks (above + here) are short — the filesystem probe runs
    // unlocked so other lookups aren't serialised behind it.
    QMutexLocker patchLocker(&m_mutex);
    m_cache[artworkDirectory].insert(baseName.toLower(), candidate);
    return candidate;
  }
  return {};
}

void DirectoryCache::prewarmDirectories(const QStringList &directories) {
  // Pre-cache multiple directories in parallel using all available CPU cores.
  // This dramatically speeds up OS dentry cache warmup for large directory
  // counts.

  // Filter out empty directories and already-cached ones
  QStringList toProcess;
  {
    QMutexLocker locker(&m_mutex);
    for (const QString &dir : directories) {
      if (!dir.isEmpty() && !m_cache.contains(dir)) {
        toProcess.append(dir);
      }
    }
  }

  if (toProcess.isEmpty()) {
    return;
  }

  // Process directories in parallel - each thread scans a different directory
  // This warms the OS dentry cache much faster than sequential scanning
  QtConcurrent::blockingMap(toProcess, [this](const QString &dir) {
    ensureDirectoryCached(dir);
    // Brief lock to update queue
    {
      QMutexLocker locker(&m_mutex);
      m_queuedDirectories.remove(dir);
    }
  });
}

void DirectoryCache::processQueuedDirectories() {
  // Process directories that were requested but not yet cached.
  // Called from background thread - uses parallel processing for speed.
  QStringList toProcess;
  {
    QMutexLocker locker(&m_mutex);
    toProcess = m_queuedDirectories.values();
  }

  if (toProcess.isEmpty()) {
    return;
  }

  qCDebug(lcPerfTrace) << "processQueuedDirectories: count=" << toProcess.size();
  // Process in parallel for faster warmup
  QtConcurrent::blockingMap(toProcess, [this](const QString &dir) {
    ensureDirectoryCached(dir);
    // Brief lock just to update queue
    {
      QMutexLocker locker(&m_mutex);
      m_queuedDirectories.remove(dir);
    }
  });
}

bool DirectoryCache::hasQueuedDirectories() const {
  QMutexLocker locker(&m_mutex);
  return !m_queuedDirectories.isEmpty();
}

void DirectoryCache::clear() {
  QMutexLocker locker(&m_mutex);
  m_cache.clear();
  m_queuedDirectories.clear();
}

int DirectoryCache::cachedDirectoryCount() const {
  QMutexLocker locker(&m_mutex);
  return m_cache.size();
}

namespace {

/**
 * @brief Internal helper to search for artwork with a given base name.
 * @return Path if found, empty string otherwise.
 */
QString searchWithName(const QDir &artworkDir, const QString &name, const QStringList &extensions) {
  for (const QString &ext : extensions) {
    QString path = artworkDir.absoluteFilePath(name + "." + ext);
    if (QFile::exists(path)) {
      return path;
    }
    path = artworkDir.absoluteFilePath(name + "." + ext.toUpper());
    if (QFile::exists(path)) {
      return path;
    }
  }
  return {};
}

} // namespace

QString findArtworkForFile(const QString &fileName, const QString &artworkDirectory) {
  if (fileName.isEmpty() || artworkDirectory.isEmpty()) {
    return {};
  }

  QDir artworkDir(artworkDirectory);
  if (!artworkDir.exists()) {
    return {};
  }

  const QString baseName = QFileInfo(fileName).completeBaseName();
  const QString fullName = QFileInfo(fileName).fileName();
  const QStringList &bases = ExtensionUtils::imageBaseExtensions();

  // Try baseName first, then fullName, at the flat artwork root (the
  // scrape mirror writes to `{artwork}/{base}.<ext>` for the grid
  // tile path).
  QString result = searchWithName(artworkDir, baseName, bases);
  if (!result.isEmpty()) {
    return result;
  }
  result = searchWithName(artworkDir, fullName, bases);
  if (!result.isEmpty()) {
    return result;
  }
  // Fallback: scan `{artwork}/front/` for a cover with the same
  // basename. Lets a user drop hand-curated cover art into the
  // gallery's "Front Cover" subdir and have the grid tile pick it
  // up too, without forcing them to also copy the file to the
  // flat root.
  QDir frontDir(artworkDir.absoluteFilePath(QStringLiteral("front")));
  if (frontDir.exists()) {
    result = searchWithName(frontDir, baseName, bases);
    if (!result.isEmpty()) {
      return result;
    }
    result = searchWithName(frontDir, fullName, bases);
    if (!result.isEmpty()) {
      return result;
    }
  }
  return {};
}

ErrorUtils::Result<QString> tryFindArtworkForFile(const QString &fileName,
                                                  const QString &artworkDirectory) {
  using ErrorUtils::ErrorCode;
  using ErrorUtils::ErrorContext;

  if (fileName.isEmpty()) {
    return ErrorContext::warning(ErrorCode::InvalidArgument, "Empty filename",
                                 "ArtworkUtils::tryFindArtworkForFile");
  }
  if (artworkDirectory.isEmpty()) {
    return ErrorContext::warning(ErrorCode::InvalidArgument, "Empty artwork directory",
                                 "ArtworkUtils::tryFindArtworkForFile");
  }

  QDir artworkDir(artworkDirectory);
  if (!artworkDir.exists()) {
    return ErrorContext::warning(ErrorCode::ArtworkDirectoryNotFound,
                                 "Artwork directory does not exist",
                                 "ArtworkUtils::tryFindArtworkForFile")
        .withDetails(artworkDirectory);
  }

  const QString baseName = QFileInfo(fileName).completeBaseName();
  const QString fullName = QFileInfo(fileName).fileName();
  const QStringList &bases = ExtensionUtils::imageBaseExtensions();

  // Try baseName first, then fullName
  QString result = searchWithName(artworkDir, baseName, bases);
  if (!result.isEmpty()) {
    return result;
  }
  result = searchWithName(artworkDir, fullName, bases);
  if (!result.isEmpty()) {
    return result;
  }

  return ErrorContext::info(ErrorCode::FileNotFound, "No matching artwork found",
                            "ArtworkUtils::tryFindArtworkForFile")
      .withDetails(QString("Searched for: %1 in %2").arg(fileName, artworkDirectory));
}

QString findArtworkForFileCached(const QString &fileName, const QString &artworkDirectory) {
  if (fileName.isEmpty() || artworkDirectory.isEmpty()) {
    return {};
  }

  QElapsedTimer perfTimer;
  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
    perfTimer.start();
  }

  const QString baseName = QFileInfo(fileName).completeBaseName();
  QString result = DirectoryCache::instance().findInDirectory(baseName, artworkDirectory);
  if (!result.isEmpty()) {
    if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") && perfTimer.elapsed() > 2) {
      qCDebug(lcPerfTrace) << "findArtworkForFileCached: ms=" << perfTimer.elapsed()
                           << "dir=" << artworkDirectory;
    }
    return result;
  }

  // Try with full filename as fallback
  const QString fullName = QFileInfo(fileName).fileName();
  if (fullName != baseName) {
    result = DirectoryCache::instance().findInDirectory(fullName, artworkDirectory);
    if (!result.isEmpty()) {
      return result;
    }
  }

  // Fall back to the `{artwork}/front/` subdir so hand-dropped
  // cover art there shows up in the grid alongside the gallery.
  // Goes through the same DirectoryCache so the lookup stays
  // cheap on repeat hits.
  const QString frontDir = QDir(artworkDirectory).absoluteFilePath(QStringLiteral("front"));
  result = DirectoryCache::instance().findInDirectory(baseName, frontDir);
  if (!result.isEmpty()) {
    return result;
  }
  if (fullName != baseName) {
    result = DirectoryCache::instance().findInDirectory(fullName, frontDir);
  }

  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE") && perfTimer.elapsed() > 2) {
    qCDebug(lcPerfTrace) << "findArtworkForFileCached: ms=" << perfTimer.elapsed()
                         << "dir=" << artworkDirectory << "(fallback)";
  }
  return result;
}

void clearDirectoryCache() {
  DirectoryCache::instance().clear();
}

QString nextArtworkType(const QString &currentType, const QStringList &availableTypes) {
  if (availableTypes.size() < 2) {
    return currentType;
  }
  const int idx = availableTypes.indexOf(currentType);
  if (idx < 0) {
    return availableTypes.first();
  }
  return availableTypes.at((idx + 1) % availableTypes.size());
}

} // namespace ArtworkUtils
