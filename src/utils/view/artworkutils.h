#ifndef ARTWORKUTILS_H
#define ARTWORKUTILS_H

#include "errorutils.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QMutex>
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
   * @brief Clear all cached directory contents and queued directories.
   * Call when collection changes or artwork directories are modified.
   */
  void clear();

private:
  DirectoryCache() = default;
  void ensureDirectoryCached(const QString &directory);

  mutable QMutex m_mutex;
  // Maps directory path -> (baseName lowercase -> full artwork path)
  QHash<QString, QHash<QString, QString>> m_cache;
  // Directories requested but not yet scanned
  QSet<QString> m_queuedDirectories;
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
 * @brief Find artwork file with structured error reporting.
 *
 * Same as findArtworkForFile() but returns a Result<QString> with
 * detailed error context on failure.
 *
 * Error codes:
 * - InvalidInput: Empty fileName or artworkDirectory
 * - DirectoryNotFound: Artwork directory doesn't exist
 * - FileNotFound: No matching artwork file found
 *
 * @param fileName The media filename to find artwork for.
 * @param artworkDirectory The directory to search in.
 * @return Result containing artwork path on success, or ErrorContext on
 * failure.
 */
[[nodiscard]] ErrorUtils::Result<QString> tryFindArtworkForFile(const QString &fileName,
                                                                const QString &artworkDirectory);

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
