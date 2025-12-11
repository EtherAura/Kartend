// Launches media items with configured emulators, handling RetroArch cores and parameters.
#include "launchmanager.h"
#include "applicationcontext.h"
#include "configvalidation.h"
#include "errorutils.h"
#include "setuputils.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>

#ifdef KARTEND_DEBUG_LOGGING
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcLaunchManager, "kartend.launchmanager")
#define debugLog(msg) qCDebug(lcLaunchManager) << msg
#else
#define debugLog(msg) do {} while(0)
#endif

SETUP_GETTER_DEF_SAME(LaunchManagerSetup, QList<CollectionConfig>*, Collections, collections)

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

auto LaunchManager::validatePathSecurity(const QString &path) -> Result<void> {
  if (path.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath,
                               "Path is empty",
                               "LaunchManager::validatePathSecurity");
  }

  // Normalize Unicode to NFC form to prevent homoglyph/normalization attacks
  // This ensures consistent representation of characters
  QString normalized = path.normalized(QString::NormalizationForm_C);
  
  // Reject if normalization changed the path (indicates potential obfuscation)
  if (normalized != path) {
    return ErrorContext::error(ErrorCode::InvalidFilePath,
                               "Path contains non-canonical Unicode",
                               "LaunchManager::validatePathSecurity")
        .withDetails("Path was modified by Unicode normalization");
  }

  // Reject shell metacharacters that could enable command injection.
  // Note: ()[] are allowed as they're common in filenames and safe with QProcess
  // which passes arguments directly without shell interpretation.
  static const QRegularExpression shellMeta(R"([;|&`$<>])");
  if (shellMeta.match(normalized).hasMatch()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath,
                               "Path contains shell metacharacters",
                               "LaunchManager::validatePathSecurity")
        .withDetails(path);
  }

  // Reject null bytes which could truncate strings in C APIs
  if (normalized.contains(QChar('\0'))) {
    return ErrorContext::error(ErrorCode::InvalidFilePath,
                               "Path contains null bytes",
                               "LaunchManager::validatePathSecurity");
  }

  // Reject newlines which could inject additional commands
  if (normalized.contains('\n') || normalized.contains('\r')) {
    return ErrorContext::error(ErrorCode::InvalidFilePath,
                               "Path contains newline characters",
                               "LaunchManager::validatePathSecurity");
  }
  
  // Reject backslash characters (Windows-style paths that could confuse Unix systems)
  if (normalized.contains('\\')) {
    return ErrorContext::error(ErrorCode::InvalidFilePath,
                               "Path contains backslash characters",
                               "LaunchManager::validatePathSecurity");
  }

  return Result<void>::success();
}

auto LaunchManager::validateLauncherPath(const QString &path) -> Result<void> {
  // First check for shell metacharacters and Unicode issues
  auto securityResult = validatePathSecurity(path);
  if (securityResult.isError()) {
    return securityResult;
  }

  QFileInfo info(path);
  QString resolvedPath = path;

  // If not an absolute path, try to resolve via PATH
  if (!info.isAbsolute()) {
    // Check if command exists in PATH
    if (!ConfigValidation::isCommandInPath(path)) {
      return ErrorContext::error(ErrorCode::FileNotFound,
                                 "Launcher command not found in PATH",
                                 "LaunchManager::validateLauncherPath")
          .withDetails(QString("Command '%1' is not in PATH. Specify absolute path or install the program.").arg(path));
    }
    
    // Resolve to absolute path using 'which' for further security validation
    QProcess whichProcess;
    whichProcess.start("which", QStringList() << path);
    whichProcess.waitForFinished(1000);
    if (whichProcess.exitCode() == 0) {
      resolvedPath = QString::fromUtf8(whichProcess.readAllStandardOutput()).trimmed();
      info.setFile(resolvedPath);
    } else {
      // Fallback: command is in PATH but 'which' failed - allow it
      // QProcess::startDetached will handle the PATH lookup
      return Result<void>::success();
    }
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
  // This catches paths like /proc/self/exe which would otherwise resolve to a valid location
  static const QStringList sensitiveDirectories = {
    "/proc", "/sys", "/dev"
  };
  for (const QString &sensitive : sensitiveDirectories) {
    if (resolvedPath.startsWith(sensitive + "/") || resolvedPath == sensitive) {
      return ErrorContext::error(ErrorCode::InvalidFilePath,
                                 "Launcher path is in restricted directory",
                                 "LaunchManager::validateLauncherPath")
          .withDetails(QString("Path: %1").arg(resolvedPath));
    }
  }
  
  // Resolve symlinks and verify the canonical path
  // This prevents symlink-based attacks where a symlink points to an unexpected location
  QString canonicalPath = info.canonicalFilePath();
  if (canonicalPath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath,
                               "Could not resolve canonical path",
                               "LaunchManager::validateLauncherPath")
        .withDetails(path);
  }
  
  // Ensure canonical path is still in a reasonable location
  // Reject if it resolves to a sensitive system directory (double-check after symlink resolution)
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

  return Result<void>::success();
}

void LaunchManager::launchItem(const QString &filePath, int collectionIndex) {
  if ((!m_collections) || collectionIndex < 0 ||
      collectionIndex >= m_collections->size()) {
    QMessageBox::warning(nullptr, "Invalid Collection",
                         "Invalid collection specified.");
    return;
  }

  const CollectionConfig &collection = (*m_collections)[collectionIndex];

  auto expandOnly = [&](const QString &text) -> QString {
    QString out = text;
    out.replace("%collection%", collection.name, Qt::CaseInsensitive);
    return out.trimmed();
  };

  QString expandedLauncherPath = expandOnly(collection.launcherPath);
  QString expandedCorePath = expandOnly(collection.corePath);

  if (expandedLauncherPath.isEmpty()) {
    QMessageBox::warning(nullptr, "No Launcher",
                         "No launcher configured for " + collection.name);
    return;
  }

  // Validate launcher path for security before execution
  auto launcherValidation = validateLauncherPath(expandedLauncherPath);
  if (launcherValidation.isError()) {
    ErrorUtils::logError(launcherValidation.error());
    QMessageBox::warning(nullptr, "Invalid Launcher",
                         QString("Launcher validation failed: %1\n\nPath: %2")
                             .arg(launcherValidation.error().message)
                             .arg(expandedLauncherPath));
    return;
  }

  // Validate core path if specified
  if (!expandedCorePath.isEmpty()) {
    auto coreValidation = validatePathSecurity(expandedCorePath);
    if (coreValidation.isError()) {
      ErrorUtils::logError(coreValidation.error());
      QMessageBox::warning(nullptr, "Invalid Core Path",
                           QString("Core path validation failed: %1\n\nPath: %2")
                               .arg(coreValidation.error().message)
                               .arg(expandedCorePath));
      return;
    }
  }

  // Validate media file path for security
  auto fileValidation = validatePathSecurity(filePath);
  if (fileValidation.isError()) {
    ErrorUtils::logError(fileValidation.error());
    QMessageBox::warning(nullptr, "Invalid File Path",
                         QString("File path validation failed: %1\n\nPath: %2")
                             .arg(fileValidation.error().message)
                             .arg(filePath));
    return;
  }

  QString program;
  QStringList arguments;

  if (expandedLauncherPath.contains("retroarch", Qt::CaseInsensitive)) {
    if (expandedCorePath.isEmpty()) {
      QMessageBox::warning(nullptr, "No Core",
                           "No RetroArch core configured for " +
                               collection.name);
      return;
    }

    program = expandedLauncherPath;
    arguments << "-L" << expandedCorePath << filePath;
  } else {
    program = expandedLauncherPath;
    arguments << filePath;

    if (!expandedCorePath.isEmpty()) {
      QString params = expandedCorePath.trimmed();
      if (!params.isEmpty()) {
        arguments.removeLast();
        auto parseResult = parseParameters(params);
        if (parseResult.isError()) {
          ErrorUtils::logError(parseResult.error());
          QMessageBox::warning(nullptr, "Invalid Parameters",
                               QString("Parameter parsing failed: %1\n\nParameters: %2")
                                   .arg(parseResult.error().message)
                                   .arg(params));
          return;
        }
        arguments.append(parseResult.value());
        arguments << filePath;
      }
    }
  }

  // TOCTOU mitigation: Re-validate launcher right before execution.
  // This reduces the window between validation and execution, though
  // cannot fully eliminate the race on systems without atomic exec.
  // Skip this check for PATH-based commands (non-absolute paths) since
  // QFileInfo can't check them - they were already validated via 'which'.
  QFileInfo launcherCheck(program);
  if (launcherCheck.isAbsolute()) {
    if (!launcherCheck.exists() || !launcherCheck.isExecutable()) {
      QMessageBox::critical(nullptr, "Launch Error",
                            QString("Launcher is no longer accessible or executable:\n%1")
                                .arg(program));
      return;
    }
  }

  bool success = QProcess::startDetached(program, arguments);

  if (!success) {
    QString errorMsg =
        QString("Failed to launch: %1\n\nCommand attempted:\n%2 %3\n\nMake "
                "sure the launcher path is correct and the file is executable.")
            .arg(expandedLauncherPath)
            .arg(program)
            .arg(arguments.join(" "));

    QMessageBox::critical(nullptr, "Launch Error", errorMsg);
  }
}

auto LaunchManager::parseParameters(const QString &paramString) -> ErrorUtils::Result<QStringList> {
  QStringList result;
  if (paramString.trimmed().isEmpty()) {
    return result;
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
        .withDetails(QString("Quote character '%1' was not closed").arg(quoteChar));
  }

  if (!currentParam.isEmpty()) {
    result.append(currentParam);
  }

  return result;
}
