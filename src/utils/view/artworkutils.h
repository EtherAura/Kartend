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
   * @brief Artwork filed against a DISC of the release @p baseName names
   * (Kartend-knub1).
   *
   * An image whose own name carries a disc marker — "Recital (Disc 1).png",
   * "Recital [CD 2].jpg" — also answers to the release's base title, so an
   * item named for the release ("Recital") resolves art that was filed per
   * disc. This is what a multi-disc release collapsed into ONE library item
   * needs: the item is keyed on the generated playlist, whose name is the
   * shared base title with the disc tag stripped, while the art beside the
   * media is still named per disc and matches nothing exactly.
   *
   * The lowest disc order wins (disc 1 before disc 2, numbers before letters —
   * MultiDisc::Marker::order), and among equal orders the extension priority
   * of the directory listing decides, so the answer does not depend on
   * readdir order.
   *
   * A FALLBACK, never a substitute: callers must exhaust their exact-name
   * cascade first (findArtworkForFile / findArtworkForFileCached do), because
   * an item's own art always outranks a disc's.
   *
   * Resolved purely from the cached listing — the disc numbers on disk are not
   * knowable from the item's name, so there is nothing to stat-probe for.
   * An uncached directory therefore returns empty and, unlike findInDirectory,
   * is NOT queued here: every caller reaches this only after an exact pass
   * that already queued it.
   *
   * @param baseName The item's base name (without extension).
   * @param artworkDirectory The directory to search in.
   * @return Full path to the disc-marked artwork if one is indexed, empty
   * string otherwise.
   */
  [[nodiscard]] QString findDiscFallbackInDirectory(const QString &baseName,
                                                    const QString &artworkDirectory);

  /**
   * @brief Pre-warm cache for multiple directories (call from background
   * thread).
   * @param directories List of directories to cache.
   */
  void prewarmDirectories(const QStringList &directories);

  /**
   * @brief Re-scan @p directories and REPLACE their cached listings, even when
   * already cached (Kartend-guyc5).
   *
   * prewarmDirectories fills gaps; this one refreshes. The distinction matters
   * for a caller whose whole job is to re-derive the truth about a collection:
   * a listing is only ever re-scanned on clear() (collection switch), so within
   * one session a cached listing can be arbitrarily old, and the cached
   * NEGATIVES it holds are what make a bulk lookup miss a cover added since.
   * Warming would leave that stale entry in place and the caller would record
   * "artless" for an item whose cover is sitting on disk.
   *
   * Both listing halves are rebuilt — exact names AND the disc-marked index —
   * and the contents generation is bumped once, so generation-keyed consumers
   * (FilterManager's artwork key set) rebuild on their next pass. Directories
   * that no longer exist cache an empty listing, exactly as a first scan would.
   *
   * Scans run OUTSIDE the lock and the swap happens under a single write lock,
   * so readers never observe a half-replaced cascade. Blocking and serial —
   * its one caller is already on a background scan thread, and borrowing the
   * prewarm pool would contend with a GUI-scheduled walk for no gain over the
   * ~10 directories a cascade holds. Never call it from the GUI thread.
   */
  void refreshDirectories(const QStringList &directories);

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
   *
   * Release base titles answered by disc-marked artwork
   * (findDiscFallbackInDirectory) are keys too: a bulk predicate built from
   * this set has to agree with what the per-item lookup resolves, or
   * FilterManager's hide-missing-artwork pass would hide a tile that renders
   * with a cover on it.
   */
  [[nodiscard]] QSet<QString> collectPositiveKeys(const QStringList &directories);

  /**
   * @brief collectPositiveKeys, but carrying the resolved PATH per key —
   * the whole cascade folded into one lookup table (Kartend-guyc5).
   *
   * collectPositiveKeys answers "does a cover exist for this name". A caller
   * that has to STORE the answer (the scan pipeline, filling
   * `items.artwork_path`) needs the path the per-item cascade would have
   * returned, and must not re-derive it with a second resolver that could
   * drift from findArtworkForFileCached.
   *
   * Priority reproduces that cascade exactly: @p directories are folded in
   * order and the first positive entry for a key wins, so `front/` outranks
   * the flat root, which outranks `box/`, and so on (Kartend-u67w0, order
   * defined by artworkLookupDirectories). Disc-marked art
   * (findDiscFallbackInDirectory) is folded in a SECOND pass over the same
   * order, so it can only answer for a key no exact name claimed anywhere —
   * the same "a fallback, never a substitute" rule the per-item lookup obeys.
   *
   * Cached negatives contribute nothing, uncached directories contribute
   * nothing and are queued for a background scan, and one read-lock
   * acquisition covers the whole enumeration — all as collectPositiveKeys.
   *
   * One divergence from the per-item cascade, shared with buildArtworkKeySet:
   * the cascade probes stem-then-full-name WITHIN each directory, while a
   * caller of this map probes the whole map by stem and then by full name.
   * The two disagree only for an item that has stem-named art in a
   * lower-priority directory AND full-name-named art in a higher-priority one.
   */
  [[nodiscard]] QHash<QString, QString> collectPositivePaths(const QStringList &directories);

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
  // Kartend-knub1: one disc-marked image per release base title, kept
  // alongside the file name so re-indexing a directory can prefer the lowest
  // disc rather than whichever entry readdir handed over first.
  struct DiscArtwork {
    QString fileName;
    int order = 0;
  };
  struct DirectoryListing {
    QString pathPrefix;
    QHash<QString, QString> byKey;
    // baseMatchKey(release base title) -> the release's lowest-ordered
    // disc-marked image. Deliberately SEPARATE from byKey: these are fallback
    // answers for a name no file actually carries, and byKey's cached-negative
    // convention (present-but-empty value) must keep meaning "the stat sweep
    // for this exact name found nothing".
    QHash<QString, DiscArtwork> byDiscBase;
  };
  // Maps directory path -> listing (baseMatchKey -> bare file name). A
  // contained key with a NULL/empty value is a cached NEGATIVE result
  // (Kartend-bjrw1): the per-extension stat sweep ran once for that
  // (dir, baseName) and found nothing, so re-materializing the same tile
  // skips the sweep entirely. Negative entries (like positives) live until
  // clear() — a file dropped in mid-session becomes visible on the next
  // collection switch, same contract as the directory listing itself.
  QHash<QString, DirectoryListing> m_cache;
  // Index a scanned image under the release base title when its own name
  // carries a disc marker. Static: it only rewrites the listing handed to it,
  // which ensureDirectoryCached builds OUTSIDE the lock.
  static void indexDiscMarkedArtwork(DirectoryListing &listing, const QString &fileName);
  // Walk one directory into a listing. No locking and no cache access — the
  // single indexer shared by the fill path (ensureDirectoryCached) and the
  // refresh path (refreshDirectories), so the two can never index a directory
  // differently from one another.
  [[nodiscard]] static DirectoryListing scanListing(const QString &directory);
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
 * Directories are walked in the shared cover-lookup order (see
 * artworkLookupDirectories): `front/` first, then the flat root, then the
 * remaining typed cover subdirs (Kartend-u67w0).
 *
 * Only when that EXACT cascade comes up empty across every one of those
 * directories does the disc fallback run (Kartend-knub1): art named
 * "<baseName> (Disc 1).png" answers for "<baseName>", which is how a
 * collapsed multi-disc item — named for the release, with the disc tag
 * stripped — finds art filed per disc. That step reads the DirectoryCache
 * index rather than probing (see findDiscFallbackInDirectory), so a cold
 * artwork directory yields the pre-Kartend-knub1 answer until the prewarm
 * lands and the caller re-resolves.
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
 * Same two-stage rule as findArtworkForFile: the exact-name cascade over the
 * shared cover-lookup order (`front/`, then the flat root, then the remaining
 * typed subdirs — Kartend-u67w0) first, then the disc-marked fallback over the
 * same directories (Kartend-knub1).
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
 * @brief The artwork-type ids that can supply an item's PRIMARY COVER, in
 * priority order (`front`, `box`, `box-3d`, … `marquee`).
 *
 * Each id doubles as a subdirectory name under the artwork root — the scrape
 * pipeline writes every media type into its own `{artwork}/<type>/` folder
 * rather than dropping a copy at the flat root. `artworkLookupDirectories`
 * walks exactly these, in this order, with the flat root spliced in after
 * `front` (Kartend-u67w0): the front cover beats a root-level file, the root
 * beats every other subdir. `front` must therefore stay the first entry.
 *
 * `front` is the canonical cover; the rest are fallbacks for items with no
 * dedicated front cover. Types absent from this list (`logo`, and any custom
 * type a collection defines) are gallery-only — they are never auto-discovered
 * as a cover, and a MANUAL link on one does not make an item "have artwork"
 * (Kartend-jkty9). That is the single definition of "a cover type" for
 * SUBDIRECTORY AUTO-DISCOVERY; do not spell the list out a second time.
 *
 * The MANUAL-link fold uses the narrower ItemArtworkStore::manualCoverTypes()
 * (this list ∩ standard types): the scraper writes `item_artwork` rows for its
 * non-standard cover variants (`box-3d`, `mixrbv1`, `mixrbv2`), and those
 * bookkeeping rows must not outrank an auto-discovered front cover
 * (Kartend-u67w0).
 */
[[nodiscard]] const QStringList &coverSubdirPriority();

/**
 * @brief The directory cascade an artwork lookup probes for one item, in
 * priority order: the `front/` subdir, then @p artworkDirectory itself, then
 * the remaining typed cover subdirs (`box/`, `box-3d/`, …) in
 * coverSubdirPriority order (Kartend-u67w0 — a scraped front cover must beat
 * the stale pre-b73642f8 root mirrors, which are often mixrbv composites,
 * while a root-level file still beats every non-front fallback).
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
 * @brief The artwork directory to search for ONE item, with the media
 * subfolder structure mirrored into it where that applies (Kartend-j5amz).
 *
 * A collection can ask for its artwork tree to follow the shape of its media
 * tree — explicitly via `includeArtworkSubfolders`, or implicitly by pointing
 * both settings at the same folder, where "beside the media" is the only thing
 * mirroring can mean. An item at `<media>/Live/Recital.flac` then resolves its
 * cover under `<artwork>/Live/` rather than the artwork root.
 *
 * That mirror is only DEFINED for an item that actually lives under
 * @p mediaDirectory. Two things in Kartend produce items that do not:
 *  - a multi-disc release collapsed into one item, whose file is the playlist
 *    Kartend generates under its own data directory; and
 *  - any load that keys items by their canonical path (symlinks resolved)
 *    while the collection's configured media directory is the symlink's own
 *    spelling.
 * For those, `QDir::relativeFilePath` answers with a `../../..` chain, and
 * appending it walks straight out of the artwork tree — a lookup in a
 * directory that has nothing to do with either setting. The rule here is that
 * an item outside the media directory has NO media-relative subfolder, so the
 * artwork directory is used exactly as given. That is also what the scan-side
 * bulk resolver (ArtworkUtils::buildArtworkPathMap, which mirrors nothing)
 * searches for such an item, so the two agree on the answer.
 *
 * @p artworkDirectory is returned unchanged when it is empty, when
 * @p mediaDirectory is empty, when neither mirroring trigger applies, and when
 * the item sits at the media root (there is no subfolder to mirror).
 *
 * Pure string work — no filesystem or cache access. Callers pass the artwork
 * directory they have already resolved for the item (the per-item override
 * under `showAllSubcollectionItems` included); this does not look one up.
 */
[[nodiscard]] QString mirroredArtworkDirectory(const QString &artworkDirectory,
                                               const QString &mediaDirectory,
                                               const QString &itemPath,
                                               bool includeArtworkSubfolders);

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
 * @brief buildArtworkKeySet with the resolved cover PATH per key
 * (Kartend-guyc5).
 *
 * Same directory cascade, same keys, same disc-marked fallback — see
 * DirectoryCache::collectPositivePaths for the priority rules and the one
 * documented divergence from the per-item cascade.
 *
 * This is the bulk form of findArtworkForFileCached for callers that must
 * PERSIST what the lookup resolves rather than paint it: the scan pipeline
 * fills `items.artwork_path` from it, so every DB-side artwork predicate
 * (smart playlists, `has:artwork` / `missing:artwork`, Collection Health, the
 * artwork review queue) answers with the same covers the grid paints.
 *
 * Look a key up with baseMatchKey(completeBaseName) first and
 * baseMatchKey(fileName) second, exactly as the key-set callers do.
 *
 * Reads only what the DirectoryCache already holds — callers that need the
 * answer to be complete rather than best-effort must warm the cascade first
 * (DirectoryCache::prewarmDirectories over artworkLookupDirectories()).
 */
[[nodiscard]] QHash<QString, QString> buildArtworkPathMap(const QString &artworkDirectory);

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
 * @brief Add a hairline outline to logo-like artwork that would blend into
 * @p background; returns @p art untouched otherwise.
 *
 * User request 2026-08-17: the collection tree grew this treatment first
 * ("crisp outline when needed"), then the item cards. Two gates keep it to
 * logos: the art must have meaningful transparency (opaque coverage under
 * 92% of its rect — photos and box covers are ~100% and skip), and at least
 * a TENTH of its opaque pixels must sit within the low-contrast band of the
 * background (measured on the real files: an unreadable navy-on-gold mark
 * scores 14%, a fully readable red-on-white one 5%). The outline is the
 * art's alpha silhouette in an opposing shade at the four cardinal offsets
 * of ONE pixel — hairline at any DPR, never a glow. The result is 2px
 * taller/wider (1px pad each side) so the outline is not clipped.
 * CollectionTreeController mirrors these thresholds on its QPixmap path
 * (post style-recolour); keep them in step.
 */
[[nodiscard]] QImage outlineLowContrastArtwork(const QImage &art, const QColor &background);

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
