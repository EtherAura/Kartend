// Launches media items with configured emulators, handling RetroArch cores and
// parameters.
#include "launchmanager.h"
#include "applicationcontext.h"
#include "configvalidation.h"
#include "errorutils.h"
#include "pathutils.h"
#include "setuputils.h"
#include "uiconstants.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcLaunchManager, "kartend.launchmanager")
#define debugLog(msg)                                                          \
  do {                                                                         \
    if (lcLaunchManager().isDebugEnabled()) {                                  \
      qCDebug(lcLaunchManager) << msg;                                         \
    }                                                                          \
  } while (0)

SETUP_GETTER_DEF_SAME(LaunchManagerSetup, QList<CollectionConfig> *,
                      Collections, collections)

LaunchManager::LaunchManager(QObject *parent) : QObject(parent) {}

void LaunchManager::setupReferences(const LaunchManagerSetup &setup) {
  m_collections = setup.getCollections();
}

bool LaunchManager::canLaunch(const QString &filePath) const {
  if (!m_lastLaunchTimes.contains(filePath)) {
    return true;
  }
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  return (now - m_lastLaunchTimes[filePath]) >= kDoubleLaunchGuardMs;
}

void LaunchManager::recordLaunch(const QString &filePath) {
  m_lastLaunchTimes[filePath] = QDateTime::currentMSecsSinceEpoch();
}

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using ErrorUtils::Result;

auto LaunchManager::buildLaunchCommand(const CollectionConfig &collection,
                                       const QString &filePath)
    -> ErrorUtils::Result<LaunchCommand> {
  auto expandOnly = [&](const QString &text) -> QString {
    QString out = text;
    out.replace("%collection%", collection.name, Qt::CaseInsensitive);
    return out.trimmed();
  };

  const QString expandedLauncherPath = expandOnly(collection.launcherPath);
  const QString expandedCorePath = expandOnly(collection.corePath);
  const QString expandedLaunchParameters =
      expandOnly(collection.launchParameters);

  if (expandedLauncherPath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "No launcher configured",
                               "LaunchManager::buildLaunchCommand")
        .withDetails(QString("Collection '%1'").arg(collection.name));
  }

  // Validate media file path for security.
  // Existence is not checked here; only character-level security checks.
  auto fileValidation = validatePathSecurity(filePath);
  if (fileValidation.isError()) {
    return fileValidation.error();
  }

  LaunchCommand cmd;
  cmd.program = expandedLauncherPath;

  const bool isRetroArch =
      expandedLauncherPath.contains("retroarch", Qt::CaseInsensitive);
  if (isRetroArch) {
    if (expandedCorePath.isEmpty()) {
      return ErrorContext::error(ErrorCode::InvalidArgument,
                                 "No RetroArch core configured",
                                 "LaunchManager::buildLaunchCommand")
          .withDetails(QString("Collection '%1'").arg(collection.name));
    }

    // Core path should be a file path, not a flag.
    if (expandedCorePath.startsWith("-")) {
      return ErrorContext::error(ErrorCode::InvalidFilePath,
                                 "Core path cannot start with a dash",
                                 "LaunchManager::buildLaunchCommand")
          .withDetails(QString("Core path '%1' looks like an option")
                           .arg(expandedCorePath));
    }

    auto coreValidation = validatePathSecurity(expandedCorePath);
    if (coreValidation.isError()) {
      return coreValidation.error();
    }

    cmd.arguments << "-L" << expandedCorePath << filePath;
    return cmd;
  }

  // Non-RetroArch: parse optional launch parameters string.
  if (!expandedLaunchParameters.isEmpty()) {
    auto parseResult = parseParameters(expandedLaunchParameters);
    if (parseResult.isError()) {
      return parseResult.error();
    }
    cmd.arguments.append(parseResult.value());
  }
  cmd.arguments << filePath;
  return cmd;
}

auto LaunchManager::validatePathSecurity(const QString &path) -> Result<void> {
  // Delegate to shared utility in PathUtils
  return PathUtils::validatePathSecurity(path);
}

auto LaunchManager::validateLauncherPath(const QString &path)
    -> Result<QString> {
  // First check for shell metacharacters and Unicode issues
  auto securityResult = validatePathSecurity(path);
  if (securityResult.isError()) {
    return securityResult.error();
  }

  QFileInfo info(path);
  QString resolvedPath = path;

  // If not an absolute path, try to resolve via PATH
  if (!info.isAbsolute()) {
    const QString executable = QStandardPaths::findExecutable(path);
    if (executable.isEmpty()) {
      return ErrorContext::error(ErrorCode::FileNotFound,
                                 "Launcher command not found in PATH",
                                 "LaunchManager::validateLauncherPath")
          .withDetails(QString("Command '%1' is not in PATH. Specify absolute "
                               "path or install the program.")
                           .arg(path));
    }

    // Resolve to an absolute path for further security validation.
    resolvedPath = executable;
    info.setFile(resolvedPath);
  }

  // Verify the file exists
  if (!info.exists()) {
    return ErrorContext::error(ErrorCode::FileNotFound,
                               "Launcher executable not found",
                               "LaunchManager::validateLauncherPath")
        .withDetails(resolvedPath);
  }

  // Verify the file is executable
  if (!info.isExecutable()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath,
                               "Launcher file is not executable",
                               "LaunchManager::validateLauncherPath")
        .withDetails(resolvedPath);
  }

  // Check original path against sensitive directories BEFORE canonicalization
  // This catches paths like /proc/self/exe which would otherwise resolve to a
  // valid location
  static const QStringList sensitiveDirectories = {"/proc", "/sys", "/dev"};
  for (const QString &sensitive : sensitiveDirectories) {
    if (resolvedPath.startsWith(sensitive + "/") || resolvedPath == sensitive) {
      return ErrorContext::error(ErrorCode::InvalidFilePath,
                                 "Launcher path is in restricted directory",
                                 "LaunchManager::validateLauncherPath")
          .withDetails(QString("Path: %1").arg(resolvedPath));
    }
  }

  // Resolve symlinks and verify the canonical path
  // This prevents symlink-based attacks where a symlink points to an unexpected
  // location
  QString canonicalPath = info.canonicalFilePath();
  if (canonicalPath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath,
                               "Could not resolve canonical path",
                               "LaunchManager::validateLauncherPath")
        .withDetails(path);
  }

  // Ensure canonical path is still in a reasonable location
  // Reject if it resolves to a sensitive system directory (double-check after
  // symlink resolution)
  for (const QString &sensitive : sensitiveDirectories) {
    if (canonicalPath.startsWith(sensitive + "/") ||
        canonicalPath == sensitive) {
      return ErrorContext::error(
                 ErrorCode::InvalidFilePath,
                 "Launcher path resolves to restricted directory",
                 "LaunchManager::validateLauncherPath")
          .withDetails(QString("Canonical path: %1").arg(canonicalPath));
    }
  }

  // Verify the canonical path also passes security checks
  // (in case symlink resolution introduced new issues)
  if (canonicalPath != path) {
    auto canonicalSecurityResult = validatePathSecurity(canonicalPath);
    if (canonicalSecurityResult.isError()) {
      return ErrorContext::error(
                 ErrorCode::InvalidFilePath,
                 "Resolved symlink path failed security validation",
                 "LaunchManager::validateLauncherPath")
          .withDetails(
              QString("Original: %1, Canonical: %2").arg(path, canonicalPath));
    }
  }

  return canonicalPath;
}

void LaunchManager::launchItem(const QString &filePath, int collectionIndex) {
  if ((!m_collections) || collectionIndex < 0 ||
      collectionIndex >= m_collections->size()) {
    QMessageBox::warning(nullptr, "Invalid Collection",
                         "Invalid collection specified.");
    return;
  }

  const CollectionConfig &collection = (*m_collections)[collectionIndex];

  // Determine the actual file to launch (may be extracted from archive)
  QString launchFilePath = filePath;

  if (collection.extractArchives && !collection.extractedExtension.isEmpty() &&
      isArchiveFile(filePath)) {
    qCDebug(lcLaunchManager) << "Archive extraction enabled for" << filePath;

    auto extractResult =
        extractArchiveToTemp(filePath, collection.extractedExtension);
    if (extractResult.isError()) {
      ErrorUtils::logError(extractResult.error());
      QMessageBox::warning(nullptr, "Extraction Error",
                           QString("Failed to extract archive:\n%1\n\n%2")
                               .arg(filePath)
                               .arg(extractResult.error().message));
      return;
    }
    launchFilePath = extractResult.value();
    qCDebug(lcLaunchManager) << "Launching extracted file:" << launchFilePath;
  }

  auto commandResult = buildLaunchCommand(collection, launchFilePath);
  if (commandResult.isError()) {
    ErrorUtils::logError(commandResult.error());
    const QString msg = commandResult.error().message;
    if (commandResult.hasErrorCode(ErrorCode::InvalidFilePath)) {
      QMessageBox::warning(nullptr, "Invalid File Path",
                           QString("%1\n\nPath: %2").arg(msg, filePath));
    } else if (msg.contains("core", Qt::CaseInsensitive)) {
      QMessageBox::warning(nullptr, "Invalid Core Path",
                           QString("%1").arg(msg));
    } else {
      QMessageBox::warning(nullptr, "Launch Error", QString("%1").arg(msg));
    }
    return;
  }

  const LaunchCommand cmd = commandResult.value();

  // Validate launcher path for security before execution.
  // This also resolves PATH commands to a canonical executable.
  auto launcherPathResult = validateLauncherPath(cmd.program);
  if (launcherPathResult.isError()) {
    ErrorUtils::logError(launcherPathResult.error());
    QMessageBox::warning(nullptr, "Invalid Launcher",
                         QString("Launcher validation failed: %1\n\nPath: %2")
                             .arg(launcherPathResult.error().message)
                             .arg(cmd.program));
    return;
  }

  const QString launcherPath = launcherPathResult.value();

  // TOCTOU mitigation: Re-validate launcher right before execution.
  // This reduces the window between validation and execution, though
  // cannot fully eliminate the race on systems without atomic exec.
  QFileInfo launcherCheck(launcherPath);
  if (!launcherCheck.exists() || !launcherCheck.isExecutable()) {
    QMessageBox::critical(
        nullptr, "Launch Error",
        QString("Launcher is no longer accessible or executable:\n%1")
            .arg(launcherPath));
    return;
  }

  bool success = QProcess::startDetached(launcherPath, cmd.arguments);

  if (!success) {
    QString errorMsg =
        QString("Failed to launch: %1\n\nCommand attempted:\n%2 %3\n\nMake "
                "sure the launcher path is correct and the file is executable.")
            .arg(launcherPath)
            .arg(launcherPath)
            .arg(cmd.arguments.join(" "));

    QMessageBox::critical(nullptr, "Launch Error", errorMsg);
  }
}

auto LaunchManager::parseParameters(const QString &paramString)
    -> ErrorUtils::Result<QStringList> {
  QStringList result;
  if (paramString.trimmed().isEmpty()) {
    return result;
  }

  // Reject null bytes/newlines which can cause confusing log/diagnostic output.
  if (paramString.contains(QChar('\0')) || paramString.contains('\n') ||
      paramString.contains('\r')) {
    return ErrorContext::error(
        ErrorCode::InvalidArgument,
        "Launch parameters contain invalid control characters",
        "LaunchManager::parseParameters");
  }

  QString params = paramString.trimmed();
  bool inQuotes = false;
  QString currentParam;
  QChar quoteChar;

  for (int i = 0; i < params.length(); ++i) {
    QChar currentChar = params[i];

    if (!inQuotes && (currentChar == '"' || currentChar == '\'')) {
      inQuotes = true;
      quoteChar = currentChar;
    } else if (inQuotes && currentChar == quoteChar) {
      inQuotes = false;
    } else if (currentChar == ' ' && !inQuotes) {
      if (!currentParam.isEmpty()) {
        result.append(currentParam);
        currentParam.clear();
      }
    } else {
      currentParam.append(currentChar);
    }
  }

  // Check for unclosed quotes - potential injection vulnerability
  if (inQuotes) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Unclosed quote in launch parameters",
                               "LaunchManager::parseParameters")
        .withDetails(
            QString("Quote character '%1' was not closed").arg(quoteChar));
  }

  if (!currentParam.isEmpty()) {
    result.append(currentParam);
  }

  return result;
}

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

auto LaunchManager::extractArchiveToTemp(const QString &archivePath,
                                         const QString &targetExtension)
    -> ErrorUtils::Result<QString> {
  // Create a persistent temp directory for extractions
  // Using a subdirectory in the standard temp location that won't auto-delete
  QString tempBasePath =
      QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  QString extractDir = tempBasePath + "/kartend_extract";

  QDir baseDir(extractDir);
  if (!baseDir.exists() && !baseDir.mkpath(".")) {
    return ErrorContext::error(ErrorCode::FileWriteError,
                               "Failed to create extraction directory",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(extractDir);
  }

  // Create a unique subdirectory for this archive based on its name
  QFileInfo archiveInfo(archivePath);
  QString archiveBaseName = archiveInfo.completeBaseName();
  QString uniqueDir = extractDir + "/" + archiveBaseName;

  QDir targetDir(uniqueDir);

  // If directory exists and has files, check if we already extracted
  if (targetDir.exists()) {
    QString existingFile = findFileWithExtension(uniqueDir, targetExtension);
    if (!existingFile.isEmpty()) {
      qCDebug(lcLaunchManager) << "Using cached extraction:" << existingFile;
      return existingFile;
    }
    // Clear stale extraction
    targetDir.removeRecursively();
  }

  if (!targetDir.mkpath(".")) {
    return ErrorContext::error(ErrorCode::FileWriteError,
                               "Failed to create extraction subdirectory",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(uniqueDir);
  }

  // Use system tools to extract (7z is most universal)
  QStringList extractors = {"7z", "unzip", "bsdtar"};
  QString extractor;

  for (const QString &cmd : extractors) {
    if (!QStandardPaths::findExecutable(cmd).isEmpty()) {
      extractor = cmd;
      break;
    }
  }

  if (extractor.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound,
                               "No archive extraction tool found",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails("Install 7z, unzip, or bsdtar to extract archives");
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
    return ErrorContext::error(ErrorCode::OperationCancelled,
                               "Archive extraction timed out",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(archivePath);
  }

  if (process.exitCode() != 0) {
    QString errorOutput = QString::fromUtf8(process.readAllStandardError());
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Archive extraction failed",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(QString("Exit code: %1, Error: %2")
                         .arg(process.exitCode())
                         .arg(errorOutput.left(200)));
  }

  // Find the file with the target extension
  QString targetFile = findFileWithExtension(uniqueDir, targetExtension);
  if (targetFile.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound,
                               "Target file not found in extracted archive",
                               "LaunchManager::extractArchiveToTemp")
        .withDetails(
            QString("Looking for *%1 in %2").arg(targetExtension, uniqueDir));
  }

  qCDebug(lcLaunchManager) << "Extracted target file:" << targetFile;
  return targetFile;
}

QString LaunchManager::findFileWithExtension(const QString &directory,
                                             const QString &extension) {
  QString normalizedExt = extension.toLower();
  if (!normalizedExt.startsWith('.')) {
    normalizedExt = "." + normalizedExt;
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
  const int rootDepth =
      rootCanonical.count(QLatin1Char('/'));

  // QDir::NoSymLinks makes the iterator skip symlinked entries entirely so
  // that a malicious archive containing symlinks cannot escape the temp dir.
  QDirIterator it(directory,
                  QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);

  int inspected = 0;
  while (it.hasNext()) {
    const QString filePath = it.next();
    if (++inspected > UIConstants::Launch::MAX_EXTRACTION_FILES_INSPECTED) {
      qCWarning(lcLaunchManager)
          << "Aborting extraction scan after"
          << UIConstants::Launch::MAX_EXTRACTION_FILES_INSPECTED
          << "files inspected; directory may be malicious:" << directory;
      return {};
    }

    // Depth bound relative to the search root.
    const int depth = filePath.count(QLatin1Char('/')) - rootDepth;
    if (depth > UIConstants::Launch::MAX_EXTRACTION_DEPTH) {
      continue;
    }

    if (!filePath.toLower().endsWith(normalizedExt)) {
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
      qCWarning(lcLaunchManager)
          << "Rejecting extraction candidate outside root:" << canonical
          << "root:" << rootCanonical;
      continue;
    }

    return canonical;
  }
  return {};
}
