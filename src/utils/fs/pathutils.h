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

/// CLI-seam path sanitizer: expands ~/%collection% (without requiring the
/// result to exist) then runs validatePathSecurity. Used by both
/// CliArgs::parseStartupArguments (unit-testable parse() path) and
/// src/core/main.cpp (production process() path) so --import-kart / --to /
/// other path options get identical pre-flight checks. Does NOT verify
/// existence — downstream KartReader / KartWriter produces a more specific
/// "Cannot open file" error than anything we could synthesize here.
/// `optionName` is woven into the error message ("--import-kart is empty
/// after expansion") so users see which flag they mistyped.
[[nodiscard]] ErrorUtils::Result<QString> expandAndValidateCliPath(const QString &raw,
                                                                   const QString &optionName);

// Flushes the parent directory's metadata to disk so that a recently-renamed
// or newly-created file survives a crash or power loss. POSIX-only; no-op on
// other platforms (NTFS journals directory metadata, no portable equivalent).
// Returns true if the sync succeeded or the platform has nothing to do.
bool syncDirectory(const QString &dirPath);

} // namespace PathUtils

#endif
