#ifndef ARTWORKUTILS_H
#define ARTWORKUTILS_H

#include <QAtomicInteger>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QReadWriteLock>
#include <QSet>
#include <QString>
#include <QStringList>

/**
 * @brief Utility functions for artwork file operations.
 *
 * Provides stateless helpers for finding and resolving artwork paths.
 * Extracted from ArtworkManager to improve testability and reusability.
 */
namespace ArtworkUtils {

/// Case-fold an artwork basename for cache keying and lookup. Artwork is matched
/// by completeBaseName, so two item files sharing a basename (e.g. Title.iso +
/// Title.cue) intentionally resolve to the same cover. Case handling follows the
/// platform's default filesystem: case-insensitive on Windows / macOS (so
/// 'Front.iso' resolves 'front.png'), case-sensitive on Linux (so Title.iso and
/// title.iso don't collide on one cover) (Kartend-58ddn).
[[nodiscard]] inline QString baseMatchKey(const QString &completeBaseName) {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  return completeBaseName.toLower();
#else
  return completeBaseName;
#endif
}

/**
 * @brief Directory content cache for fast artwork lookups.
 *
 * Caches the contents of artwork directories to avoid repeated filesystem
 * scans. The cache maps base names (without extension) to full file paths.
 * Thread-safe.
 *
 * Non-blocking mode: When a directory isn't cached, findInDirectory returns
 * empty immediately and queues the directory for background scanning.
 * This prevents main-thread stalls during scrollbar jumps.
 */
class DirectoryCache {
public:
  static DirectoryCache &instance();

  /**
   * @brief Find artwork in directory using cached directory listing.
   *
   * Non-blocking: If directory is not yet cached, returns empty immediately
   * and queues the directory for background scanning. Call again after
   * the prewarm thread has run to get results.
   *
   * @param baseName The base name (without extension) to search for.
   * @param artworkDirectory The directory to search in.
   * @return Full path to artwork file if found, empty string otherwise.
   */
  [[nodiscard]] QString findInDirectory(const QString &baseName, const QString &artworkDirectory);

  /**
   * @brief Pre-warm cache for multiple directories (call from background
   * thread).
   * @param directories List of directories to cache.
   */
  void prewarmDirectories(const QStringList &directories);

  /**
   * @brief Schedule a background dentry prewarm of @p directories on the global
   * thread pool, capped at one in-flight prewarm at a time (Kartend-uzs42).
   *
   * Rapid collection switching would otherwise enqueue a fresh, untracked walk
   * per switch, piling up redundant disk I/O on the shared global pool for
   * collections the user has already left. Best-effort: if a prewarm is already
   * running this call is dropped (the in-flight walk still warms its dirs, and
   * the newly-selected collection's own enumeration warms the rest). No-op for
   * an empty list. Safe to call from the GUI thread.
   */
  void schedulePrewarm(const QStringList &directories);

  /**
   * @brief Process queued directories in background.
   * Called by the background thread to scan directories that were
   * requested but not yet cached.
   */
  void processQueuedDirectories();

  /**
   * @brief Check whether @p directory is currently pending or being
   * processed by a background prewarm.
   *
   * Callers use this to decide whether a synchronous filesystem fallback
   * is worthwhile. If a prewarm is in flight, the cache will be warm
   * within tens of ms and the post-prewarm reconfigure path will fire —
   * paying for a synchronous stat in the meantime just doubles the work
   * and blocks the GUI thread per call. Returns true while the directory
   * is in `m_queuedDirectories`, which holds entries from the moment
   * they're queued until `prewarmDirectories` / `processQueuedDirectories`
   * removes them after the scan finishes.
   */
  [[nodiscard]] bool isDirectoryQueued(const QString &directory) const;

  /**
   * @brief True when @p directory's listing is already in the cache
   * (Kartend-urrpp).
   *
   * Callers use this to prefer the O(1) cached lookup (positive AND
   * negative entries, self-patching on first miss) over a synchronous
   * per-extension stat sweep. Distinct from isDirectoryQueued(): queued
   * means a background scan is pending; cached means lookups are warm now.
   */
  [[nodiscard]] bool isDirectoryCached(const QString &directory) const;

  /**
   * @brief Clear all cached directory contents and queued directories.
   * Call when collection changes or artwork directories are modified.
   */
  void clear();

private:
  DirectoryCache() = default;
  void ensureDirectoryCached(const QString &directory);

  // Reader/writer split (Kartend-s723v): the cache-hit path — per visible
  // tile during scroll, from the GUI thread AND artwork worker threads —
  // only reads, so lookups take a shared read lock and run concurrently.
  // The write lock is reserved for inserts/patches/clear, which are brief
  // (directory scans happen outside the lock).
  mutable QReadWriteLock m_lock;
  // Maps directory path -> (baseMatchKey -> full artwork path). A contained
  // key with a NULL/empty value is a cached NEGATIVE result (Kartend-bjrw1):
  // the per-extension stat sweep ran once for that (dir, baseName) and found
  // nothing, so re-materializing the same tile skips the sweep entirely.
  // Negative entries (like positives) live until clear() — a file dropped in
  // mid-session becomes visible on the next collection switch, same contract
  // as the directory listing itself.
  QHash<QString, QHash<QString, QString>> m_cache;
  // Directories requested but not yet scanned
  QSet<QString> m_queuedDirectories;
  // Kartend-uzs42: 1 while a schedulePrewarm() walk is in flight; caps the
  // background prewarm to one concurrent global-pool task so rapid collection
  // switches don't pile up redundant directory walks.
  QAtomicInteger<int> m_prewarmInFlight = 0;
};

/**
 * @brief Find artwork file matching a media filename.
 *
 * Searches the artwork directory for an image file matching the given
 * media filename. Tries both the base name (without extension) and the
 * full filename, with various image extensions in both lower and upper case.
 *
 * Search order:
 * 1. baseName.{png,jpg,jpeg,webp,gif,bmp} (lowercase)
 * 2. baseName.{PNG,JPG,JPEG,WEBP,GIF,BMP} (uppercase)
 * 3. fullName.{extensions} (same pattern)
 *
 * @param fileName The media filename to find artwork for.
 * @param artworkDirectory The directory to search in.
 * @return The full path to the artwork file if found, empty string otherwise.
 */
[[nodiscard]] QString findArtworkForFile(const QString &fileName, const QString &artworkDirectory);

/**
 * @brief Find artwork using cached directory listing (fast path).
 *
 * Uses DirectoryCache to avoid repeated filesystem scans.
 * Preferred for bulk operations like showAllSubcollectionItems.
 *
 * @param fileName The media filename to find artwork for.
 * @param artworkDirectory The directory to search in.
 * @return The full path to the artwork file if found, empty string otherwise.
 */
[[nodiscard]] QString findArtworkForFileCached(const QString &fileName,
                                               const QString &artworkDirectory);

/**
 * @brief findArtworkForFileCached for callers that already hold the
 * extension-stripped stem (QFileInfo::completeBaseName()).
 *
 * findArtworkForFileCached strips its argument itself, so passing it an
 * already-stripped stem double-strips dotted names ("Game v1.2" probes as
 * "Game v1" first) and can resolve another item's artwork. This variant
 * probes exactly the given stem.
 *
 * @param completeBaseName The media file's stem, already stripped.
 * @param artworkDirectory The directory to search in.
 * @return The full path to the artwork file if found, empty string otherwise.
 */
[[nodiscard]] QString findArtworkForBaseNameCached(const QString &completeBaseName,
                                                   const QString &artworkDirectory);

/**
 * @brief Compose the final item-card image: scale-to-fit + center on a
 * background + rounded-corner mask, at the physical target size.
 *
 * Kartend-63wg: this is the per-tile work ItemWidget::onArtworkChanged used
 * to do on the GUI thread for every delivered pixmap. Extracted so the
 * artwork worker can produce the finished card off-thread (QImage + QPainter
 * are reentrant; QPixmap is not) and the GUI just sets it. ItemWidget still
 * calls this for the placeholder / size-mismatch fallback paths, so the two
 * stay pixel-identical.
 *
 * @p source decoded artwork (any DPR; treated as raw pixels).
 * @p targetWidthLogical / @p targetHeightLogical the label's logical size.
 * @p dpr device pixel ratio; output is sized targetW*dpr x targetH*dpr and
 *    tagged with @p dpr so it renders crisp at the logical size.
 * @p cornerRadiusLogical rounded-corner radius in logical px; <=0 disables.
 * @p background fill behind the letterboxed artwork.
 * Returns a null QImage when the target size is empty or @p source is null.
 */
[[nodiscard]] QImage composeArtworkCard(const QImage &source, int targetWidthLogical,
                                        int targetHeightLogical, qreal dpr, int cornerRadiusLogical,
                                        const QColor &background);

/**
 * @brief Clear the artwork directory cache.
 * Call when collections change or need fresh filesystem data.
 */
void clearDirectoryCache();

/// Picks the next artwork type id in the cycle order. The cycle
/// is the @p availableTypes list in order, wrapping from the last entry back
/// to the first. The empty-string entry conventionally represents the legacy
/// flat-directory artwork (the "primary"). When @p currentType is not present
/// in @p availableTypes, the first entry is returned so the user always lands
/// on a defined state. When @p availableTypes has fewer than two entries,
/// @p currentType is returned unchanged (cycling has no effect).
[[nodiscard]] QString nextArtworkType(const QString &currentType,
                                      const QStringList &availableTypes);

} // namespace ArtworkUtils

#endif // ARTWORKUTILS_H
