#include "archiverepack.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>

#ifdef KARTEND_HAS_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace ArchiveRepack {

namespace {

constexpr const char *kOrigin = "ArchiveRepack::repack";
// Sanity ceilings mirroring the hasher's compression-bomb guards — generous,
// since these are the user's own archives being fixed, but still bounded so a
// pathological input cannot exhaust the host.
constexpr int kMaxEntries = 200000;
constexpr qint64 kMaxBytes = 64LL * 1024 * 1024 * 1024; // 64 GiB total payload
constexpr size_t kChunk = 1U << 20;

bool isSameFile(const QString &a, const QString &b) {
  return QFileInfo(a).absoluteFilePath() == QFileInfo(b).absoluteFilePath();
}

#ifdef KARTEND_HAS_LIBARCHIVE
// Stream src → dst via libarchive, renaming matched entries. No temp extraction:
// each entry's header (with the new name) and decompressed bytes are piped
// straight into the destination zip.
ErrorUtils::Result<int> repackLibarchive(const QString &src, const QString &dst,
                                         const QHash<QString, QString> &renames) {
  struct archive *in = archive_read_new();
  archive_read_support_filter_all(in);
  archive_read_support_format_all(in);
  if (archive_read_open_filename(in, QFile::encodeName(src).constData(), kChunk) != ARCHIVE_OK) {
    const QString err = QString::fromUtf8(archive_error_string(in));
    archive_read_free(in);
    return ErrorContext::error(ErrorCode::FileReadError, "Could not open source archive", kOrigin)
        .withDetails(src + QStringLiteral(": ") + err);
  }
  struct archive *out = archive_write_new();
  // Preserve the source archive's format: a .7z stays 7z, everything else (the
  // common .zip) is written as zip. libarchive can write both; it can't write
  // rar etc., which isRepackableFormat() keeps the fix engine away from. The
  // destination is a temp with no extension, so the format must come from src.
  if (QFileInfo(src).suffix().toLower() == QLatin1String("7z")) {
    archive_write_set_format_7zip(out);
  } else {
    archive_write_set_format_zip(out);
  }
  if (archive_write_open_filename(out, QFile::encodeName(dst).constData()) != ARCHIVE_OK) {
    const QString err = QString::fromUtf8(archive_error_string(out));
    archive_read_free(in);
    archive_write_free(out);
    return ErrorContext::error(ErrorCode::FileWriteError, "Could not open destination archive",
                               kOrigin)
        .withDetails(dst + QStringLiteral(": ") + err);
  }

  int renamed = 0;
  int entries = 0;
  qint64 total = 0;
  QSet<QString> writtenNames; // guards against writing two entries with one name
  struct archive_entry *entry = nullptr;
  int hdr = ARCHIVE_OK;
  // Tear down both archives, drop any half-written destination, and surface the
  // error. A partial dst must never survive — the caller swaps it over the real
  // file only on success.
  auto fail = [&](ErrorCode code, const QString &msg, const QString &det) {
    archive_read_free(in);
    archive_write_free(out);
    QFile::remove(dst);
    return ErrorContext::error(code, msg, kOrigin).withDetails(det);
  };
  while ((hdr = archive_read_next_header(in, &entry)) == ARCHIVE_OK) {
    if (++entries > kMaxEntries) {
      return fail(ErrorCode::ResourceLimitExceeded, "Archive has too many entries to repack", src);
    }
    const char *nm = archive_entry_pathname(entry);
    QString finalName = nm != nullptr ? QString::fromUtf8(nm) : QString();
    if (const auto it = renames.constFind(finalName); it != renames.constEnd()) {
      finalName = it.value();
      const QByteArray newName = finalName.toUtf8();
      archive_entry_set_pathname(entry, newName.constData());
      ++renamed;
    }
    // Refuse two entries with the same final name: libarchive's zip/7z writer
    // accepts duplicates silently and one would shadow the other on extraction
    // (a lost ROM). The planner already skips colliding renames; guard here too.
    if (!finalName.isEmpty()) {
      if (writtenNames.contains(finalName)) {
        return fail(ErrorCode::FileWriteError, "Refusing to write a duplicate entry name",
                    finalName);
      }
      writtenNames.insert(finalName);
    }
    if (archive_write_header(out, entry) != ARCHIVE_OK) {
      return fail(ErrorCode::FileWriteError, "Could not write archive entry header",
                  dst + QStringLiteral(": ") + QString::fromUtf8(archive_error_string(out)));
    }
    // Copy the entry payload (a no-op for directories: read_data_block returns
    // EOF immediately).
    const void *buff = nullptr;
    size_t len = 0;
    la_int64_t offset = 0;
    int blk = ARCHIVE_OK;
    while ((blk = archive_read_data_block(in, &buff, &len, &offset)) == ARCHIVE_OK) {
      total += static_cast<qint64>(len);
      if (total > kMaxBytes) {
        return fail(ErrorCode::ResourceLimitExceeded,
                    "Archive payload exceeds the repack size ceiling", src);
      }
      // archive_write_data may write fewer bytes than requested; loop until the
      // whole block is consumed or it errors (w <= 0 with bytes left = stuck).
      const char *p = static_cast<const char *>(buff);
      size_t remaining = len;
      while (remaining > 0) {
        const la_ssize_t w = archive_write_data(out, p, remaining);
        if (w <= 0) {
          return fail(ErrorCode::FileWriteError, "Could not write archive entry data",
                      dst + QStringLiteral(": ") + QString::fromUtf8(archive_error_string(out)));
        }
        p += w;
        remaining -= static_cast<size_t>(w);
      }
    }
    if (blk != ARCHIVE_EOF) {
      return fail(ErrorCode::FileReadError, "Could not read archive entry data",
                  src + QStringLiteral(": ") + QString::fromUtf8(archive_error_string(in)));
    }
    if (archive_write_finish_entry(out) != ARCHIVE_OK) {
      return fail(ErrorCode::FileWriteError, "Could not finalize archive entry",
                  dst + QStringLiteral(": ") + QString::fromUtf8(archive_error_string(out)));
    }
  }
  if (hdr != ARCHIVE_EOF) {
    return fail(ErrorCode::FileReadError, "Could not read archive",
                src + QStringLiteral(": ") + QString::fromUtf8(archive_error_string(in)));
  }
  archive_read_free(in);
  const int closeRc = archive_write_close(out);
  archive_write_free(out);
  if (closeRc != ARCHIVE_OK) {
    QFile::remove(dst);
    return ErrorContext::error(ErrorCode::FileWriteError, "Could not finalize destination archive",
                               kOrigin)
        .withDetails(dst);
  }
  return renamed;
}
#endif // KARTEND_HAS_LIBARCHIVE

} // namespace

#ifdef KARTEND_HAS_LIBARCHIVE
bool nativeAvailable() {
  return true;
}
#else
bool nativeAvailable() {
  return false;
}
#endif

bool isRepackableFormat(const QString &archivePath) {
  const QString ext = QFileInfo(archivePath).suffix().toLower();
  return ext == QLatin1String("zip") || ext == QLatin1String("7z");
}

ErrorUtils::Result<int> repack(const QString &srcArchive, const QString &dstArchive,
                               const QHash<QString, QString> &entryRenames) {
  if (srcArchive.isEmpty() || dstArchive.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Empty archive path", kOrigin);
  }
  if (isSameFile(srcArchive, dstArchive)) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Repack destination must differ from the source", kOrigin);
  }
  if (const QFileInfo fi(srcArchive); !fi.exists() || !fi.isFile()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "Source archive does not exist", kOrigin)
        .withDetails(srcArchive);
  }
#ifdef KARTEND_HAS_LIBARCHIVE
  return repackLibarchive(srcArchive, dstArchive, entryRenames);
#else
  // No shell-out fallback: `7z rn` applies multi-entry renames sequentially
  // (order-sensitive; swaps/chains corrupt the set) and trusts the exit code, so
  // it can silently mis-name a multi-ROM set. Repack therefore requires the
  // in-process libarchive path; callers gate on nativeAvailable().
  return ErrorContext::error(ErrorCode::InvalidArgument,
                             "Repacking archives requires libarchive (not built in)", kOrigin);
#endif
}

} // namespace ArchiveRepack
