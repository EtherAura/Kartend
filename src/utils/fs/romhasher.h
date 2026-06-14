#ifndef ROMHASHER_H
#define ROMHASHER_H

#include <atomic>
#include <memory>

#include <QList>
#include <QString>
#include <QStringList>

#include "errorutils.h"

/// Stream-hashes ROM/media files for hash-based scraper ROM
/// identification. ScreenScraper.fr's jeuInfos.php endpoint accepts
/// `md5=` / `sha1=` / `crc=` / `romsize=` query params; passing them
/// alongside (or in place of) `romnom` dramatically improves match
/// quality vs filename-only lookups.
///
/// Implementation: chunked QFile reads through QCryptographicHash so
/// big disc images (multi-GB ISOs) don't pin the file in RAM. The
/// computation is synchronous on the calling thread — providers that
/// scrape a single item at a time can run it from the main thread
/// (typical ROMs hash sub-second; gigabyte-class ISOs cause a brief
/// freeze that the result-dialog's wait-cursor covers). Future
/// background-batch scraping (the rate-limit-throttled batch issue)
/// will move this off-thread.
namespace RomHasher {

/// Computed hashes for a single file. All fields filled when the
/// hash succeeded; empty + size=-1 on read failure.
struct Result {
  /// Lowercase hex digest of the file's MD5.
  QString md5;
  /// Lowercase hex digest of the file's SHA-1.
  QString sha1;
  /// Lowercase, zero-padded 8-digit hex of the file's CRC-32 (the
  /// zip/PNG polynomial, the identifier No-Intro / Redump DATs key
  /// on). Empty only on read failure.
  QString crc;
  /// File size in bytes; -1 on failure.
  qint64 size = -1;
};

/// Stream-hash the file at `filePath`. Returns an empty Result (size
/// = -1) on read failure. Reads the file once; both hashes are
/// computed in the same pass. Chunk size is tuned for typical ROM
/// sizes — small enough to keep memory low, large enough that the
/// per-call overhead is amortised. When @p cancelToken is set true
/// mid-stream the hash aborts promptly with ErrorCode::OperationCancelled
/// (checked once per chunk), so a batch cancel doesn't keep hashing a
/// multi-GB ISO on a worker thread after the user already bailed.
[[nodiscard]] ErrorUtils::Result<Result>
hashFile(const QString &filePath, const std::shared_ptr<std::atomic<bool>> &cancelToken = {});

/// True when `filePath` ends with one of the recognised archive
/// extensions (.zip / .7z / .rar / .gz / .tar / .bz2 / .xz). Mirrors
/// LaunchManager::isArchiveFile (kept here so the hasher doesn't pull
/// LaunchManager into the scraper module).
[[nodiscard]] bool isArchivePath(const QString &filePath);

/// Archive extractors (command names, in priority order) able to handle
/// `archivePath`'s format. `unzip` reads only `.zip`, so it is omitted for
/// `.gz`/`.xz`/`.bz2`/`.tar`/`.7z`/`.rar`, where it would fail; `7z` and
/// `bsdtar` cover the broad set. The caller runs the first that is present on
/// PATH. Exposed so the capability matrix can be unit-tested without the tools.
[[nodiscard]] QStringList extractorCandidates(const QString &archivePath);

/// Extract the archive at `archivePath` to a temporary directory,
/// pick the largest regular file inside, and return its hash. The
/// "largest file" heuristic picks the right thing for typical ROM
/// archives (single dump file plus optional readme / NFO sidecars).
/// Falls back to an error when no extractor (7z / unzip / bsdtar) is
/// installed, the archive can't be opened, or it contains no
/// regular files. The temp directory is auto-cleaned when the call
/// returns. Symlinks inside the archive are skipped to keep a
/// malicious archive from making us hash an arbitrary path on disk.
/// When @p cancelToken is set true mid-extraction, the extractor
/// QProcess is killed and the call aborts with OperationCancelled —
/// this is the multi-minute path a batch cancel needs to interrupt.
[[nodiscard]] ErrorUtils::Result<Result>
hashArchiveInnerRom(const QString &archivePath,
                    const std::shared_ptr<std::atomic<bool>> &cancelToken = {});

/// One archive member's hashes. `memberPath` is the member's path inside the
/// archive, '/'-separated with no leading slash (subfolders preserved).
struct MemberResult {
  QString memberPath;
  Result hashes;
};

/// Extract `archivePath` once and hash EVERY regular file inside. The
/// archive-per-item audit (Kartend-m6qsb.7) needs all members — unlike the
/// scraper's hashArchiveInnerRom, which must identify exactly one dump.
/// Shares hashArchiveInnerRom's extractor selection, size-ceiling, timeout,
/// cancellation, and smuggled-symlink defences. An unreadable member yields
/// an entry with empty hashes (size -1) rather than failing the archive;
/// cancellation aborts the whole call with OperationCancelled.
[[nodiscard]] ErrorUtils::Result<QList<MemberResult>>
hashArchiveMembers(const QString &archivePath,
                   const std::shared_ptr<std::atomic<bool>> &cancelToken = {});

} // namespace RomHasher

#endif // ROMHASHER_H
