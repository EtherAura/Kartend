// Archive extraction helpers split out from launchmanager.cpp.
#include "archivesafety.h"
#include "errorutils.h"
#include "extensionutils.h"
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
#include <QStorageInfo>
#include <QString>
#include <QTemporaryDir>

#include <algorithm>
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
// What the extraction watchdog found when it last looked at the output dir.
enum class WatchdogTrip { None, SizeExceeded, TooManyFiles };

// Kartend-ijglg / Kartend-si0p5: walk the extraction dir and report whether it
// has outgrown its bounds. `maxBytes` is an explicit cumulative-byte cap; pass
// a negative value for no byte cap, in which case only the entry count is
// checked (free space is watched separately, and far more cheaply, by
// volumeSpaceExhausted). The entry-count trip stands on its own regardless: a
// many-tiny-files bomb costs inodes and scan time rather than bytes, and would
// otherwise make this walk itself the DoS.
//
// NoSymLinks: a symlink consumes no meaningful space and following one could
// double-count or escape the extraction root.
WatchdogTrip inspectExtraction(const QString &directory, qint64 maxBytes) {
  qint64 total = 0;
  int inspected = 0;
  QDirIterator it(directory,
                  QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks |
                      QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    if (maxBytes >= 0) {
      total += it.fileInfo().size();
      if (total > maxBytes) {
        return WatchdogTrip::SizeExceeded;
      }
    }
    if (++inspected > UIConstants::Launch::MAX_EXTRACTION_FILES_INSPECTED) {
      return WatchdogTrip::TooManyFiles;
    }
  }
  return WatchdogTrip::None;
}

// Kartend-si0p5: true when the extraction volume has less than the safety
// margin left. This is the bound that replaced the fixed byte cap — it costs
// one statfs rather than a tree walk, and it tracks the resource actually at
// stake (space on the destination volume) rather than a guess made at compile
// time.
//
// An unreadable/unmounted volume returns false: we cannot prove the extraction
// is unsafe, and refusing every launch because QStorageInfo could not answer
// would be worse than the risk. The post-extraction check and the extractor's
// own write errors still catch a genuinely full disk.
bool volumeSpaceExhausted(const QString &directory) {
  const QStorageInfo info(directory);
  if (!info.isValid() || !info.isReady()) {
    return false;
  }
  return info.bytesAvailable() < UIConstants::Launch::EXTRACTION_FREE_SPACE_MARGIN_BYTES;
}

// Bytes an extraction may write to @p directory before it would breach the
// free-space margin. Negative when the volume cannot be queried, meaning
// "unbounded" to callers.
qint64 spaceAvailableForExtraction(const QString &directory) {
  const QStorageInfo info(directory);
  if (!info.isValid() || !info.isReady()) {
    return -1;
  }
  return info.bytesAvailable() - UIConstants::Launch::EXTRACTION_FREE_SPACE_MARGIN_BYTES;
}

// Kartend-ab8ri: preference order for locating a disc image inside a member
// archive when the collection sets no explicit extractedExtension. Index files
// (.cue, .gdi) win over the tracks they reference, matching
// findFileWithExtension's earlier-extension-wins contract.
//
// A collapsed multi-disc item needs this because extractedExtension is a
// property the user sets for archives they launch directly; a release can be
// collapsed without that ever being filled in (and on the reporting user's
// PlayStation collection it was empty).
QString defaultDiscExtensions() {
  return QStringLiteral(".cue,.gdi,.chd,.iso,.pbp,.img,.bin");
}

// Kartend-si0p5: the directory launch-time extraction writes into.
//
// Deliberately NOT TempLocation. On most Linux systems /tmp is tmpfs, so
// extracting a disc image there spends gigabytes of RAM — the hazard the old
// byte cap was really working around. An explicit user setting overrides the
// default so large libraries can be pointed at a roomier volume.
//
// Contents here are bounded, not permanent: an extraction survives at most
// LauncherSettings::extractionRetentionHours past its last use (Kartend-ra8sf),
// and sweepStaleExtractions enforces that at startup and before each new
// extraction.
QString resolveExtractionBase(const QString &configuredDir) {
  const QString configured = configuredDir.trimmed();
  if (!configured.isEmpty()) {
    return configured;
  }
  return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
         QStringLiteral("/extract");
}

// One spelling per directory for identity comparisons: an in-use path may
// reach a comparison spelled differently (symlinked extraction dir, trailing
// slash) from the entry it is compared against. Canonical when the path
// exists; a cleaned path otherwise, so a just-deleted dir still compares.
QString comparablePath(const QString &path) {
  const QString canonical = QFileInfo(path).canonicalFilePath();
  return canonical.isEmpty() ? QDir::cleanPath(path) : canonical;
}
} // namespace

bool LaunchManager::isArchiveFile(const QString &filePath) {
  // Delegates to the shared archive-extension table so this predicate and
  // RomHasher::isArchivePath can never drift apart — a file the launcher
  // unpacks must also be unpacked for scraper hash-ID.
  return ExtensionUtils::isArchivePath(filePath);
}

auto LaunchManager::extractArchiveToTemp(const QString &archivePath, const QString &targetExtension,
                                         const std::atomic_bool *cancelRequested,
                                         qint64 maxDecompressedBytes,
                                         const QString &extractionBaseDir)
    -> ErrorUtils::Result<QString> {
  // Validate archivePath at the same gate as media files in
  // buildLaunchCommand. QProcess argument lists prevent shell injection, but
  // unvalidated paths still leak into log output and violate the project's
  // path-security model.
  auto archiveValidation = PathUtils::validatePathSecurity(archivePath);
  if (archiveValidation.isError()) {
    return archiveValidation.error();
  }

  // Kartend-si0p5: a negative maxDecompressedBytes means "no fixed byte cap" —
  // the extraction is bounded by free space on the destination volume instead
  // (see EXTRACTION_FREE_SPACE_MARGIN_BYTES). A caller may still pass an
  // explicit cap; tests use that to exercise the watchdog without needing
  // multi-GiB fixtures.
  const qint64 explicitCap = maxDecompressedBytes;
  const bool hasExplicitCap = explicitCap >= 0;

  // Honour a cancellation that raced ahead of the worker actually starting.
  if (cancelRequested && cancelRequested->load()) {
    return ErrorContext::error(ErrorCode::OperationCancelled, "Archive extraction cancelled",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(archivePath);
  }

  // Disk-backed extraction root (Kartend-si0p5) — see resolveExtractionBase.
  const QString tempBasePath = resolveExtractionBase(extractionBaseDir);
  QString extractDir = tempBasePath + "/kartend_extract";

  // Kartend-qubev: the cache base may sit under a world-writable root — that
  // was always true of the old TempLocation default, and stays possible now
  // that the user can point extractionDirectory anywhere (Kartend-si0p5). On a
  // shared host a co-resident user could pre-create kartend_extract/ (and a
  // crafted per-archive subdir) and have the cache-hit branch serve their
  // payload. Guard it: create the base owner-only (0700)
  // when it's ours to make, and refuse to trust a pre-existing base that isn't
  // private to us. A hijacked base falls back to an unguessable per-run
  // QTemporaryDir — we still launch, and never serve another user's content.
  //
  // The cache-hit branch below is now a narrow one: Kartend-2ygme reclaims each
  // launch's extraction when its child exits, so a hit only occurs where a
  // previous reclaim did not complete. It is retained because that window is
  // exactly when serving the wrong archive's content (Kartend-nrykk) would be
  // possible, and the marker is what rules it out.
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
    // return. finishLaunch reclaims it when the child exits (Kartend-2ygme).
    perRunRoot->setAutoRemove(false);
    extractDir = perRunRoot->path();
  }

  // Kartend-ijglg / Kartend-si0p5: bound the *input* before any disk work.
  // Compression can only inflate the payload, so an archive whose on-disk size
  // already exceeds what we are willing (or able) to write can never
  // legitimately extract within that bound — reject without spawning an
  // extractor. Runs after the root is resolved because the default bound is a
  // property of the destination volume.
  const qint64 archiveSize = QFileInfo(archivePath).size();
  if (hasExplicitCap) {
    if (archiveSize > explicitCap) {
      return ErrorContext::error(ErrorCode::ResourceLimitExceeded,
                                 "Archive is larger than the extraction size limit",
                                 "LaunchManager::extractArchiveToTemp")
          .withDetails(QString("%1 (limit: %2 bytes)").arg(archivePath).arg(explicitCap));
    }
  } else {
    // Negative headroom means the volume could not be queried — treat as
    // unbounded rather than blocking the launch on a missing answer.
    const qint64 headroom = spaceAvailableForExtraction(extractDir);
    if (headroom >= 0 && archiveSize > headroom) {
      return ErrorContext::error(ErrorCode::ResourceLimitExceeded,
                                 "Not enough free space to extract this archive",
                                 "LaunchManager::extractArchiveToTemp")
          .withDetails(QString("%1 needs at least %2 bytes but only %3 are usable in %4 "
                               "(a %5-byte margin is reserved)")
                           .arg(archivePath)
                           .arg(archiveSize)
                           .arg(headroom)
                           .arg(extractDir)
                           .arg(UIConstants::Launch::EXTRACTION_FREE_SPACE_MARGIN_BYTES));
    }
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
      // Kartend-ra8sf: restart the retention clock. The marker's mtime is what
      // sweepStaleExtractions ages against, so touching it here is what makes
      // retention run from LAST USE rather than from extraction — a title
      // played regularly stays warm, one tried once ages out.
      if (QFile touch(markerPath); touch.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        touch.write(sourceId.toUtf8());
        touch.close();
      }
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

  // Use system tools to extract. bsdtar leads: libarchive's default extract
  // mode refuses ".." components and refuses to write through a symlink, so
  // it is the safest against crafted archives. 7z sanitizes entry paths but
  // its symlink behaviour varies by version, so it only runs behind the
  // symlink-rejecting pre-scan. unzip is gone entirely — it recreates symlink
  // entries and then happily writes through them (the classic
  // zip-slip-via-symlink primitive), and bsdtar/7z cover .zip anyway.
  //
  // Each candidate tool is scanned AND extracted with the SAME tool, and on a
  // pure format failure (the tool cannot read the archive — e.g.
  // bsdtar/libarchive on some RAR5 / solid / multi-volume variants that 7z
  // handles) we fall through to the next available tool. A security rejection
  // from the scan, a size-cap breach, a timeout, or a cancel aborts outright:
  // those must never be retried with another tool.
  const QStringList candidateTools{QStringLiteral("bsdtar"), QStringLiteral("7z")};
  QStringList availableTools;
  for (const QString &cmd : candidateTools) {
    if (!QStandardPaths::findExecutable(cmd).isEmpty()) availableTools << cmd;
  }
  if (availableTools.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "No archive extraction tool found",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails("Install bsdtar or 7z to extract archives");
  }

  // The archive path is a positional operand for 7z; a leading '-' would be
  // misparsed as an option (argv-flag injection — no shell involved).
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

  ErrorContext lastFailure =
      ErrorContext::error(ErrorCode::InvalidArgument, "Archive extraction failed",
                          "LaunchManager::extractArchiveToTemp");
  bool extracted = false;
  for (int toolIdx = 0; toolIdx < availableTools.size() && !extracted; ++toolIdx) {
    const QString &extractor = availableTools.at(toolIdx);

    // Start every attempt from a clean extraction dir — a prior tool may have
    // written partial output before failing.
    if (toolIdx > 0) {
      QDir(uniqueDir).removeRecursively();
      if (!QDir().mkpath(uniqueDir)) {
        return ErrorContext::error(ErrorCode::FileWriteError,
                                   "Failed to create extraction subdirectory",
                                   "LaunchManager::extractArchiveToTemp")
            .withDetails(uniqueDir);
      }
    }

    // Refuse archives whose listing shows symlink/hardlink entries or
    // path-escape attempts BEFORE anything is written — scanned with THIS
    // tool so the security verdict matches what will actually extract. The
    // post-extraction NoSymLinks walks below only choose which file gets
    // launched; a write routed through a symlink entry lands OUTSIDE uniqueDir
    // where those walks never look, so the only effective defense runs up
    // front. A security rejection aborts (never retried with another tool); a
    // tool that simply can't list the format falls through to the next tool.
    if (const auto scan = ArchiveSafety::scanArchiveEntriesWithTool(archivePath, extractor);
        scan.isError()) {
      if (ArchiveSafety::isSecurityRejection(scan.error())) {
        return ErrorContext::error(ErrorCode::InvalidFilePath,
                                   "Archive failed the pre-extraction safety scan",
                                   "LaunchManager::extractArchiveToTemp")
            .withDetails(QString("%1 — %2").arg(archivePath, scan.error().userFacingSummary()));
      }
      lastFailure = scan.error();
      continue; // this tool can't list the format — try the next
    }

    QProcess process;
    process.setWorkingDirectory(uniqueDir);

    QStringList args;
    if (extractor == "7z") {
      args << "x" << "-y" << archivePath;
    } else if (extractor == "bsdtar") {
      args << "-xf" << archivePath;
    }

    qCDebug(lcLaunchManager) << "Extracting with" << extractor << args;

    process.start(extractor, args);
    if (!process.waitForStarted(UIConstants::Launch::EXTRACTION_KILL_GRACE_MS)) {
      lastFailure =
          ErrorContext::error(ErrorCode::UnknownError, "Failed to start archive extractor",
                              "LaunchManager::extractArchiveToTemp")
              .withDetails(QString("%1: %2").arg(extractor, process.errorString()));
      continue; // maybe the next tool starts
    }

    // Kartend-mkcak / Kartend-ijglg: poll instead of a single blocking
    // waitForFinished so the extraction (running on a worker thread) stays
    // cancellable and the decompressed-size watchdog can kill a zip bomb
    // mid-flight instead of letting it fill tmpfs. Each pass blocks at most
    // one poll interval, then re-checks cancel flag, size cap, and the
    // overall timeout.
    QElapsedTimer extractionClock;
    extractionClock.start();
    enum class Abort { None, Cancelled, SizeExceeded, TooManyFiles, DiskFull, TimedOut };
    Abort abort = Abort::None;
    while (!process.waitForFinished(UIConstants::Launch::EXTRACTION_WATCHDOG_POLL_MS)) {
      if (cancelRequested && cancelRequested->load()) {
        abort = Abort::Cancelled;
        break;
      }
      switch (inspectExtraction(uniqueDir, hasExplicitCap ? explicitCap : -1)) {
      case WatchdogTrip::SizeExceeded:
        abort = Abort::SizeExceeded;
        break;
      case WatchdogTrip::TooManyFiles:
        abort = Abort::TooManyFiles;
        break;
      case WatchdogTrip::None:
        break;
      }
      if (abort != Abort::None) {
        break;
      }
      // Kartend-si0p5: the default bound. One statfs per poll, versus the tree
      // walk above — and it catches space consumed by anything else on the
      // volume too, not just this extraction.
      if (!hasExplicitCap && volumeSpaceExhausted(extractDir)) {
        abort = Abort::DiskFull;
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
      // These are hard aborts — user cancel or a resource/security cap — so we
      // do NOT fall through to another tool.
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
            .withDetails(QString("%1 (limit: %2 bytes)").arg(archivePath).arg(explicitCap));
      case Abort::TooManyFiles:
        return ErrorContext::error(ErrorCode::ResourceLimitExceeded,
                                   "Archive contains too many files to extract safely",
                                   "LaunchManager::extractArchiveToTemp")
            .withDetails(QString("%1 (limit: %2 entries)")
                             .arg(archivePath)
                             .arg(UIConstants::Launch::MAX_EXTRACTION_FILES_INSPECTED));
      case Abort::DiskFull:
        return ErrorContext::error(ErrorCode::ResourceLimitExceeded,
                                   "Ran out of free space while extracting this archive",
                                   "LaunchManager::extractArchiveToTemp")
            .withDetails(QString("%1 (extracting into %2; a %3-byte margin is reserved)")
                             .arg(archivePath, extractDir)
                             .arg(UIConstants::Launch::EXTRACTION_FREE_SPACE_MARGIN_BYTES));
      case Abort::TimedOut:
      default:
        return ErrorContext::error(ErrorCode::OperationCancelled, "Archive extraction timed out",
                                   "LaunchManager::extractArchiveToTemp")
            .withDetails(archivePath);
      }
    }

    if (process.exitCode() != 0) {
      QString errorOutput = QString::fromUtf8(process.readAllStandardError());
      lastFailure = ErrorContext::error(ErrorCode::InvalidArgument, "Archive extraction failed",
                                        "LaunchManager::extractArchiveToTemp")
                        .withDetails(QString("%1 exit code: %2, Error: %3")
                                         .arg(extractor)
                                         .arg(process.exitCode())
                                         .arg(errorOutput.left(200)));
      continue; // extraction/format failure — try the next tool
    }

    extracted = true;
  }

  if (!extracted) {
    return lastFailure;
  }

  // Kartend-ijglg: a fast extraction can finish inside the first poll window
  // without the watchdog ever running — re-check unconditionally so the bounds
  // hold regardless of how quickly the extractor returned.
  switch (inspectExtraction(uniqueDir, hasExplicitCap ? explicitCap : -1)) {
  case WatchdogTrip::SizeExceeded:
    return ErrorContext::error(ErrorCode::ResourceLimitExceeded,
                               "Archive extraction exceeded the decompressed size limit",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(QString("%1 (limit: %2 bytes)").arg(archivePath).arg(explicitCap));
  case WatchdogTrip::TooManyFiles:
    return ErrorContext::error(ErrorCode::ResourceLimitExceeded,
                               "Archive contains too many files to extract safely",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(QString("%1 (limit: %2 entries)")
                         .arg(archivePath)
                         .arg(UIConstants::Launch::MAX_EXTRACTION_FILES_INSPECTED));
  case WatchdogTrip::None:
    break;
  }
  if (!hasExplicitCap && volumeSpaceExhausted(extractDir)) {
    return ErrorContext::error(ErrorCode::ResourceLimitExceeded,
                               "Ran out of free space while extracting this archive",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(QString("%1 (extracting into %2; a %3-byte margin is reserved)")
                         .arg(archivePath, extractDir)
                         .arg(UIConstants::Launch::EXTRACTION_FREE_SPACE_MARGIN_BYTES));
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

// ============================================================================
// Multi-disc playlists (Kartend-ab8ri)
// ============================================================================

bool LaunchManager::isPlaylistFile(const QString &filePath) {
  return filePath.endsWith(QStringLiteral(".m3u"), Qt::CaseInsensitive) ||
         filePath.endsWith(QStringLiteral(".m3u8"), Qt::CaseInsensitive);
}

auto LaunchManager::readPlaylistEntries(const QString &playlistPath)
    -> ErrorUtils::Result<QStringList> {
  QFileInfo info(playlistPath);
  if (info.size() > UIConstants::Launch::MAX_PLAYLIST_BYTES) {
    return ErrorContext::error(ErrorCode::ResourceLimitExceeded,
                               "Playlist file is implausibly large",
                               "LaunchManager::readPlaylistEntries")
        .withDetails(QString("%1 (%2 bytes)").arg(playlistPath).arg(info.size()));
  }
  QFile file(playlistPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return ErrorContext::error(ErrorCode::FileNotFound, "Could not read playlist",
                               "LaunchManager::readPlaylistEntries")
        .withDetails(QString("%1: %2").arg(playlistPath, file.errorString()));
  }
  const QByteArray raw = file.readAll();
  file.close();

  // buildM3uContents writes paths relative when they sit directly under the
  // playlist's own directory, so relative entries resolve against that dir —
  // not the process CWD.
  const QDir baseDir = info.absoluteDir();
  QStringList entries;
  const QList<QByteArray> lines = raw.split('\n');
  for (const QByteArray &rawLine : lines) {
    const QString line = QString::fromUtf8(rawLine).trimmed();
    // '#' opens an extended-m3u directive (#EXTM3U, #EXTINF) — never a path.
    if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
      continue;
    }
    entries.append(QDir::isAbsolutePath(line) ? QDir::cleanPath(line)
                                              : QDir::cleanPath(baseDir.absoluteFilePath(line)));
    if (entries.size() > UIConstants::Launch::MAX_PLAYLIST_ENTRIES) {
      return ErrorContext::error(ErrorCode::ResourceLimitExceeded,
                                 "Playlist lists more entries than a release can plausibly have",
                                 "LaunchManager::readPlaylistEntries")
          .withDetails(QString("%1 (limit: %2)")
                           .arg(playlistPath)
                           .arg(UIConstants::Launch::MAX_PLAYLIST_ENTRIES));
    }
  }
  return entries;
}

bool LaunchManager::playlistNeedsExtraction(const QString &playlistPath) {
  if (!isPlaylistFile(playlistPath)) {
    return false;
  }
  const auto entries = readPlaylistEntries(playlistPath);
  if (entries.isError()) {
    // Let the launch proceed and fail visibly downstream rather than silently
    // swallowing a malformed playlist here.
    return false;
  }
  return std::any_of(entries.value().cbegin(), entries.value().cend(),
                     [](const QString &e) { return isArchiveFile(e); });
}

auto LaunchManager::resolvePlaylistForLaunch(const QString &playlistPath,
                                             const QString &targetExtension,
                                             const std::atomic_bool *cancelRequested,
                                             const QString &extractionBaseDir)
    -> ErrorUtils::Result<QString> {
  // Kartend-ab8ri: a collapsed multi-disc item's launch path is the generated
  // .m3u, which is not itself an archive — so launchItem's extraction branch
  // never fires and the launcher was handed a playlist of .zip paths it cannot
  // open. Unpack the archived members here and hand over a playlist that
  // points at real disc images.
  auto entriesResult = readPlaylistEntries(playlistPath);
  if (entriesResult.isError()) {
    return entriesResult.error();
  }
  // Result::value() returns const T&, and entriesResult outlives this scope,
  // so bind rather than copy the list (performance-unnecessary-copy-initialization).
  const QStringList &entries = entriesResult.value();
  if (entries.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "Playlist is empty",
                               "LaunchManager::resolvePlaylistForLaunch")
        .withDetails(playlistPath);
  }

  // Extension preference for locating a disc image inside each member archive.
  // The collection's extractedExtension wins when set; a collapsed release can
  // exist without one, so fall back to the disc-image list.
  const QString memberExtension =
      targetExtension.trimmed().isEmpty() ? defaultDiscExtensions() : targetExtension;

  // Kartend-2ygme: every byte this launch produces — the rewritten playlist AND
  // the discs it points at — must live under ONE directory, because a launch
  // reclaims exactly its ownedExtractionDir() — here kartend_playlists/<release>
  // — when the child exits.
  // Extracting members to the shared per-archive root instead would leak the
  // discs (the actual gigabytes) on every multi-disc launch while reclaiming
  // only the small playlist beside them.
  const QString base = resolveExtractionBase(extractionBaseDir);
  const QString releaseName = QFileInfo(playlistPath).completeBaseName();
  const QString releaseDir = base + QStringLiteral("/kartend_playlists/") + releaseName;
  const QString memberBase = releaseDir + QStringLiteral("/discs");

  QStringList resolved;
  resolved.reserve(entries.size());
  for (const QString &entry : entries) {
    if (cancelRequested && cancelRequested->load()) {
      return ErrorContext::error(ErrorCode::OperationCancelled, "Archive extraction cancelled",
                                 "LaunchManager::resolvePlaylistForLaunch")
          .withDetails(playlistPath);
    }
    if (!isArchiveFile(entry)) {
      // A plain disc image passes through untouched.
      resolved.append(entry);
      continue;
    }
    // Each member goes through the normal extraction path, so it inherits the
    // per-archive cache, the safety scan and the free-space bound. Re-launching
    // a collapsed release therefore does not re-extract every disc.
    auto extracted = extractArchiveToTemp(entry, memberExtension, cancelRequested, -1, memberBase);
    if (extracted.isError()) {
      return extracted.error();
    }
    resolved.append(extracted.value());
  }

  // Nothing was archived — hand back the original playlist untouched so the
  // launcher sees exactly what it saw before.
  if (resolved == entries) {
    return playlistPath;
  }

  // The rewritten playlist sits at the top of the per-release directory, with
  // the discs it references in discs/ beneath it.
  //
  // That boundary is load-bearing twice over. ownedExtractionDir() maps this
  // playlist to kartend_playlists/<release>, which finishLaunch then
  // removeRecursively()s both when a launch fails to start and when the child
  // exits (Kartend-2ygme, Kartend-dmg5y), so this directory defines exactly
  // what one launch owns: sharing it between releases would let one launch
  // delete another's playlist, and putting the discs outside it would leak
  // them. (A playlist returned unchanged above lies outside every extraction
  // root, so it owns nothing and nothing is ever reclaimed for it.)
  if (!QDir().mkpath(releaseDir)) {
    return ErrorContext::error(ErrorCode::FileWriteError, "Failed to create playlist directory",
                               "LaunchManager::resolvePlaylistForLaunch")
        .withDetails(releaseDir);
  }
  const QString outPath = releaseDir + QLatin1Char('/') + releaseName + QStringLiteral(".m3u");
  QFile out(outPath);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return ErrorContext::error(ErrorCode::FileWriteError, "Failed to write resolved playlist",
                               "LaunchManager::resolvePlaylistForLaunch")
        .withDetails(QString("%1: %2").arg(outPath, out.errorString()));
  }
  // Absolute paths, LF endings, trailing newline — the shape
  // MultiDisc::buildM3uContents produces and every launcher parses.
  const QByteArray body = (resolved.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
  if (out.write(body) != body.size()) {
    out.close();
    QFile::remove(outPath);
    return ErrorContext::error(ErrorCode::FileWriteError, "Failed to write resolved playlist",
                               "LaunchManager::resolvePlaylistForLaunch")
        .withDetails(outPath);
  }
  out.close();

  // Kartend-dmg5y: give the release its own .kartend-source, the last-use stamp
  // the sweep ages entries by and touchExtractionMarker refreshes on exit.
  // Without it the sweep fell back to the directory's mtime, which rewriting
  // the playlist in place never moves — so a release played daily still
  // expired a retention period after its FIRST launch. Rewritten on every
  // resolve, so the clock also restarts at each launch. Best-effort, like the
  // per-archive marker: a missing one only costs a re-extraction.
  QFile releaseMarker(releaseDir + QStringLiteral("/.kartend-source"));
  if (releaseMarker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    releaseMarker.write(QFileInfo(playlistPath).absoluteFilePath().toUtf8());
    releaseMarker.close();
  }

  qCDebug(lcLaunchManager) << "Resolved multi-disc playlist" << playlistPath << "->" << outPath;
  return outPath;
}

void LaunchManager::sweepStaleExtractions(const QString &extractionBaseDir, int retentionHours,
                                          const QStringList &inUseDirs) {
  // Kartend-2ygme / Kartend-ra8sf: bound how long extracted media survives.
  //
  // Two things have to be caught here. A crash, SIGKILL or power loss between
  // spawn and child exit leaves an extraction no in-process hook can reclaim.
  // And under a non-zero retention nothing deletes on exit at all — ageing out
  // is the only thing keeping the folder from growing without limit.
  //
  // Scoped to the two roots this file creates, never to the configured
  // directory itself: a user may well point extractionDirectory at a folder
  // holding other things, and wiping it wholesale would destroy their data.
  if (retentionHours < 0) {
    return; // Never expire — the user reclaims the folder themselves.
  }

  const QString base = resolveExtractionBase(extractionBaseDir);
  // Canonicalise the exclusions once: an in-use path may reach us spelled
  // differently (symlinked extraction dir, trailing slash) than the entry we
  // are about to compare it against.
  QStringList protectedDirs;
  protectedDirs.reserve(inUseDirs.size());
  for (const QString &d : inUseDirs) {
    if (!d.isEmpty()) {
      protectedDirs.append(comparablePath(d));
    }
  }

  const QDateTime cutoff =
      QDateTime::currentDateTimeUtc().addSecs(-static_cast<qint64>(retentionHours) * 3600);

  for (const QString &sub :
       {QStringLiteral("/kartend_extract"), QStringLiteral("/kartend_playlists")}) {
    QDir root(base + sub);
    if (!root.exists()) {
      continue;
    }
    const QStringList entries =
        root.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QString &name : entries) {
      const QString path = root.absoluteFilePath(name);

      // Never expire an extraction a running program is reading from. A play
      // session can outlast the retention period (24h retention, a 30h
      // session), and pulling the disc image out from under a running emulator
      // is a hard crash — the one outcome worse than keeping a stale folder.
      if (protectedDirs.contains(comparablePath(path))) {
        continue;
      }

      // retentionHours == 0 means "no grace period": everything found here is
      // by definition finished with (the in-use dirs were excluded above), so
      // there is no timestamp worth consulting.
      if (retentionHours > 0) {
        // The .kartend-source marker doubles as the last-use stamp — it is
        // rewritten on a cache hit, so the clock runs from last use rather
        // than from extraction. A directory with no marker (a partial or
        // pre-marker extraction) falls back to the directory's own mtime.
        const QFileInfo marker(path + QStringLiteral("/.kartend-source"));
        const QDateTime lastUsed = marker.exists() ? marker.lastModified().toUTC()
                                                   : QFileInfo(path).lastModified().toUTC();
        if (lastUsed > cutoff) {
          continue; // Still within its retention window.
        }
      }

      if (QDir(path).removeRecursively()) {
        qCDebug(lcLaunchManager) << "Swept expired extraction" << path;
      } else {
        qCWarning(lcLaunchManager) << "Could not sweep expired extraction" << path;
      }
    }
  }
}

QString LaunchManager::ownedExtractionDir(const QString &launchFilePath,
                                          const QString &extractionBaseDir) {
  // Kartend-dmg5y: every reclaim path removeRecursively()s what this returns,
  // so it answers only for paths the extraction itself created and returns
  // empty for anything else. Canonical on both sides, so a symlinked cache
  // directory or a `..` in the launch path cannot steer the answer outside
  // the extraction root.
  if (launchFilePath.isEmpty()) {
    return {};
  }
  const QString base = QFileInfo(resolveExtractionBase(extractionBaseDir)).canonicalFilePath();
  const QString file = QFileInfo(launchFilePath).canonicalFilePath();
  if (base.isEmpty() || file.isEmpty()) {
    return {};
  }
  const QString rel = QDir(base).relativeFilePath(file);
  if (rel.isEmpty() || rel.startsWith(QLatin1String("..")) || QDir::isAbsolutePath(rel)) {
    return {}; // outside the extraction root altogether
  }
  // <root>/<entry>/…/<file>: the launch file must sit INSIDE an entry. The
  // entry — not the file's own folder, which may be a subfolder the archive
  // carried — is the unit the sweep ages and the in-use exclusion names.
  const QStringList parts = rel.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  if (parts.size() < 3) {
    return {};
  }
  if (parts.at(0) == QLatin1String("kartend_extract") ||
      parts.at(0) == QLatin1String("kartend_playlists")) {
    return QDir(base).filePath(parts.at(0) + QLatin1Char('/') + parts.at(1));
  }
  // A per-run root (the Kartend-qubev fallback for an untrusted shared base)
  // holds exactly one launch, so the root itself is what that launch owns.
  if (parts.at(0).startsWith(QLatin1String("kartend_extract_"))) {
    return QDir(base).filePath(parts.at(0));
  }
  return {};
}

bool LaunchManager::isExtractionDirInUse(const QString &dir) const {
  if (dir.isEmpty()) {
    return false;
  }
  const QString target = comparablePath(dir);
  const QStringList active = activeExtractionDirs();
  return std::any_of(active.cbegin(), active.cend(), [&target](const QString &d) {
    return !d.isEmpty() && comparablePath(d) == target;
  });
}

void LaunchManager::touchExtractionMarker(const QString &extractionDir) {
  // Kartend-ra8sf: sweepStaleExtractions ages entries by this marker's mtime,
  // so rewriting it is what makes retention run from LAST USE. Rewriting the
  // existing contents rather than truncating keeps the source-identity check
  // (Kartend-nrykk) intact — an empty marker would force a needless re-extract.
  const QString markerPath = extractionDir + QStringLiteral("/.kartend-source");
  QFile marker(markerPath);
  if (!marker.open(QIODevice::ReadOnly)) {
    return; // No marker (partial extraction); the dir mtime is the fallback.
  }
  const QByteArray sourceId = marker.readAll();
  marker.close();
  if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    marker.write(sourceId);
    marker.close();
  }
}
