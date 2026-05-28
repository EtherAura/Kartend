#ifndef KARTEND_MODULES_DATA_SETTINGS_COLLECTIONHIERARCHYBUILDER_H
#define KARTEND_MODULES_DATA_SETTINGS_COLLECTIONHIERARCHYBUILDER_H

// Pure-algorithm extraction from settingsmanagercollections.cpp
// (Kartend-1mud step: aggressive < 500 LOC). Builds the final
// ordered/parent-linked collection list from the per-INI-section temp
// hash. No SettingsManager dependency — the rewrite-on-needsRewrite
// trigger stays at the call site so this helper can be unit-tested
// without a full SettingsManager.

#include <QHash>
#include <QList>
#include <QString>

#include "collection/collectionconfig.h"

namespace CollectionHierarchyBuilder {

// Walks `tempCollections` in sorted-section-name order and appends the
// parent-first, subcollections-second layout into `collections`. Each
// subcollection's parentCollectionIndex is resolved via the path-aware
// matcher so reorder/rename round-trips don't strand children.
void build(const QHash<QString, CollectionConfig> &tempCollections,
           QList<CollectionConfig> &collections);

} // namespace CollectionHierarchyBuilder

#endif // KARTEND_MODULES_DATA_SETTINGS_COLLECTIONHIERARCHYBUILDER_H
