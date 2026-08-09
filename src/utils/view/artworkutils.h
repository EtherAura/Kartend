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
   * @brief True only when EVERY directory in @p directories has a cached
   * listing (Kartend-t4rjw).
   *
   * The single-directory isDirectoryCached() answers a narrower question than
   * most artwork callers actually mean. An item's cover is resolved through a
   * CASCADE — the flat artwork root, then each typed cover subdir (see
   * ArtworkUtils::artworkLookupDirectories) — and warming is not atomic across
   * it: schedulePrewarm walks the roots first and only then drains the subdirs
   * findInDirectory queued. So `isDirectoryCached(root)` can be true while
   * `{root}/front` is still cold, and a caller reading that as "the lookup has
   * settled, an empty result means genuinely artless" concludes the opposite
   * of the truth. Ask about the whole cascade instead.
   *
   * Returns false for an empty list — "nothing is known to be warm" is the
   * safe answer for every caller, all of which use this to decide whether it
   * is still worth retrying.
   *
   * One read-lock acquisition covers the whole list. Unlike collectPositiveKeys
   * this does NOT queue the uncached directories: callers here are polling for
   * a warm-up someone else scheduled, and queueing on every poll would churn
   * the write lock.
   */
  [[nodiscard]] bool areDirectoriesCached(const QStringList &directories) const;

  /**
   * @brief Monotonic generation counter for the cached listings.
   *
   * Incremented under the write lock on every listing mutation — a full
   * directory insert (ensureDirectoryCached), a first-miss positive/negative
   * patch (findInDirectory), and clear(). Callers that precompute derived
   * structures from the listings (see ArtworkUtils::buildArtworkKeySet) key
   * them on this value and rebuild only when it moves, instead of re-probing
   * the cache per item.
   */
  [[nodiscard]] quint64 contentsGeneration() const;

  /**
   * @brief Union of every basename key with a cached POSITIVE artwork path
   * across the listings of @p directories.
   *
   * Present-but-empty values are cached negatives and contribute nothing.
   * Directories without a cached listing are queued for a background scan
   * and contribute nothing, mirroring findInDirectory's non-blocking
   * contract. One read-lock acquisition covers the whole enumeration.
   */
  [[nodiscard]] QSet<QString> collectPositiveKeys(const QStringList &directories);

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
  // Kartend-235kv: one listing per directory, with paths stored factored.
  // Storing the full absolute path per file meant the directory half repeated
  // for every entry — heaptrack measured 68.5M retained across 277k artwork
  // files. `pathPrefix` holds the "<directory>/" spelling exactly as the scan
  // produced it (captured once, from the first entry), and `byKey` values are
  // bare file names; positive lookups reconstruct prefix + fileName. The
  // byte-identical reconstruction matters: these paths key CacheManager's
  // memory cache and the MD5-keyed disk artwork cache, so a changed spelling
  // would orphan every existing entry.
  struct DirectoryListing {
    QString pathPrefix;
    QHash<QString, QString> byKey;
  };
  // Maps directory path -> listing (baseMatchKey -> bare file name). A
  // contained key with a NULL/empty value is a cached NEGATIVE result
  // (Kartend-bjrw1): the per-extension stat sweep ran once for that
  // (dir, baseName) and found nothing, so re-materializing the same tile
  // skips the sweep entirely. Negative entries (like positives) live until
  // clear() — a file dropped in mid-session becomes visible on the next
  // collection switch, same contract as the directory listing itself.
  QHash<QString, DirectoryListing> m_cache;
  // Directories requested but not yet scanned
  QSet<QString> m_queuedDirectories;
  // Bumped (under the write lock) on every m_cache mutation; lets derived
  // structures (buildArtworkKeySet callers) detect staleness with one read
  // instead of re-enumerating the listings. See contentsGeneration().
  quint64 m_contentsGeneration = 0;
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
 * @brief The directory cascade a cached artwork lookup probes for one item:
 * @p artworkDirectory itself, then each typed cover subdir (`front/`, `box/`,
 * …) in coverSubdirPriority order.
 *
 * This is the authoritative spelling of "where a cover for this item could
 * live" — findArtworkForFileCached walks exactly these, in this order, and
 * buildArtworkKeySet enumerates exactly these. Callers that need to reason
 * about the lookup as a whole (is it warm? which directories should be
 * prewarmed?) should build the list from here rather than assuming the flat
 * root stands for the cascade — it does not (Kartend-t4rjw).
 *
 * Returns an empty list for an empty @p artworkDirectory. Pure string work, no
 * filesystem or cache access; the result is stable for a given input, so hot
 * callers should hoist it out of per-item loops.
 */
[[nodiscard]] QStringList artworkLookupDirectories(const QString &artworkDirectory);

/**
 * @brief Build the set of artwork-backed basename keys for @p artworkDirectory.
 *
 * Enumerates the flat root plus the typed cover subdirs (`front/`, `box/`, …)
 * through the DirectoryCache listings ONCE and returns every baseMatchKey a
 * findArtworkForFileCached probe would hit. Bulk predicates (FilterManager's
 * hideMissingArtwork pass) test membership with both name-key variants —
 * baseMatchKey(completeBaseName) and baseMatchKey(fileName) — instead of
 * paying the per-item 20-probe cascade (flat root + 9 subdirs x 2 keys, each
 * a lock acquisition and potentially a first-miss stat sweep).
 *
 * Divergences from the per-item cascade, both narrow:
 *  - Uncached directories contribute nothing and are queued for a background
 *    scan — the per-item path also returns empty for them, so a cold cache
 *    behaves identically.
 *  - A file dropped into an ALREADY-CACHED directory mid-session is invisible
 *    here until some lookup's first-miss stat sweep patches it into the cache
 *    (bumping contentsGeneration, so generation-keyed callers converge on
 *    their next pass). The per-item path would find such a file once via its
 *    own sweep; after that sweep caches a negative, both paths agree until
 *    clear().
 */
[[nodiscard]] QSet<QString> buildArtworkKeySet(const QString &artworkDirectory);

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
