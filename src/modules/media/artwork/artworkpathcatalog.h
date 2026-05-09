#ifndef ARTWORKPATHCATALOG_H
#define ARTWORKPATHCATALOG_H

#include <QList>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QStringList>

struct CollectionConfig;

/// Owns the discovered artwork-file list for the active collection plus the
/// silent-load progression state for it. Internal QMutex makes every operation
/// thread-safe.
///
/// Two concerns colocate intentionally: path discovery (buildFromCollection
/// walks the collection tree, scans dirs in parallel, populates the path
/// list), and the silent-load cursor + caches that consume that list (batch
/// pop, "is this path already silently cached / pending?" lookups). They
/// share the same mutex so callers don't have to hold two locks atomically.
class ArtworkPathCatalog {
public:
  ArtworkPathCatalog() = default;

  /// Walks @p collections from @p currentIndex to populate the path list.
  /// Includes descendants when showAllSubcollectionItems is set, with
  /// parallel directory scans and a background dentry-prewarm pass.
  /// Resets the silent-load cursor.
  void buildFromCollection(const QList<CollectionConfig> *collections, int currentIndex);

  /// Collects every artwork directory reachable from the given collection,
  /// optionally including descendants. Used by early dentry-prewarm callers.
  [[nodiscard]] static QStringList collectArtworkDirs(const QList<CollectionConfig> *collections,
                                                      int collectionIndex, bool includeDescendants);

  [[nodiscard]] int totalPaths() const;
  [[nodiscard]] bool isEmpty() const;
  [[nodiscard]] bool isExhausted() const;

  /// Pop up to @p maxCount paths starting at the current cursor. Advances
  /// the cursor past whatever was returned.
  [[nodiscard]] QStringList takeNextBatch(int maxCount);

  /// Filter @p batch through the silent-cached + silent-pending sets,
  /// inserting newly-pending paths into the pending set. Returns the subset
  /// that should actually be dispatched to a precache batch.
  [[nodiscard]] QStringList filterAndMarkPending(const QStringList &batch);

  void markSilentlyCached(const QString &path);
  void unmarkSilentPending(const QString &path);

  /// Drops only the silent-pending set. Used by viewport-cancel paths that
  /// abandon in-flight precache work but want to keep the path list, the
  /// cursor, and the silently-cached set.
  void clearSilentPendingOnly();

  /// Drops the path list and resets the cursor and silent-pending tracking,
  /// but keeps the silently-cached set (used after viewport-cancel where
  /// already-decoded artwork is still valid).
  void clearPathsAndPending();

  /// Drops everything: paths, cursor, silent-cached, silent-pending.
  void clearAll();

private:
  static void appendDirImagesNoLock(const QString &dirPath, QSet<QString> &processedDirs,
                                    QStringList &out);

  mutable QMutex m_mutex;
  QStringList m_allPaths;
  int m_index = 0;
  QSet<QString> m_silentlyCached;
  QSet<QString> m_silentPending;
};

#endif // ARTWORKPATHCATALOG_H
