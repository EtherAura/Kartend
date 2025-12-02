// Provides file path normalization, extension handling, and path manipulation.
#include "pathutils.h"
#include <QDir>
#include <QFileInfo>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using ErrorUtils::Result;

namespace PathUtils {

// Helper to expand placeholders in path
static QString expandPath(const QString &path, const QString &collectionName) {
  QString result = path.trimmed();
  if (!collectionName.isEmpty()) {
    result.replace("%collection%", collectionName, Qt::CaseInsensitive);
  }
  return result;
}

// New Result-returning version with structured error context
Result<QString> tryValidateAndExpandPath(const QString &path,
                                         const QString &collectionName) {
  QString result = expandPath(path, collectionName);
  
  if (result.isEmpty()) {
    return ErrorContext::error(
        ErrorCode::InvalidFilePath,
        "Path is empty after expansion",
        "PathUtils::tryValidateAndExpandPath")
        .withDetails(QString("Original: '%1', Collection: '%2'")
                         .arg(path, collectionName));
  }

  QDir dir(result);
  if (!dir.isAbsolute()) {
    return ErrorContext::error(
        ErrorCode::InvalidFilePath,
        "Path is not absolute",
        "PathUtils::tryValidateAndExpandPath")
        .withDetails(QString("Path: '%1'").arg(result));
  }
  
  if (!dir.exists()) {
    return ErrorContext::error(
        ErrorCode::FileNotFound,
        "Directory does not exist",
        "PathUtils::tryValidateAndExpandPath")
        .withDetails(QString("Path: '%1'").arg(result));
  }
  
  return dir.absolutePath();
}

// Legacy function - delegates to Result version for consistency
QString validateAndExpandPath(const QString &path,
                              const QString &collectionName) {
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

} // namespace PathUtils