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

} // namespace PathUtils

#endif