#ifndef SETTINGSDIALOGTREEHELPERS_H
#define SETTINGSDIALOGTREEHELPERS_H

#include "applysettingsdialog.h"
#include "collection/collectionconfig.h"
#include <functional>
#include <QList>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

// Pure tree-mutation / tree-sync logic extracted from the SettingsDialog tree
// partials (settingsdialogtree.cpp / settingsdialogtreemutation.cpp) and the
// TreeManager controller so it can be unit-tested without constructing the
// full dialog. Mirrors the NavigationHelpers extraction pattern: stateless
// functions over plain inputs (collection lists + indices); the dialog /
// controller call sites are one-line delegations. Only
// resyncParentIndicesFromTree touches QTreeWidget — the post-drop reparent
// walk is intrinsic to the tree — and tests drive it with a real offscreen
// widget.
namespace SettingsTreeHelpers {

/// Copy a per-category subset from @p src onto @p dst. Each flag in
/// @p categories enables one logical group of appearance/layout fields.
/// Categories not in the mask leave @p dst's existing values untouched.
/// Identity fields (name, parent linkage, icon), path/content fields, the
/// launcher list, and scan-affecting flags are never copied regardless of
/// the mask — those still require an explicit per-collection edit.
/// @p dst is clamped (CollectionConfig::clampValues) even for an empty mask.
///
/// The per-field copies are deliberately an explicit, opt-in allowlist — NOT
/// a whole-leaf `dst.background = src.background` / `dst.listView =
/// src.listView` assignment. Today the Colors block happens to cover every
/// CollectionBackground field and the ListView block every ListViewOptions
/// field, so collapsing them would be field-equivalent *right now*. It is
/// intentionally left expanded so that a future leaf field which should NOT
/// propagate (e.g. a runtime-only member, as
/// FolderBrowsingOptions::currentSubfolder already is) does not start
/// propagating silently the moment it is added.
void copyAppearanceAndLayoutFields(const CollectionConfig &src, CollectionConfig &dst,
                                   ApplySettingsDialog::FieldCategories categories);

/// Mask-aware bulk apply: copies the @p categories subset from
/// @p collections[sourceIndex] onto every valid entry of @p targetIndices in
/// BOTH lists (workingCollections is skipped per-target when shorter than
/// collections). Returns the number of targets actually mutated; invalid
/// targets, the source itself, an empty mask, and an invalid source all
/// contribute zero. Extracted from SettingsDialog::applyCategoriesToIndices.
int applyCategoriesToLists(QList<CollectionConfig> &collections,
                           QList<CollectionConfig> &workingCollections,
                           const QList<int> &targetIndices,
                           ApplySettingsDialog::FieldCategories categories, int sourceIndex);

/// Turn @p child into a subcollection of @p parent (at @p parentIndex):
/// sets the parent linkage and inherits the parent's grid/layout dimensions,
/// spacing, font, scrollbar/sidebar modes, view type, alignment and title
/// visibility so a fresh child starts visually consistent with its parent.
/// Paths, extensions, launcher and identity fields are left untouched.
/// Extracted from the addCollection() inheritance block.
void applySubcollectionDefaults(CollectionConfig &child, const CollectionConfig &parent,
                                int parentIndex);

/// Row model for a parent-collection combo box. Row 0 is always the
/// "no parent" row (label = caller-supplied none-label, mapping = -1);
/// subsequent rows map 1:1 onto eligible collection indices.
struct ParentComboModel {
  QStringList labels;
  QList<int> mapping;
  int selectedRow = 0;
};

/// Predicate deciding whether parenting @p childIndex under
/// @p potentialParentIndex would create a hierarchy cycle. A default-
/// constructed (empty) callable disables cycle filtering — mirroring the
/// dialog's behaviour before its TreeManager is wired.
using CycleCheck = std::function<bool(int childIndex, int potentialParentIndex)>;

/// Build the parent-collection combo model for the collection at
/// @p currentIndex: excludes the collection itself and (when
/// @p wouldCreateCycle is set) any candidate that would create a cycle.
/// selectedRow resolves to the row of the collection's current parent, or 0
/// (the none row) when @p currentIndex is invalid or the parent was filtered
/// out. Extracted from SettingsDialog::updateParentCollectionComboBox.
[[nodiscard]] ParentComboModel buildParentComboModel(const QList<CollectionConfig> &collections,
                                                     int currentIndex, const QString &noneLabel,
                                                     const CycleCheck &wouldCreateCycle);

/// Build the parent-picker combo model for the Duplicate Collection dialog:
/// excludes playlists (virtual, can't be parents) but keeps the source
/// collection itself (a duplicate may nest under its original). selectedRow
/// defaults to the source's current parent, falling back to the none row.
/// Extracted from SettingsDialog::duplicateCollection.
[[nodiscard]] ParentComboModel buildDuplicateParentModel(const QList<CollectionConfig> &collections,
                                                         int sourceParentIndex,
                                                         const QString &noneLabel);

/// Validation outcome for a proposed duplicate-collection name, checked in
/// this order: Empty (blank after trim), Unsafe (would inject a traversal
/// segment when '%collection%' is substituted into launcher paths),
/// Duplicate (name already taken), Ok.
enum class DuplicateNameError { Ok, Empty, Unsafe, Duplicate };

/// Validate @p name (already trimmed by the caller) as the name for a new
/// duplicate of an existing collection. Extracted from
/// SettingsDialog::duplicateCollection.
[[nodiscard]] DuplicateNameError validateDuplicateName(const QString &name,
                                                       const QList<CollectionConfig> &collections);

/// Full copy of @p source overriding only what must differ on a fresh
/// collection: @p name, parent linkage from @p parentIndex, and runtime-only
/// state (virtual-subfolder cursor, playlist markers) which never belongs in
/// a copy. The result is clamped. Extracted from
/// SettingsDialog::duplicateCollection.
[[nodiscard]] CollectionConfig makeDuplicateConfig(const CollectionConfig &source,
                                                   const QString &name, int parentIndex);

/// Rewrite or remove @p oldName references in every collection's
/// additionalParentNames list when a collection is renamed (@p newName
/// non-empty) or removed (@p newName empty). Mutates @p collections and
/// mirrors each touched row into @p workingCollections when provided (rows
/// past its end are skipped). No-op when the names match. Extracted from
/// TreeManager::propagateNameChange.
void propagateNameChange(QList<CollectionConfig> &collections,
                         QList<CollectionConfig> *workingCollections, const QString &oldName,
                         const QString &newName);

/// Maps a tree item back to its collection index (-1 for unknown items).
using ItemIndexResolver = std::function<int(const QTreeWidgetItem *)>;

/// Walk the post-drop tree and resync parentCollectionIndex /
/// isSubcollection on every collection in both lists (workingCollections
/// rows past its end are skipped). Linked-appearance mirrors — items whose
/// Qt::UserRole data is true — aren't draggable but can shuffle along with
/// their primary parent, so they are skipped when resolving the canonical
/// parent and recursed through so their children are still visited.
/// Extracted from TreeManager::onWidgetRearranged.
void resyncParentIndicesFromTree(const QTreeWidget *tree, const ItemIndexResolver &indexOf,
                                 QList<CollectionConfig> &collections,
                                 QList<CollectionConfig> &workingCollections);

} // namespace SettingsTreeHelpers

#endif // SETTINGSDIALOGTREEHELPERS_H
