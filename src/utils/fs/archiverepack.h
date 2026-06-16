#ifndef KARTEND_UTILS_FS_ARCHIVEREPACK_H
#define KARTEND_UTILS_FS_ARCHIVEREPACK_H

#include <QHash>
#include <QString>

#include "errorutils.h"

/// Rewrite archives, renaming selected member entries. Used by the DAT-audit fix
/// engine to make a zip-packed ROM set DAT-canonical: the inner ROM entry is
/// renamed to its catalogue name while the container is renamed by the caller.
namespace ArchiveRepack {

/// True when the in-process libarchive repack is compiled in. When false,
/// repack() falls back to a `7z rn` shell-out if 7z is on PATH, otherwise errors.
[[nodiscard]] bool nativeAvailable();

/// Rewrite srcArchive into dstArchive (a zip), renaming every entry whose name is
/// a key of entryRenames to its mapped value; all other entries are copied
/// verbatim. dstArchive MUST differ from srcArchive — callers write to a temp
/// path and swap, so a failure never corrupts the original. Returns the number
/// of entries actually renamed, so a caller can detect a stale plan whose rename
/// matched nothing. Streams through libarchive with no temp extraction; on a
/// native failure (or when libarchive is absent) falls back to copying src→dst
/// then `7z rn` per rename if 7z is available.
[[nodiscard]] ErrorUtils::Result<int> repack(const QString &srcArchive, const QString &dstArchive,
                                             const QHash<QString, QString> &entryRenames);

} // namespace ArchiveRepack

#endif // KARTEND_UTILS_FS_ARCHIVEREPACK_H
