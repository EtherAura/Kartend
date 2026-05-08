#include "filterhelpers.h"

#include <QDir>
#include <QFileInfo>

namespace FilterHelpers {

auto mapVisualToActualIndex(int visualIndex, bool isFiltered, const QList<int> &filteredIndices)
    -> int {
  if (!isFiltered) {
    return visualIndex;
  }
  if (visualIndex < 0 || visualIndex >= filteredIndices.size()) {
    return -1;
  }
  return filteredIndices[visualIndex];
}

auto subcollectionNameMatches(const QString &subcollectionName, const QString &needle) -> bool {
  return subcollectionName.contains(needle, Qt::CaseInsensitive);
}

auto displayNameForMediaEntry(const QString &rawEntry, bool showAllSubcollectionItems,
                              const QString &mediaDirectory,
                              const QHash<QString, QString> *filePathToDisplayName,
                              const QHash<QString, QString> *fileNames) -> QString {
  if (showAllSubcollectionItems) {
    if (filePathToDisplayName) {
      const QString display = filePathToDisplayName->value(rawEntry);
      if (!display.isEmpty()) {
        return display;
      }
    }
    return QFileInfo(rawEntry).completeBaseName();
  }

  const QString mediaDir = mediaDirectory.trimmed();
  if (mediaDir.isEmpty()) {
    return QFileInfo(rawEntry).completeBaseName();
  }

  const QString fullPath = QDir(mediaDir).absoluteFilePath(rawEntry);
  if (fileNames) {
    return fileNames->value(fullPath, QFileInfo(rawEntry).completeBaseName());
  }
  return QFileInfo(rawEntry).completeBaseName();
}

} // namespace FilterHelpers
