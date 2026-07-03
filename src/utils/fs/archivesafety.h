#ifndef ARCHIVESAFETY_H
#define ARCHIVESAFETY_H

#include <QString>

#include "errorutils.h"

/// Pre-extraction archive safety scan.
///
/// The extraction call sites (launch-time archive unpack, DAT-pack unpack)
/// spawn external extractors into a temp directory and then walk the result
/// with QDir::NoSymLinks. Those post-extraction walks only govern which file
/// is *read* — they cannot catch the write-through-symlink zip-slip class: a
/// crafted archive first delivers a symlink entry (`out -> /home/user`) and
/// then a regular file written *through* it (`out/x`), landing the write
/// outside the extraction root where no later walk ever looks.
///
/// scanArchiveEntries() lists the archive with the best available listing
/// tool (bsdtar, else 7z — the same binaries the extraction sites prefer)
/// and refuses archives that contain symlink or hardlink entries, absolute
/// entry paths, Windows drive-absolute paths, or ".." traversal components.
/// Callers run it before spawning the extractor and fail closed on error —
/// including the no-listing-tool case, which cannot happen when the caller
/// selected one of the same binaries as its extractor.
namespace ArchiveSafety {

/// Scan @p archivePath's entry listing for symlink/hardlink entries and
/// path-escape attempts. Returns success only when a listing was obtained
/// and every entry is clean; every other outcome (unsafe entry, listing tool
/// missing or failing, unreadable archive) is an error the caller must treat
/// as "do not extract". Tries bsdtar, then 7z: a security rejection from the
/// first tool is returned immediately, but a tool that simply cannot LIST the
/// format (e.g. bsdtar/libarchive on some RAR5 variants) falls through to the
/// next available tool.
[[nodiscard]] ErrorUtils::Result<void> scanArchiveEntries(const QString &archivePath);

/// Scan @p archivePath using one specific listing tool (@p tool is a binary
/// name, "bsdtar" or "7z"). For callers that extract with a chosen tool and
/// want the security scan to reflect exactly that tool's view of the archive
/// (so a per-tool extraction fallback stays coherent — the tool that will
/// write the bytes is the tool that vetted them). Returns InvalidFilePath on a
/// security rejection (symlink/hardlink/path-escape — caller must abort, never
/// try another tool) and a different code when the tool could not list the
/// archive (caller may try the next tool).
[[nodiscard]] ErrorUtils::Result<void> scanArchiveEntriesWithTool(const QString &archivePath,
                                                                  const QString &tool);

/// True when @p err from a scan is a SECURITY rejection (an unsafe entry),
/// versus a tool-could-not-list failure. A security rejection must never be
/// retried with another tool.
[[nodiscard]] bool isSecurityRejection(const ErrorUtils::ErrorContext &err);

/// True when @p entryName escapes the extraction root by construction:
/// absolute (POSIX or Windows drive/UNC form) or containing a ".." path
/// component (checked on both separator styles). Exposed for tests.
[[nodiscard]] bool entryPathEscapes(const QString &entryName);

} // namespace ArchiveSafety

#endif // ARCHIVESAFETY_H
