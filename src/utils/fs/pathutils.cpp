// Provides file path normalization, extension handling, and path manipulation.
#include "pathutils.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using ErrorUtils::Result;

// Per-module logging category. Defaults to warning level so a refused
// %collection% substitution stays visible in release logs while letting
// users silence it with QT_LOGGING_RULES="kartend.pathutils=false".
Q_LOGGING_CATEGORY(lcPathUtils, "kartend.pathutils", QtWarningMsg)

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

  // Kartend-2ml9: validate at the substitution seam. A name like "../../etc"
  // interpolated into a %collection% template would yield a traversal path, so
  // this central seam (every validateAndExpandPath caller flows through it) is
  // the last line of defense behind the definition-time checks. On failure we
  // leave the placeholder literal: the resolved path then fails the downstream
  // existence check rather than escaping the configured root.
  if (!collectionName.isEmpty() && result.contains("%collection%", Qt::CaseInsensitive)) {
    if (validateCollectionNameForSubstitution(collectionName).isOk()) {
      result.replace("%collection%", collectionName, Qt::CaseInsensitive);
    } else {
      qCWarning(lcPathUtils).nospace()
          << "Refusing to substitute traversal-unsafe collection name '" << collectionName
          << "' into path '" << path << "'; leaving %collection% placeholder literal";
    }
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

QString expandPathWithoutExistenceCheck(const QString &path, const QString &collectionName) {
  return expandPath(path, collectionName);
}

QString truncatePathForDisplay(const QString &path, int maxLength) {
  // Need room for the "..." prefix: a maxLength below 3 would make
  // path.right(maxLength - 3) negative, which QString::right returns as the
  // whole string — yielding a result longer than the input (Kartend-3pxm).
  maxLength = qMax(maxLength, 3);
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

  // Normalize Unicode to NFC for the security checks below so a decomposed
  // (NFD) form can't slip a different byte sequence past them. We validate the
  // NFC form but accept the original path: NFD filenames are legitimate (macOS
  // and cross-platform media), and rejecting them blocked valid accented / CJK
  // names at this seam. NFC folds only canonical equivalents — this was never a
  // homoglyph defense.
  const QString normalized = path.normalized(QString::NormalizationForm_C);

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

  // Reject backslashes on non-Windows: there a backslash is a literal filename
  // character that Unix tooling can mishandle, and there is no legitimate path
  // separator reason for it. On Windows the backslash is the native separator,
  // so accepting it is required for the Windows cross-build's native CLI paths;
  // the traversal check below splits on both separators so this stays safe.
#ifndef Q_OS_WIN
  if (normalized.contains('\\')) {
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Path contains backslash characters",
                               "PathUtils::validatePathSecurity");
  }
#endif

  // Reject any path component equal to `..` — a traversal segment that escapes
  // the intended directory. The CLI seam (expandAndValidateCliPath) relies
  // solely on this validator, and validateCollectionNameForSubstitution already
  // rejects `..` for the same reason (Kartend-w13c). Split on BOTH separators:
  // non-Windows rejects backslash above so only `/` appears, but Windows accepts
  // backslash as a separator, and without splitting on it a `..\` segment would
  // slip through. A literal `..` inside a name (e.g. "my..file") is left alone —
  // only a standalone segment traverses.
  QString separatorNormalized = normalized;
  separatorNormalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
  const QStringList segments = separatorNormalized.split(QLatin1Char('/'));
  for (const QString &segment : segments) {
    if (segment == QLatin1String("..")) {
      return ErrorContext::error(ErrorCode::InvalidFilePath,
                                 "Path contains a '..' traversal segment",
                                 "PathUtils::validatePathSecurity")
          .withDetails(path);
    }
  }

  return Result<void>::success();
}

Result<void> validateCollectionNameForSubstitution(const QString &collectionName) {
  if (collectionName.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Collection name is empty",
                               "PathUtils::validateCollectionNameForSubstitution");
  }
  if (collectionName.contains('/') || collectionName.contains('\\')) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Collection name contains path separators",
                               "PathUtils::validateCollectionNameForSubstitution")
        .withDetails(QString("Collection '%1'").arg(collectionName));
  }
  // Reject `..` as a whole-segment value to prevent path-traversal injection
  // through `%collection%` substitution. We split on the only separators we
  // accept above (none), so a literal `..` anywhere in the name is the only
  // way it could become a traversal segment after substitution.
  if (collectionName == QStringLiteral("..") || collectionName == QStringLiteral(".")) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Collection name resolves to a relative path segment",
                               "PathUtils::validateCollectionNameForSubstitution")
        .withDetails(QString("Collection '%1'").arg(collectionName));
  }
  return Result<void>::success();
}

Result<QString> expandAndValidateCliPath(const QString &raw, const QString &optionName) {
  const QString expanded = expandPathWithoutExistenceCheck(raw);
  if (expanded.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath,
                               QString("--%1 is empty after expansion").arg(optionName),
                               "PathUtils::expandAndValidateCliPath")
        .withDetails(QString("Raw value: '%1'").arg(raw));
  }
  auto security = validatePathSecurity(expanded);
  if (security.isError()) {
    return security.error();
  }
  return expanded;
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

bool isPrivateDirOfCurrentUser(const QString &dirPath) {
  if (dirPath.isEmpty()) {
    return false;
  }
#if defined(Q_OS_UNIX)
  const QByteArray native = QFile::encodeName(dirPath);
  struct stat st{};
  // lstat: never follow a final symlink — an attacker could point the cache
  // base at a directory they control via a symlink that itself passes for
  // "ours". A symlink fails the S_ISDIR test below regardless.
  if (::lstat(native.constData(), &st) != 0) {
    return false;
  }
  if (!S_ISDIR(st.st_mode)) {
    return false;
  }
  if (st.st_uid != ::geteuid()) {
    return false;
  }
  // No group or other permission bits — only the owner may traverse/write.
  return (st.st_mode & (S_IRWXG | S_IRWXO)) == 0;
#else
  // Windows/other: the per-user temp dir isn't shared across local users, so
  // the cross-user pre-population attack this guards doesn't apply.
  const QFileInfo info(dirPath);
  return info.exists() && info.isDir();
#endif
}

PathStatus checkLauncherPath(const QString &path) {
  if (path.isEmpty()) {
    return PathStatus::Empty;
  }
  QFileInfo info(path);
  // Bare command (e.g. "mpv"): resolve through PATH so the user's stored
  // value is interpreted the same way the launcher would resolve it.
  if (!info.isAbsolute()) {
    const QString resolved = QStandardPaths::findExecutable(path);
    if (resolved.isEmpty()) {
      return PathStatus::Missing;
    }
    info.setFile(resolved);
  }
  if (!info.exists()) {
    return PathStatus::Missing;
  }
  if (!info.isExecutable()) {
    return PathStatus::NotExecutable;
  }
  return PathStatus::OK;
}

PathStatus checkDirectoryPath(const QString &path) {
  if (path.isEmpty()) {
    return PathStatus::Empty;
  }
  const QFileInfo info(path);
  if (!info.exists()) {
    return PathStatus::Missing;
  }
  if (!info.isDir()) {
    return PathStatus::WrongType;
  }
  if (!info.isReadable()) {
    return PathStatus::NotReadable;
  }
  return PathStatus::OK;
}

PathStatus checkFilePath(const QString &path) {
  if (path.isEmpty()) {
    return PathStatus::Empty;
  }
  const QFileInfo info(path);
  if (!info.exists()) {
    return PathStatus::Missing;
  }
  if (!info.isFile()) {
    return PathStatus::WrongType;
  }
  if (!info.isReadable()) {
    return PathStatus::NotReadable;
  }
  return PathStatus::OK;
}

QString pathStatusDescription(PathStatus status) {
  switch (status) {
  case PathStatus::OK:
  case PathStatus::Empty:
    return QString();
  case PathStatus::Missing:
    return QStringLiteral("does not exist on this host");
  case PathStatus::NotExecutable:
    return QStringLiteral("exists but is not executable");
  case PathStatus::NotReadable:
    return QStringLiteral("exists but is not readable");
  case PathStatus::WrongType:
    return QStringLiteral("exists but is the wrong kind (file vs directory)");
  }
  return QString();
}

bool isSafePathComponent(const QString &s) {
  return !s.isEmpty() && !s.contains(QLatin1Char('/')) && !s.contains(QLatin1Char('\\')) &&
         s != QLatin1String(".") && s != QLatin1String("..");
}

} // namespace PathUtils
