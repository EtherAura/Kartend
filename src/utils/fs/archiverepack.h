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
/// repack() returns an error — callers should gate on this and skip repacking.
[[nodiscard]] bool nativeAvailable();

/// True when an archive of this path's type can be rewritten by repack() — zip
/// and 7z (the ROM container formats libarchive can write). rar/gz/tar etc. are
/// not, so callers should not plan a repack for them. Pure extension check.
[[nodiscard]] bool isRepackableFormat(const QString &archivePath);

/// Rewrite srcArchive into dstArchive (zip or 7z, matching the source format),
/// renaming every entry whose name is a key of entryRenames to its mapped value;
/// all other entries are copied
/// verbatim. dstArchive MUST differ from srcArchive — callers write to a temp
/// path and swap, so a failure never corrupts the original. Returns the number
/// of entries actually renamed, so a caller can detect a stale plan whose rename
/// matched nothing. Streams through libarchive with no temp extraction, and
/// refuses to write two entries with the same name (which would silently drop a
/// ROM). Requires libarchive — errors when it is not built in.
[[nodiscard]] ErrorUtils::Result<int> repack(const QString &srcArchive, const QString &dstArchive,
                                             const QHash<QString, QString> &entryRenames);

} // namespace ArchiveRepack

#endif // KARTEND_UTILS_FS_ARCHIVEREPACK_H
