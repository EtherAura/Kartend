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

/// Validates a collection name before it is substituted into a launcher
/// template via `%collection%`. Rejects names that could inject path
/// traversal once substituted — i.e. names containing `/`, `\`, or any
/// segment equal to `..`. Empty names are rejected too. This is the
/// defence-in-depth check at the launch-command seam; definition-time
/// validation in the settings dialog and kart importer is tracked
/// separately.
[[nodiscard]] ErrorUtils::Result<void>
validateCollectionNameForSubstitution(const QString &collectionName);

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

// Existence + kind + permission status for a stored path (Kartend-qc1c). The
// loader uses these helpers to surface "the binary you configured isn't on
// this host anymore" / "the artwork dir got unmounted" at startup instead of
// at first launch or first paint. The status enum is also intended for a
// future settings-dialog UI hint (warning glyph next to the field) — see
// Kartend-qc1c follow-ups.
enum class PathStatus {
  OK,            ///< Exists with the required kind + permissions.
  Empty,         ///< Input was empty — not really a failure, just "no path stored".
  Missing,       ///< Path doesn't resolve to an entry on disk.
  NotExecutable, ///< Exists but lacks +x (only checked for launcher binaries).
  NotReadable,   ///< Exists but lacks +r (file or dir not readable).
  WrongType,     ///< Exists but isn't the expected kind (e.g. file where dir expected).
};

/// Status check for a launcher binary path. Bare commands (no '/') are
/// resolved via QStandardPaths::findExecutable so a PATH-only launcher
/// reports OK iff it's actually on the host. Same set of checks as
/// LaunchManager::validateLauncherPath but without the
/// sensitive-directory / canonical-symlink / shell-metacharacter side
/// validations — those are launch-time security concerns, not load-time
/// "does this still exist" status.
[[nodiscard]] PathStatus checkLauncherPath(const QString &path);

/// Status check for an artwork directory. Returns OK when the path exists,
/// is a directory, and is readable.
[[nodiscard]] PathStatus checkDirectoryPath(const QString &path);

/// Status check for a regular file (placeholder artwork, header logo
/// image, etc.). Returns OK when the path exists, is a file, and is
/// readable.
[[nodiscard]] PathStatus checkFilePath(const QString &path);

/// Single-line English string suitable for inclusion in a log warning.
/// Returns an empty string for `Empty` / `OK` so the caller doesn't need
/// to special-case those.
[[nodiscard]] QString pathStatusDescription(PathStatus status);

} // namespace PathUtils

#endif
