// Collection utility functions
#include "collectionutils.h"
#include "settingsutils.h"
#include <QCryptographicHash>

namespace CollectionUtils {

QString computeCollectionUuid(const QString &name, const QString &mediaDir) {
  QByteArray norm = (name + "|" + mediaDir).trimmed().toLower().toUtf8();
  QByteArray digest = QCryptographicHash::hash(norm, QCryptographicHash::Sha1).toHex();
  return QString::fromLatin1(digest);
}

} // namespace CollectionUtils

void CollectionHierarchyCache::rebuild(const QList<CollectionConfig> &collections) {
  m_directChildren.clear();
  m_linkedDirectChildren.clear();
  m_allDescendants.clear();
  m_collectionUuids.clear();
  m_uuidToMediaDir.clear();
  m_uuidToArtworkDir.clear();
  m_uuidToCollectionIndex.clear();
  m_expandedMediaDirs.clear();
  m_expandedArtworkDirs.clear();
  m_mediaDirToArtworkDir.clear();
  m_collections = &collections;

  // Primary children — single parent per collection via parentCollectionIndex.
  for (int i = 0; i < collections.size(); ++i) {
    int parent = collections[i].parentCollectionIndex;
    if (parent >= 0) {
      m_directChildren[parent].append(i);
    }
  }

  // Kartend-gzmk: linked children — additionalParentNames resolved to indices.
  // Names are user-controlled and may reference deleted/typo'd parents; we
  // silently skip unknowns and self-references so a stale config can't wedge
  // the tree. Linked children are appended after primary so the merged list
  // visually groups "real" children first.
  QHash<QString, int> nameToIndex;
  nameToIndex.reserve(collections.size());
  for (int i = 0; i < collections.size(); ++i) {
    nameToIndex.insert(collections[i].name, i);
  }
  for (int i = 0; i < collections.size(); ++i) {
    for (const QString &parentName : collections[i].additionalParentNames) {
      const int parentIdx = nameToIndex.value(parentName, -1);
      if (parentIdx < 0 || parentIdx == i) {
        continue;
      }
      // Suppress duplicate links (same parent named twice in the list)
      // and skip if the user already has it as their primary parent —
      // listing it again would just push the same child twice through the
      // merge below.
      if (m_linkedDirectChildren[parentIdx].contains(i)) {
        continue;
      }
      if (collections[i].parentCollectionIndex == parentIdx) {
        continue;
      }
      m_linkedDirectChildren[parentIdx].append(i);
      // Append to the merged children list (primary entries are already
      // present; linked entries trail).
      m_directChildren[parentIdx].append(i);
    }
  }

  // Pre-compute all descendants. computeDescendants() walks m_directChildren,
  // so it already sees both primary and linked children, and dedupes via the
  // visited set so a Kartend-gzmk cycle resolves to a finite walk.
  for (int i = 0; i < collections.size(); ++i) {
    m_allDescendants[i] = computeDescendants(i);
  }

  // Pre-compute UUIDs and directory mappings for all collections.
  // This eliminates repeated SHA1 hashing and path expansion during startup
  // when showAllSubcollectionItems is enabled.
  for (int i = 0; i < collections.size(); ++i) {
    const CollectionConfig &cfg = collections[i];
    QString mediaDir = SettingsUtils::expandConfigVariables(cfg.mediaDirectory, cfg.name);
    QString artworkDir = SettingsUtils::expandConfigVariables(cfg.artworkDirectory, cfg.name);

    // Resolve artwork directory with parent fallback for subcollections
    if (artworkDir.trimmed().isEmpty() && cfg.isSubcollection) {
      artworkDir = SettingsUtils::expandConfigVariables(
          CollectionUtils::resolveArtworkDirectory(i, collections), cfg.name);
    }

    m_expandedMediaDirs[i] = mediaDir;
    m_expandedArtworkDirs[i] = artworkDir;

    // Build media dir → artwork dir mapping for file path lookups
    if (!mediaDir.trimmed().isEmpty() && !artworkDir.trimmed().isEmpty()) {
      m_mediaDirToArtworkDir[mediaDir] = artworkDir;
    }

    // Only compute UUID if media directory exists
    if (!mediaDir.trimmed().isEmpty()) {
      QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, mediaDir);
      m_collectionUuids[i] = uuid;
      m_uuidToMediaDir[uuid] = mediaDir;
      m_uuidToArtworkDir[uuid] = artworkDir;
      m_uuidToCollectionIndex[uuid] = i;
    }
  }
}
