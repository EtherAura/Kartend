// Utility functions for artwork file operations.
#include "artworkutils.h"
#include "extensionutils.h"
#include "loggingcategories.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QReadWriteLock>
#include <QtConcurrent>
#include <QThreadPool>

namespace ArtworkUtils {

QImage composeArtworkCard(const QImage &source, int targetWidthLogical, int targetHeightLogical,
                          qreal dpr, int cornerRadiusLogical, const QColor &background) {
  const int physicalW = qRound(targetWidthLogical * dpr);
  const int physicalH = qRound(targetHeightLogical * dpr);
  if (physicalW <= 0 || physicalH <= 0 || source.isNull()) {
    return {};
  }

  // Work in raw physical pixels: strip any DPR off the source so scaled()
  // targets the physical card size directly.
  QImage sourceNoDpr = source;
  sourceNoDpr.setDevicePixelRatio(1.0);
  const QImage scaledArtwork =
      sourceNoDpr.scaled(physicalW, physicalH, Qt::KeepAspectRatio, Qt::SmoothTransformation);

  QImage result(physicalW, physicalH, QImage::Format_ARGB32_Premultiplied);
  result.fill(background);
  {
    QPainter painter(&result);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    const int offsetX = (physicalW - scaledArtwork.width()) / 2;
    const int offsetY = (physicalH - scaledArtwork.height()) / 2;
    painter.drawImage(offsetX, offsetY, scaledArtwork);
  }

  if (cornerRadiusLogical > 0) {
    const int physicalRadius = qRound(cornerRadiusLogical * dpr);
    QImage masked(physicalW, physicalH, QImage::Format_ARGB32_Premultiplied);
    masked.fill(Qt::transparent);
    QPainter maskPainter(&masked);
    maskPainter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clipPath;
    clipPath.addRoundedRect(QRectF(0, 0, physicalW, physicalH), physicalRadius, physicalRadius);
    maskPainter.setClipPath(clipPath);
    maskPainter.drawImage(0, 0, result);
    maskPainter.end();
    result = masked;
  }

  result.setDevicePixelRatio(dpr);
  return result;
}

namespace {
/// Subdirectories under the artwork root that can supply an item's
/// primary cover, in display-priority order. The scrape pipeline
/// writes each media type into its own `{artwork}/<type>/` subdir and
/// no longer drops a copy at the flat root, so the grid tile and the
/// details-pane preview resolve the cover by walking these in order.
/// `front` is the canonical cover; the rest are sensible fallbacks
/// for items ScreenScraper has no dedicated front cover for.
const QStringList &coverSubdirPriority() {
  static const QStringList kDirs = {
      QStringLiteral("front"),   QStringLiteral("box"),     QStringLiteral("box-3d"),
      QStringLiteral("mixrbv1"), QStringLiteral("mixrbv2"), QStringLiteral("screenshot"),
      QStringLiteral("title"),   QStringLiteral("fanart"),  QStringLiteral("marquee"),
  };
  return kDirs;
}

/// Small dedicated pool for the background prewarm walk (mirrors
/// ArtworkLoadDispatcher's dedicated-pool pattern). schedulePrewarm used to
/// start its task on the GLOBAL pool and the walk then ran
/// QtConcurrent::blockingMap on that same global pool from inside the task —
/// a blocked-thread-in-pool: under load the walk both occupied a global
/// worker and waited on more global workers. The walk now owns its threads;
/// blockingMap's calling thread participates in the map as before, so a
/// saturated pool degrades to the caller scanning serially rather than
/// deadlocking or starving unrelated global-pool work.
QThreadPool &prewarmWalkPool() {
  static QThreadPool pool;
  static const bool configured = [] {
    pool.setObjectName(QStringLiteral("kartend-artwork-prewarm"));
    pool.setMaxThreadCount(2);
    return true;
  }();
  Q_UNUSED(configured)
  return pool;
}
} // namespace

// Singleton instance
DirectoryCache &DirectoryCache::instance() {
  static DirectoryCache cache;
  return cache;
}

void DirectoryCache::ensureDirectoryCached(const QString &directory) {
  // NOTE: This is now called WITHOUT the lock held.
  // Check if already cached first (with brief read lock).
  {
    QReadLocker locker(&m_lock);
    if (m_cache.contains(directory)) {
      return;
    }
  }

  QElapsedTimer perfTimer;
  if (lcPerfTrace().isDebugEnabled()) {
    perfTimer.start();
  }

  QDir dir(directory);
  if (!dir.exists()) {
    // Cache empty listing to avoid repeated checks
    QWriteLocker locker(&m_lock);
    m_cache.insert(directory, DirectoryListing());
    ++m_contentsGeneration;
    qCDebug(lcPerfTrace) << "ensureDirectoryCached: dir NOT EXISTS ms=" << perfTimer.elapsed()
                         << "dir=" << directory;
    return;
  }

  DirectoryListing dirContents;
  const QStringList &imageFilters = ExtensionUtils::imageFilters();

  // Scan directory WITHOUT holding mutex - this is the slow part
  QDirIterator it(directory, imageFilters, QDir::Files);
  int fileCount = 0;
  while (it.hasNext()) {
    const QFileInfo fi = it.nextFileInfo();
    const QString fileName = fi.fileName();
    // Kartend-235kv: capture the "<directory>/" prefix once, from the
    // iterator's own path spelling, so prefix + fileName reconstructs the
    // exact bytes it.next() used to return — path-keyed caches (memory and
    // MD5 disk) keep hitting across this change.
    if (dirContents.pathPrefix.isEmpty()) {
      const QString path = fi.filePath();
      dirContents.pathPrefix = path.left(path.size() - fileName.size());
    }
    QString baseName = baseMatchKey(fi.completeBaseName());
    // First match wins (preserves priority of extensions in imageFilters)
    if (!dirContents.byKey.contains(baseName)) {
      dirContents.byKey.insert(baseName, fileName);
    }
    ++fileCount;
  }

  // Now briefly lock to insert results
  {
    QWriteLocker locker(&m_lock);
    // Double-check another thread didn't cache it while we were scanning
    if (!m_cache.contains(directory)) {
      m_cache.insert(directory, dirContents);
      ++m_contentsGeneration;
    }
  }

  if (lcPerfTrace().isDebugEnabled() && perfTimer.elapsed() > 1) {
    qCDebug(lcPerfTrace) << "ensureDirectoryCached: SCAN ms=" << perfTimer.elapsed()
                         << "files=" << fileCount << "dir=" << directory;
  }
}

QString DirectoryCache::findInDirectory(const QString &baseName, const QString &artworkDirectory) {
  if (baseName.isEmpty() || artworkDirectory.isEmpty()) {
    return {};
  }

  QElapsedTimer timer;
  if (lcPerfTrace().isDebugEnabled()) {
    timer.start();
  }

  const QString key = baseMatchKey(baseName);
  bool dirCached = false;
  bool keyKnown = false;
  QString result;
  {
    // Shared read lock (Kartend-s723v): the hit path only reads, so
    // concurrent per-tile lookups from the GUI + worker threads no longer
    // serialise on one global mutex during scroll storms.
    QReadLocker locker(&m_lock);

    qint64 afterLock = lcPerfTrace().isDebugEnabled() ? timer.elapsed() : 0;

    const auto dirIt = m_cache.constFind(artworkDirectory);
    dirCached = dirIt != m_cache.constEnd();
    if (dirCached) {
      const auto keyIt = dirIt->byKey.constFind(key);
      // A present-but-empty value is a cached NEGATIVE result
      // (Kartend-bjrw1): the stat sweep below already ran for this
      // (dir, baseName) and found nothing — short-circuit without
      // touching the filesystem again.
      keyKnown = keyIt != dirIt->byKey.constEnd();
      if (keyKnown && !keyIt->isEmpty()) {
        // Kartend-235kv: values are bare file names; rebuild the full path
        // from the listing's captured prefix. One small concat per positive
        // hit, immediately ahead of the artwork decode it feeds.
        result = dirIt->pathPrefix + *keyIt;
      }
      if (lcPerfTrace().isDebugEnabled() && timer.elapsed() > 2) {
        qCDebug(lcPerfTrace) << "findInDirectory: CACHED lockMs=" << afterLock
                             << "totalMs=" << timer.elapsed() << "found=" << !result.isEmpty()
                             << "dirContentsSize=" << dirIt->byKey.size()
                             << "dir=" << artworkDirectory;
      }
    } else if (lcPerfTrace().isDebugEnabled()) {
      static int queuedLogCount = 0;
      if (++queuedLogCount <= 10) {
        qCDebug(lcPerfTrace) << "findInDirectory: QUEUED lockMs=" << afterLock
                             << "cacheSize=" << m_cache.size()
                             << "queueSize=" << m_queuedDirectories.size()
                             << "dir=" << artworkDirectory;
      }
    }
  }

  // Non-blocking: if not cached, queue for background scan and return empty.
  // The queue insert needs the write lock, taken only on this cold path.
  if (!dirCached) {
    QWriteLocker locker(&m_lock);
    m_queuedDirectories.insert(artworkDirectory);
    return {};
  }
  if (keyKnown) {
    return result; // positive hit, or cached-negative empty
  }

  // First-ever miss for this basename in a cached directory. The cache only
  // re-scans on collection switch — files dropped into the directory between
  // switches (manual artwork drops, external editors, etc.) would stay
  // invisible until the user navigated away and back. Probe the filesystem
  // directly for the expected per-extension paths and patch the cache with
  // the outcome — POSITIVE OR NEGATIVE (Kartend-bjrw1) — so exactly ONE
  // stat-sweep round runs per (dir, baseName) per cache generation. Before
  // negatives were cached, every re-materialization of a sparsely-arted tile
  // re-paid the full sweep on the GUI thread.
  const QStringList &exts = ExtensionUtils::imageBaseExtensions();
  for (const QString &ext : exts) {
    QString candidate = QDir(artworkDirectory).absoluteFilePath(baseName + "." + ext);
    if (!QFile::exists(candidate)) {
      candidate = QDir(artworkDirectory).absoluteFilePath(baseName + "." + ext.toUpper());
      if (!QFile::exists(candidate)) continue;
    }
    // Found a new file on disk. Patch the cache and return. Both lock
    // scopes are short — the filesystem probe runs unlocked so other
    // lookups aren't serialised behind it.
    QWriteLocker patchLocker(&m_lock);
    DirectoryListing &listing = m_cache[artworkDirectory];
    const QString candidateFileName = QFileInfo(candidate).fileName();
    // A listing patched before any scan entry exists (dir cached empty, file
    // dropped in later) has no prefix yet — derive it from the candidate the
    // sweep just stat'ed. Scanned listings keep their scan-time prefix so
    // sibling entries stay uniformly spelled.
    if (listing.pathPrefix.isEmpty()) {
      listing.pathPrefix = candidate.left(candidate.size() - candidateFileName.size());
    }
    listing.byKey.insert(key, candidateFileName);
    ++m_contentsGeneration;
    // Return the exact string the sweep probed, not a reconstruction.
    return candidate;
  }
  {
    QWriteLocker patchLocker(&m_lock);
    m_cache[artworkDirectory].byKey.insert(key, QString());
    ++m_contentsGeneration;
  }
  return {};
}

void DirectoryCache::prewarmDirectories(const QStringList &directories) {
  // Pre-cache multiple directories in parallel using all available CPU cores.
  // This dramatically speeds up OS dentry cache warmup for large directory
  // counts.

  // Each artwork root is expanded with its typed cover subdirs
  // (`front/`, `box/`, …): scrapes write the cover into those rather
  // than the flat root, so findArtworkForFileCached resolves it from
  // there — prewarming them avoids a cold-start round of blank tiles.
  QStringList expanded;
  expanded.reserve(directories.size() * 2);
  for (const QString &dir : directories) {
    if (dir.isEmpty()) {
      continue;
    }
    expanded.append(dir);
    const QDir root(dir);
    for (const QString &subdir : coverSubdirPriority()) {
      if (root.exists(subdir)) {
        expanded.append(root.absoluteFilePath(subdir));
      }
    }
  }

  // Filter out empty directories and already-cached ones
  QStringList toProcess;
  {
    QReadLocker locker(&m_lock);
    for (const QString &dir : expanded) {
      if (!dir.isEmpty() && !m_cache.contains(dir)) {
        toProcess.append(dir);
      }
    }
  }

  if (toProcess.isEmpty()) {
    return;
  }

  // Process directories in parallel - each thread scans a different directory
  // This warms the OS dentry cache much faster than sequential scanning.
  // The dedicated walk pool (not the global pool) supplies the parallelism —
  // see prewarmWalkPool() for why.
  QtConcurrent::blockingMap(&prewarmWalkPool(), toProcess, [this](const QString &dir) {
    ensureDirectoryCached(dir);
    // Brief lock to update queue
    {
      QWriteLocker locker(&m_lock);
      m_queuedDirectories.remove(dir);
    }
  });
}

void DirectoryCache::schedulePrewarm(const QStringList &directories) {
  if (directories.isEmpty()) {
    return;
  }
  // Queue FIRST, before the in-flight guard below can drop this request.
  //
  // Kartend-hrgf5: the guard used to return outright, on the assumption that
  // "the running walk warms its dirs and the newly-selected collection's own
  // enumeration warms the rest". That holds only when the caller wants the
  // same directories the running walk already covers. It is false for a caller
  // asking about directories nothing else is going to touch — CoverFlow's
  // pending-artwork retry (coverflowcontroller.cpp) is exactly that: it waits
  // on isDirectoryCached() for the dirs it just asked to be warmed, so a
  // dropped request left those cards showing the placeholder permanently while
  // the retry budget drained against a directory nobody would ever scan.
  //
  // Queueing is enough to make the drop lossless: the in-flight walk ends with
  // processQueuedDirectories(), so whatever lands here still gets scanned by
  // the walk that displaced us.
  {
    QWriteLocker locker(&m_lock);
    for (const QString &dir : directories) {
      if (!dir.isEmpty() && !m_cache.contains(dir)) {
        m_queuedDirectories.insert(dir);
      }
    }
  }

  // Kartend-uzs42: cap at one in-flight prewarm, so a burst of collection
  // switches cannot pile up redundant walks. Dropping the WALK is still right;
  // dropping the REQUEST was the bug.
  if (!m_prewarmInFlight.testAndSetOrdered(0, 1)) {
    return;
  }
  prewarmWalkPool().start([this, directories]() {
    prewarmDirectories(directories);
    // Drain more than once: a caller that lost the in-flight guard queues its
    // directories at any moment, including just after the first drain read the
    // queue. Without a second pass those sit until some later prewarm happens
    // to run, which for CoverFlow's retry means never. Bounded so a caller
    // queueing continuously cannot pin this thread.
    for (int pass = 0; pass < 3; ++pass) {
      processQueuedDirectories();
      QReadLocker locker(&m_lock);
      if (m_queuedDirectories.isEmpty()) {
        break;
      }
    }
    m_prewarmInFlight.storeRelaxed(0);
    qCDebug(lcPerfTrace) << "Background dentry prewarm complete: dirs=" << directories.size();
  });
}

void DirectoryCache::processQueuedDirectories() {
  // Process directories that were requested but not yet cached.
  // Called from background thread - uses parallel processing for speed.
  QStringList toProcess;
  {
    QReadLocker locker(&m_lock);
    toProcess = m_queuedDirectories.values();
  }

  if (toProcess.isEmpty()) {
    return;
  }

  qCDebug(lcPerfTrace) << "processQueuedDirectories: count=" << toProcess.size();
  // Process in parallel for faster warmup (dedicated walk pool — see
  // prewarmWalkPool()).
  QtConcurrent::blockingMap(&prewarmWalkPool(), toProcess, [this](const QString &dir) {
    ensureDirectoryCached(dir);
    // Brief lock just to update queue
    {
      QWriteLocker locker(&m_lock);
      m_queuedDirectories.remove(dir);
    }
  });
}

bool DirectoryCache::isDirectoryQueued(const QString &directory) const {
  if (directory.isEmpty()) {
    return false;
  }
  QReadLocker locker(&m_lock);
  return m_queuedDirectories.contains(directory);
}

bool DirectoryCache::isDirectoryCached(const QString &directory) const {
  if (directory.isEmpty()) {
    return false;
  }
  QReadLocker locker(&m_lock);
  return m_cache.contains(directory);
}

bool DirectoryCache::areDirectoriesCached(const QStringList &directories) const {
  if (directories.isEmpty()) {
    return false;
  }
  QReadLocker locker(&m_lock);
  for (const QString &dir : directories) {
    if (dir.isEmpty() || !m_cache.contains(dir)) {
      return false;
    }
  }
  return true;
}

quint64 DirectoryCache::contentsGeneration() const {
  QReadLocker locker(&m_lock);
  return m_contentsGeneration;
}

QSet<QString> DirectoryCache::collectPositiveKeys(const QStringList &directories) {
  QSet<QString> keys;
  QStringList uncached;
  {
    QReadLocker locker(&m_lock);
    for (const QString &dir : directories) {
      const auto dirIt = m_cache.constFind(dir);
      if (dirIt == m_cache.constEnd()) {
        uncached.append(dir);
        continue;
      }
      for (auto it = dirIt->byKey.constBegin(); it != dirIt->byKey.constEnd(); ++it) {
        // A present-but-empty value is a cached NEGATIVE (Kartend-bjrw1) —
        // only real artwork entries contribute a key.
        if (!it.value().isEmpty()) {
          keys.insert(it.key());
        }
      }
    }
  }
  // Mirror findInDirectory's non-blocking contract: uncached directories are
  // queued for a background scan instead of being scanned here.
  if (!uncached.isEmpty()) {
    QWriteLocker locker(&m_lock);
    for (const QString &dir : uncached) {
      m_queuedDirectories.insert(dir);
    }
  }
  return keys;
}

void DirectoryCache::clear() {
  QWriteLocker locker(&m_lock);
  m_cache.clear();
  m_queuedDirectories.clear();
  ++m_contentsGeneration;
}

namespace {

/**
 * @brief Internal helper to search for artwork with a given base name.
 * @return Path if found, empty string otherwise.
 */
QString searchWithName(const QDir &artworkDir, const QString &name, const QStringList &extensions) {
  return ExtensionUtils::findFileWithExtensions(artworkDir, name, extensions);
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

  // Try baseName first, then fullName, at the flat artwork root.
  // Scrapes no longer write a flat-root copy, but a user may still
  // drop a cover there by hand, and pre-existing libraries keep the
  // old mirror files — so the flat root stays the first lookup.
  QString result = searchWithName(artworkDir, baseName, bases);
  if (!result.isEmpty()) {
    return result;
  }
  result = searchWithName(artworkDir, fullName, bases);
  if (!result.isEmpty()) {
    return result;
  }
  // Fallback: walk the typed cover subdirs in priority order
  // (`front` → box → box-3d → … ). This is where scrapes now put
  // the cover, and it also lets hand-dropped gallery art surface on
  // the grid tile.
  for (const QString &subdir : coverSubdirPriority()) {
    QDir coverDir(artworkDir.absoluteFilePath(subdir));
    if (!coverDir.exists()) {
      continue;
    }
    result = searchWithName(coverDir, baseName, bases);
    if (!result.isEmpty()) {
      return result;
    }
    result = searchWithName(coverDir, fullName, bases);
    if (!result.isEmpty()) {
      return result;
    }
  }
  return {};
}

namespace {
/// Shared cascade for the cached lookups: probe @p baseName (and, when it
/// differs, @p fullName) at the flat root, then per typed cover subdir in
/// priority order. Callers own the strip policy — see
/// findArtworkForBaseNameCached's header note on double-stripping.
QString findCachedWithKeys(const QString &baseName, const QString &fullName,
                           const QString &artworkDirectory);
} // namespace

QString findArtworkForFileCached(const QString &fileName, const QString &artworkDirectory) {
  if (fileName.isEmpty() || artworkDirectory.isEmpty()) {
    return {};
  }
  return findCachedWithKeys(QFileInfo(fileName).completeBaseName(), QFileInfo(fileName).fileName(),
                            artworkDirectory);
}

QString findArtworkForBaseNameCached(const QString &completeBaseName,
                                     const QString &artworkDirectory) {
  if (completeBaseName.isEmpty() || artworkDirectory.isEmpty()) {
    return {};
  }
  return findCachedWithKeys(completeBaseName, completeBaseName, artworkDirectory);
}

namespace {
QString findCachedWithKeys(const QString &baseName, const QString &fullName,
                           const QString &artworkDirectory) {
  QElapsedTimer perfTimer;
  if (lcPerfTrace().isDebugEnabled()) {
    perfTimer.start();
  }

  QString result = DirectoryCache::instance().findInDirectory(baseName, artworkDirectory);
  if (!result.isEmpty()) {
    if (lcPerfTrace().isDebugEnabled() && perfTimer.elapsed() > 2) {
      qCDebug(lcPerfTrace) << "findArtworkForFileCached: ms=" << perfTimer.elapsed()
                           << "dir=" << artworkDirectory;
    }
    return result;
  }

  // Try with full filename as fallback
  if (fullName != baseName) {
    result = DirectoryCache::instance().findInDirectory(fullName, artworkDirectory);
    if (!result.isEmpty()) {
      return result;
    }
  }

  // Fall back to the typed cover subdirs in priority order
  // (`front` → box → box-3d → … ) — scrapes write the cover there
  // rather than at the flat root. Each subdir goes through the same
  // DirectoryCache so repeat hits stay cheap — including cached NEGATIVES
  // (Kartend-bjrw1), so a sparsely-arted tile's 9-subdir sweep collapses to
  // hash lookups on re-materialization. The root QDir is resolved once per
  // call instead of once per subdir (path normalization isn't free at
  // per-tile-per-scroll frequency).
  const QDir artRoot(artworkDirectory);
  for (const QString &subdir : coverSubdirPriority()) {
    const QString coverDir = artRoot.absoluteFilePath(subdir);
    result = DirectoryCache::instance().findInDirectory(baseName, coverDir);
    if (!result.isEmpty()) {
      return result;
    }
    if (fullName != baseName) {
      result = DirectoryCache::instance().findInDirectory(fullName, coverDir);
      if (!result.isEmpty()) {
        return result;
      }
    }
  }

  if (lcPerfTrace().isDebugEnabled() && perfTimer.elapsed() > 2) {
    qCDebug(lcPerfTrace) << "findArtworkForFileCached: ms=" << perfTimer.elapsed()
                         << "dir=" << artworkDirectory << "(fallback)";
  }
  return result;
}
} // namespace

QStringList artworkLookupDirectories(const QString &artworkDirectory) {
  if (artworkDirectory.isEmpty()) {
    return {};
  }
  QStringList directories;
  directories.reserve(1 + coverSubdirPriority().size());
  directories.append(artworkDirectory);
  const QDir artRoot(artworkDirectory);
  for (const QString &subdir : coverSubdirPriority()) {
    directories.append(artRoot.absoluteFilePath(subdir));
  }
  return directories;
}

QSet<QString> buildArtworkKeySet(const QString &artworkDirectory) {
  if (artworkDirectory.isEmpty()) {
    return {};
  }
  // Same directory cascade findCachedWithKeys probes per item: the flat root
  // first, then every typed cover subdir. Nonexistent subdirs cost one queued
  // background scan that caches an empty listing — identical to the per-item
  // path, which probes them through findInDirectory regardless.
  return DirectoryCache::instance().collectPositiveKeys(artworkLookupDirectories(artworkDirectory));
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
