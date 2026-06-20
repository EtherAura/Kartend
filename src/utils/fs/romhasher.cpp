// Stream-hashing of ROM files for scraper ROM identification.
// Single-pass over the file, both MD5 and SHA-1 computed in lockstep
// so the disk read happens once.
#include "romhasher.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStringList>
#include <QTemporaryDir>

#include <array>
#include <atomic>
#include <memory>

#ifdef KARTEND_HAS_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace RomHasher {

namespace {

// 1 MiB chunks — a sweet spot between memory pressure (negligible)
// and syscall overhead (a 4 GB ISO is 4096 read calls, all served
// from the kernel's readahead cache after the first).
constexpr qint64 CHUNK_SIZE = 1 << 20;

// Extraction timeout. The old 30s was tuned for cartridge-sized ROMs
// (NES/SNES/GBA/etc., a few hundred KB at most). PS1/PS2 disc images
// inside .zip routinely exceed 1 GB; even with `unzip -o` to a local
// SSD a 4 GB ISO needs minutes. With the 30s ceiling the hash silently
// failed on every disc-image collection, dropping SS into filename-only
// matching and producing the wrong-region match Kartend-ou0a tracks.
// 10 minutes covers ~4 GB extracted at 7 MB/s — a slow USB drive in
// the worst case — without making the scrape feel hung.
constexpr int ARCHIVE_EXTRACT_TIMEOUT_MS = 600000;

// Hard cap on the number of files we'll consider as the "largest
// inner file" candidate. Mirrors LaunchManager's malicious-archive
// guard — a zip-bomb-style archive with millions of empty entries
// won't make us walk forever.
constexpr int MAX_INNER_FILES_INSPECTED = 4096;

// Disk-fill DoS guard (Kartend-y4nz). The extractor runs to completion before
// the file-count guard above, so a compression bomb could otherwise fill the
// volume first. While extracting we poll the temp dir's total size and abort if
// it crosses a ceiling = min(absolute cap, free space - safety margin). The
// absolute cap is generous so legit multi-GB disc images still extract; the
// free-space bound stops a bomb exhausting a small volume below that cap.
constexpr qint64 MAX_EXTRACTED_BYTES = qint64(24) * 1024 * 1024 * 1024;      // 24 GiB
constexpr qint64 EXTRACT_FREE_SPACE_MARGIN = qint64(1) * 1024 * 1024 * 1024; // keep 1 GiB free
constexpr int EXTRACT_POLL_INTERVAL_MS = 500;
// Bound the size-walk itself so a millions-of-tiny-files archive can't make the
// poll loop walk forever; hitting it is itself bomb-like, so we abort.
constexpr int MAX_EXTRACT_WALK_ENTRIES = 200000;

// True once the extracted tree's cumulative regular-file bytes exceed `ceiling`
// (or the entry count crosses MAX_EXTRACT_WALK_ENTRIES — a many-tiny-files
// bomb). Walks lazily and early-exits, so a normal ROM (a handful of files)
// costs almost nothing per poll.
bool extractionExceedsCeiling(const QString &root, qint64 ceiling) {
  QDirIterator it(root, QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  qint64 total = 0;
  int walked = 0;
  while (it.hasNext()) {
    it.next();
    if (++walked > MAX_EXTRACT_WALK_ENTRIES) {
      return true;
    }
    total += it.fileInfo().size();
    if (total > ceiling) {
      return true;
    }
  }
  return false;
}

// Streaming CRC-32 (the reflected zip/PNG polynomial 0xEDB88320 — the
// identifier No-Intro / Redump DATs and ScreenScraper key on). Qt's
// QCryptographicHash has no CRC-32 and qChecksum() is only CRC-16, so
// the small table-driven implementation lives here. Fed the same
// chunks as the MD5 / SHA-1 hashes so the file is still read once.
class Crc32 {
public:
  void addData(const QByteArray &chunk) {
    // Hoist the table reference and the running CRC into locals and walk the
    // buffer through a raw pointer. For a multi-GB disc image this is the hot
    // loop running alongside MD5+SHA1, so dropping the per-byte QByteArray
    // iterator and the per-byte table() function-static guard branch is worth
    // it. Same table-driven CRC-32 (poly 0xEDB88320), bit-for-bit identical
    // output to the previous byte-at-a-time form.
    const std::array<quint32, 256> &t = table();
    const auto *p = reinterpret_cast<const quint8 *>(chunk.constData());
    const auto *const end = p + chunk.size();
    quint32 crc = m_crc;
    for (; p != end; ++p) {
      crc = (crc >> 8) ^ t[(crc ^ *p) & 0xFFu];
    }
    m_crc = crc;
  }
  // 8-digit lowercase hex, the form SS and No-Intro / Redump DATs use.
  [[nodiscard]] QString hex() const {
    return QString::number(~m_crc, 16).rightJustified(8, QLatin1Char('0'));
  }

private:
  static const std::array<quint32, 256> &table() {
    static const std::array<quint32, 256> kTable = [] {
      std::array<quint32, 256> t{};
      for (quint32 i = 0; i < 256; ++i) {
        quint32 c = i;
        for (int k = 0; k < 8; ++k) {
          c = (c & 1u) != 0u ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        t[i] = c;
      }
      return t;
    }();
    return kTable;
  }
  quint32 m_crc = 0xFFFFFFFFu;
};

} // namespace

ErrorUtils::Result<Result> hashFile(const QString &filePath,
                                    const std::shared_ptr<std::atomic<bool>> &cancelToken) {
  if (filePath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Empty file path",
                               "RomHasher::hashFile");
  }
  // Symlink-safe: SS hash-ID matches on the underlying file bytes, so we
  // must hash the symlink's target — not the symlink entry itself. Qt's
  // QFile + QFileInfo *usually* dereference symlinks transparently, but
  // the canonical-path round-trip also fixes paths that mix relative
  // segments or escape a mount with `..`, and it surfaces broken
  // symlinks as an empty canonical (rather than letting QFile::open
  // produce a less-actionable "Failed to open" later). Kartend-ou0a:
  // without this, a symlinked ROM silently produced no md5/sha1 in the
  // SS jeuInfos.php URL and SS fell back to filename-only matching —
  // landing on a wrong-region game record.
  QFileInfo info(filePath);
  if (info.isSymLink()) {
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty()) {
      return ErrorContext::error(ErrorCode::FileNotFound, "ROM symlink target does not resolve",
                                 "RomHasher::hashFile")
          .withDetails(filePath);
    }
    info = QFileInfo(canonical);
  }
  if (!info.exists() || !info.isFile()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "ROM file does not exist",
                               "RomHasher::hashFile")
        .withDetails(filePath);
  }
  QFile f(info.absoluteFilePath());
  if (!f.open(QIODevice::ReadOnly)) {
    return ErrorContext::error(ErrorCode::FileNotFound, "Failed to open ROM file for hashing",
                               "RomHasher::hashFile")
        .withDetails(f.errorString());
  }

  QCryptographicHash md5(QCryptographicHash::Md5);
  QCryptographicHash sha1(QCryptographicHash::Sha1);
  Crc32 crc;
  qint64 totalRead = 0;
  while (!f.atEnd()) {
    if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
      f.close();
      return ErrorContext::error(ErrorCode::OperationCancelled, "ROM hashing cancelled",
                                 "RomHasher::hashFile");
    }
    const QByteArray chunk = f.read(CHUNK_SIZE);
    if (chunk.isEmpty()) {
      // Premature end / read error mid-file. Treat as failure rather
      // than silently truncate the hash.
      f.close();
      return ErrorContext::error(ErrorCode::FileNotFound, "Read error while hashing ROM",
                                 "RomHasher::hashFile")
          .withDetails(f.errorString());
    }
    md5.addData(chunk);
    sha1.addData(chunk);
    crc.addData(chunk);
    totalRead += chunk.size();
  }
  f.close();

  Result r;
  r.md5 = QString::fromLatin1(md5.result().toHex());
  r.sha1 = QString::fromLatin1(sha1.result().toHex());
  r.crc = crc.hex();
  r.size = totalRead;
  return r;
}

bool isArchivePath(const QString &filePath) {
  // Mirrors LaunchManager::isArchiveFile. Kept in sync deliberately —
  // both call sites need to recognise the same set so a file the
  // launcher unpacks is also unpacked for SS hash-ID.
  static const QStringList archiveExtensions = {
      QStringLiteral(".zip"), QStringLiteral(".7z"),  QStringLiteral(".rar"), QStringLiteral(".gz"),
      QStringLiteral(".tar"), QStringLiteral(".bz2"), QStringLiteral(".xz")};
  const QString lowered = filePath.toLower();
  for (const QString &ext : archiveExtensions) {
    if (lowered.endsWith(ext)) return true;
  }
  return false;
}

QStringList extractorCandidates(const QString &archivePath) {
  // 7z first (widest format coverage), then unzip ONLY for .zip — it cannot read
  // gz/xz/bz2/tar/7z/rar — then bsdtar/libarchive for the rest. Without the .zip
  // gate, a non-zip archive on a box that has unzip but not 7z was handed to
  // unzip and failed, silently dropping the ROM to filename-only SS matching
  // even when bsdtar could have extracted it (Kartend-akaww).
  QStringList candidates;
  candidates << QStringLiteral("7z");
  if (archivePath.toLower().endsWith(QStringLiteral(".zip"))) {
    candidates << QStringLiteral("unzip");
  }
  candidates << QStringLiteral("bsdtar");
  return candidates;
}

/// Shared extraction harness for the archive hashers: validate + symlink-
/// resolve the path, pick a format-capable extractor, extract into the
/// caller's `tmp` under the disk-ceiling / timeout / cancellation guards,
/// and return the extraction root's canonical path. `origin` keeps every
/// error context naming the public entry point that failed.
static ErrorUtils::Result<QString>
extractArchiveForHashing(const QString &archivePath, QTemporaryDir &tmp,
                         const std::shared_ptr<std::atomic<bool>> &cancelToken,
                         const char *origin) {
  if (archivePath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Empty archive path", origin);
  }
  // Symlink-safe (Kartend-ou0a): canonicalize before invoking the
  // extractor so a symlinked .zip / .7z resolves to its target — matches
  // the hashFile() pattern above. Empty canonical signals a broken
  // symlink; surface as FileNotFound instead of trusting QProcess's
  // less-actionable extractor error.
  QFileInfo info(archivePath);
  if (info.isSymLink()) {
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty()) {
      return ErrorContext::error(ErrorCode::FileNotFound, "Archive symlink target does not resolve",
                                 origin)
          .withDetails(archivePath);
    }
    info = QFileInfo(canonical);
  }
  if (!info.isFile()) {
    return ErrorContext::error(ErrorCode::FileNotFound,
                               "Archive does not exist or is not a regular file", origin)
        .withDetails(archivePath);
  }
  // absoluteFilePath() guarantees a leading '/', so when this is appended as a
  // positional operand to 7z/unzip below it can never be misparsed as an
  // option (no argv-flag injection); bsdtar passes it as -f's argument, also
  // safe. Hence no `--` separator is needed (and `--` would break bsdtar).
  const QString resolvedArchivePath = info.absoluteFilePath();

  // Pick the first format-capable extractor the user has on PATH. The candidate
  // list is gated by extension (extractorCandidates) so a non-.zip archive is
  // never handed to unzip, which can't read it.
  QString extractor;
  QStringList args;
  for (const QString &cmd : extractorCandidates(resolvedArchivePath)) {
    if (!QStandardPaths::findExecutable(cmd).isEmpty()) {
      extractor = cmd;
      break;
    }
  }
  if (extractor.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "No archive extraction tool found", origin)
        .withDetails("Install 7z or bsdtar to hash inner ROMs (unzip handles only .zip)");
  }

  // The caller's QTemporaryDir auto-cleans on destruction so we don't
  // accumulate hash leftovers in /tmp the way LaunchManager's persistent
  // cache does. The hash workflow doesn't need cross-call caching — repeat
  // scrapes of the same item are uncommon and re-extracting is cheap
  // for typical ROM-archive sizes.
  if (!tmp.isValid()) {
    return ErrorContext::error(ErrorCode::FileWriteError,
                               "Failed to create temporary extraction directory", origin);
  }

  if (extractor == QStringLiteral("7z")) {
    args << QStringLiteral("x") << QStringLiteral("-y") << resolvedArchivePath;
  } else if (extractor == QStringLiteral("unzip")) {
    args << QStringLiteral("-o") << resolvedArchivePath;
  } else { // bsdtar
    args << QStringLiteral("-xf") << resolvedArchivePath;
  }

  QProcess proc;
  proc.setWorkingDirectory(tmp.path());
  proc.start(extractor, args);

  // Disk-fill ceiling: the smaller of an absolute cap and the free space (less
  // a margin) at extraction start, so a compression bomb can't exhaust the
  // volume (Kartend-y4nz).
  const qint64 freeBytes = QStorageInfo(tmp.path()).bytesAvailable();
  const qint64 freeBudget =
      freeBytes > EXTRACT_FREE_SPACE_MARGIN ? freeBytes - EXTRACT_FREE_SPACE_MARGIN : 0;
  const qint64 ceiling =
      freeBudget > 0 ? qMin(MAX_EXTRACTED_BYTES, freeBudget) : MAX_EXTRACTED_BYTES;

  // Poll rather than block on a single waitForFinished so we can kill the
  // extractor the moment its output crosses the ceiling — the file-count guard
  // below only runs after extraction has already finished filling the disk.
  QElapsedTimer clock;
  clock.start();
  bool finished = false;
  bool overSize = false;
  bool cancelled = false;
  while (clock.elapsed() < ARCHIVE_EXTRACT_TIMEOUT_MS) {
    if (proc.waitForFinished(EXTRACT_POLL_INTERVAL_MS)) {
      finished = true;
      break;
    }
    // Cooperative cancel: the batch driver flips this when the user cancels.
    // Checked once per poll so we kill the extractor within one poll interval
    // instead of running the (multi-minute) extraction to completion.
    if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
      cancelled = true;
      break;
    }
    if (extractionExceedsCeiling(tmp.path(), ceiling)) {
      overSize = true;
      break;
    }
  }
  if (!finished) {
    proc.kill();
    proc.waitForFinished(1000);
    if (cancelled) {
      return ErrorContext::error(ErrorCode::OperationCancelled, "Archive extraction cancelled",
                                 origin)
          .withDetails(archivePath);
    }
    if (overSize) {
      return ErrorContext::error(ErrorCode::InvalidArgument,
                                 "Archive extraction exceeded the size limit", origin)
          .withDetails(archivePath);
    }
    return ErrorContext::error(ErrorCode::OperationCancelled, "Archive extraction timed out",
                               origin)
        .withDetails(archivePath);
  }
  if (proc.exitCode() != 0) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Archive extraction failed", origin)
        .withDetails(QString::fromUtf8(proc.readAllStandardError()).left(200));
  }

  const QString rootCanonical = QFileInfo(tmp.path()).canonicalFilePath();
  if (rootCanonical.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound,
                               "Failed to canonicalise extraction directory", origin);
  }
  return rootCanonical;
}

#ifdef KARTEND_HAS_LIBARCHIVE
namespace {

// Hash every regular-file member of an archive natively via libarchive — no
// temp directory, no external process (Kartend-cnlsy follow-up). Streams each
// member's decompressed bytes straight through MD5/SHA-1/CRC-32, so it carries
// the shell-out path's defences without the temp disk: the entry-count cap
// guards a millions-of-files bomb, the running decompressed-byte cap guards a
// high-ratio bomb, non-regular entries (dirs, smuggled symlinks, devices) are
// skipped, and the cancel token aborts promptly. memberPath is the in-archive
// path, '/'-separated, leading slashes stripped — matching the shell-out form.
[[nodiscard]] ErrorUtils::Result<QList<MemberResult>>
hashMembersLibarchive(const QString &archivePath,
                      const std::shared_ptr<std::atomic<bool>> &cancelToken, const char *origin) {
  // Mirror the shell-out path's up-front guards (libarchive treats an empty
  // name as a no-op stream that "succeeds" empty, which must instead be a
  // clear error).
  if (archivePath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Empty archive path", origin);
  }
  if (const QFileInfo afi(archivePath); !afi.exists() || !afi.isFile()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "Archive file does not exist", origin)
        .withDetails(archivePath);
  }
  struct archive *a = archive_read_new();
  archive_read_support_filter_all(a);
  archive_read_support_format_all(a);
  if (archive_read_open_filename(a, QFile::encodeName(archivePath).constData(), CHUNK_SIZE) !=
      ARCHIVE_OK) {
    const QString err = QString::fromUtf8(archive_error_string(a));
    archive_read_free(a);
    return ErrorContext::error(ErrorCode::InvalidArgument, "Could not open archive", origin)
        .withDetails(archivePath + QStringLiteral(": ") + err);
  }

  QList<MemberResult> members;
  qint64 totalBytes = 0;
  int inspected = 0;
  struct archive_entry *entry = nullptr;
  int headerRc = ARCHIVE_OK;
  while ((headerRc = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
    if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
      archive_read_free(a);
      return ErrorContext::error(ErrorCode::OperationCancelled, "Archive hashing cancelled",
                                 origin);
    }
    if (++inspected > MAX_INNER_FILES_INSPECTED) {
      break; // malicious-archive guard: stop walking a millions-of-entries bomb
    }
    if (archive_entry_filetype(entry) != AE_IFREG) {
      archive_read_data_skip(a); // dirs / symlinks / devices are not audit units
      continue;
    }
    const char *nm = archive_entry_pathname(entry);
    QString memberPath = nm != nullptr ? QString::fromUtf8(nm) : QString();
    while (memberPath.startsWith(QLatin1Char('/'))) {
      memberPath.remove(0, 1);
    }

    QCryptographicHash md5(QCryptographicHash::Md5);
    QCryptographicHash sha1(QCryptographicHash::Sha1);
    Crc32 crc;
    qint64 sz = 0;
    bool readOk = true;
    const void *buff = nullptr;
    size_t len = 0;
    la_int64_t offset = 0;
    int blockRc = ARCHIVE_OK;
    while ((blockRc = archive_read_data_block(a, &buff, &len, &offset)) == ARCHIVE_OK) {
      if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
        archive_read_free(a);
        return ErrorContext::error(ErrorCode::OperationCancelled, "Archive hashing cancelled",
                                   origin);
      }
      // Zero-copy view over libarchive's block (valid until the next read).
      const QByteArray chunk =
          QByteArray::fromRawData(static_cast<const char *>(buff), static_cast<qsizetype>(len));
      md5.addData(chunk);
      sha1.addData(chunk);
      crc.addData(chunk);
      sz += static_cast<qint64>(len);
      totalBytes += static_cast<qint64>(len);
      if (totalBytes > MAX_EXTRACTED_BYTES) {
        archive_read_free(a);
        return ErrorContext::error(
                   ErrorCode::InvalidArgument,
                   "Archive decompresses past the size ceiling (possible compression bomb)", origin)
            .withDetails(archivePath);
      }
    }
    if (blockRc != ARCHIVE_EOF) {
      readOk = false; // member read error: report empty hashes, keep the rest
    }
    MemberResult m;
    m.memberPath = memberPath;
    if (readOk) {
      m.hashes.md5 = QString::fromLatin1(md5.result().toHex());
      m.hashes.sha1 = QString::fromLatin1(sha1.result().toHex());
      m.hashes.crc = crc.hex();
      m.hashes.size = sz;
    } else {
      m.hashes = Result{}; // size -1
    }
    members.append(m);
  }
  // A header read that ended on a hard error before any member was seen means
  // the file was not a (readable) archive — let the caller fall back.
  const bool fatalOpenError = members.isEmpty() && headerRc != ARCHIVE_EOF;
  archive_read_free(a);
  if (fatalOpenError) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Archive contained no readable members",
                               origin)
        .withDetails(archivePath);
  }
  return members;
}

} // namespace
#endif // KARTEND_HAS_LIBARCHIVE

ErrorUtils::Result<Result>
hashArchiveInnerRom(const QString &archivePath,
                    const std::shared_ptr<std::atomic<bool>> &cancelToken) {
#ifdef KARTEND_HAS_LIBARCHIVE
  // Native first; fall through to the external extractor only if libarchive
  // can't read this archive (a format its build lacks, or a corrupt file the
  // CLI tools happen to recover).
  if (auto native =
          hashMembersLibarchive(archivePath, cancelToken, "RomHasher::hashArchiveInnerRom");
      native.isOk()) {
    // Same largest-wins + ambiguous-multi-dump guard as the shell-out path,
    // computed from the members' sizes.
    const QList<MemberResult> &ms = native.value();
    qint64 largestSize = -1;
    qint64 secondLargestSize = -1;
    const MemberResult *largest = nullptr;
    for (const MemberResult &m : ms) {
      if (m.hashes.size < 0) {
        continue;
      }
      if (m.hashes.size > largestSize) {
        secondLargestSize = largestSize;
        largestSize = m.hashes.size;
        largest = &m;
      } else if (m.hashes.size > secondLargestSize) {
        secondLargestSize = m.hashes.size;
      }
    }
    if (largest == nullptr) {
      return ErrorContext::error(ErrorCode::FileNotFound,
                                 "Archive contained no regular files to hash",
                                 "RomHasher::hashArchiveInnerRom")
          .withDetails(archivePath);
    }
    if (secondLargestSize >= 0 && 2 * secondLargestSize >= largestSize) {
      return ErrorContext::error(
                 ErrorCode::InvalidArgument,
                 "Archive has multiple comparably-large files (multi-disc/track); cannot pick a "
                 "single inner ROM to hash",
                 "RomHasher::hashArchiveInnerRom")
          .withDetails(archivePath);
    }
    return largest->hashes;
  } else if (native.hasErrorCode(ErrorCode::OperationCancelled)) {
    return native.error(); // honor cancel; do not retry via the slow path
  }
#endif
  QTemporaryDir tmp;
  auto extracted =
      extractArchiveForHashing(archivePath, tmp, cancelToken, "RomHasher::hashArchiveInnerRom");
  if (extracted.isError()) {
    return extracted.error();
  }
  const QString rootCanonical = extracted.value();
  const QString rootPrefix = rootCanonical + QLatin1Char('/');

  // Walk the extracted tree, picking the largest non-symlink regular
  // file. ROM archives are nearly always one dump file plus optional
  // small sidecars (readme, NFO, .cue index). Largest-wins is the
  // simplest heuristic that lands on the right file without us having
  // to learn any per-platform extension list (which the project's
  // no-platform-names rule forbids anyway).

  QDirIterator it(tmp.path(), QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  QString largestPath;
  qint64 largestSize = -1;
  qint64 secondLargestSize = -1;
  int inspected = 0;
  while (it.hasNext()) {
    const QString candidate = it.next();
    if (++inspected > MAX_INNER_FILES_INSPECTED) break;
    const QFileInfo entryInfo(candidate);
    if (entryInfo.isSymLink()) continue;
    // Defence-in-depth against malicious archives that smuggle
    // symlinks past NoSymLinks.
    const QString canon = entryInfo.canonicalFilePath();
    if (canon.isEmpty() || (canon != rootCanonical && !canon.startsWith(rootPrefix))) {
      continue;
    }
    const qint64 sz = entryInfo.size();
    if (sz > largestSize) {
      secondLargestSize = largestSize;
      largestSize = sz;
      largestPath = canon;
    } else if (sz > secondLargestSize) {
      secondLargestSize = sz;
    }
  }
  if (largestPath.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound,
                               "Archive contained no regular files to hash",
                               "RomHasher::hashArchiveInnerRom")
        .withDetails(archivePath);
  }
  // Ambiguous multi-dump guard: a single ROM dump is one large file plus small
  // sidecars (.cue / readme / NFO). When a SECOND file is comparably large
  // (>= half the largest), the archive is almost certainly a multi-disc or
  // multi-track set, and "largest wins" would hash an arbitrary member —
  // producing a CRC that matches the wrong scraper record or none. Refuse
  // rather than guess; the caller falls back to name-based matching
  // (Kartend-uhcfm).
  if (secondLargestSize >= 0 && 2 * secondLargestSize >= largestSize) {
    return ErrorContext::error(
               ErrorCode::InvalidArgument,
               "Archive has multiple comparably-large files (multi-disc/track); cannot pick a "
               "single inner ROM to hash",
               "RomHasher::hashArchiveInnerRom")
        .withDetails(archivePath);
  }
  return hashFile(largestPath, cancelToken);
}

ErrorUtils::Result<QList<MemberResult>>
hashArchiveMembers(const QString &archivePath,
                   const std::shared_ptr<std::atomic<bool>> &cancelToken) {
#ifdef KARTEND_HAS_LIBARCHIVE
  // Native first; fall through to the external extractor only on a non-cancel
  // failure (a format libarchive's build can't read).
  if (auto native =
          hashMembersLibarchive(archivePath, cancelToken, "RomHasher::hashArchiveMembers");
      native.isOk() || native.hasErrorCode(ErrorCode::OperationCancelled)) {
    return native;
  }
#endif
  QTemporaryDir tmp;
  auto extracted =
      extractArchiveForHashing(archivePath, tmp, cancelToken, "RomHasher::hashArchiveMembers");
  if (extracted.isError()) {
    return extracted.error();
  }
  const QString rootCanonical = extracted.value();
  const QString rootPrefix = rootCanonical + QLatin1Char('/');

  // Hash EVERY regular member — the archive-per-item audit needs the full
  // contents, unlike the scraper's pick-one-dump heuristic above. The same
  // symlink defence and inspection cap apply; an unreadable member becomes
  // an entry with empty hashes (size -1) so one bad member doesn't void the
  // rest of the archive, while a cancellation aborts the whole call.
  QList<MemberResult> members;
  QDirIterator it(tmp.path(), QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  int inspected = 0;
  while (it.hasNext()) {
    const QString candidate = it.next();
    if (++inspected > MAX_INNER_FILES_INSPECTED) break;
    const QFileInfo entryInfo(candidate);
    if (entryInfo.isSymLink()) continue;
    const QString canon = entryInfo.canonicalFilePath();
    if (canon.isEmpty() || !canon.startsWith(rootPrefix)) {
      continue; // smuggled symlink / escape — same defence as the picker above
    }
    MemberResult m;
    m.memberPath = canon.mid(rootPrefix.size());
    auto hashed = hashFile(canon, cancelToken);
    if (hashed.isError()) {
      if (hashed.hasErrorCode(ErrorCode::OperationCancelled)) {
        return hashed.error();
      }
      m.hashes = Result{}; // unreadable member: empty hashes, size -1
    } else {
      m.hashes = hashed.value();
    }
    members.append(m);
  }
  // The libarchive backend (above) yields members in true central-directory
  // order, which becomes the ZipIndex (Kartend-7iqhl.4). This QDirIterator
  // fallback only runs without libarchive and walks the extracted tree in
  // filesystem order; sort by member path so the derived index is at least
  // deterministic run-to-run (it is not the archive's real order).
  std::sort(members.begin(), members.end(),
            [](const MemberResult &a, const MemberResult &b) { return a.memberPath < b.memberPath; });
  if (members.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound,
                               "Archive contained no regular files to hash",
                               "RomHasher::hashArchiveMembers")
        .withDetails(archivePath);
  }
  return members;
}

} // namespace RomHasher
