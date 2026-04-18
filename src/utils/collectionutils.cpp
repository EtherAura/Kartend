// Collection utility functions
#include "collectionutils.h"
#include "settingsutils.h"
#include <QCryptographicHash>

namespace CollectionUtils {

QString computeCollectionUuid(const QString &name, const QString &mediaDir) {
  QByteArray norm = (name + "|" + mediaDir).trimmed().toLower().toUtf8();
  QByteArray digest =
      QCryptographicHash::hash(norm, QCryptographicHash::Sha1).toHex();
  return QString::fromLatin1(digest);
}

} // namespace CollectionUtils

void CollectionHierarchyCache::rebuild(
    const QList<CollectionConfig> &collections) {
  m_directChildren.clear();
  m_allDescendants.clear();
  m_collectionUuids.clear();
  m_uuidToMediaDir.clear();
  m_uuidToArtworkDir.clear();
  m_uuidToCollectionIndex.clear();
  m_expandedMediaDirs.clear();
  m_expandedArtworkDirs.clear();
  m_mediaDirToArtworkDir.clear();
  m_collections = &collections;

  // Build direct children map
  for (int i = 0; i < collections.size(); ++i) {
    int parent = collections[i].parentCollectionIndex;
    if (parent >= 0) {
      m_directChildren[parent].append(i);
    }
  }

  // Pre-compute all descendants for each collection
  for (int i = 0; i < collections.size(); ++i) {
    m_allDescendants[i] = computeDescendants(i);
  }

  // Pre-compute UUIDs and directory mappings for all collections.
  // This eliminates repeated SHA1 hashing and path expansion during startup
  // when showAllSubcollectionItems is enabled.
  for (int i = 0; i < collections.size(); ++i) {
    const CollectionConfig &cfg = collections[i];
    QString mediaDir =
        SettingsUtils::expandConfigVariables(cfg.mediaDirectory, cfg.name);
    QString artworkDir =
        SettingsUtils::expandConfigVariables(cfg.artworkDirectory, cfg.name);

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
