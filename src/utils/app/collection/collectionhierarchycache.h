#ifndef KARTEND_UTILS_APP_COLLECTION_COLLECTIONHIERARCHYCACHE_H
#define KARTEND_UTILS_APP_COLLECTION_COLLECTIONHIERARCHYCACHE_H

// Collection hierarchy cache extracted from collectionutils.h
// (Kartend-0yz3 step 14 — last big peel). Precomputed
// parent → children + descendant indices, per-index UUID strings,
// per-index expanded media + artwork directories, and a UUID → dir
// reverse map. Rebuilt once per navigation entry by MainWindow's
// CollectionHierarchyCache hook so the QueryManager / scroll pipeline /
// search code paths can lookup descendants and UUIDs in O(1) instead
// of re-running SHA1 + filesystem checks on every query. Lives in its
// own translation-unit-input so TUs that only need the cache no
// longer drag in GeneralSettings + CollectionContext from the umbrella.
// rebuild()'s implementation stays in collectionutils.cpp during the
// Kartend-0yz3 split — moving it requires either a new translation
// unit or rolling it into a future collectionhierarchycache.cpp.

#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include "collectionconfig.h"

// Cache for collection hierarchy lookups - avoids repeated O(n) scans
class CollectionHierarchyCache {
public:
  CollectionHierarchyCache() = default;

  // Rebuilds the hierarchy cache with pre-computed UUIDs and directory
  // mappings. Implemented in collectionutils.cpp to avoid header dependencies.
  void rebuild(const QList<CollectionConfig> &collections);

  /// Returns the union of primary children and linked children
  /// for @p parentIndex. Primary children come first in
  /// insertion order; linked children are appended in insertion order with
  /// duplicates suppressed. Most navigation/scroll/search code paths read
  /// children through this accessor, so they pick up alias parents
  /// automatically when the cache is rebuilt.
  [[nodiscard]] const QList<int> &directChildren(int parentIndex) const {
    auto it = m_directChildren.constFind(parentIndex);
    if (it == m_directChildren.cend()) {
      static const QList<int> kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  /// Subset of directChildren(@p parentIndex) reachable only via the
  /// CollectionConfig::additionalParentNames link list — i.e. the
  /// "see-also" appearances. Used by the settings tree (
  /// stage 2) to render linked appearances in italics. Does NOT include
  /// the primary children.
  [[nodiscard]] const QList<int> &linkedDirectChildren(int parentIndex) const {
    auto it = m_linkedDirectChildren.constFind(parentIndex);
    if (it == m_linkedDirectChildren.cend()) {
      static const QList<int> kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  /// All descendants of @p parentIndex via the merged child graph
  /// (primary + linked). Deduped and cycle-bounded — even mutual links
  /// resolve to a finite set. The starting node itself is excluded.
  [[nodiscard]] const QList<int> &allDescendants(int parentIndex) const {
    auto it = m_allDescendants.constFind(parentIndex);
    if (it == m_allDescendants.cend()) {
      static const QList<int> kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  // UUID accessors - O(1) lookup of pre-computed values
  [[nodiscard]] const QString &collectionUuid(int index) const {
    auto it = m_collectionUuids.constFind(index);
    if (it == m_collectionUuids.cend()) {
      static const QString kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  [[nodiscard]] const QString &expandedMediaDir(int index) const {
    auto it = m_expandedMediaDirs.constFind(index);
    if (it == m_expandedMediaDirs.cend()) {
      static const QString kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  [[nodiscard]] const QString &expandedArtworkDir(int index) const {
    auto it = m_expandedArtworkDirs.constFind(index);
    if (it == m_expandedArtworkDirs.cend()) {
      static const QString kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  [[nodiscard]] const QString &uuidToMediaDir(const QString &uuid) const {
    auto it = m_uuidToMediaDir.constFind(uuid);
    if (it == m_uuidToMediaDir.cend()) {
      static const QString kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  [[nodiscard]] const QString &uuidToArtworkDir(const QString &uuid) const {
    auto it = m_uuidToArtworkDir.constFind(uuid);
    if (it == m_uuidToArtworkDir.cend()) {
      static const QString kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  [[nodiscard]] int uuidToCollectionIndex(const QString &uuid) const {
    return m_uuidToCollectionIndex.value(uuid, -1);
  }

  // Lookup artwork directory from a file path's parent directory
  [[nodiscard]] QString artworkDirForFilePath(const QString &filePath) const {
    // Extract the parent directory and try to find the artwork dir
    QFileInfo fi(filePath);
    QString parentDir = fi.absolutePath();
    // Exact match first
    if (m_mediaDirToArtworkDir.contains(parentDir)) {
      return m_mediaDirToArtworkDir.value(parentDir);
    }
    // Try without trailing slash variations
    if (parentDir.endsWith('/')) {
      QString normalized = parentDir.chopped(1);
      if (m_mediaDirToArtworkDir.contains(normalized)) {
        return m_mediaDirToArtworkDir.value(normalized);
      }
    } else {
      QString withSlash = parentDir + '/';
      if (m_mediaDirToArtworkDir.contains(withSlash)) {
        return m_mediaDirToArtworkDir.value(withSlash);
      }
    }
    return QString();
  }

  // Get all UUIDs for a collection and its descendants (for DB queries).
  // Precomputed in rebuild() so this is an O(1) lookup on the hot path.
  [[nodiscard]] const QStringList &descendantUuids(int parentIndex) const {
    auto it = m_descendantUuids.constFind(parentIndex);
    if (it == m_descendantUuids.cend()) {
      static const QStringList kEmpty;
      return kEmpty;
    }
    return it.value();
  }

  [[nodiscard]] bool isValid() const { return m_collections; }

private:
  /// Cycle-safe descendant walk over the merged child graph. Without the
  /// visited set, a link cycle (A links B, B links A) would
  /// loop forever. Traversal order is BFS; result excludes the starting
  /// node so a self-link doesn't make a collection its own descendant.
  QList<int> computeDescendants(int parentIndex) const {
    QList<int> result;
    QSet<int> visited;
    visited.insert(parentIndex);
    QList<int> stack = m_directChildren.value(parentIndex);
    while (!stack.isEmpty()) {
      int idx = stack.takeFirst();
      if (visited.contains(idx)) {
        continue;
      }
      visited.insert(idx);
      result.append(idx);
      stack.append(m_directChildren.value(idx));
    }
    return result;
  }

  const QList<CollectionConfig> *m_collections = nullptr;
  QHash<int, QList<int>> m_directChildren;       // primary ∪ linked, primary first
  QHash<int, QList<int>> m_linkedDirectChildren; // linked-only subset
  QHash<int, QList<int>> m_allDescendants;

  // Pre-computed UUIDs and directory mappings (eliminates SHA1 on each startup)
  QHash<int, QString> m_collectionUuids;          // index -> UUID
  QHash<int, QString> m_expandedMediaDirs;        // index -> expanded media dir
  QHash<int, QString> m_expandedArtworkDirs;      // index -> expanded artwork dir
  QHash<QString, QString> m_uuidToMediaDir;       // UUID -> expanded media dir
  QHash<QString, QString> m_uuidToArtworkDir;     // UUID -> expanded artwork dir
  QHash<QString, int> m_uuidToCollectionIndex;    // UUID -> collection index
  QHash<QString, QString> m_mediaDirToArtworkDir; // media dir -> artwork dir (for file lookups)
  QHash<int, QStringList> m_descendantUuids;      // index -> [self_uuid, descendant_uuids...]
};

#endif
