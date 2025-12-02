#ifndef ARTWORKUTILS_H
#define ARTWORKUTILS_H

#include "errorutils.h"

#include <QString>

/**
 * @brief Utility functions for artwork file operations.
 *
 * Provides stateless helpers for finding and resolving artwork paths.
 * Extracted from ArtworkManager to improve testability and reusability.
 */
namespace ArtworkUtils {

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
[[nodiscard]] QString findArtworkForFile(const QString &fileName,
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
 * @return Result containing artwork path on success, or ErrorContext on failure.
 */
[[nodiscard]] ErrorUtils::Result<QString>
tryFindArtworkForFile(const QString &fileName, const QString &artworkDirectory);

} // namespace ArtworkUtils

#endif // ARTWORKUTILS_H
