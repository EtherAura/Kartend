#ifndef PATHUTILS_H
#define PATHUTILS_H

#include "errorutils.h"
#include <QString>
#include <QStringList>

namespace PathUtils {

// Reason for validation failure
enum class PathValidationError { EmptyPath, NotAbsolute, DoesNotExist };

// Legacy function - returns empty string on failure (backward compatible)
[[nodiscard]] QString validateAndExpandPath(const QString &path,
                                            const QString &collectionName = QString());

// New function - returns Result with structured error context
[[nodiscard]] ErrorUtils::Result<QString>
tryValidateAndExpandPath(const QString &path, const QString &collectionName = QString());

/// Expands `~/`, `~`, and `%collection%` placeholders **without** requiring
/// the resulting path to exist. Use this when the caller intends to create
/// the directory itself (e.g. mkpath) — `validateAndExpandPath` would
/// reject a not-yet-created path with `DoesNotExist`. Returns an empty
/// string when the input is blank after expansion.
[[nodiscard]] QString expandPathWithoutExistenceCheck(const QString &path,
                                                      const QString &collectionName = QString());

[[nodiscard]] QString truncatePathForDisplay(const QString &path, int maxLength = 50);
[[nodiscard]] QString normalizeDisplayName(const QString &input);

/// Validates that a path doesn't contain unsupported shell metacharacters, null
/// bytes, newlines, or other characters that could enable command injection.
/// Ampersands are allowed because they are common in filenames and safe when
/// paths are passed as process arguments without shell interpretation.
/// Returns success if path is safe, or an error context describing the issue.
[[nodiscard]] ErrorUtils::Result<void> validatePathSecurity(const QString &path);

// Flushes the parent directory's metadata to disk so that a recently-renamed
// or newly-created file survives a crash or power loss. POSIX-only; no-op on
// other platforms (NTFS journals directory metadata, no portable equivalent).
// Returns true if the sync succeeded or the platform has nothing to do.
bool syncDirectory(const QString &dirPath);

} // namespace PathUtils

#endif
