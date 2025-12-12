#ifndef PATHUTILS_H
#define PATHUTILS_H

#include "errorutils.h"
#include <QString>
#include <QStringList>

namespace PathUtils {

// Reason for validation failure
enum class PathValidationError {
  EmptyPath,
  NotAbsolute,
  DoesNotExist
};

// Legacy function - returns empty string on failure (backward compatible)
[[nodiscard]] QString validateAndExpandPath(const QString &path,
                              const QString &collectionName = QString());

// New function - returns Result with structured error context
[[nodiscard]] ErrorUtils::Result<QString> tryValidateAndExpandPath(
    const QString &path, const QString &collectionName = QString());

[[nodiscard]] QString truncatePathForDisplay(const QString &path, int maxLength = 50);
[[nodiscard]] QString normalizeDisplayName(const QString &input);

/// Validates that a path doesn't contain shell metacharacters, null bytes,
/// newlines, or other characters that could enable command injection.
/// Returns success if path is safe, or an error context describing the issue.
[[nodiscard]] ErrorUtils::Result<void> validatePathSecurity(const QString &path);

} // namespace PathUtils

#endif