// Per-item preview video lookup helpers.
#include "videoutils.h"

#include "extensionutils.h"

#include <QDir>
#include <QFileInfo>

namespace VideoUtils {

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

  return ExtensionUtils::findFileWithExtensions(dir, baseName,
                                                ExtensionUtils::videoBaseExtensions());
}

} // namespace VideoUtils
