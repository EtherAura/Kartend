// Stream-hashing of ROM files for scraper ROM identification.
// Single-pass over the file, both MD5 and SHA-1 computed in lockstep
// so the disk read happens once.
#include "romhasher.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include <array>

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

// Streaming CRC-32 (the reflected zip/PNG polynomial 0xEDB88320 — the
// identifier No-Intro / Redump DATs and ScreenScraper key on). Qt's
// QCryptographicHash has no CRC-32 and qChecksum() is only CRC-16, so
// the small table-driven implementation lives here. Fed the same
// chunks as the MD5 / SHA-1 hashes so the file is still read once.
class Crc32 {
public:
  void addData(const QByteArray &chunk) {
    for (const char ch : chunk) {
      const auto byte = static_cast<quint8>(ch);
      m_crc = (m_crc >> 8) ^ table()[(m_crc ^ byte) & 0xFFu];
    }
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

ErrorUtils::Result<Result> hashFile(const QString &filePath) {
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

ErrorUtils::Result<Result> hashArchiveInnerRom(const QString &archivePath) {
  if (archivePath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Empty archive path",
                               "RomHasher::hashArchiveInnerRom");
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
                                 "RomHasher::hashArchiveInnerRom")
          .withDetails(archivePath);
    }
    info = QFileInfo(canonical);
  }
  if (!info.isFile()) {
    return ErrorContext::error(ErrorCode::FileNotFound,
                               "Archive does not exist or is not a regular file",
                               "RomHasher::hashArchiveInnerRom")
        .withDetails(archivePath);
  }
  const QString resolvedArchivePath = info.absoluteFilePath();

  // Pick whichever extractor the user has on PATH. Same priority order
  // as LaunchManager's extractor — 7z first because it handles the
  // widest format set; unzip / bsdtar as fallbacks for stripped-down
  // installs.
  QString extractor;
  QStringList args;
  for (const QString &cmd :
       {QStringLiteral("7z"), QStringLiteral("unzip"), QStringLiteral("bsdtar")}) {
    if (!QStandardPaths::findExecutable(cmd).isEmpty()) {
      extractor = cmd;
      break;
    }
  }
  if (extractor.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "No archive extraction tool found",
                               "RomHasher::hashArchiveInnerRom")
        .withDetails("Install 7z, unzip, or bsdtar to hash inner ROMs");
  }

  // QTemporaryDir auto-cleans on destruction so we don't accumulate
  // hash leftovers in /tmp the way LaunchManager's persistent cache
  // does. The hash workflow doesn't need cross-call caching — repeat
  // scrapes of the same item are uncommon and re-extracting is cheap
  // for typical ROM-archive sizes.
  QTemporaryDir tmp;
  if (!tmp.isValid()) {
    return ErrorContext::error(ErrorCode::FileWriteError,
                               "Failed to create temporary extraction directory",
                               "RomHasher::hashArchiveInnerRom");
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
  if (!proc.waitForFinished(ARCHIVE_EXTRACT_TIMEOUT_MS)) {
    proc.kill();
    proc.waitForFinished(1000);
    return ErrorContext::error(ErrorCode::OperationCancelled, "Archive extraction timed out",
                               "RomHasher::hashArchiveInnerRom")
        .withDetails(archivePath);
  }
  if (proc.exitCode() != 0) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Archive extraction failed",
                               "RomHasher::hashArchiveInnerRom")
        .withDetails(QString::fromUtf8(proc.readAllStandardError()).left(200));
  }

  // Walk the extracted tree, picking the largest non-symlink regular
  // file. ROM archives are nearly always one dump file plus optional
  // small sidecars (readme, NFO, .cue index). Largest-wins is the
  // simplest heuristic that lands on the right file without us having
  // to learn any per-platform extension list (which the project's
  // no-platform-names rule forbids anyway).
  const QString rootCanonical = QFileInfo(tmp.path()).canonicalFilePath();
  if (rootCanonical.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound,
                               "Failed to canonicalise extraction directory",
                               "RomHasher::hashArchiveInnerRom");
  }
  const QString rootPrefix = rootCanonical + QLatin1Char('/');

  QDirIterator it(tmp.path(), QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  QString largestPath;
  qint64 largestSize = -1;
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
      largestSize = sz;
      largestPath = canon;
    }
  }
  if (largestPath.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound,
                               "Archive contained no regular files to hash",
                               "RomHasher::hashArchiveInnerRom")
        .withDetails(archivePath);
  }
  return hashFile(largestPath);
}

} // namespace RomHasher
