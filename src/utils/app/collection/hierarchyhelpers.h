#ifndef KARTEND_COLLECTION_HIERARCHYHELPERS_H
#define KARTEND_COLLECTION_HIERARCHYHELPERS_H

// CollectionUtils hierarchy + identity helpers, plus the inherited-directory
// resolvers and the virtual-folder count. Split out of the former
// collection/helpers.h grab-bag. Declarations only — definitions live in
// hierarchyhelpers.cpp (they touch QDir + recursion, which would balloon the
// include cost if dragged inline).

#include "collectionconfig.h"

#include <QList>
#include <QString>

namespace CollectionUtils {

/// Count virtual folders (subdirectories) for a collection config.
/// Returns 0 if includeContentSubfolders is disabled or showAllSubfolderItems
/// is enabled. Definition lives in hierarchyhelpers.cpp because it touches QDir
/// + PathUtils, which would balloon the include cost if dragged into a header.
[[nodiscard]] int countVirtualFolders(const CollectionConfig &config);

[[nodiscard]] QList<int> collectDescendantIndices(int parentIndex,
                                                  const QList<CollectionConfig> &collections);

/**
 * @brief Rebuilds a collection list after a deletion, dropping removed rows
 * and remapping every survivor's parent link to its new position.
 *
 * @p oldToNew maps each original index to its post-removal index, with a
 * negative entry marking a removed (or unmapped) row. A survivor keeps its
 * row when @c oldToNew[i] >= 0; its @c parentCollectionIndex is translated
 * through the same map. A survivor whose parent maps to a negative value —
 * the parent was itself removed, or the stored index is stale/out of range —
 * is orphaned to the root (parent -1, @c isSubcollection false).
 *
 * Skipping this remap is what leaves subcollections pointing at the wrong
 * row, or off the end of the list, after a delete: the source of stray
 * "ghost" collections in the root view and of crashes when the stale index
 * is later dereferenced.
 */
[[nodiscard]] QList<CollectionConfig>
applyCollectionRemoval(const QList<CollectionConfig> &collections, const QList<int> &oldToNew);

[[nodiscard]] QString hierarchicalNameFor(const CollectionConfig &collection,
                                          const QList<CollectionConfig> &collections);

/**
 * @brief Returns the ancestor index chain for a collection, root-first.
 *
 * Walks up `parentCollectionIndex` from `collection`'s direct parent until the
 * first non-subcollection ancestor (inclusive). The returned list is ordered
 * from the outermost (root-most) ancestor to the direct parent and does NOT
 * include `collection` itself. Returns an empty list for non-subcollections or
 * collections whose `parentCollectionIndex` is out of range.
 *
 * Use this to render multi-level breadcrumbs.
 */
[[nodiscard]] QList<int> ancestorIndexChain(const CollectionConfig &collection,
                                            const QList<CollectionConfig> &collections);

[[nodiscard]] QString selectionSessionKeyFor(const CollectionConfig &collection,
                                             const QList<CollectionConfig> &collections);

/**
 * @brief Detects whether reparenting `childIndex` under `potentialParentIndex`
 * would create a cycle in the collection hierarchy.
 *
 * Walks up `potentialParentIndex`'s ancestor chain looking for `childIndex`.
 * If found, the proposed reparent is illegal. Also defends against pre-existing
 * cycles in the input data by tracking visited indices.
 *
 * Returns true (i.e. "circular, reject the operation") in these cases:
 *   - either index is out of range
 *   - childIndex == potentialParentIndex (self-parenting)
 *   - childIndex is already an ancestor of potentialParentIndex
 *   - the existing chain has a pre-existing cycle (data corruption)
 *
 * Pure function — extracted from SettingsDialog so the validation can be
 * unit-tested without instantiating the full settings dialog.
 */
[[nodiscard]] bool wouldCreateCircularReference(int childIndex, int potentialParentIndex,
                                                const QList<CollectionConfig> &collections);

[[nodiscard]] QList<int> directChildrenOf(int parentIndex,
                                          const QList<CollectionConfig> &collections);

/**
 * @brief Walks up the parent chain looking for a non-empty value of @p field
 * on @p collectionIndex or its nearest ancestor. Returns the empty string if
 * no ancestor has the field set.
 *
 * The four directory resolvers below were textually identical except for the
 * member they read; this helper consolidates the parent-chain walk so future
 * fixes (cycle handling, subcollection semantics) need only one edit.
 */
[[nodiscard]] QString resolveInheritedField(int collectionIndex,
                                            const QList<CollectionConfig> &collections,
                                            QString CollectionConfig::*field);

[[nodiscard]] QString resolveArtworkDirectory(int collectionIndex,
                                              const QList<CollectionConfig> &collections);

/**
 * @brief Resolves video directory for a collection, falling back to parent if
 * empty. Used by the sidebar so subcollections inherit a parent's
 * videoDirectory in showAllSubcollectionItems mode.
 */
[[nodiscard]] QString resolveVideoDirectory(int collectionIndex,
                                            const QList<CollectionConfig> &collections);

[[nodiscard]] QString resolveManualDirectory(int collectionIndex,
                                             const QList<CollectionConfig> &collections);

[[nodiscard]] QString resolvePlaceholderArtwork(int collectionIndex,
                                                const QList<CollectionConfig> &collections);

} // namespace CollectionUtils

#endif // KARTEND_COLLECTION_HIERARCHYHELPERS_H
