// Archive extraction helpers split out from launchmanager.cpp.
#include "errorutils.h"
#include "launchmanager.h"
#include "pathutils.h"
#include "uiconstants/launch.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

#include <atomic>
#include <optional>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcLaunchManager)
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcLaunchManager().isDebugEnabled()) {                                                      \
      qCDebug(lcLaunchManager) << msg;                                                             \
    }                                                                                              \
  } while (0)

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using ErrorUtils::Result;

namespace {
// Kartend-ijglg: true when the extraction dir's cumulative file size exceeds
// maxBytes — the decompressed-size watchdog's measurement. Also trips on an
// absurd entry count (a many-tiny-files bomb costs inodes and scan time
// rather than bytes, and would otherwise make this walk itself the DoS).
// NoSymLinks: a symlink consumes no meaningful space and following one could
// double-count or escape the extraction root.
bool extractionSizeExceeds(const QString &directory, qint64 maxBytes) {
  qint64 total = 0;
  int inspected = 0;
  QDirIterator it(directory,
                  QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks |
                      QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    total += it.fileInfo().size();
    if (total > maxBytes) {
      return true;
    }
    if (++inspected > UIConstants::Launch::MAX_EXTRACTION_FILES_INSPECTED) {
      return true;
    }
  }
  return false;
}
} // namespace

bool LaunchManager::isArchiveFile(const QString &filePath) {
  static const QStringList archiveExtensions = {".zip", ".7z",  ".rar", ".gz",
                                                ".tar", ".bz2", ".xz"};
  QString lowerPath = filePath.toLower();
  for (const QString &ext : archiveExtensions) {
    if (lowerPath.endsWith(ext)) {
      return true;
    }
  }
  return false;
}

auto LaunchManager::extractArchiveToTemp(const QString &archivePath, const QString &targetExtension,
                                         const std::atomic_bool *cancelRequested,
                                         qint64 maxDecompressedBytes)
    -> ErrorUtils::Result<QString> {
  // Validate archivePath at the same gate as media files in
  // buildLaunchCommand. QProcess argument lists prevent shell injection, but
  // unvalidated paths still leak into log output and violate the project's
  // path-security model.
  auto archiveValidation = PathUtils::validatePathSecurity(archivePath);
  if (archiveValidation.isError()) {
    return archiveValidation.error();
  }

  const qint64 maxBytes = maxDecompressedBytes < 0
                              ? static_cast<qint64>(UIConstants::Launch::MAX_EXTRACTION_BYTES)
                              : maxDecompressedBytes;

  // Kartend-ijglg: bound the *input* before any disk work. Compression can
  // only inflate the payload, so an archive whose on-disk size already
  // exceeds the decompressed-size cap can never legitimately extract within
  // it — reject without spawning an extractor.
  if (QFileInfo(archivePath).size() > maxBytes) {
    return ErrorContext::error(ErrorCode::ResourceLimitExceeded,
                               "Archive is larger than the extraction size limit",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(QString("%1 (limit: %2 bytes)").arg(archivePath).arg(maxBytes));
  }

  // Honour a cancellation that raced ahead of the worker actually starting.
  if (cancelRequested && cancelRequested->load()) {
    return ErrorContext::error(ErrorCode::OperationCancelled, "Archive extraction cancelled",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(archivePath);
  }

  // Create a persistent temp directory for extractions
  // Using a subdirectory in the standard temp location that won't auto-delete
  QString tempBasePath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  QString extractDir = tempBasePath + "/kartend_extract";

  // Kartend-qubev: the persistent cache base sits under a world-writable temp
  // root, so on a shared host a co-resident user could pre-create
  // kartend_extract/ (and a crafted per-archive subdir) and have the cache-hit
  // branch serve their payload. Guard it: create the base owner-only (0700)
  // when it's ours to make, and refuse to trust a pre-existing base that isn't
  // private to us. A hijacked base falls back to an unguessable per-run
  // QTemporaryDir — we still launch, just without cross-run caching, and never
  // serve another user's content.
  constexpr QFileDevice::Permissions kOwnerOnly =
      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;

  QDir baseDir(extractDir);
  bool persistentCacheUsable = true;
  if (baseDir.exists()) {
    if (!PathUtils::isPrivateDirOfCurrentUser(extractDir)) {
      // Pre-existing base that isn't owner-only. If we own it (e.g. an older
      // Kartend left it 0755, or umask widened it), chmod succeeds and recovers
      // the cache. If it's owned by another local user, chmod fails and the
      // re-check still reports non-private → fall back to a per-run dir.
      QFile::setPermissions(extractDir, kOwnerOnly);
      if (!PathUtils::isPrivateDirOfCurrentUser(extractDir)) {
        qCWarning(lcLaunchManager) << "Extraction cache base is not private to this user; "
                                      "using a per-run temp dir instead:"
                                   << extractDir;
        persistentCacheUsable = false;
      }
    }
  } else {
    if (!baseDir.mkpath(".")) {
      return ErrorContext::error(ErrorCode::FileWriteError, "Failed to create extraction directory",
                                 "LaunchManager::extractArchiveToTemp")
          .withDetails(extractDir);
    }
    // Owner-only so no other local user can create subdirs under it later.
    QFile::setPermissions(extractDir, kOwnerOnly);
  }

  // Hijacked/unsafe base: extract into a fresh, unguessable, 0700 per-run root
  // (QTemporaryDir) and skip the persistent cache entirely for this launch.
  std::optional<QTemporaryDir> perRunRoot;
  if (!persistentCacheUsable) {
    perRunRoot.emplace(tempBasePath + QStringLiteral("/kartend_extract_XXXXXX"));
    if (!perRunRoot->isValid()) {
      return ErrorContext::error(ErrorCode::FileWriteError,
                                 "Failed to create per-run extraction directory",
                                 "LaunchManager::extractArchiveToTemp")
          .withDetails(perRunRoot->errorString());
    }
    // Persist past this scope: the launcher opens the extracted file after we
    // return. The OS reclaims the temp root at reboot.
    perRunRoot->setAutoRemove(false);
    extractDir = perRunRoot->path();
  }

  // Per-archive extraction dir, keyed on completeBaseName(). Two distinct
  // archives that share a base name (RomsA/game.zip vs RomsB/game.zip, or
  // game.zip vs game.7z) map to the SAME dir, so a naive cache hit would launch
  // the wrong extracted media (Kartend-nrykk). Guard the hit with a source
  // marker: only reuse the cache when it records THIS exact archive (absolute
  // path + size + mtime); otherwise treat it as a collision/stale and
  // re-extract. Also catches in-place replacement of the same path.
  QFileInfo archiveInfo(archivePath);
  QString archiveBaseName = archiveInfo.completeBaseName();
  QString uniqueDir = extractDir + "/" + archiveBaseName;

  const QString sourceId = archiveInfo.absoluteFilePath() + QStringLiteral("|") +
                           QString::number(archiveInfo.size()) + QStringLiteral("|") +
                           QString::number(archiveInfo.lastModified().toMSecsSinceEpoch());
  const QString markerPath = uniqueDir + QStringLiteral("/.kartend-source");

  QDir targetDir(uniqueDir);

  // Reuse the cache only when the marker proves it came from this same archive.
  if (targetDir.exists()) {
    QString cachedSourceId;
    QFile marker(markerPath);
    if (marker.open(QIODevice::ReadOnly)) {
      cachedSourceId = QString::fromUtf8(marker.readAll());
      marker.close();
    }
    QString existingFile = findFileWithExtension(uniqueDir, targetExtension);
    if (!existingFile.isEmpty() && cachedSourceId == sourceId) {
      qCDebug(lcLaunchManager) << "Using cached extraction:" << existingFile;
      return existingFile;
    }
    // Stale, or a different archive sharing this base name — wipe and
    // re-extract so we never serve another archive's contents.
    targetDir.removeRecursively();
  }

  if (!targetDir.mkpath(".")) {
    return ErrorContext::error(ErrorCode::FileWriteError,
                               "Failed to create extraction subdirectory",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(uniqueDir);
  }

  // Atomic-on-failure: every error path below leaves the extraction dir clean.
  // Dismissed on the successful return so the cache directory persists.
  auto cleanupTargetDir = qScopeGuard([&uniqueDir]() { QDir(uniqueDir).removeRecursively(); });

  // Use system tools to extract. unzip reads only .zip, so gate it on the
  // extension — handing a non-zip archive (gz/xz/bz2/tar/...) to unzip fails even
  // when bsdtar, which handles them, is installed (Kartend-9ys3w; mirrors the
  // RomHasher::extractorCandidates fix in Kartend-akaww). 7z stays first.
  QStringList extractors;
  extractors << "7z";
  if (archivePath.toLower().endsWith(QStringLiteral(".zip"))) {
    extractors << "unzip";
  }
  extractors << "bsdtar";
  QString extractor;

  for (const QString &cmd : extractors) {
    if (!QStandardPaths::findExecutable(cmd).isEmpty()) {
      extractor = cmd;
      break;
    }
  }

  if (extractor.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "No archive extraction tool found",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails("Install 7z or bsdtar to extract archives (unzip handles only .zip)");
  }

  // The archive path is a positional operand for 7z/unzip; a leading '-'
  // would be misparsed as an option (argv-flag injection — no shell involved).
  // Extraction already requires an absolute path (the child CWD is the temp
  // dir), so this never fires for real inputs; it is defense-in-depth. We use
  // a leading-dash guard rather than a `--` separator because bsdtar passes
  // the path as -f's argument (consumed literally, already safe), so injecting
  // `--` would break the bsdtar branch.
  if (archivePath.startsWith('-')) {
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Archive path cannot start with a dash",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(archivePath);
  }

  QProcess process;
  process.setWorkingDirectory(uniqueDir);

  QStringList args;
  if (extractor == "7z") {
    args << "x" << "-y" << archivePath;
  } else if (extractor == "unzip") {
    args << "-o" << archivePath;
  } else if (extractor == "bsdtar") {
    args << "-xf" << archivePath;
  }

  qCDebug(lcLaunchManager) << "Extracting with" << extractor << args;

  process.start(extractor, args);
  if (!process.waitForStarted(UIConstants::Launch::EXTRACTION_KILL_GRACE_MS)) {
    return ErrorContext::error(ErrorCode::UnknownError, "Failed to start archive extractor",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(QString("%1: %2").arg(extractor, process.errorString()));
  }

  // Kartend-mkcak / Kartend-ijglg: poll instead of a single blocking
  // waitForFinished so the extraction (running on a worker thread) stays
  // cancellable and the decompressed-size watchdog can kill a zip bomb
  // mid-flight instead of letting it fill tmpfs. Each pass blocks at most
  // one poll interval, then re-checks cancel flag, size cap, and the
  // overall timeout.
  QElapsedTimer extractionClock;
  extractionClock.start();
  enum class Abort { None, Cancelled, SizeExceeded, TimedOut };
  Abort abort = Abort::None;
  while (!process.waitForFinished(UIConstants::Launch::EXTRACTION_WATCHDOG_POLL_MS)) {
    if (cancelRequested && cancelRequested->load()) {
      abort = Abort::Cancelled;
      break;
    }
    if (extractionSizeExceeds(uniqueDir, maxBytes)) {
      abort = Abort::SizeExceeded;
      break;
    }
    if (extractionClock.elapsed() >= UIConstants::Launch::EXTRACTION_TIMEOUT_MS) {
      abort = Abort::TimedOut;
      break;
    }
  }
  if (abort != Abort::None) {
    // Never abandon the child: kill it and give it a bounded grace period to
    // die so the cleanupTargetDir guard below removes a quiescent tree.
    process.kill();
    process.waitForFinished(UIConstants::Launch::EXTRACTION_KILL_GRACE_MS);
    switch (abort) {
    case Abort::Cancelled:
      return ErrorContext::error(ErrorCode::OperationCancelled, "Archive extraction cancelled",
                                 "LaunchManager::extractArchiveToTemp")
          .withDetails(archivePath);
    case Abort::SizeExceeded:
      return ErrorContext::error(ErrorCode::ResourceLimitExceeded,
                                 "Archive extraction exceeded the decompressed size limit",
                                 "LaunchManager::extractArchiveToTemp")
          .withDetails(QString("%1 (limit: %2 bytes)").arg(archivePath).arg(maxBytes));
    case Abort::TimedOut:
    default:
      return ErrorContext::error(ErrorCode::OperationCancelled, "Archive extraction timed out",
                                 "LaunchManager::extractArchiveToTemp")
          .withDetails(archivePath);
    }
  }

  if (process.exitCode() != 0) {
    QString errorOutput = QString::fromUtf8(process.readAllStandardError());
    return ErrorContext::error(ErrorCode::InvalidArgument, "Archive extraction failed",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(
            QString("Exit code: %1, Error: %2").arg(process.exitCode()).arg(errorOutput.left(200)));
  }

  // Kartend-ijglg: a fast extraction can finish inside the first poll window
  // without the watchdog ever running — re-check the final size so the cap
  // holds unconditionally.
  if (extractionSizeExceeds(uniqueDir, maxBytes)) {
    return ErrorContext::error(ErrorCode::ResourceLimitExceeded,
                               "Archive extraction exceeded the decompressed size limit",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(QString("%1 (limit: %2 bytes)").arg(archivePath).arg(maxBytes));
  }

  // Find the file with the target extension
  QString targetFile = findFileWithExtension(uniqueDir, targetExtension);
  if (targetFile.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound,
                               "Target file not found in extracted archive",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(QString("Looking for *%1 in %2").arg(targetExtension, uniqueDir));
  }

  // Record which archive this extraction came from so a later call for a
  // different archive sharing the base name re-extracts instead of serving
  // these files (Kartend-nrykk). Best-effort: a missing marker next time just
  // forces a safe re-extract.
  QFile marker(markerPath);
  if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    marker.write(sourceId.toUtf8());
    marker.close();
  }

  cleanupTargetDir.dismiss();
  qCDebug(lcLaunchManager) << "Extracted target file:" << targetFile;
  return targetFile;
}

QString LaunchManager::findFileWithExtension(const QString &directory, const QString &extension) {
  // the launch-extension field accepts a comma-separated list
  // (".cue, .bin, .iso") expressing user preference. Earlier extensions are
  // preferred — a .cue index file wins over a .bin track when the archive
  // ships both. We do one directory pass and keep the lowest-priority match
  // we see, falling back to lower-priority extensions when none of the
  // earlier ones turn up.
  QStringList normalizedExts;
  for (QString ext : extension.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    ext = ext.trimmed().toLower();
    if (ext.isEmpty()) {
      continue;
    }
    if (!ext.startsWith(QLatin1Char('.'))) {
      ext.prepend(QLatin1Char('.'));
    }
    normalizedExts.append(ext);
  }
  if (normalizedExts.isEmpty()) {
    return {};
  }

  // Resolve the search root once so we can verify that every candidate file
  // stays underneath it. Without this, a symlink in the extracted archive
  // could point at /etc/passwd (or any other absolute path) and we would
  // happily return that path to the launcher.
  const QString rootCanonical = QFileInfo(directory).canonicalFilePath();
  if (rootCanonical.isEmpty()) {
    return {};
  }
  const QString rootPrefix = rootCanonical + QLatin1Char('/');
  const int rootDepth = rootCanonical.count(QLatin1Char('/'));

  // QDir::NoSymLinks makes the iterator skip symlinked entries entirely so
  // that a malicious archive containing symlinks cannot escape the temp dir.
  QDirIterator it(directory, QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);

  int bestPriority = normalizedExts.size(); // sentinel: nothing matched yet
  QString bestPath;
  int inspected = 0;
  while (it.hasNext()) {
    const QString filePath = it.next();
    if (++inspected > UIConstants::Launch::MAX_EXTRACTION_FILES_INSPECTED) {
      qCWarning(lcLaunchManager) << "Aborting extraction scan after"
                                 << UIConstants::Launch::MAX_EXTRACTION_FILES_INSPECTED
                                 << "files inspected; directory may be malicious:" << directory;
      return {};
    }

    // Depth bound relative to the search root.
    const int depth = filePath.count(QLatin1Char('/')) - rootDepth;
    if (depth > UIConstants::Launch::MAX_EXTRACTION_DEPTH) {
      continue;
    }

    const QString lowerPath = filePath.toLower();
    int matchPriority = -1;
    for (int i = 0; i < bestPriority; ++i) {
      if (lowerPath.endsWith(normalizedExts[i])) {
        matchPriority = i;
        break;
      }
    }
    if (matchPriority < 0) {
      continue;
    }

    // Defense-in-depth: even with NoSymLinks, verify the candidate's canonical
    // path is still inside the extraction root before returning it.
    const QFileInfo info(filePath);
    if (info.isSymLink()) {
      continue;
    }
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty()) {
      continue;
    }
    if (canonical != rootCanonical && !canonical.startsWith(rootPrefix)) {
      qCWarning(lcLaunchManager) << "Rejecting extraction candidate outside root:" << canonical
                                 << "root:" << rootCanonical;
      continue;
    }

    bestPriority = matchPriority;
    bestPath = canonical;
    if (bestPriority == 0) {
      // Top-priority extension matched — no further file can do better.
      break;
    }
  }
  return bestPath;
}
