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

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"

QStringList QueryManager::loadOrScanCollection(int collectionIndex,
                                               const CollectionConfig &collection,
                                               QHash<QString, QDateTime> &timestamps,
                                               QHash<QString, qint64> *sizes) {
  QStringList filePaths;
  if (collection.mediaDirectory.trimmed().isEmpty()) {
    return filePaths;
  }

  if (m_scanService.needsRescan(collectionIndex, collection)) {
    QString dirSignature;
    bool scanCompleted = false;
    filePaths =
        m_scanService.scanMediaDirectory(collection, timestamps, &dirSignature, &scanCompleted);
    // Kartend-fys4o: persist zero-file results too, as long as the scan
    // genuinely COMPLETED (directory exists, not cancelled). Skipping the
    // save on empty conflated "emptied directory" with "missing directory /
    // cancelled": stale items rows were never pruned (ghost items vs DB
    // counts) and last_scanned/dir_signature never updated, so needsRescan
    // stayed true and the directory was re-walked on every load.
    // saveItemsToDatabase handles the empty list correctly: zero staged
    // rows -> deleteItemsMissingFromScan prunes everything -> scan metadata
    // is stamped.
    if (!filePaths.isEmpty() || scanCompleted) {
      m_scanService.saveItemsToDatabase(collectionIndex, filePaths, timestamps, collection,
                                        dirSignature);
    }
  } else {
    const QString uuid =
        CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);
    // Kartend-m9r1s: pull last_modified/file_size out of the same SELECT so
    // the Date/Size sort modes downstream reuse them instead of statting
    // every file again.
    filePaths = loadItemsFromDatabaseByUuid(uuid, &timestamps, sizes);
  }

  return filePaths;
}
