#ifndef DATPACKIMPORT_H
#define DATPACKIMPORT_H

#include <atomic>
#include <memory>

#include <QString>
#include <QStringList>

#include "errorutils.h"

/// Source-agnostic DAT-pack import (Kartend-m6qsb.22): bring DAT catalogues
/// from ANY cataloguer (Redump, TOSEC, No-Good, a hand-downloaded pack) into
/// the DAT library folder, so the existing matcher/audit pipeline can use
/// them. Accepts a zip/archive, a folder, or a single .dat file — the audit
/// side already handles any Logiqx/MAME DAT once it's on disk, so this is the
/// one piece that was missing for non-No-Intro sources.
namespace DatPackImport {

using CancelToken = std::shared_ptr<std::atomic<bool>>;

/// Import every `*.dat` reachable from `source` into `destDir` (flattened),
/// returning the destination paths. `source` may be:
///   - an archive (.zip/.7z/… — anything RomHasher::isArchivePath accepts):
///     extracted via NoIntroDownload::extractDatsTo;
///   - a directory: every *.dat under it (recursively) is copied in;
///   - a single .dat file: copied in.
/// Existing files of the same name are overwritten (re-import = refresh).
/// Errors when `source` doesn't exist, yields no DATs, or can't be unpacked.
[[nodiscard]] ErrorUtils::Result<QStringList>
importInto(const QString &source, const QString &destDir, const CancelToken &cancel = {});

} // namespace DatPackImport

#endif // DATPACKIMPORT_H
