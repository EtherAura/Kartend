// Per-item preview video lookup helpers.
#include "videoutils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace VideoUtils {

const QStringList &videoBaseExtensions() {
  static const QStringList exts = {QStringLiteral("mp4"), QStringLiteral("webm"),
                                   QStringLiteral("mkv"), QStringLiteral("mov"),
                                   QStringLiteral("avi")};
  return exts;
}

QStringList videoFilters() {
  const QStringList &bases = videoBaseExtensions();
  QStringList filters;
  filters.reserve(bases.size());
  for (const QString &ext : bases) {
    filters.append(QStringLiteral("*.") + ext);
  }
  return filters;
}

QString findVideoForFile(const QString &fileName, const QString &videoDirectory) {
  if (fileName.isEmpty() || videoDirectory.isEmpty()) {
    return {};
  }

  QDir dir(videoDirectory);
  if (!dir.exists()) {
    return {};
  }

  const QString baseName = QFileInfo(fileName).completeBaseName();
  if (baseName.isEmpty()) {
    return {};
  }

  for (const QString &ext : videoBaseExtensions()) {
    QString candidate = dir.absoluteFilePath(baseName + "." + ext);
    if (QFile::exists(candidate)) {
      return candidate;
    }
    candidate = dir.absoluteFilePath(baseName + "." + ext.toUpper());
    if (QFile::exists(candidate)) {
      return candidate;
    }
  }

  return {};
}

} // namespace VideoUtils
