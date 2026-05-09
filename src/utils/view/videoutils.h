#ifndef VIDEOUTILS_H
#define VIDEOUTILS_H

#include <QString>
#include <QStringList>

/**
 * @brief Utility functions for per-item preview video file lookup.
 *
 * Mirrors the artwork lookup pattern in ArtworkUtils but for video previews.
 * Stateless, thread-safe.
 */
namespace VideoUtils {

/**
 * @brief Canonical lowercase video extensions (no dots, no wildcards).
 */
[[nodiscard]] const QStringList &videoBaseExtensions();

/**
 * @brief "*.ext" filters suitable for QDir name filters.
 */
[[nodiscard]] QStringList videoFilters();

/**
 * @brief Find a preview video file matching a media filename.
 *
 * Searches @p videoDirectory for a video file whose base name (without
 * extension) matches the base name of @p fileName. Tries each known video
 * extension in lowercase first, then uppercase.
 *
 * @param fileName The media filename to find a preview for (basename used).
 * @param videoDirectory The directory to search in.
 * @return Full path to the video file if found, empty string otherwise.
 */
[[nodiscard]] QString findVideoForFile(const QString &fileName, const QString &videoDirectory);

} // namespace VideoUtils

#endif // VIDEOUTILS_H
