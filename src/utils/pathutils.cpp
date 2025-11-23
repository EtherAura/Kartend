#include "pathutils.h"
#include <QDir>
#include <QFileInfo>

// Expands placeholders and returns an absolute existing path; returns empty for
// empty/non-absolute/non-existent paths
auto PathUtils::validateAndExpandPath(const QString &path,
                                      const QString &collectionName)
    -> QString {
  QString result = path.trimmed();
  if (!collectionName.isEmpty()) {
    result.replace("%collection%", collectionName, Qt::CaseInsensitive);
  }
  if (result.isEmpty()) {
    return {};
  }

  QDir dir(result);
  if (!dir.isAbsolute()) {
    return {};
  }
  if (!dir.exists()) {
    return {};
  }
  return dir.absolutePath();
}

auto PathUtils::truncatePathForDisplay(const QString &path, int maxLength)
    -> QString {
  if (path.length() <= maxLength) {
    return path;
  }

  return "..." + path.right(maxLength - 3);
}

auto PathUtils::normalizeDisplayName(const QString &input) -> QString {
  QString out = input;
  out.replace('_', ' ').replace('-', ' ');
  out = out.simplified().toLower();
  return out;
}