#include "collectionhierarchybuilder.h"

#include <QStringList>

#include "collection/collectionconfig.h"
#include "collection/helpers.h"
namespace {

int findParentCollectionIndex(const QStringList &parts, const QString &immediateParentName,
                              const QList<CollectionConfig> &collections) {
  for (int i = 0; i < collections.size(); ++i) {
    if (collections[i].name == immediateParentName) {
      if (parts.size() == 2 && !collections[i].isSubcollection) {
        return i;
      }
      if (parts.size() > 2) {
        QStringList parentPath = parts.mid(0, parts.size() - 1);
        QString expectedParentPath = parentPath.join('/');
        QString actualParentPath =
            CollectionUtils::hierarchicalNameFor(collections[i], collections);
        if (actualParentPath == expectedParentPath) {
          return i;
        }
      }
    }
  }
  return -1;
}

void processSubcollection(const QString &sectionName, CollectionConfig &collection,
                          QList<CollectionConfig> &collections) {
  QStringList parts = sectionName.split('/', Qt::KeepEmptyParts);
  if (parts.size() < 2) {
    return;
  }

  const QString &immediateParentName = parts[parts.size() - 2];
  int parentIndex = findParentCollectionIndex(parts, immediateParentName, collections);

  if (parentIndex >= 0) {
    collection.parentCollectionIndex = parentIndex;
    collection.isSubcollection = true;
    collections.append(collection);
  }
}

} // namespace

namespace CollectionHierarchyBuilder {

void build(const QHash<QString, CollectionConfig> &tempCollections,
           QList<CollectionConfig> &collections) {
  QStringList sectionNames = tempCollections.keys();
  sectionNames.sort();

  for (const QString &sectionName : sectionNames) {
    CollectionConfig collection = tempCollections[sectionName];
    if (!sectionName.contains('/')) {
      collection.isSubcollection = false;
      collection.parentCollectionIndex = -1;
      collections.append(collection);
    }
  }

  for (const QString &sectionName : sectionNames) {
    if (sectionName.contains('/')) {
      CollectionConfig collection = tempCollections[sectionName];
      processSubcollection(sectionName, collection, collections);
    }
  }
}

} // namespace CollectionHierarchyBuilder
