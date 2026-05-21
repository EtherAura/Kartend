#ifndef COLLECTIONUTILS_H
#define COLLECTIONUTILS_H

// Standalone enums (HorizontalAlignment, DetailsPane*, BackgroundType,
// ViewType, SortMode) live in collectiontypes.h so files that only need
// the type tags don't pay the cost of including UIConstants, CollectionConfig,
// and the hierarchy cache. This header re-includes that file so existing
// callers compile unchanged.
#include "collectiontypes.h"

#include <algorithm>
#include <QDir>
#include <QHash>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtCore/Qt>
// Pull only the four UIConstants sub-namespaces that this header actually
// references in its inline bodies (Grid, Item, ListView, DetailsPane). The
// umbrella <uiconstants.h> aggregates all 29 subheaders (~1015 LOC) — pulling
// it here forced every one of the ~113 TUs that includes collectionutils.h to
// reparse all 29 even though only these four are used. Subheaders are
// self-contained (no includes themselves), so this is a pure preprocessor
// cost cut with no behavioural change.
#include <uiconstants/detailspane.h>
#include <uiconstants/grid.h>
#include <uiconstants/item.h>
#include <uiconstants/listview.h>

// Leaf structs progressively extracted into src/utils/app/collection/ as part
// of the Kartend-0yz3 god-header split. Re-included here so existing callers
// of collectionutils.h see the same types until the umbrella is retired.
#include "collection/archiveoptions.h"
#include "collection/collectionbackground.h"
#include "collection/collectionconfig.h"
#include "collection/collectioncontext.h"
#include "collection/collectionfilterpreferences.h"
#include "collection/collectionhierarchycache.h"
#include "collection/folderbrowsingoptions.h"
#include "collection/generalsettings.h"
#include "collection/gridlayoutpreferences.h"
#include "collection/launcherconfig.h"
#include "collection/launcherpreset.h"
#include "collection/listviewoptions.h"
#include "collection/scraperoverrides.h"
#include "collection/sidebarappearance.h"

// Kartend-jw6k cleanup: stale ErrorContext forward declaration removed —
// no validators live in this header anymore (they moved to settingsutils
// and the collection/ subheaders during the Kartend-0yz3 / -ysyn / -7uia
// splits).

// Kartend-ysyn: enum<->string converters + index validation + grid sizing +
// virtual-folder counting moved to collection/helpers.h. Re-included here so
// existing callers of collectionutils.h pick them up transparently.
#include "collection/helpers.h"

// LauncherPreset moved to collection/launcherpreset.h (Kartend-0yz3 step 1).
// LauncherConfig + LauncherUtils::{usesLibretroCore,resolvePreset} moved to
// collection/launcherconfig.h (Kartend-0yz3 step 10). resolvePreset's
// implementation still lives in collectionutils.cpp.

// CollectionFilterPreferences moved to collection/collectionfilterpreferences.h (Kartend-0yz3 step 6).

// LauncherProfile moved to collection/launcherconfig.h (Kartend-0yz3 step 10).

// SidebarAppearance moved to collection/sidebarappearance.h (Kartend-0yz3 step 7).

// GridLayoutPreferences moved to collection/gridlayoutpreferences.h (Kartend-0yz3 step 8).

// CollectionBackground moved to collection/collectionbackground.h (Kartend-0yz3 step 9).

// ArchiveOptions moved to collection/archiveoptions.h (Kartend-0yz3 step 2).

// FolderBrowsingOptions moved to collection/folderbrowsingoptions.h (Kartend-0yz3 step 3).

// ListViewOptions moved to collection/listviewoptions.h (Kartend-0yz3 step 4).

// ScraperOverrides moved to collection/scraperoverrides.h (Kartend-0yz3 step 5).
// CollectionConfig moved to collection/collectionconfig.h (Kartend-0yz3 step 11).


// Kartend-ysyn: index validation, grid sizing, and virtual-folder counting
// helpers moved to collection/helpers.h (already included above).

// CollectionContext moved to collection/collectioncontext.h (Kartend-0yz3 step 12).
// GeneralSettings moved to collection/generalsettings.h (Kartend-0yz3 step 13).
// CollectionHierarchyCache moved to collection/collectionhierarchycache.h
// (Kartend-0yz3 step 14); rebuild() impl still lives in collectionutils.cpp.

// Legacy inline functions for backward compatibility
namespace CollectionUtils {

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
 * @brief Resolves artwork directory for a collection, falling back to parent if
 * empty.
 * @param collectionIndex Index of the collection to resolve artwork for.
 * @param collections List of all collections.
 * @return Artwork directory from this collection or nearest ancestor with one
 * set.
 *
 * Walks up the parent chain until a non-empty artworkDirectory is found.
 * Returns empty string if no ancestor has an artwork directory.
 */
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

/**
 * @brief Returns the effective category/type for a collection, walking up
 * the parent chain when the collection's own `type` field is empty.
 *
 * the per-collection type is optional. Subcollections can either
 * declare their own type or inherit from the nearest non-empty ancestor —
 * this matches the user's mental model of "this whole branch is Games" while
 * still letting an oddball subcollection be tagged differently. Returns an
 * empty string when nothing in the chain is tagged.
 *
 * Cycle-safe: bounds the walk by `collections.size()` so a malformed
 * parentCollectionIndex chain can't loop forever.
 */
[[nodiscard]] QString effectiveCollectionType(int collectionIndex,
                                              const QList<CollectionConfig> &collections);

/**
 * @brief Returns the set of distinct non-empty `type` labels across the full
 * collection list (roots and subcollections), case-insensitive deduped and
 * sorted alphabetically. Used to populate the toolbar filter dropdown and
 * the per-collection editor's combobox completion.
 */
[[nodiscard]] QStringList collectAllCollectionTypes(const QList<CollectionConfig> &collections);

/**
 * @brief Returns the curated built-in media-type labels offered as presets in
 * the collection-type dropdowns (creation dialog + per-collection editor).
 * The combobox stays editable, so these are suggestions rather than a closed
 * set — a user can still type a custom type. Order is display order.
 */
[[nodiscard]] QStringList standardCollectionTypes();

/**
 * @brief Builds the type-combobox item list: a leading blank entry (untagged),
 * then the standard presets, then any custom types already in use across
 * @p collections. Case-insensitive deduped; presets keep their display order
 * and custom extras are appended sorted.
 */
[[nodiscard]] QStringList collectionTypeChoices(const QList<CollectionConfig> &collections);

/**
 * @brief Computes a deterministic UUID from collection name and media
 * directory.
 * @param name Collection name.
 * @param mediaDir Media directory path.
 * @return SHA1 hash as hex string.
 */
[[nodiscard]] QString computeCollectionUuid(const QString &name, const QString &mediaDir);

} // namespace CollectionUtils

Q_DECLARE_METATYPE(CollectionConfig)
Q_DECLARE_METATYPE(CollectionContext)

#endif
