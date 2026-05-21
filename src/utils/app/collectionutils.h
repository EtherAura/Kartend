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

// Forward declaration for validation
namespace ErrorUtils {
struct ErrorContext;
}

namespace CollectionUtils {

[[nodiscard]] inline QString alignmentToString(HorizontalAlignment alignment) {
  switch (alignment) {
  case HorizontalAlignment::Left:
    return "left";
  case HorizontalAlignment::Center:
    return "center";
  case HorizontalAlignment::Right:
    return "right";
  default:
    return "center";
  }
}

[[nodiscard]] inline HorizontalAlignment stringToAlignment(const QString &str) {
  QString lower = str.toLower();
  if (lower == "left") return HorizontalAlignment::Left;
  if (lower == "right") return HorizontalAlignment::Right;
  return HorizontalAlignment::Center;
}

[[nodiscard]] inline QString viewTypeToString(ViewType viewType) {
  switch (viewType) {
  case ViewType::List:
    return "list";
  case ViewType::CoverFlow:
    return "coverflow";
  case ViewType::Horizontal:
    return "horizontal";
  case ViewType::Grid:
  default:
    return "grid";
  }
}

[[nodiscard]] inline ViewType stringToViewType(const QString &str) {
  QString lower = str.toLower();
  if (lower == "list") return ViewType::List;
  if (lower == "coverflow") return ViewType::CoverFlow;
  if (lower == "horizontal") return ViewType::Horizontal;
  return ViewType::Grid;
}

[[nodiscard]] inline QString headerLogoPositionToString(HeaderLogoPosition pos) {
  switch (pos) {
  case HeaderLogoPosition::TopLeft:
    return "topleft";
  case HeaderLogoPosition::TopRight:
    return "topright";
  case HeaderLogoPosition::TopCenter:
  default:
    return "topcenter";
  }
}

[[nodiscard]] inline HeaderLogoPosition stringToHeaderLogoPosition(const QString &str) {
  const QString lower = str.toLower();
  if (lower == "topleft") return HeaderLogoPosition::TopLeft;
  if (lower == "topright") return HeaderLogoPosition::TopRight;
  return HeaderLogoPosition::TopCenter;
}

[[nodiscard]] inline QString detailsPanePositionToString(DetailsPanePosition pos) {
  switch (pos) {
  case DetailsPanePosition::Left:
    return "left";
  case DetailsPanePosition::Top:
    return "top";
  case DetailsPanePosition::Bottom:
    return "bottom";
  case DetailsPanePosition::Right:
  default:
    return "right";
  }
}

[[nodiscard]] inline DetailsPanePosition stringToDetailsPanePosition(const QString &str) {
  const QString lower = str.toLower();
  if (lower == "left") return DetailsPanePosition::Left;
  if (lower == "top") return DetailsPanePosition::Top;
  if (lower == "bottom") return DetailsPanePosition::Bottom;
  return DetailsPanePosition::Right;
}

/// true for Top/Bottom dock — tells layout/drag code to treat the
/// pane's height (not width) as the configurable dimension and to span the full
/// viewport perpendicular to the dock edge.
[[nodiscard]] inline bool isDetailsPaneHorizontal(DetailsPanePosition pos) {
  return pos == DetailsPanePosition::Top || pos == DetailsPanePosition::Bottom;
}

[[nodiscard]] inline QString detailsPaneBackgroundTypeToString(DetailsPaneBackgroundType type) {
  switch (type) {
  case DetailsPaneBackgroundType::Image:
    return "image";
  case DetailsPaneBackgroundType::Pattern:
    return "pattern";
  case DetailsPaneBackgroundType::Color:
  default:
    return "color";
  }
}

[[nodiscard]] inline DetailsPaneBackgroundType
stringToDetailsPaneBackgroundType(const QString &str) {
  const QString lower = str.toLower();
  if (lower == "image") return DetailsPaneBackgroundType::Image;
  if (lower == "pattern") return DetailsPaneBackgroundType::Pattern;
  return DetailsPaneBackgroundType::Color;
}

[[nodiscard]] inline QString detailsPanePatternToString(DetailsPanePattern pattern) {
  switch (pattern) {
  case DetailsPanePattern::Crosshatch:
  default:
    return "crosshatch";
  }
}

[[nodiscard]] inline DetailsPanePattern stringToDetailsPanePattern(const QString &str) {
  // Single value for now; future patterns slot in here without persistence breakage.
  Q_UNUSED(str);
  return DetailsPanePattern::Crosshatch;
}

[[nodiscard]] inline QString detailsPaneTabToString(DetailsPaneTab tab) {
  switch (tab) {
  case DetailsPaneTab::Collection:
    return "collection";
  case DetailsPaneTab::File:
    return "file";
  case DetailsPaneTab::Item:
  default:
    return "item";
  }
}

[[nodiscard]] inline DetailsPaneTab stringToDetailsPaneTab(const QString &str) {
  const QString lower = str.toLower();
  if (lower == "collection") return DetailsPaneTab::Collection;
  if (lower == "file") return DetailsPaneTab::File;
  return DetailsPaneTab::Item;
}

} // namespace CollectionUtils

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


// Collection index validation helpers - reduces repeated null checks
// Placed after CollectionConfig definition to avoid forward declaration issues
namespace CollectionUtils {

// ─────────────────────────────────────────────────────────────────────────────
// Index validation helpers - reduces repeated null/bounds checks
// ─────────────────────────────────────────────────────────────────────────────

/// Validates index against collection pointer (null-safe)
[[nodiscard]] inline bool isValidIndex(int index, const QList<CollectionConfig> *collections) {
  return collections && index >= 0 && index < collections->size();
}

/// Validates index pointer against collection pointer (null-safe for both)
[[nodiscard]] inline bool isValidIndex(const int *indexPtr,
                                       const QList<CollectionConfig> *collections) {
  return indexPtr && isValidIndex(*indexPtr, collections);
}

/// True when the index points at a real collection OR the synthetic
/// root / home view (index -1). The home view renders the root
/// collections as tiles but has no backing collection; input handlers
/// gate on this (instead of isValidIndex) so it still accepts keyboard
/// + mouse interaction. Callers that then read a per-collection
/// property must still guard that access with isValidIndex — only the
/// outer "is this an interactive view" gate is widened.
[[nodiscard]] inline bool isInteractiveViewIndex(const int *indexPtr,
                                                 const QList<CollectionConfig> *collections) {
  return isValidIndex(indexPtr, collections) || (indexPtr && *indexPtr == -1);
}

/// Validates index against collection reference (no null check needed)
[[nodiscard]] inline bool isValidIndex(int index, const QList<CollectionConfig> &collections) {
  return index >= 0 && index < collections.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// Collection property accessors with validation
// ─────────────────────────────────────────────────────────────────────────────

/// Get grid width with fallback to default - reduces duplication across
/// managers
[[nodiscard]] inline int getGridWidth(const int *indexPtr,
                                      const QList<CollectionConfig> *collections) {
  if (!isValidIndex(indexPtr, collections)) {
    return UIConstants::Grid::DEFAULT_WIDTH;
  }
  return (*collections)[*indexPtr].gridLayout.gridWidth;
}

// ─────────────────────────────────────────────────────────────────────────────
// Effective grid sizing helpers
// ─────────────────────────────────────────────────────────────────────────────
//
// "Sidebar shrinking active" is the predicate captured by the caller: sidebar
// is currently hidden AND the collection's sidebarMode is Expand (i.e. the
// sidebar would push the grid when shown). Overlay mode never shrinks, so we
// always use the primary value there. The alt fields default to 0, which means
// "inherit the primary" — a fresh upgrade keeps existing layout behavior.

[[nodiscard]] inline int effectiveGridWidth(const CollectionConfig &config,
                                            bool sidebarShrinkingActive) {
  if (sidebarShrinkingActive && config.gridLayout.gridWidthSidebarHidden > 0) {
    return config.gridLayout.gridWidthSidebarHidden;
  }
  return config.gridLayout.gridWidth;
}

[[nodiscard]] inline int effectiveHorizontalGridHeight(const CollectionConfig &config,
                                                       bool sidebarShrinkingActive) {
  if (sidebarShrinkingActive && config.gridLayout.horizontalGridHeightSidebarHidden > 0) {
    return config.gridLayout.horizontalGridHeightSidebarHidden;
  }
  return config.gridLayout.horizontalGridHeight;
}

// ─────────────────────────────────────────────────────────────────────────────
// Virtual folder counting
// ─────────────────────────────────────────────────────────────────────────────

/// Count virtual folders (subdirectories) for a collection config.
/// Returns 0 if includeContentSubfolders is disabled or showAllSubfolderItems
/// is enabled.
[[nodiscard]] int countVirtualFolders(const CollectionConfig &config);

} // namespace CollectionUtils

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
