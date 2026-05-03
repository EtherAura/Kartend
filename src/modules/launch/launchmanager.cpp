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
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcLaunchManager().isDebugEnabled()) {                                                      \
      qCDebug(lcLaunchManager) << msg;                                                             \
    }                                                                                              \
  } while (0)

SETUP_GETTER_DEF_COL_SAME(LaunchManagerSetup, QList<CollectionConfig> *, Collections, collections)

LaunchManager::LaunchManager(QObject *parent) : QObject(parent) {}

void LaunchManager::setupReferences(const LaunchManagerSetup &setup) {
  m_ctx = setup.ctx;
  m_collections = setup.getCollections();
  if (m_ctx) {
    m_generalSettings = m_ctx->collection.generalSettings;
  }
  m_onLaunched = setup.onLaunched;
  m_onPlaySessionEnded = setup.onPlaySessionEnded;
}

bool LaunchManager::runtimeDetectionEnabled() const {
  return m_generalSettings && m_generalSettings->runtimeDetectionEnabled;
}

QString LaunchManager::resolveCollectionUuid(int collectionIndex) const {
  if (!m_collections || collectionIndex < 0 || collectionIndex >= m_collections->size()) {
    return {};
  }
  const CollectionConfig &collection = (*m_collections)[collectionIndex];
  // Mirrors how SidebarManager keys per-item rows: name + expanded media dir.
  // Uses the same helper so UUIDs stay consistent across reads/writes.
  const QString expandedMediaDir =
      PathUtils::validateAndExpandPath(collection.mediaDirectory, collection.name);
  return CollectionUtils::computeCollectionUuid(collection.name, expandedMediaDir);
}

void LaunchManager::recordSuccessfulLaunch(const QString &filePath, const QString &collectionUuid) {
  if (collectionUuid.isEmpty() || filePath.isEmpty()) {
    return;
  }
  if (m_onLaunched) {
    m_onLaunched(collectionUuid, filePath);
  }
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

auto LaunchManager::buildLaunchCommand(const CollectionConfig &collection, const QString &filePath)
    -> ErrorUtils::Result<LaunchCommand> {
  auto expandOnly = [&](const QString &text) -> QString {
    QString out = text;
    out.replace("%collection%", collection.name, Qt::CaseInsensitive);
    return out.trimmed();
  };

  const QString expandedLauncherPath = expandOnly(collection.launcherPath);
  const QString expandedCorePath = expandOnly(collection.corePath);
  const QString expandedLaunchParameters = expandOnly(collection.launchParameters);

  if (expandedLauncherPath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "No launcher configured",
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

  const bool isRetroArch = expandedLauncherPath.contains("retroarch", Qt::CaseInsensitive);
  if (isRetroArch) {
    if (expandedCorePath.isEmpty()) {
      return ErrorContext::error(ErrorCode::InvalidArgument, "No RetroArch core configured",
                                 "LaunchManager::buildLaunchCommand")
          .withDetails(QString("Collection '%1'").arg(collection.name));
    }

    // Core path should be a file path, not a flag.
    if (expandedCorePath.startsWith("-")) {
      return ErrorContext::error(ErrorCode::InvalidFilePath, "Core path cannot start with a dash",
                                 "LaunchManager::buildLaunchCommand")
          .withDetails(QString("Core path '%1' looks like an option").arg(expandedCorePath));
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

auto LaunchManager::validateLauncherPath(const QString &path) -> Result<QString> {
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
      return ErrorContext::error(ErrorCode::FileNotFound, "Launcher command not found in PATH",
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
    return ErrorContext::error(ErrorCode::FileNotFound, "Launcher executable not found",
                               "LaunchManager::validateLauncherPath")
        .withDetails(resolvedPath);
  }

  // Verify the file is executable
  if (!info.isExecutable()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Launcher file is not executable",
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
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Could not resolve canonical path",
                               "LaunchManager::validateLauncherPath")
        .withDetails(path);
  }

  // Ensure canonical path is still in a reasonable location
  // Reject if it resolves to a sensitive system directory (double-check after
  // symlink resolution)
  for (const QString &sensitive : sensitiveDirectories) {
    if (canonicalPath.startsWith(sensitive + "/") || canonicalPath == sensitive) {
      return ErrorContext::error(ErrorCode::InvalidFilePath,
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
      return ErrorContext::error(ErrorCode::InvalidFilePath,
                                 "Resolved symlink path failed security validation",
                                 "LaunchManager::validateLauncherPath")
          .withDetails(QString("Original: %1, Canonical: %2").arg(path, canonicalPath));
    }
  }

  return canonicalPath;
}

void LaunchManager::launchItem(const QString &filePath, int collectionIndex) {
  if ((!m_collections) || collectionIndex < 0 || collectionIndex >= m_collections->size()) {
    QMessageBox::warning(nullptr, "Invalid Collection", "Invalid collection specified.");
    return;
  }

  const CollectionConfig &collection = (*m_collections)[collectionIndex];

  // Determine the actual file to launch (may be extracted from archive)
  QString launchFilePath = filePath;

  if (collection.extractArchives && !collection.extractedExtension.isEmpty() &&
      isArchiveFile(filePath)) {
    qCDebug(lcLaunchManager) << "Archive extraction enabled for" << filePath;

    auto extractResult = extractArchiveToTemp(filePath, collection.extractedExtension);
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
      QMessageBox::warning(nullptr, "Invalid Core Path", QString("%1").arg(msg));
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
        QString("Launcher is no longer accessible or executable:\n%1").arg(launcherPath));
    return;
  }

  // Resolve UUID once: both the detached and tracked paths route stats
  // updates through the same key. Empty UUID skips tracking silently — the
  // launch itself still proceeds.
  const QString collectionUuid = resolveCollectionUuid(collectionIndex);

  // Kartend-qxv: when runtime detection is enabled, route through a tracked
  // QProcess so we can emit started/finished signals and let the UI sleep
  // behind a "Now Playing" overlay. Otherwise fall back to the historical
  // detached launch which leaves Kartend ignorant of the child lifetime.
  if (runtimeDetectionEnabled()) {
    if (launchTracked(launcherPath, cmd, launchFilePath, collectionUuid)) {
      // Kartend-7vi: increment play_count + last_played as soon as the
      // tracked child has been spawned. Session duration is recorded
      // separately when runtimeFinished fires.
      recordSuccessfulLaunch(filePath, collectionUuid);
      return;
    }
    // launchTracked already showed a message box on failure to start.
    return;
  }

  bool success = QProcess::startDetached(launcherPath, cmd.arguments);

  if (!success) {
    QString errorMsg = QString("Failed to launch: %1\n\nCommand attempted:\n%2 %3\n\nMake "
                               "sure the launcher path is correct and the file is executable.")
                           .arg(launcherPath)
                           .arg(launcherPath)
                           .arg(cmd.arguments.join(" "));

    QMessageBox::critical(nullptr, "Launch Error", errorMsg);
    return;
  }

  // Kartend-7vi: detached launches can't measure session duration (we don't
  // own the child PID), but we still record the launch event. Time-played
  // remains zero until the user enables runtime detection.
  recordSuccessfulLaunch(filePath, collectionUuid);
}

bool LaunchManager::launchTracked(const QString &launcherPath, const LaunchCommand &cmd,
                                  const QString &filePath, const QString &collectionUuid) {
  // Only one tracked child at a time; reject overlapping launches so the
  // overlay state stays coherent.
  if (m_trackedChild) {
    QMessageBox::information(
        nullptr, "Already Running",
        QString("Another tracked item is currently running:\n%1").arg(m_trackedFilePath));
    return false;
  }

  auto *child = new QProcess(this);
  m_trackedChild = child;
  m_trackedFilePath = filePath;
  m_trackedCollectionUuid = collectionUuid;
  m_trackedStartTime = QDateTime();

  // Detach the child from Kartend's stdio so a busy launcher doesn't fill
  // our pipes (and to avoid blocking on closed channels at exit).
  child->setProcessChannelMode(QProcess::ForwardedChannels);
  child->setInputChannelMode(QProcess::ForwardedInputChannel);

  const QString displayName = QFileInfo(filePath).completeBaseName();

  connect(child, &QProcess::started, this, [this, filePath, displayName]() {
    // Capture the start moment here rather than at child->start() so the
    // recorded duration reflects actual run time (not queueing delay).
    m_trackedStartTime = QDateTime::currentDateTimeUtc();
    emit runtimeStarted(filePath, displayName);
  });

  // QProcess emits exactly one of finished() or errorOccurred()-with-FailedToStart
  // before the object is safe to delete. Funnel both through a single cleanup
  // lambda so the UI always sees a balanced started/finished pair.
  auto cleanup = [this, child, filePath]() {
    if (m_trackedChild != child) {
      return; // Already cleaned up.
    }
    // Kartend-7vi: record the session duration before clearing the tracked
    // state. Skip when the child never reached `started` (FailedToStart) —
    // m_trackedStartTime stays default-constructed in that case.
    if (m_trackedStartTime.isValid() && !m_trackedCollectionUuid.isEmpty() &&
        m_onPlaySessionEnded) {
      const qint64 elapsed = m_trackedStartTime.secsTo(QDateTime::currentDateTimeUtc());
      if (elapsed > 0) {
        m_onPlaySessionEnded(m_trackedCollectionUuid, filePath, elapsed);
      }
    }
    m_trackedChild.clear();
    m_trackedFilePath.clear();
    m_trackedCollectionUuid.clear();
    m_trackedStartTime = QDateTime();
    emit runtimeFinished(filePath);
    child->deleteLater();
  };

  connect(child, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [cleanup](int /*code*/, QProcess::ExitStatus /*status*/) { cleanup(); });

  connect(child, &QProcess::errorOccurred, this,
          [this, child, cleanup](QProcess::ProcessError error) {
            // Only treat FailedToStart as terminal here — finished() will fire
            // for crashes after start, and we want to keep the overlay up
            // until the process is actually gone.
            if (error == QProcess::FailedToStart) {
              QMessageBox::critical(
                  nullptr, "Launch Error",
                  QString("Failed to start tracked launcher:\n%1").arg(child->errorString()));
              cleanup();
            }
          });

  child->start(launcherPath, cmd.arguments);
  // start() returns void; FailedToStart is reported via errorOccurred.
  return true;
}

auto LaunchManager::parseParameters(const QString &paramString) -> ErrorUtils::Result<QStringList> {
  QStringList result;
  if (paramString.trimmed().isEmpty()) {
    return result;
  }

  // Reject null bytes/newlines which can cause confusing log/diagnostic output.
  if (paramString.contains(QChar('\0')) || paramString.contains('\n') ||
      paramString.contains('\r')) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
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
    return ErrorContext::error(ErrorCode::InvalidArgument, "Unclosed quote in launch parameters",
                               "LaunchManager::parseParameters")
        .withDetails(QString("Quote character '%1' was not closed").arg(quoteChar));
  }

  if (!currentParam.isEmpty()) {
    result.append(currentParam);
  }

  return result;
}
