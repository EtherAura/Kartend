// Archive extraction helpers split out from launchmanager.cpp.
#include "errorutils.h"
#include "launchmanager.h"
#include "pathutils.h"
#include "uiconstants/launch.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

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

auto LaunchManager::extractArchiveToTemp(const QString &archivePath, const QString &targetExtension)
    -> ErrorUtils::Result<QString> {
  // Validate archivePath at the same gate as media files in
  // buildLaunchCommand. QProcess argument lists prevent shell injection, but
  // unvalidated paths still leak into log output and violate the project's
  // path-security model.
  auto archiveValidation = PathUtils::validatePathSecurity(archivePath);
  if (archiveValidation.isError()) {
    return archiveValidation.error();
  }

  // Create a persistent temp directory for extractions
  // Using a subdirectory in the standard temp location that won't auto-delete
  QString tempBasePath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  QString extractDir = tempBasePath + "/kartend_extract";

  QDir baseDir(extractDir);
  if (!baseDir.exists() && !baseDir.mkpath(".")) {
    return ErrorContext::error(ErrorCode::FileWriteError, "Failed to create extraction directory",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(extractDir);
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
  if (!process.waitForFinished(30000)) { // 30 second timeout
    return ErrorContext::error(ErrorCode::OperationCancelled, "Archive extraction timed out",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(archivePath);
  }

  if (process.exitCode() != 0) {
    QString errorOutput = QString::fromUtf8(process.readAllStandardError());
    return ErrorContext::error(ErrorCode::InvalidArgument, "Archive extraction failed",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(
            QString("Exit code: %1, Error: %2").arg(process.exitCode()).arg(errorOutput.left(200)));
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
