#include "datpackimport.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include "nointrodownloader.h"
#include "romhasher.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace DatPackImport {

namespace {

// Copy one .dat into destDir (flattened), overwriting a same-named file so a
// re-import refreshes. Returns the destination path, or empty on failure.
QString copyDatInto(const QString &src, const QString &destDir) {
  const QString dst = QDir(destDir).filePath(QFileInfo(src).fileName());
  if (QFile::exists(dst)) {
    QFile::remove(dst);
  }
  return QFile::copy(src, dst) ? dst : QString();
}

} // namespace

ErrorUtils::Result<QStringList> importInto(const QString &source, const QString &destDir,
                                           const CancelToken &cancel) {
  const QFileInfo info(source);
  if (source.isEmpty() || !info.exists()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "Import source does not exist",
                               "DatPackImport::importInto")
        .withDetails(source);
  }
  if (destDir.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "No destination folder",
                               "DatPackImport::importInto");
  }

  // Archive → reuse the download path's extractor (7z/unzip/bsdtar).
  if (info.isFile() && RomHasher::isArchivePath(source)) {
    return NoIntroDownload::extractDatsTo(source, destDir, cancel);
  }

  QDir().mkpath(destDir);
  QStringList imported;

  // A single .dat file → copy it.
  if (info.isFile()) {
    if (info.suffix().compare(QLatin1String("dat"), Qt::CaseInsensitive) != 0) {
      return ErrorContext::error(ErrorCode::InvalidArgument, "Not a DAT file or supported archive",
                                 "DatPackImport::importInto")
          .withDetails(source);
    }
    const QString dst = copyDatInto(source, destDir);
    if (dst.isEmpty()) {
      return ErrorContext::error(ErrorCode::FileWriteError, "Could not copy the DAT file",
                                 "DatPackImport::importInto")
          .withDetails(source);
    }
    imported.append(dst);
    return imported;
  }

  // A folder → copy every *.dat under it.
  if (info.isDir()) {
    QDirIterator it(source, {QStringLiteral("*.dat")}, QDir::Files | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      if (cancel && cancel->load(std::memory_order_relaxed)) {
        return ErrorContext::error(ErrorCode::OperationCancelled, "Cancelled",
                                   "DatPackImport::importInto");
      }
      const QString dst = copyDatInto(it.next(), destDir);
      if (!dst.isEmpty()) {
        imported.append(dst);
      }
    }
    if (imported.isEmpty()) {
      return ErrorContext::error(ErrorCode::InvalidArgument, "No DAT files found in the folder",
                                 "DatPackImport::importInto")
          .withDetails(source);
    }
    return imported;
  }

  return ErrorContext::error(ErrorCode::InvalidArgument, "Unsupported import source",
                             "DatPackImport::importInto")
      .withDetails(source);
}

} // namespace DatPackImport
