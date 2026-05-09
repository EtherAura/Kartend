// Provides file path normalization, extension handling, and path manipulation.
#include "pathutils.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using ErrorUtils::Result;

namespace PathUtils {

// Helper to expand placeholders and ~ in path
static QString expandPath(const QString &path, const QString &collectionName) {
  QString result = path.trimmed();

  // Expand ~ to home directory (must be at start of path)
  if (result.startsWith("~/")) {
    result = QDir::homePath() + result.mid(1); // Replace ~ with home path
  } else if (result == "~") {
    result = QDir::homePath();
  }

  if (!collectionName.isEmpty()) {
    result.replace("%collection%", collectionName, Qt::CaseInsensitive);
  }
  return result;
}

// New Result-returning version with structured error context
Result<QString> tryValidateAndExpandPath(const QString &path, const QString &collectionName) {
  QString result = expandPath(path, collectionName);

  if (result.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Path is empty after expansion",
                               "PathUtils::tryValidateAndExpandPath")
        .withDetails(QString("Original: '%1', Collection: '%2'").arg(path, collectionName));
  }

  QDir dir(result);
  if (!dir.isAbsolute()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Path is not absolute",
                               "PathUtils::tryValidateAndExpandPath")
        .withDetails(QString("Path: '%1'").arg(result));
  }

  if (!dir.exists()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "Directory does not exist",
                               "PathUtils::tryValidateAndExpandPath")
        .withDetails(QString("Path: '%1'").arg(result));
  }

  return dir.absolutePath();
}

// Legacy function - delegates to Result version for consistency
QString validateAndExpandPath(const QString &path, const QString &collectionName) {
  auto result = tryValidateAndExpandPath(path, collectionName);
  return result.isOk() ? result.value() : QString();
}

QString truncatePathForDisplay(const QString &path, int maxLength) {
  if (path.length() <= maxLength) {
    return path;
  }

  return "..." + path.right(maxLength - 3);
}

QString normalizeDisplayName(const QString &input) {
  QString out = input;
  out.replace('_', ' ').replace('-', ' ');
  out = out.simplified().toLower();
  return out;
}

Result<void> validatePathSecurity(const QString &path) {
  if (path.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Path is empty",
                               "PathUtils::validatePathSecurity");
  }

  // Normalize Unicode to NFC form to prevent homoglyph/normalization attacks
  // This ensures consistent representation of characters
  QString normalized = path.normalized(QString::NormalizationForm_C);

  // Reject if normalization changed the path (indicates potential obfuscation)
  if (normalized != path) {
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Path contains non-canonical Unicode",
                               "PathUtils::validatePathSecurity")
        .withDetails("Path was modified by Unicode normalization");
  }

  // Reject high-risk shell metacharacters that are rarely valid in configured
  // paths. Ampersands are intentionally allowed: they are common in ROM titles
  // (for example, "Sonic & Knuckles") and are safe when passed via QProcess
  // argument lists without shell interpretation.
  // Note: ()[] are also allowed as they're common in filenames and safe with
  // QProcess which passes arguments directly without shell interpretation.
  static const QRegularExpression shellMeta(R"([;|`$<>])");
  if (shellMeta.match(normalized).hasMatch()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Path contains shell metacharacters",
                               "PathUtils::validatePathSecurity")
        .withDetails(path);
  }

  // Reject null bytes which could truncate strings in C APIs
  if (normalized.contains(QChar('\0'))) {
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Path contains null bytes",
                               "PathUtils::validatePathSecurity");
  }

  // Reject newlines which could inject additional commands
  if (normalized.contains('\n') || normalized.contains('\r')) {
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Path contains newline characters",
                               "PathUtils::validatePathSecurity");
  }

  // Reject backslash characters (Windows-style paths that could confuse Unix
  // systems)
  if (normalized.contains('\\')) {
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Path contains backslash characters",
                               "PathUtils::validatePathSecurity");
  }

  return Result<void>::success();
}

bool syncDirectory(const QString &dirPath) {
#if defined(Q_OS_UNIX)
  if (dirPath.isEmpty()) {
    return false;
  }
  const QByteArray native = QFile::encodeName(dirPath);
  const int fd = ::open(native.constData(), O_RDONLY
#ifdef O_DIRECTORY
                                                | O_DIRECTORY
#endif
#ifdef O_CLOEXEC
                                                | O_CLOEXEC
#endif
  );
  if (fd < 0) {
    return false;
  }
  const int rc = ::fsync(fd);
  ::close(fd);
  // EINVAL: filesystem doesn't support directory fsync (e.g. some tmpfs).
  // Treat that as success — the rename is still durable on supported FSes.
  return rc == 0 || errno == EINVAL;
#else
  Q_UNUSED(dirPath);
  return true;
#endif
}

} // namespace PathUtils
