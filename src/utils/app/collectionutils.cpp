// Collection utility functions
#include "collectionutils.h"
#include "settingsutils.h"

#include <algorithm>
#include <QCryptographicHash>
#include <QDir>
#include <QSet>
#include <QString>
#include <QStringList>

namespace LauncherUtils {

LauncherConfig resolvePreset(const LauncherConfig &lc, const QList<LauncherPreset> &presets) {
  if (lc.presetId.isEmpty()) {
    return lc;
  }
  for (const LauncherPreset &preset : presets) {
    if (preset.id == lc.presetId) {
      LauncherConfig resolved;
      resolved.name = preset.name;
      resolved.launcherPath = preset.launcherPath;
      resolved.corePath = preset.corePath;
      resolved.launchParameters = preset.launchParameters;
      resolved.presetId = lc.presetId;
      return resolved;
    }
  }
  return lc;
}

} // namespace LauncherUtils

namespace CollectionUtils {

QString computeCollectionUuid(const QString &name, const QString &mediaDir) {
  QByteArray norm = (name + "|" + mediaDir).trimmed().toLower().toUtf8();
  QByteArray digest = QCryptographicHash::hash(norm, QCryptographicHash::Sha1).toHex();
  return QString::fromLatin1(digest);
}

int countVirtualFolders(const CollectionConfig &config) {
  // Only count virtual folders if includeContentSubfolders is enabled
  // AND showAllSubfolderItems is false (otherwise items are flattened)
  if (!config.includeContentSubfolders || config.showAllSubfolderItems) {
    return 0;
  }

  // Determine the effective directory to scan
  QString scanDir = config.mediaDirectory;
  if (!config.currentSubfolder.isEmpty()) {
    scanDir = QDir(scanDir).absoluteFilePath(config.currentSubfolder);
  }

  QDir dir(scanDir);
  if (!dir.exists()) {
    return 0;
  }

  // Count subdirectories, optionally including hidden folders
  QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot;
  if (config.showHiddenFolders) {
    filters |= QDir::Hidden;
  }
  return dir.entryList(filters).size();
}

QList<int> collectDescendantIndices(int parentIndex, const QList<CollectionConfig> &collections) {
  QList<int> descendants;
  for (int index = 0; index < collections.size(); ++index) {
    if (collections[index].parentCollectionIndex == parentIndex) {
      descendants.append(index);
      descendants.append(collectDescendantIndices(index, collections));
    }
  }
  return descendants;
}

QString hierarchicalNameFor(const CollectionConfig &collection,
                            const QList<CollectionConfig> &collections) {
  if (!collection.isSubcollection || collection.parentCollectionIndex < 0) {
    return collection.name;
  }
  QStringList parts;
  parts.prepend(collection.name);
  int parent = collection.parentCollectionIndex;
  while (parent >= 0 && parent < collections.size()) {
    const CollectionConfig &p = collections[parent];
    parts.prepend(p.name);
    if (!p.isSubcollection) break;
    parent = p.parentCollectionIndex;
  }
  return parts.join('/');
}

QList<int> ancestorIndexChain(const CollectionConfig &collection,
                              const QList<CollectionConfig> &collections) {
  QList<int> chain;
  if (!collection.isSubcollection || collection.parentCollectionIndex < 0) {
    return chain;
  }
  int parent = collection.parentCollectionIndex;
  // Guard against cycles by bounding the walk length.
  const int maxDepth = collections.size();
  int steps = 0;
  while (parent >= 0 && parent < collections.size() && steps < maxDepth) {
    chain.prepend(parent);
    const CollectionConfig &p = collections[parent];
    if (!p.isSubcollection) {
      break;
    }
    parent = p.parentCollectionIndex;
    ++steps;
  }
  return chain;
}

QString selectionSessionKeyFor(const CollectionConfig &collection,
                               const QList<CollectionConfig> &collections) {
  const QString base = hierarchicalNameFor(collection, collections);
  QString subfolder = QDir::cleanPath(collection.currentSubfolder.trimmed());
  if (subfolder.isEmpty() || subfolder == ".") {
    return base;
  }
  return base + "|subfolder=" + subfolder;
}

bool wouldCreateCircularReference(int childIndex, int potentialParentIndex,
                                  const QList<CollectionConfig> &collections) {
  // Out-of-range or self-parenting: reject.
  if (childIndex < 0 || childIndex >= collections.size() || potentialParentIndex < 0 ||
      potentialParentIndex >= collections.size()) {
    return true;
  }
  if (potentialParentIndex == childIndex) {
    return true;
  }

  // Walk up the proposed parent's ancestor chain. Track visited indices to
  // defend against pre-existing cycles in corrupt input.
  int currentParent = collections[potentialParentIndex].parentCollectionIndex;
  QSet<int> visited;
  while (currentParent >= 0 && currentParent < collections.size()) {
    if (visited.contains(currentParent)) {
      return true; // pre-existing cycle, treat as circular
    }
    visited.insert(currentParent);
    if (currentParent == childIndex) {
      return true;
    }
    currentParent = collections[currentParent].parentCollectionIndex;
  }
  return false;
}

QList<int> directChildrenOf(int parentIndex, const QList<CollectionConfig> &collections) {
  QList<int> children;
  for (int i = 0; i < collections.size(); ++i) {
    if (collections[i].parentCollectionIndex == parentIndex) {
      children.append(i);
    }
  }
  return children;
}

QString resolveInheritedField(int collectionIndex, const QList<CollectionConfig> &collections,
                              QString CollectionConfig::*field) {
  if (collectionIndex < 0 || collectionIndex >= collections.size()) {
    return {};
  }
  int current = collectionIndex;
  while (current >= 0 && current < collections.size()) {
    const CollectionConfig &c = collections[current];
    if (!(c.*field).trimmed().isEmpty()) {
      return c.*field;
    }
    if (!c.isSubcollection || c.parentCollectionIndex < 0) {
      break;
    }
    current = c.parentCollectionIndex;
  }
  return {};
}

QString resolveArtworkDirectory(int collectionIndex, const QList<CollectionConfig> &collections) {
  return resolveInheritedField(collectionIndex, collections, &CollectionConfig::artworkDirectory);
}

QString resolveVideoDirectory(int collectionIndex, const QList<CollectionConfig> &collections) {
  return resolveInheritedField(collectionIndex, collections, &CollectionConfig::videoDirectory);
}

QString resolveManualDirectory(int collectionIndex, const QList<CollectionConfig> &collections) {
  return resolveInheritedField(collectionIndex, collections, &CollectionConfig::manualDirectory);
}

QString resolvePlaceholderArtwork(int collectionIndex, const QList<CollectionConfig> &collections) {
  return resolveInheritedField(collectionIndex, collections, &CollectionConfig::placeholderArtwork);
}

QString effectiveCollectionType(int collectionIndex, const QList<CollectionConfig> &collections) {
  if (collectionIndex < 0 || collectionIndex >= collections.size()) {
    return {};
  }
  int current = collectionIndex;
  const int maxDepth = collections.size();
  for (int steps = 0; steps < maxDepth; ++steps) {
    if (current < 0 || current >= collections.size()) {
      break;
    }
    const CollectionConfig &c = collections[current];
    if (!c.type.trimmed().isEmpty()) {
      return c.type.trimmed();
    }
    if (!c.isSubcollection || c.parentCollectionIndex < 0) {
      break;
    }
    current = c.parentCollectionIndex;
  }
  return {};
}

QStringList collectAllCollectionTypes(const QList<CollectionConfig> &collections) {
  QStringList result;
  QSet<QString> seenLower;
  for (const CollectionConfig &c : collections) {
    QString trimmed = c.type.trimmed();
    if (trimmed.isEmpty()) {
      continue;
    }
    QString lower = trimmed.toLower();
    if (seenLower.contains(lower)) {
      continue;
    }
    seenLower.insert(lower);
    result.append(trimmed);
  }
  std::sort(result.begin(), result.end(), [](const QString &a, const QString &b) {
    return QString::compare(a, b, Qt::CaseInsensitive) < 0;
  });
  return result;
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
  m_descendantUuids.clear();
  m_collections = &collections;

  // Primary children — single parent per collection via parentCollectionIndex.
  for (int i = 0; i < collections.size(); ++i) {
    int parent = collections[i].parentCollectionIndex;
    if (parent >= 0) {
      m_directChildren[parent].append(i);
    }
  }

  // linked children — additionalParentNames resolved to indices.
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
  // visited set so a cycle resolves to a finite walk.
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

  // Precompute descendantUuids — the accessor was the hottest read on the
  // navigation/query path and rebuilt the QStringList every call. We have
  // all the inputs above (m_collectionUuids + m_allDescendants), so do it
  // once here and let descendantUuids() return-by-const-ref.
  for (int i = 0; i < collections.size(); ++i) {
    QStringList &uuids = m_descendantUuids[i];
    const QString &parentUuid = m_collectionUuids.value(i);
    if (!parentUuid.isEmpty()) {
      uuids << parentUuid;
    }
    for (int descendant : m_allDescendants.value(i)) {
      const QString &uuid = m_collectionUuids.value(descendant);
      if (!uuid.isEmpty()) {
        uuids << uuid;
      }
    }
  }
}
