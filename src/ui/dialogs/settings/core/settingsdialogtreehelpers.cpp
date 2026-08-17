// Pure tree-mutation / tree-sync helpers for SettingsDialog. Bodies moved
// verbatim (behaviour-preserving) from settingsdialogtreemutation.cpp /
// settingsdialogtree.cpp / treemanager.cpp so the logic is unit-testable
// without the dialog; see settingsdialogtreehelpers.h for per-function
// provenance and contracts.
#include "settingsdialogtreehelpers.h"

#include "collection/validationhelpers.h"
#include "pathutils.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace SettingsTreeHelpers {

void copyAppearanceAndLayoutFields(const CollectionConfig &src, CollectionConfig &dst,
                                   ApplySettingsDialog::FieldCategories categories) {
  if (categories.testFlag(ApplySettingsDialog::GridLayout)) {
    dst.gridLayout.gridWidth = src.gridLayout.gridWidth;
    dst.gridLayout.horizontalGridHeight = src.gridLayout.horizontalGridHeight;
    dst.gridLayout.gridWidthSidebarHidden = src.gridLayout.gridWidthSidebarHidden;
    dst.gridLayout.horizontalGridHeightSidebarHidden =
        src.gridLayout.horizontalGridHeightSidebarHidden;
    dst.gridLayout.horizontalSpacing = src.gridLayout.horizontalSpacing;
    dst.gridLayout.verticalSpacing = src.gridLayout.verticalSpacing;
    dst.gridLayout.itemWidth = src.gridLayout.itemWidth;
    dst.gridLayout.itemHeight = src.gridLayout.itemHeight;
    dst.gridLayout.cornerRadius = src.gridLayout.cornerRadius;
    dst.horizontalAlignment = src.horizontalAlignment;
    dst.viewType = src.viewType;
  }
  if (categories.testFlag(ApplySettingsDialog::ItemText)) {
    dst.gridLayout.fontSize = src.gridLayout.fontSize;
    dst.customFontFamily = src.customFontFamily;
  }
  if (categories.testFlag(ApplySettingsDialog::Visibility)) {
    dst.hideTitles = src.hideTitles;
    dst.hideSubcollectionTitles = src.hideSubcollectionTitles;
    dst.gridLayout.hideHorizontalScrollbar = src.gridLayout.hideHorizontalScrollbar;
    dst.gridLayout.hideVerticalScrollbar = src.gridLayout.hideVerticalScrollbar;
    dst.sidebar.sidebarMode = src.sidebar.sidebarMode;
  }
  if (categories.testFlag(ApplySettingsDialog::Colors)) {
    dst.background.backgroundType = src.background.backgroundType;
    dst.background.backgroundColor = src.background.backgroundColor;
    dst.background.backgroundImage = src.background.backgroundImage;
    dst.background.backgroundVideo = src.background.backgroundVideo;
    dst.background.primaryColor = src.background.primaryColor;
    dst.background.tileColor = src.background.tileColor;
    dst.background.selectionColor = src.background.selectionColor;
    // Header logo + vignette + parallax + backdrop blur ride along with the
    // Colors category since they're presented in the same dialog area and
    // users intuitively expect "apply theme" to cover them too.
    dst.background.headerLogoImage = src.background.headerLogoImage;
    dst.background.headerLogoPosition = src.background.headerLogoPosition;
    dst.background.vignetteEnabled = src.background.vignetteEnabled;
    dst.background.vignetteIntensity = src.background.vignetteIntensity;
    dst.background.wallpaperParallax = src.background.wallpaperParallax;
    dst.background.parallaxStrength = src.background.parallaxStrength;
    dst.background.toolbarBackdropBlur = src.background.toolbarBackdropBlur;
    dst.background.backdropBlurRadius = src.background.backdropBlurRadius;
  }
  if (categories.testFlag(ApplySettingsDialog::ListView)) {
    dst.listView.listFontSize = src.listView.listFontSize;
    dst.listView.listRowHeight = src.listView.listRowHeight;
    dst.listView.listRowColor = src.listView.listRowColor;
    dst.listView.listAltRowColor = src.listView.listAltRowColor;
  }
  if (categories.testFlag(ApplySettingsDialog::Sidebars)) {
    // WHOLE-STRUCT copies, deliberately (field report 2026-08-17: sidebar
    // prefs silently dropped by this workflow). The per-field lists above
    // drift every time a struct gains a member; these two blocks are pure
    // per-collection preference — no media/artwork paths, no launchers, no
    // scan flags — so everything in them is transferrable, including
    // whatever gets added next.
    dst.sidebar = src.sidebar;
    dst.collectionTree = src.collectionTree;
  }
  dst.clampValues();
}

int applyCategoriesToLists(QList<CollectionConfig> &collections,
                           QList<CollectionConfig> &workingCollections,
                           const QList<int> &targetIndices,
                           ApplySettingsDialog::FieldCategories categories, int sourceIndex) {
  if (categories == ApplySettingsDialog::None) {
    return 0;
  }
  if (!CollectionUtils::isValidIndex(sourceIndex, collections)) {
    return 0;
  }
  // Copy the source by value — a target row assignment may not alias it, and
  // the caller is allowed to include the source's own list in the targets.
  const CollectionConfig source = collections[sourceIndex];
  int applied = 0;
  for (int idx : targetIndices) {
    if (idx < 0 || idx >= collections.size()) {
      continue;
    }
    if (idx == sourceIndex) {
      continue;
    }
    copyAppearanceAndLayoutFields(source, collections[idx], categories);
    if (idx < workingCollections.size()) {
      copyAppearanceAndLayoutFields(source, workingCollections[idx], categories);
    }
    ++applied;
  }
  return applied;
}

void applySubcollectionDefaults(CollectionConfig &child, const CollectionConfig &parent,
                                int parentIndex) {
  child.parentCollectionIndex = parentIndex;
  child.isSubcollection = true;
  child.gridLayout.gridWidth = parent.gridLayout.gridWidth;
  child.gridLayout.horizontalGridHeight = parent.gridLayout.horizontalGridHeight;
  child.gridLayout.gridWidthSidebarHidden = parent.gridLayout.gridWidthSidebarHidden;
  child.gridLayout.horizontalGridHeightSidebarHidden =
      parent.gridLayout.horizontalGridHeightSidebarHidden;
  child.gridLayout.horizontalSpacing = parent.gridLayout.horizontalSpacing;
  child.gridLayout.verticalSpacing = parent.gridLayout.verticalSpacing;
  child.gridLayout.itemWidth = parent.gridLayout.itemWidth;
  child.gridLayout.itemHeight = parent.gridLayout.itemHeight;
  child.gridLayout.fontSize = parent.gridLayout.fontSize;
  child.gridLayout.hideHorizontalScrollbar = parent.gridLayout.hideHorizontalScrollbar;
  child.gridLayout.hideVerticalScrollbar = parent.gridLayout.hideVerticalScrollbar;
  child.sidebar.sidebarMode = parent.sidebar.sidebarMode;
  child.viewType = parent.viewType;
  child.showAllSubcollectionItems = parent.showAllSubcollectionItems;
  child.horizontalAlignment = parent.horizontalAlignment;
  child.hideTitles = parent.hideTitles;
  child.hideSubcollectionTitles = parent.hideSubcollectionTitles;
}

ParentComboModel buildParentComboModel(const QList<CollectionConfig> &collections, int currentIndex,
                                       const QString &noneLabel,
                                       const CycleCheck &wouldCreateCycle) {
  ParentComboModel model;
  model.labels.append(noneLabel);
  model.mapping.append(-1);

  for (int i = 0; i < collections.size(); ++i) {
    if (i == currentIndex) {
      continue;
    }
    if (wouldCreateCycle && wouldCreateCycle(currentIndex, i)) {
      continue;
    }
    model.labels.append(collections[i].name);
    model.mapping.append(i);
  }

  const int desiredParentIndex = (currentIndex >= 0 && currentIndex < collections.size())
                                     ? collections[currentIndex].parentCollectionIndex
                                     : -1;
  const int row = model.mapping.indexOf(desiredParentIndex);
  model.selectedRow = (row < 0) ? 0 : row;
  return model;
}

ParentComboModel buildDuplicateParentModel(const QList<CollectionConfig> &collections,
                                           int sourceParentIndex, const QString &noneLabel) {
  ParentComboModel model;
  model.labels.append(noneLabel);
  model.mapping.append(-1);

  for (int i = 0; i < collections.size(); ++i) {
    if (collections[i].isPlaylist) {
      // Playlists can't be parents — they're not real persisted collections.
      continue;
    }
    model.labels.append(collections[i].name);
    model.mapping.append(i);
  }

  const int row = model.mapping.indexOf(sourceParentIndex);
  model.selectedRow = (row < 0) ? 0 : row;
  return model;
}

DuplicateNameError validateDuplicateName(const QString &name,
                                         const QList<CollectionConfig> &collections) {
  if (name.isEmpty()) {
    return DuplicateNameError::Empty;
  }
  if (PathUtils::validateCollectionNameForSubstitution(name).isError()) {
    return DuplicateNameError::Unsafe;
  }
  for (const auto &existing : collections) {
    if (existing.name == name) {
      return DuplicateNameError::Duplicate;
    }
  }
  return DuplicateNameError::Ok;
}

CollectionConfig makeDuplicateConfig(const CollectionConfig &source, const QString &name,
                                     int parentIndex) {
  // Full copy. Override only the fields that must differ on a fresh
  // collection: name + parent linkage + runtime state.
  CollectionConfig copy = source;
  copy.name = name;
  copy.parentCollectionIndex = parentIndex;
  copy.isSubcollection = (parentIndex >= 0);
  copy.folderBrowsing.currentSubfolder.clear();
  copy.isPlaylist = false;
  copy.playlistId.clear();
  copy.playlistReservedKind.clear();
  copy.clampValues();
  return copy;
}

void propagateNameChange(QList<CollectionConfig> &collections,
                         QList<CollectionConfig> *workingCollections, const QString &oldName,
                         const QString &newName) {
  if (oldName == newName) {
    return;
  }
  for (int i = 0; i < collections.size(); ++i) {
    QStringList &names = collections[i].additionalParentNames;
    if (!names.contains(oldName)) {
      continue;
    }
    if (newName.isEmpty()) {
      names.removeAll(oldName);
    } else {
      for (QString &n : names) {
        if (n == oldName) {
          n = newName;
        }
      }
    }
    if (workingCollections && i < workingCollections->size()) {
      (*workingCollections)[i].additionalParentNames = names;
    }
  }
}

void resyncParentIndicesFromTree(const QTreeWidget *tree, const ItemIndexResolver &indexOf,
                                 QList<CollectionConfig> &collections,
                                 QList<CollectionConfig> &workingCollections) {
  if (!tree || !indexOf) {
    return;
  }
  std::function<void(QTreeWidgetItem *, int)> walk = [&](QTreeWidgetItem *item, int parentIdx) {
    if (!item) {
      return;
    }
    const bool isLinked = item->data(0, Qt::UserRole).toBool();
    const int idx = indexOf(item);
    if (!isLinked && CollectionUtils::isValidIndex(idx, collections)) {
      collections[idx].parentCollectionIndex = parentIdx;
      collections[idx].isSubcollection = (parentIdx >= 0);
      if (idx < workingCollections.size()) {
        workingCollections[idx].parentCollectionIndex = parentIdx;
        workingCollections[idx].isSubcollection = (parentIdx >= 0);
      }
    }
    const int childParentIdx = isLinked ? parentIdx : idx;
    for (int i = 0; i < item->childCount(); ++i) {
      walk(item->child(i), childParentIdx);
    }
  };
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    walk(tree->topLevelItem(i), -1);
  }
}

} // namespace SettingsTreeHelpers
