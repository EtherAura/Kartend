#ifndef ARTWORKUTILS_H
#define ARTWORKUTILS_H

#include "errorutils.h"

#include <QHash>
#include <QMutex>
#include <QSet>
#include <QString>

/**
 * @brief Utility functions for artwork file operations.
 *
 * Provides stateless helpers for finding and resolving artwork paths.
 * Extracted from ArtworkManager to improve testability and reusability.
 */
namespace ArtworkUtils {

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
   * @brief Check if there are directories waiting to be scanned.
   */
  [[nodiscard]] bool hasQueuedDirectories() const;

  /**
   * @brief Clear all cached directory contents and queued directories.
   * Call when collection changes or artwork directories are modified.
   */
  void clear();

  /**
   * @brief Get cache statistics for diagnostics.
   */
  [[nodiscard]] int cachedDirectoryCount() const;

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

} // namespace ArtworkUtils

#endif // ARTWORKUTILS_H
