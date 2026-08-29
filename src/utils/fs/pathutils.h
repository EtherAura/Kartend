#ifndef PATHUTILS_H
#define PATHUTILS_H

#include "errorutils.h"
#include <QString>
#include <QStringList>

namespace PathUtils {

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

/// FILE-shaped counterparts of validateAndExpandPath (Kartend-80h8o): same
/// `~` / `%collection%` expansion and absolute-path requirement, but the
/// existence gate is QFileInfo::isFile. validateAndExpandPath's gate is
/// QDir::exists — true only for DIRECTORIES — so routing a single-asset
/// file key (placeholderArtwork, a background image…) through it silently
/// resolves every real file to empty. Use these for config keys that name
/// one file; keep validateAndExpandPath for directory keys.
[[nodiscard]] QString validateAndExpandFilePath(const QString &path,
                                                const QString &collectionName = QString());
[[nodiscard]] ErrorUtils::Result<QString>
tryValidateAndExpandFilePath(const QString &path, const QString &collectionName = QString());

/// Validates that a path doesn't contain unsupported shell metacharacters, null
/// bytes, newlines, backslashes, or a `..` traversal segment — characters that
/// could enable command injection or let the path escape its intended directory
/// (Kartend-w13c). Ampersands are allowed because they are common in filenames
/// and safe when paths are passed as process arguments without shell
/// interpretation. Returns success if path is safe, or an error context
/// describing the issue.
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

/// The sanctioned whole-file durable write: creates the parent directory,
/// writes `data` to a sibling temp file via QSaveFile (cancelling on a short
/// write), atomic-renames it over `filePath` on commit, then syncDirectory()s
/// the parent so the rename itself survives a crash or power loss. Returns
/// false after logging the failing stage under `kartend.pathutils`; a failed
/// call never leaves a partial file at `filePath`. Deliberately bool rather
/// than Result<void>: every adopter either consumes it as a plain bool or
/// wraps the failure in its own domain-specific ErrorContext (naming *what*
/// it was writing), so a generic Result here would only be re-wrapped.
/// Writers that stream their payload incrementally (KartWriter) or batch the
/// directory fsync across many files (CacheDiskStorage) keep their own
/// QSaveFile sequence — see docs/dev/architecture.md "Atomic File Writes".
[[nodiscard]] bool atomicWriteFile(const QString &filePath, const QByteArray &data);

// Kartend-qubev: true when @p dirPath is a directory owned by the current
// effective user with no group/other access bits set (POSIX mode & 0077 == 0).
// This is the "safe to trust on a shared host" test used before reusing a
// persistent cache directory under a world-writable temp root: a co-resident
// attacker who pre-created the directory would own it (different uid) or leave
// it group/other-accessible, and either fails the check. Returns false if the
// path doesn't exist or isn't a directory. On non-POSIX platforms (where the
// system temp dir is already per-user) it returns true.
[[nodiscard]] bool isPrivateDirOfCurrentUser(const QString &dirPath);

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

/// True iff `s` is a single safe path component: non-empty, contains no path
/// separator (`/` or `\`), and is neither "." nor "..". Used to refuse building
/// a filesystem path from an untrusted provider/response string. Shared by the
/// scraper persistence and ScreenScraper parser paths (Kartend-2mol7).
[[nodiscard]] bool isSafePathComponent(const QString &s);

/// Canonical sanitizer for a FILENAME BASE derived from a title — the single
/// rule set every "turn this display name into a file on disk" path must use.
///
/// Applies, in order: replace path separators, shell metacharacters and C0
/// control characters (including NUL) with @p replacement; simplify whitespace;
/// chop trailing dots and spaces (Windows drops both, so "Disc 1..." and
/// "Disc 1" would collide); strip leading dashes (a name starting with one is
/// argv-flag-shaped if the path is ever passed through verbatim); prefix "t_"
/// onto a Windows reserved device name; cap at 120 characters; fall back to
/// @p fallback when nothing survives.
///
/// The reserved-name rule (Kartend-ildfg) PREFIXES rather than rejects: CON,
/// PRN, AUX, NUL, COM1-9 and LPT1-9 cannot be created on Windows in any
/// directory, at any extension, but both callers here are WRITERS turning a
/// display name into a filename — so a release genuinely titled "NUL" becomes
/// "t_NUL" and stays recognisable, where falling back to "Untitled" would throw
/// the user's name away. kartreader's isSegmentSafe deliberately does the
/// opposite and REFUSES them: it validates untrusted bundle input, where
/// rewriting a hostile name is the wrong answer.
///
/// Exists because two sanitizers ~40 lines apart had silently diverged
/// (Kartend-tb5nb): the launcher-import stub namer did all of the above while
/// the .m3u playlist namer did the character replacement ALONE — no trailing-dot
/// chop, no control-character or NUL strip, no leading-dash strip, no length
/// cap. Containment held, so it was drift risk rather than a live hole, but the
/// two must not be able to drift again.
///
/// Two parameters exist ONLY to preserve each caller's deliberate, tested
/// behaviour — they are cosmetic/portability choices, never security ones:
///   - @p replacement: the stub namer has always substituted a space and the
///     playlist namer an underscore. Unifying that would rename files already
///     on disk for no security gain.
///   - @p extraForbidden: additional characters to replace. The playlist namer
///     passes ":" because a .m3u must be writable on a Windows/SMB share; the
///     stub namer deliberately KEEPS colons, which are legal in POSIX filenames
///     and common in titles ("Half-Life 2: Episode Two") — pinned by its own
///     "colon kept" test case. That difference is a real requirement, not drift.
[[nodiscard]] QString sanitizeFileBaseName(const QString &title, const QString &replacement,
                                           const QString &fallback,
                                           const QString &extraForbidden = QString());

/// True for the MS-DOS device names Windows still reserves in every directory:
/// CON, PRN, AUX, NUL, COM1-9, LPT1-9. Case-insensitive, because the reservation
/// is — "nul", "NUL" and "Nul" all name the same device.
///
/// The caller passes the stem BEFORE the first dot, not the whole base name:
/// Windows resolves "NUL.txt" to the device too, so the extension buys nothing.
///
/// Shared rather than private to sanitizeFileBaseName because it is the ONLY
/// genuinely duplicated part of the tree's three name rules (Kartend-7abos).
/// The rest legitimately differ: sanitizeFileBaseName prefixes "t_" onto a
/// title-derived filename, kartwriter's artworkTypeFileStem does the same to an
/// already-lowercased [a-z0-9_-] stem it built itself, and kartreader's
/// isSegmentSafe REFUSES the name outright because it validates untrusted
/// bundle input. Only the predicate — "is this a reserved device name" — is one
/// fact, and it is the one that must not be able to drift.
[[nodiscard]] bool isWindowsReservedStem(const QString &stem);

} // namespace PathUtils

#endif
