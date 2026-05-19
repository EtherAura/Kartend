// loadOrScanCollection — the load-or-scan hybrid called by the four load
// slots in querymanagerload.cpp.
//
// The filesystem scan + rescan-needed checks (needsRescan, scanMediaDirectory)
// and the scan-and-save pipelines were extracted into ScanService — see
// scanservice.{h,cpp}. loadOrScanCollection stays a QueryManager member: it
// is a unidirectional bridge into the scan subsystem (QueryManager ->
// ScanService) and otherwise loads cached rows via loadItemsFromDatabaseByUuid.
#include "querymanager.h"

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>

#include "collectionutils.h"

QStringList QueryManager::loadOrScanCollection(int collectionIndex,
                                               const CollectionConfig &collection,
                                               QHash<QString, QDateTime> &timestamps) {
  QStringList filePaths;
  if (collection.mediaDirectory.trimmed().isEmpty()) {
    return filePaths;
  }

  if (m_scanService.needsRescan(collectionIndex, collection)) {
    QString dirSignature;
    filePaths = m_scanService.scanMediaDirectory(collection, timestamps, &dirSignature);
    if (!filePaths.isEmpty()) {
      m_scanService.saveItemsToDatabase(collectionIndex, filePaths, timestamps, collection,
                                        dirSignature);
    }
  } else {
    const QString uuid =
        CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);
    filePaths = loadItemsFromDatabaseByUuid(uuid);
  }

  return filePaths;
}
