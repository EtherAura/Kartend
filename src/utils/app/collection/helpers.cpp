// Helpers split out of the retired collectionutils.cpp. Hosts the non-inline
// CollectionUtils:: function bodies whose declarations live in helpers.h —
// hierarchy walks (collectDescendantIndices, ancestorIndexChain,
// directChildrenOf), parent-link mutation (applyCollectionRemoval), inherited
// scalar resolution (resolveInheritedField + 4 wrappers), the type-string
// helpers (effectiveCollectionType, standardCollectionTypes,
// collectAllCollectionTypes, collectionTypeChoices), plus the small
// filesystem helper (countVirtualFolders) and the SHA1 UUID hash
// (computeCollectionUuid). Putting them here keeps helpers.h cheap to
// include while letting the API stay near its inline siblings.
#include "helpers.h"

#include <algorithm>
#include <QCryptographicHash>
#include <QDir>
#include <QSet>
#include <QString>
#include <QStringList>

namespace CollectionUtils {

QString computeCollectionUuid(const QString &name, const QString &mediaDir) {
  QByteArray norm = (name + "|" + mediaDir).trimmed().toLower().toUtf8();
  QByteArray digest = QCryptographicHash::hash(norm, QCryptographicHash::Sha1).toHex();
  return QString::fromLatin1(digest);
}

int countVirtualFolders(const CollectionConfig &config) {
  // Only count virtual folders if includeContentSubfolders is enabled
  // AND showAllSubfolderItems is false (otherwise items are flattened)
  if (!config.folderBrowsing.includeContentSubfolders ||
      config.folderBrowsing.showAllSubfolderItems) {
    return 0;
  }

  // Determine the effective directory to scan
  QString scanDir = config.mediaDirectory;
  if (!config.folderBrowsing.currentSubfolder.isEmpty()) {
    scanDir = QDir(scanDir).absoluteFilePath(config.folderBrowsing.currentSubfolder);
  }

  QDir dir(scanDir);
  if (!dir.exists()) {
    return 0;
  }

  // Count subdirectories, optionally including hidden folders
  QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot;
  if (config.folderBrowsing.showHiddenFolders) {
    filters |= QDir::Hidden;
  }
  return dir.entryList(filters).size();
}

namespace {
// Recursive worker for collectDescendantIndices. `visited` tracks indices
// already expanded as a parent, so a corrupt config whose parent links form
// a cycle terminates instead of recursing until the stack overflows.
void collectDescendantsInto(int parentIndex, const QList<CollectionConfig> &collections,
                            QSet<int> &visited, QList<int> &out) {
  if (visited.contains(parentIndex)) {
    return;
  }
  visited.insert(parentIndex);
  for (int index = 0; index < collections.size(); ++index) {
    if (collections[index].parentCollectionIndex == parentIndex) {
      out.append(index);
      collectDescendantsInto(index, collections, visited, out);
    }
  }
}
} // namespace

QList<int> collectDescendantIndices(int parentIndex, const QList<CollectionConfig> &collections) {
  QList<int> descendants;
  QSet<int> visited;
  collectDescendantsInto(parentIndex, collections, visited, descendants);
  return descendants;
}

QList<CollectionConfig> applyCollectionRemoval(const QList<CollectionConfig> &collections,
                                               const QList<int> &oldToNew) {
  QList<CollectionConfig> result;
  result.reserve(collections.size());
  for (int i = 0; i < collections.size(); ++i) {
    // A row with no non-negative mapping was removed (or the map doesn't
    // cover it) — drop it.
    if (oldToNew.value(i, -1) < 0) {
      continue;
    }
    CollectionConfig c = collections[i];
    // parentCollectionIndex still holds an ORIGINAL index — translate it.
    // A parent that maps to a negative value was itself removed or is
    // stale, so the survivor is orphaned to the root rather than left
    // pointing at a wrong (or out-of-range) row.
    const int newParent =
        c.parentCollectionIndex >= 0 ? oldToNew.value(c.parentCollectionIndex, -1) : -1;
    c.parentCollectionIndex = newParent;
    c.isSubcollection = (newParent >= 0);
    result.append(c);
  }
  return result;
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
  QString subfolder = QDir::cleanPath(collection.folderBrowsing.currentSubfolder.trimmed());
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

QStringList standardCollectionTypes() {
  // Curated presets surfaced in the type dropdowns. Each maps to a
  // scraper category via MetadataProviderRegistry::normaliseCategory
  // ("Documents" → "reference"; "Images" has no provider). The combobox
  // stays editable so custom types are still allowed.
  return {QStringLiteral("Video"), QStringLiteral("Audio"), QStringLiteral("Images"),
          QStringLiteral("Documents"), QStringLiteral("Games")};
}

QStringList collectionTypeChoices(const QList<CollectionConfig> &collections) {
  // Leading blank = untagged. Standard presets keep display order; any
  // custom type already in use is appended (collectAllCollectionTypes
  // returns it sorted) unless it case-insensitively duplicates a preset.
  QStringList result;
  result.append(QString());
  QSet<QString> seenLower;
  for (const QString &preset : standardCollectionTypes()) {
    result.append(preset);
    seenLower.insert(preset.toLower());
  }
  for (const QString &inUse : collectAllCollectionTypes(collections)) {
    if (!seenLower.contains(inUse.toLower())) {
      seenLower.insert(inUse.toLower());
      result.append(inUse);
    }
  }
  return result;
}

} // namespace CollectionUtils
