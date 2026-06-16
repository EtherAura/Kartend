#include "archiverepack.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

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

// 7z fallback: copy src→dst, then `7z rn <dst> <old1> <new1> …` renames entries
// in place. Used when libarchive is absent or its repack failed.
ErrorUtils::Result<int> repackShellOut(const QString &src, const QString &dst,
                                       const QHash<QString, QString> &renames) {
  const QString sevenZip = QStandardPaths::findExecutable(QStringLiteral("7z"));
  if (sevenZip.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Repacking archives needs libarchive or 7z; neither is available",
                               kOrigin);
  }
  if (QFile::exists(dst) && !QFile::remove(dst)) {
    return ErrorContext::error(ErrorCode::FileWriteError, "Could not clear destination archive",
                               kOrigin)
        .withDetails(dst);
  }
  if (!QFile::copy(src, dst)) {
    return ErrorContext::error(ErrorCode::FileWriteError, "Could not copy archive for repack",
                               kOrigin)
        .withDetails(src + QStringLiteral(" -> ") + dst);
  }
  QStringList args{QStringLiteral("rn"), dst};
  for (auto it = renames.constBegin(); it != renames.constEnd(); ++it) {
    args << it.key() << it.value();
  }
  QProcess proc;
  proc.start(sevenZip, args);
  if (!proc.waitForStarted(10000) || !proc.waitForFinished(120000) ||
      proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
    QFile::remove(dst);
    return ErrorContext::error(ErrorCode::FileWriteError, "7z rename failed", kOrigin)
        .withDetails(QString::fromUtf8(proc.readAllStandardError()));
  }
  return renames.size(); // 7z rn has no per-entry report; assume all requested
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
  archive_write_set_format_zip(out);
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
    const QString name = nm != nullptr ? QString::fromUtf8(nm) : QString();
    if (const auto it = renames.constFind(name); it != renames.constEnd()) {
      const QByteArray newName = it.value().toUtf8();
      archive_entry_set_pathname(entry, newName.constData());
      ++renamed;
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
      if (archive_write_data(out, buff, len) < 0) {
        return fail(ErrorCode::FileWriteError, "Could not write archive entry data",
                    dst + QStringLiteral(": ") + QString::fromUtf8(archive_error_string(out)));
      }
    }
    if (blk != ARCHIVE_EOF) {
      return fail(ErrorCode::FileReadError, "Could not read archive entry data",
                  src + QStringLiteral(": ") + QString::fromUtf8(archive_error_string(in)));
    }
    archive_write_finish_entry(out);
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
  auto native = repackLibarchive(srcArchive, dstArchive, entryRenames);
  if (!native.isError()) {
    return native;
  }
  // Native failed — try the 7z fallback (if present) before giving up; the
  // native path already removed any half-written destination.
  if (!QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty()) {
    return repackShellOut(srcArchive, dstArchive, entryRenames);
  }
  return native;
#else
  return repackShellOut(srcArchive, dstArchive, entryRenames);
#endif
}

} // namespace ArchiveRepack
