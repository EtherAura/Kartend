#include "pathutils.h"
#include <QDir>
#include <QFileInfo>

namespace PathUtils {

// Expands placeholders and returns an absolute existing path; returns empty for
// empty/non-absolute/non-existent paths
QString validateAndExpandPath(const QString &path,
                              const QString &collectionName) {
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