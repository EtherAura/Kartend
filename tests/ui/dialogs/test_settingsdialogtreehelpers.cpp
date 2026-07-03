// SettingsTreeHelpers: the pure tree-mutation / tree-sync logic extracted
// from the SettingsDialog tree partials. Covers the per-category
// appearance/layout allowlist (one slot per FieldCategory + the never-copied
// identity/path fields), the mask-aware bulk apply over both collection
// lists, subcollection inheritance on add, both parent-combo row models
// (self/cycle exclusion vs playlist exclusion), duplicate-name validation,
// duplicate-config scrubbing, additionalParentNames rename/removal
// propagation, and the post-drop reparent walk driven with a real offscreen
// QTreeWidget (linked-appearance mirrors skipped but recursed through).

#include "settingsdialogtreehelpers.h"

#include "collection/collectionconfig.h"
#include "collection/hierarchyhelpers.h"
#include "uiconstants/grid.h"
#include "uiconstants/item.h"

#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItem>

using SettingsTreeHelpers::DuplicateNameError;
using SettingsTreeHelpers::ParentComboModel;

class TestSettingsDialogTreeHelpers : public QObject {
  Q_OBJECT

private slots:
  // copyAppearanceAndLayoutFields
  void gridLayoutCategory_copiesOnlyGridFields();
  void itemTextCategory_copiesFontAndFamily();
  void visibilityCategory_copiesTitleAndScrollbarFlags();
  void colorsCategory_copiesBackgroundClusterIncludingEffects();
  void listViewCategory_copiesListFields();
  void emptyMask_copiesNothingButStillClamps();
  void allMask_neverTouchesIdentityOrPaths();

  // applyCategoriesToLists
  void applyCategories_invalidSourceOrEmptyMaskReturnsZero();
  void applyCategories_skipsSelfAndOutOfRangeTargets();
  void applyCategories_mutatesBothLists();
  void applyCategories_workingListShorterStillMutatesLive();

  // applySubcollectionDefaults
  void subcollectionDefaults_setLinkageAndInheritedSubset();

  // buildParentComboModel
  void parentCombo_excludesSelfAndCycleCandidates();
  void parentCombo_selectsCurrentParentRow();
  void parentCombo_invalidCurrentSelectsNoneRow();
  void parentCombo_noCycleCheckKeepsDescendants();

  // buildDuplicateParentModel
  void duplicateParentModel_skipsPlaylistsKeepsSelf();
  void duplicateParentModel_selectsSourceParent();

  // validateDuplicateName
  void validateDuplicateName_matrix();

  // makeDuplicateConfig
  void makeDuplicateConfig_overridesAndScrubsRuntimeState();

  // propagateNameChange
  void propagateNameChange_renameRewritesBothLists();
  void propagateNameChange_emptyNewNameRemovesReferences();
  void propagateNameChange_noopWhenUnchanged();

  // resyncParentIndicesFromTree (real offscreen QTreeWidget)
  void resyncParentIndices_reflectsReparentedTree();
  void resyncParentIndices_skipsLinkedMirrorsButVisitsChildren();
};

namespace {

CollectionConfig makeCol(const QString &name, int parent = -1) {
  CollectionConfig c;
  c.name = name;
  c.parentCollectionIndex = parent;
  c.isSubcollection = (parent >= 0);
  return c;
}

/// Source with a distinctive value in every field the copy allowlist covers,
/// all inside clampValues() bounds so clamping can't mask a copy.
CollectionConfig styledSource() {
  CollectionConfig c = makeCol(QStringLiteral("Styled"));
  c.type = QStringLiteral("Video");
  c.mediaDirectory = QStringLiteral("/media/styled");
  c.artworkDirectory = QStringLiteral("/art/styled");
  c.extensions = QStringList{QStringLiteral("mp4")};
  c.launcher.launcherPath = QStringLiteral("/usr/bin/player");
  c.collectionIcon = QStringLiteral("/icons/styled.png");
  // GridLayout category
  c.gridLayout.gridWidth = 9;
  c.gridLayout.horizontalGridHeight = 5;
  c.gridLayout.gridWidthSidebarHidden = 11;
  c.gridLayout.horizontalGridHeightSidebarHidden = 6;
  c.gridLayout.horizontalSpacing = 33;
  c.gridLayout.verticalSpacing = 44;
  c.gridLayout.itemWidth = 321;
  c.gridLayout.itemHeight = 345;
  c.gridLayout.cornerRadius = 12;
  c.horizontalAlignment = HorizontalAlignment::Right;
  c.viewType = ViewType::List;
  // ItemText category
  c.gridLayout.fontSize = 19;
  c.customFontFamily = QStringLiteral("Styled Sans");
  // Visibility category
  c.hideTitles = true;
  c.hideSubcollectionTitles = true;
  c.gridLayout.hideHorizontalScrollbar = true;
  c.gridLayout.hideVerticalScrollbar = true;
  c.sidebar.sidebarMode = DetailsPaneMode::Expand;
  // Colors category (background cluster incl. logo/vignette/parallax/blur)
  c.background.backgroundType = BackgroundType::Image;
  c.background.backgroundColor = QStringLiteral("#101010");
  c.background.backgroundImage = QStringLiteral("/bg/styled.png");
  c.background.backgroundVideo = QStringLiteral("/bg/styled.mp4");
  c.background.primaryColor = QStringLiteral("#202020");
  c.background.tileColor = QStringLiteral("#303030");
  c.background.selectionColor = QStringLiteral("#404040");
  c.background.headerLogoImage = QStringLiteral("/logo/styled.png");
  c.background.headerLogoPosition = HeaderLogoPosition::TopRight;
  c.background.vignetteEnabled = true;
  c.background.vignetteIntensity = 77;
  c.background.wallpaperParallax = true;
  c.background.parallaxStrength = 55;
  c.background.toolbarBackdropBlur = true;
  c.background.backdropBlurRadius = 24;
  // ListView category
  c.listView.listFontSize = 15;
  c.listView.listRowHeight = 48;
  c.listView.listRowColor = QStringLiteral("#505050");
  c.listView.listAltRowColor = QStringLiteral("#606060");
  return c;
}

/// Target whose pre-copy values differ from styledSource() in every
/// category, so an unexpected copy is always observable.
CollectionConfig plainTarget() {
  CollectionConfig c = makeCol(QStringLiteral("Plain"));
  c.type = QStringLiteral("Audio");
  c.mediaDirectory = QStringLiteral("/media/plain");
  c.artworkDirectory = QStringLiteral("/art/plain");
  c.extensions = QStringList{QStringLiteral("flac")};
  c.launcher.launcherPath = QStringLiteral("/usr/bin/other");
  c.collectionIcon = QStringLiteral("/icons/plain.png");
  return c;
}

} // namespace

void TestSettingsDialogTreeHelpers::gridLayoutCategory_copiesOnlyGridFields() {
  const CollectionConfig src = styledSource();
  CollectionConfig dst = plainTarget();

  SettingsTreeHelpers::copyAppearanceAndLayoutFields(src, dst, ApplySettingsDialog::GridLayout);

  QCOMPARE(dst.gridLayout.gridWidth, 9);
  QCOMPARE(dst.gridLayout.horizontalGridHeight, 5);
  QCOMPARE(dst.gridLayout.gridWidthSidebarHidden, 11);
  QCOMPARE(dst.gridLayout.horizontalGridHeightSidebarHidden, 6);
  QCOMPARE(dst.gridLayout.horizontalSpacing, 33);
  QCOMPARE(dst.gridLayout.verticalSpacing, 44);
  QCOMPARE(dst.gridLayout.itemWidth, 321);
  QCOMPARE(dst.gridLayout.itemHeight, 345);
  QCOMPARE(dst.gridLayout.cornerRadius, 12);
  QCOMPARE(dst.horizontalAlignment, HorizontalAlignment::Right);
  QCOMPARE(dst.viewType, ViewType::List);

  // The other categories stay untouched.
  QCOMPARE(dst.gridLayout.fontSize, UIConstants::Item::DEFAULT_FONT_SIZE);
  QVERIFY(dst.customFontFamily.isEmpty());
  QVERIFY(!dst.hideTitles);
  QCOMPARE(dst.sidebar.sidebarMode, DetailsPaneMode::Overlay);
  QCOMPARE(dst.background.backgroundType, BackgroundType::Color);
  QVERIFY(dst.background.primaryColor.isEmpty());
  QVERIFY(dst.listView.listRowColor.isEmpty());
}

void TestSettingsDialogTreeHelpers::itemTextCategory_copiesFontAndFamily() {
  const CollectionConfig src = styledSource();
  CollectionConfig dst = plainTarget();

  SettingsTreeHelpers::copyAppearanceAndLayoutFields(src, dst, ApplySettingsDialog::ItemText);

  QCOMPARE(dst.gridLayout.fontSize, 19);
  QCOMPARE(dst.customFontFamily, QStringLiteral("Styled Sans"));
  // Grid dimensions belong to GridLayout, not ItemText.
  QCOMPARE(dst.gridLayout.gridWidth, 4);
  QCOMPARE(dst.viewType, ViewType::Grid);
}

void TestSettingsDialogTreeHelpers::visibilityCategory_copiesTitleAndScrollbarFlags() {
  const CollectionConfig src = styledSource();
  CollectionConfig dst = plainTarget();

  SettingsTreeHelpers::copyAppearanceAndLayoutFields(src, dst, ApplySettingsDialog::Visibility);

  QVERIFY(dst.hideTitles);
  QVERIFY(dst.hideSubcollectionTitles);
  QVERIFY(dst.gridLayout.hideHorizontalScrollbar);
  QVERIFY(dst.gridLayout.hideVerticalScrollbar);
  QCOMPARE(dst.sidebar.sidebarMode, DetailsPaneMode::Expand);
  // Font size rides with ItemText, not Visibility.
  QCOMPARE(dst.gridLayout.fontSize, UIConstants::Item::DEFAULT_FONT_SIZE);
}

void TestSettingsDialogTreeHelpers::colorsCategory_copiesBackgroundClusterIncludingEffects() {
  const CollectionConfig src = styledSource();
  CollectionConfig dst = plainTarget();

  SettingsTreeHelpers::copyAppearanceAndLayoutFields(src, dst, ApplySettingsDialog::Colors);

  QCOMPARE(dst.background.backgroundType, BackgroundType::Image);
  QCOMPARE(dst.background.backgroundColor, QStringLiteral("#101010"));
  QCOMPARE(dst.background.backgroundImage, QStringLiteral("/bg/styled.png"));
  QCOMPARE(dst.background.backgroundVideo, QStringLiteral("/bg/styled.mp4"));
  QCOMPARE(dst.background.primaryColor, QStringLiteral("#202020"));
  QCOMPARE(dst.background.tileColor, QStringLiteral("#303030"));
  QCOMPARE(dst.background.selectionColor, QStringLiteral("#404040"));
  // Header logo / vignette / parallax / backdrop blur ride along with Colors.
  QCOMPARE(dst.background.headerLogoImage, QStringLiteral("/logo/styled.png"));
  QCOMPARE(dst.background.headerLogoPosition, HeaderLogoPosition::TopRight);
  QVERIFY(dst.background.vignetteEnabled);
  QCOMPARE(dst.background.vignetteIntensity, 77);
  QVERIFY(dst.background.wallpaperParallax);
  QCOMPARE(dst.background.parallaxStrength, 55);
  QVERIFY(dst.background.toolbarBackdropBlur);
  QCOMPARE(dst.background.backdropBlurRadius, 24);
  // List-row colors belong to the ListView category, not Colors.
  QVERIFY(dst.listView.listRowColor.isEmpty());
}

void TestSettingsDialogTreeHelpers::listViewCategory_copiesListFields() {
  const CollectionConfig src = styledSource();
  CollectionConfig dst = plainTarget();

  SettingsTreeHelpers::copyAppearanceAndLayoutFields(src, dst, ApplySettingsDialog::ListView);

  QCOMPARE(dst.listView.listFontSize, 15);
  QCOMPARE(dst.listView.listRowHeight, 48);
  QCOMPARE(dst.listView.listRowColor, QStringLiteral("#505050"));
  QCOMPARE(dst.listView.listAltRowColor, QStringLiteral("#606060"));
  QCOMPARE(dst.background.backgroundType, BackgroundType::Color);
}

void TestSettingsDialogTreeHelpers::emptyMask_copiesNothingButStillClamps() {
  const CollectionConfig src = styledSource();
  CollectionConfig dst = plainTarget();
  dst.gridLayout.gridWidth = 0; // below the MIN_WIDTH floor

  SettingsTreeHelpers::copyAppearanceAndLayoutFields(src, dst, ApplySettingsDialog::None);

  // No category copied...
  QVERIFY(!dst.hideTitles);
  QCOMPARE(dst.background.backgroundType, BackgroundType::Color);
  QVERIFY(dst.customFontFamily.isEmpty());
  // ...but the unconditional clamp still ran.
  QCOMPARE(dst.gridLayout.gridWidth, UIConstants::Grid::MIN_WIDTH);
}

void TestSettingsDialogTreeHelpers::allMask_neverTouchesIdentityOrPaths() {
  const CollectionConfig src = styledSource();
  CollectionConfig dst = plainTarget();
  dst.parentCollectionIndex = 3;
  dst.isSubcollection = true;

  SettingsTreeHelpers::copyAppearanceAndLayoutFields(src, dst, ApplySettingsDialog::All);

  QCOMPARE(dst.name, QStringLiteral("Plain"));
  QCOMPARE(dst.type, QStringLiteral("Audio"));
  QCOMPARE(dst.parentCollectionIndex, 3);
  QVERIFY(dst.isSubcollection);
  QCOMPARE(dst.mediaDirectory, QStringLiteral("/media/plain"));
  QCOMPARE(dst.artworkDirectory, QStringLiteral("/art/plain"));
  QCOMPARE(dst.extensions, QStringList{QStringLiteral("flac")});
  QCOMPARE(dst.launcher.launcherPath, QStringLiteral("/usr/bin/other"));
  QCOMPARE(dst.collectionIcon, QStringLiteral("/icons/plain.png"));
}

void TestSettingsDialogTreeHelpers::applyCategories_invalidSourceOrEmptyMaskReturnsZero() {
  QList<CollectionConfig> live{styledSource(), plainTarget()};
  QList<CollectionConfig> working = live;

  QCOMPARE(
      SettingsTreeHelpers::applyCategoriesToLists(live, working, {1}, ApplySettingsDialog::None, 0),
      0);
  QCOMPARE(
      SettingsTreeHelpers::applyCategoriesToLists(live, working, {1}, ApplySettingsDialog::All, -1),
      0);
  QCOMPARE(
      SettingsTreeHelpers::applyCategoriesToLists(live, working, {1}, ApplySettingsDialog::All, 2),
      0);
  QCOMPARE(live[1], plainTarget());
}

void TestSettingsDialogTreeHelpers::applyCategories_skipsSelfAndOutOfRangeTargets() {
  QList<CollectionConfig> live{styledSource(), plainTarget()};
  QList<CollectionConfig> working = live;

  const int applied = SettingsTreeHelpers::applyCategoriesToLists(
      live, working, {-3, 0, 1, 7}, ApplySettingsDialog::GridLayout, 0);

  QCOMPARE(applied, 1); // only index 1 — self and out-of-range skipped
  QCOMPARE(live[0], styledSource());
  QCOMPARE(live[1].gridLayout.gridWidth, 9);
}

void TestSettingsDialogTreeHelpers::applyCategories_mutatesBothLists() {
  QList<CollectionConfig> live{styledSource(), plainTarget(), plainTarget()};
  QList<CollectionConfig> working = live;

  const int applied = SettingsTreeHelpers::applyCategoriesToLists(live, working, {1, 2},
                                                                  ApplySettingsDialog::Colors, 0);

  QCOMPARE(applied, 2);
  for (int idx : {1, 2}) {
    QCOMPARE(live[idx].background.primaryColor, QStringLiteral("#202020"));
    QCOMPARE(working[idx].background.primaryColor, QStringLiteral("#202020"));
    // Non-Colors categories untouched in both lists.
    QCOMPARE(live[idx].gridLayout.gridWidth, 4);
    QCOMPARE(working[idx].gridLayout.gridWidth, 4);
  }
}

void TestSettingsDialogTreeHelpers::applyCategories_workingListShorterStillMutatesLive() {
  QList<CollectionConfig> live{styledSource(), plainTarget()};
  QList<CollectionConfig> working{live[0]}; // index 1 missing from working

  const int applied = SettingsTreeHelpers::applyCategoriesToLists(live, working, {1},
                                                                  ApplySettingsDialog::ListView, 0);

  QCOMPARE(applied, 1);
  QCOMPARE(live[1].listView.listRowColor, QStringLiteral("#505050"));
  QCOMPARE(working.size(), 1);
}

void TestSettingsDialogTreeHelpers::subcollectionDefaults_setLinkageAndInheritedSubset() {
  const CollectionConfig parent = styledSource();
  CollectionConfig child = makeCol(QStringLiteral("Fresh Child"));
  child.mediaDirectory = QStringLiteral("/media/child");
  child.extensions = QStringList{QStringLiteral("mkv")};

  SettingsTreeHelpers::applySubcollectionDefaults(child, parent, 0);

  QCOMPARE(child.parentCollectionIndex, 0);
  QVERIFY(child.isSubcollection);
  // Inherited layout/view subset.
  QCOMPARE(child.gridLayout.gridWidth, 9);
  QCOMPARE(child.gridLayout.horizontalGridHeight, 5);
  QCOMPARE(child.gridLayout.gridWidthSidebarHidden, 11);
  QCOMPARE(child.gridLayout.horizontalGridHeightSidebarHidden, 6);
  QCOMPARE(child.gridLayout.horizontalSpacing, 33);
  QCOMPARE(child.gridLayout.verticalSpacing, 44);
  QCOMPARE(child.gridLayout.itemWidth, 321);
  QCOMPARE(child.gridLayout.itemHeight, 345);
  QCOMPARE(child.gridLayout.fontSize, 19);
  QVERIFY(child.gridLayout.hideHorizontalScrollbar);
  QVERIFY(child.gridLayout.hideVerticalScrollbar);
  QCOMPARE(child.sidebar.sidebarMode, DetailsPaneMode::Expand);
  QCOMPARE(child.viewType, ViewType::List);
  QCOMPARE(child.showAllSubcollectionItems, parent.showAllSubcollectionItems);
  QCOMPARE(child.horizontalAlignment, HorizontalAlignment::Right);
  QVERIFY(child.hideTitles);
  QVERIFY(child.hideSubcollectionTitles);
  // Identity / paths / extensions are the child's own.
  QCOMPARE(child.name, QStringLiteral("Fresh Child"));
  QCOMPARE(child.mediaDirectory, QStringLiteral("/media/child"));
  QCOMPARE(child.extensions, QStringList{QStringLiteral("mkv")});
  // Corner radius / colors are NOT part of the inheritance subset.
  QCOMPARE(child.gridLayout.cornerRadius, UIConstants::Item::DEFAULT_CORNER_RADIUS);
  QVERIFY(child.background.primaryColor.isEmpty());
}

void TestSettingsDialogTreeHelpers::parentCombo_excludesSelfAndCycleCandidates() {
  // Root(0) → Child(1) → Grandchild(2); Other(3) is unrelated.
  QList<CollectionConfig> cols{makeCol(QStringLiteral("Root")), makeCol(QStringLiteral("Child"), 0),
                               makeCol(QStringLiteral("Grandchild"), 1),
                               makeCol(QStringLiteral("Other"))};
  const auto cycleCheck = [&cols](int child, int parent) {
    return CollectionUtils::wouldCreateCircularReference(child, parent, cols);
  };

  const ParentComboModel model =
      SettingsTreeHelpers::buildParentComboModel(cols, 0, QStringLiteral("None"), cycleCheck);

  // Root can't be its own parent, and its descendants would form cycles —
  // only the none row and Other remain.
  QCOMPARE(model.labels, (QStringList{QStringLiteral("None"), QStringLiteral("Other")}));
  QCOMPARE(model.mapping, (QList<int>{-1, 3}));
  QCOMPARE(model.selectedRow, 0);
}

void TestSettingsDialogTreeHelpers::parentCombo_selectsCurrentParentRow() {
  QList<CollectionConfig> cols{makeCol(QStringLiteral("Root")), makeCol(QStringLiteral("Child"), 0),
                               makeCol(QStringLiteral("Other"))};
  const auto cycleCheck = [&cols](int child, int parent) {
    return CollectionUtils::wouldCreateCircularReference(child, parent, cols);
  };

  const ParentComboModel model =
      SettingsTreeHelpers::buildParentComboModel(cols, 1, QStringLiteral("None"), cycleCheck);

  QCOMPARE(model.labels,
           (QStringList{QStringLiteral("None"), QStringLiteral("Root"), QStringLiteral("Other")}));
  QCOMPARE(model.mapping, (QList<int>{-1, 0, 2}));
  QCOMPARE(model.selectedRow, 1); // Child's current parent is Root
}

void TestSettingsDialogTreeHelpers::parentCombo_invalidCurrentSelectsNoneRow() {
  QList<CollectionConfig> cols{makeCol(QStringLiteral("A")), makeCol(QStringLiteral("B"))};

  // No cycle check: with an invalid current index every collection is listed
  // and the none row is selected.
  const ParentComboModel model = SettingsTreeHelpers::buildParentComboModel(
      cols, -1, QStringLiteral("None"), SettingsTreeHelpers::CycleCheck());

  QCOMPARE(model.labels,
           (QStringList{QStringLiteral("None"), QStringLiteral("A"), QStringLiteral("B")}));
  QCOMPARE(model.mapping, (QList<int>{-1, 0, 1}));
  QCOMPARE(model.selectedRow, 0);
}

void TestSettingsDialogTreeHelpers::parentCombo_noCycleCheckKeepsDescendants() {
  QList<CollectionConfig> cols{makeCol(QStringLiteral("Root")),
                               makeCol(QStringLiteral("Child"), 0)};

  const ParentComboModel model = SettingsTreeHelpers::buildParentComboModel(
      cols, 0, QStringLiteral("None"), SettingsTreeHelpers::CycleCheck());

  // Only self-exclusion applies when no cycle predicate is wired.
  QCOMPARE(model.labels, (QStringList{QStringLiteral("None"), QStringLiteral("Child")}));
  QCOMPARE(model.mapping, (QList<int>{-1, 1}));
}

void TestSettingsDialogTreeHelpers::duplicateParentModel_skipsPlaylistsKeepsSelf() {
  QList<CollectionConfig> cols{makeCol(QStringLiteral("Source")),
                               makeCol(QStringLiteral("Playlist")),
                               makeCol(QStringLiteral("Other"))};
  cols[1].isPlaylist = true;

  const ParentComboModel model =
      SettingsTreeHelpers::buildDuplicateParentModel(cols, -1, QStringLiteral("None"));

  // The source itself stays eligible (a duplicate may nest under its
  // original); playlists are dropped.
  QCOMPARE(model.labels, (QStringList{QStringLiteral("None"), QStringLiteral("Source"),
                                      QStringLiteral("Other")}));
  QCOMPARE(model.mapping, (QList<int>{-1, 0, 2}));
  QCOMPARE(model.selectedRow, 0);
}

void TestSettingsDialogTreeHelpers::duplicateParentModel_selectsSourceParent() {
  QList<CollectionConfig> cols{makeCol(QStringLiteral("Root")),
                               makeCol(QStringLiteral("Child"), 0)};

  const ParentComboModel model =
      SettingsTreeHelpers::buildDuplicateParentModel(cols, 0, QStringLiteral("None"));
  QCOMPARE(model.selectedRow, 1); // row of "Root"

  // A parent index that maps to no row (e.g. stale) falls back to none.
  const ParentComboModel stale =
      SettingsTreeHelpers::buildDuplicateParentModel(cols, 99, QStringLiteral("None"));
  QCOMPARE(stale.selectedRow, 0);
}

void TestSettingsDialogTreeHelpers::validateDuplicateName_matrix() {
  const QList<CollectionConfig> cols{makeCol(QStringLiteral("Existing"))};

  QCOMPARE(SettingsTreeHelpers::validateDuplicateName(QString(), cols), DuplicateNameError::Empty);
  QCOMPARE(SettingsTreeHelpers::validateDuplicateName(QStringLiteral("a/b"), cols),
           DuplicateNameError::Unsafe);
  QCOMPARE(SettingsTreeHelpers::validateDuplicateName(QStringLiteral("a\\b"), cols),
           DuplicateNameError::Unsafe);
  QCOMPARE(SettingsTreeHelpers::validateDuplicateName(QStringLiteral(".."), cols),
           DuplicateNameError::Unsafe);
  QCOMPARE(SettingsTreeHelpers::validateDuplicateName(QStringLiteral("Existing"), cols),
           DuplicateNameError::Duplicate);
  QCOMPARE(SettingsTreeHelpers::validateDuplicateName(QStringLiteral("Existing copy"), cols),
           DuplicateNameError::Ok);
}

void TestSettingsDialogTreeHelpers::makeDuplicateConfig_overridesAndScrubsRuntimeState() {
  CollectionConfig source = styledSource();
  source.folderBrowsing.currentSubfolder = QStringLiteral("Deep/Path");
  source.isPlaylist = true;
  source.playlistId = QStringLiteral("uuid-1234");
  source.playlistReservedKind = QStringLiteral("favorites");
  source.parentCollectionIndex = 5;

  const CollectionConfig copy =
      SettingsTreeHelpers::makeDuplicateConfig(source, QStringLiteral("Styled copy"), 2);

  QCOMPARE(copy.name, QStringLiteral("Styled copy"));
  QCOMPARE(copy.parentCollectionIndex, 2);
  QVERIFY(copy.isSubcollection);
  // Runtime-only state scrubbed.
  QVERIFY(copy.folderBrowsing.currentSubfolder.isEmpty());
  QVERIFY(!copy.isPlaylist);
  QVERIFY(copy.playlistId.isEmpty());
  QVERIFY(copy.playlistReservedKind.isEmpty());
  // Everything else is a full copy.
  QCOMPARE(copy.mediaDirectory, source.mediaDirectory);
  QCOMPARE(copy.artworkDirectory, source.artworkDirectory);
  QCOMPARE(copy.extensions, source.extensions);
  QCOMPARE(copy.launcher.launcherPath, source.launcher.launcherPath);
  QCOMPARE(copy.gridLayout, source.gridLayout);
  QCOMPARE(copy.background, source.background);

  // Root duplicate: parent -1 clears the subcollection flag.
  const CollectionConfig rootCopy =
      SettingsTreeHelpers::makeDuplicateConfig(source, QStringLiteral("Styled root"), -1);
  QCOMPARE(rootCopy.parentCollectionIndex, -1);
  QVERIFY(!rootCopy.isSubcollection);
}

void TestSettingsDialogTreeHelpers::propagateNameChange_renameRewritesBothLists() {
  QList<CollectionConfig> live{makeCol(QStringLiteral("Alias Parent")),
                               makeCol(QStringLiteral("Linked"))};
  live[1].additionalParentNames = QStringList{QStringLiteral("Alias Parent")};
  QList<CollectionConfig> working = live;

  SettingsTreeHelpers::propagateNameChange(live, &working, QStringLiteral("Alias Parent"),
                                           QStringLiteral("Renamed Parent"));

  QCOMPARE(live[1].additionalParentNames, QStringList{QStringLiteral("Renamed Parent")});
  QCOMPARE(working[1].additionalParentNames, QStringList{QStringLiteral("Renamed Parent")});

  // A working list shorter than live is tolerated (row skipped, no crash).
  QList<CollectionConfig> shortWorking{live[0]};
  SettingsTreeHelpers::propagateNameChange(live, &shortWorking, QStringLiteral("Renamed Parent"),
                                           QStringLiteral("Again"));
  QCOMPARE(live[1].additionalParentNames, QStringList{QStringLiteral("Again")});
  QCOMPARE(shortWorking.size(), 1);
}

void TestSettingsDialogTreeHelpers::propagateNameChange_emptyNewNameRemovesReferences() {
  QList<CollectionConfig> live{makeCol(QStringLiteral("Linked"))};
  live[0].additionalParentNames = QStringList{QStringLiteral("Vanishing"), QStringLiteral("Keeper"),
                                              QStringLiteral("Vanishing")};

  SettingsTreeHelpers::propagateNameChange(live, nullptr, QStringLiteral("Vanishing"), QString());

  QCOMPARE(live[0].additionalParentNames, QStringList{QStringLiteral("Keeper")});
}

void TestSettingsDialogTreeHelpers::propagateNameChange_noopWhenUnchanged() {
  QList<CollectionConfig> live{makeCol(QStringLiteral("Linked"))};
  live[0].additionalParentNames = QStringList{QStringLiteral("Same")};

  SettingsTreeHelpers::propagateNameChange(live, nullptr, QStringLiteral("Same"),
                                           QStringLiteral("Same"));

  QCOMPARE(live[0].additionalParentNames, QStringList{QStringLiteral("Same")});
}

void TestSettingsDialogTreeHelpers::resyncParentIndices_reflectsReparentedTree() {
  // Collections believe: A(root), B(root), C(child of B). The tree says
  // otherwise after a simulated drop: A > B > C. The walk must rewrite the
  // lists to match the tree.
  QList<CollectionConfig> live{makeCol(QStringLiteral("A")), makeCol(QStringLiteral("B")),
                               makeCol(QStringLiteral("C"), 1)};
  QList<CollectionConfig> working = live;

  QTreeWidget tree;
  auto *itemA = new QTreeWidgetItem(&tree);
  auto *itemB = new QTreeWidgetItem(itemA);
  auto *itemC = new QTreeWidgetItem(itemB);
  for (auto *item : {itemA, itemB, itemC}) {
    item->setData(0, Qt::UserRole, false); // canonical rows
  }
  QHash<const QTreeWidgetItem *, int> indexMap{{itemA, 0}, {itemB, 1}, {itemC, 2}};
  const auto indexOf = [&indexMap](const QTreeWidgetItem *item) {
    return indexMap.value(item, -1);
  };

  SettingsTreeHelpers::resyncParentIndicesFromTree(&tree, indexOf, live, working);

  QCOMPARE(live[0].parentCollectionIndex, -1);
  QVERIFY(!live[0].isSubcollection);
  QCOMPARE(live[1].parentCollectionIndex, 0);
  QVERIFY(live[1].isSubcollection);
  QCOMPARE(live[2].parentCollectionIndex, 1);
  QVERIFY(live[2].isSubcollection);
  for (int i = 0; i < live.size(); ++i) {
    QCOMPARE(working[i].parentCollectionIndex, live[i].parentCollectionIndex);
    QCOMPARE(working[i].isSubcollection, live[i].isSubcollection);
  }
}

void TestSettingsDialogTreeHelpers::resyncParentIndices_skipsLinkedMirrorsButVisitsChildren() {
  // Root(0) holds a linked-appearance mirror of collection 1; the mirror in
  // turn holds collection 2's canonical row (mirrors shuffle along with
  // their parent on drop). The mirror must NOT reparent collection 1, but
  // collection 2 under it resolves against the mirror's OUTER parent (0).
  QList<CollectionConfig> live{makeCol(QStringLiteral("Root")),
                               makeCol(QStringLiteral("Mirrored"), -1),
                               makeCol(QStringLiteral("Nested"), -1)};
  live[1].additionalParentNames = QStringList{QStringLiteral("Root")};
  QList<CollectionConfig> working = live;

  QTreeWidget tree;
  auto *root = new QTreeWidgetItem(&tree);
  root->setData(0, Qt::UserRole, false);
  auto *mirror = new QTreeWidgetItem(root);
  mirror->setData(0, Qt::UserRole, true); // linked-appearance mirror
  auto *nested = new QTreeWidgetItem(mirror);
  nested->setData(0, Qt::UserRole, false);
  QHash<const QTreeWidgetItem *, int> indexMap{{root, 0}, {mirror, 1}, {nested, 2}};
  const auto indexOf = [&indexMap](const QTreeWidgetItem *item) {
    return indexMap.value(item, -1);
  };

  SettingsTreeHelpers::resyncParentIndicesFromTree(&tree, indexOf, live, working);

  // The mirror's collection keeps its canonical (root) parent.
  QCOMPARE(live[1].parentCollectionIndex, -1);
  QVERIFY(!live[1].isSubcollection);
  // The canonical row under the mirror reparents to the mirror's own parent.
  QCOMPARE(live[2].parentCollectionIndex, 0);
  QVERIFY(live[2].isSubcollection);
}

QTEST_MAIN(TestSettingsDialogTreeHelpers)
#include "test_settingsdialogtreehelpers.moc"
