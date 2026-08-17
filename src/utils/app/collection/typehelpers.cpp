// Out-of-line CollectionUtils media-type/category bodies whose declarations
// live in typehelpers.h — the inherited type resolver (effectiveCollectionType),
// the in-use/preset type-label builders (collectAllCollectionTypes,
// standardCollectionTypes, collectionTypeChoices), and the SHA1 UUID hash
// (computeCollectionUuid). Kept out of the header because they touch
// QCryptographicHash + std::sort, which would balloon the include cost if
// inlined.
#include "typehelpers.h"

#include <algorithm>
#include <QCryptographicHash>
#include <QSet>

#include "artworkutils.h"
#include "pathutils.h"

namespace CollectionUtils {

QString computeCollectionUuid(const QString &name, const QString &mediaDir) {
  QByteArray norm = (name + "|" + mediaDir).trimmed().toLower().toUtf8();
  QByteArray digest = QCryptographicHash::hash(norm, QCryptographicHash::Sha1).toHex();
  return QString::fromLatin1(digest);
}

QString computeCollectionUuid(const CollectionConfig &collection) {
  return computeCollectionUuid(collection.name, PathUtils::validateAndExpandPath(
                                                    collection.mediaDirectory, collection.name));
}

int indexForUuid(const QList<CollectionConfig> &collections, const QString &uuid) {
  if (uuid.isEmpty()) {
    return -1;
  }
  for (int i = 0; i < collections.size(); ++i) {
    if (computeCollectionUuid(collections[i]) == uuid) {
      return i;
    }
  }
  return -1;
}

const CollectionConfig *findByUuid(const QList<CollectionConfig> &collections,
                                   const QString &uuid) {
  const int idx = indexForUuid(collections, uuid);
  return idx < 0 ? nullptr : &collections[idx];
}

QString resolvedAssetPath(const QString &raw, const QString &collectionName) {
  const QString trimmed = raw.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }
  return PathUtils::expandPathWithoutExistenceCheck(trimmed, collectionName);
}

QString resolvedCollectionIcon(const CollectionConfig &collection) {
  return resolvedAssetPath(collection.collectionIcon, collection.name);
}

QString resolveCollectionTileArtwork(const QList<CollectionConfig> *collections,
                                     int collectionIndex, const QString &collectionName,
                                     const QString &parentArtworkDirectory) {
  // 1. The collection's own collectionIcon — an explicit per-collection choice.
  if (collections && collectionIndex >= 0 && collectionIndex < collections->size()) {
    const QString icon = resolvedCollectionIcon(collections->at(collectionIndex));
    if (!icon.isEmpty()) {
      return icon;
    }
  }
  // 2. An image named after the collection, in the PARENT's artwork directory.
  if (collectionName.isEmpty() || parentArtworkDirectory.isEmpty()) {
    return {};
  }
  return ArtworkUtils::findArtworkForFile(collectionName, parentArtworkDirectory);
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
