#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "applysettingsdialog.h"
#include "collectionutils.h"
#include <QDialog>
#include <QHash>
#include <QList>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QTreeWidgetItem>

QT_BEGIN_NAMESPACE
namespace Ui {
class SettingsDialog;
}
class QShowEvent;
class QResizeEvent;
class QGraphicsDropShadowEffect;
class QPropertyAnimation;
QT_END_NAMESPACE

class CollectionTreeWidget;
class DetailsPaneManager;
class ScrollManager;
class NavigationManager;

class SettingsDialog : public QDialog {
  Q_OBJECT

public:
  /// Kartend-enq: scope selector for Save actions in the settings dialog.
  /// `Current` saves only the selected collection (default, legacy behavior).
  /// `CurrentAndSubcollections` propagates appearance/layout fields from the
  /// edited collection to its descendants on Save. `All` propagates them to
  /// every other collection. The propagation set mirrors the curated subset
  /// used by Kartend-63o's explicit "Apply to..." action — identity, paths,
  /// extensions, and scan-affecting flags are never copied.
  enum class SettingsScope { Current, CurrentAndSubcollections, All };
  Q_ENUM(SettingsScope)

  explicit SettingsDialog(QWidget *parent, const QList<CollectionConfig> &initialCollections,
                          int initialIndex = -1);
  ~SettingsDialog() override;

  [[nodiscard]] const QList<CollectionConfig> &getCollections() const { return collections; }

  /// Returns the index of the collection currently selected in the tree.
  [[nodiscard]] int getSelectedCollectionIndex() const { return currentCollectionIndex; }

  /// Kartend-enq: returns the active Settings Mode scope.
  [[nodiscard]] SettingsScope currentSettingsScope() const { return m_settingsScope; }

  /// Kartend-iyk: low-level mask-aware copy. Public so tests can verify the
  /// per-category subset behaviour directly. Returns the number of target
  /// collections actually mutated (excludes invalid indices and the source
  /// itself). Production callers go through the higher-level apply/copy
  /// slots which open ApplySettingsDialog first.
  int applyCategoriesToIndices(const QList<int> &targetIndices,
                               ApplySettingsDialog::FieldCategories categories, int sourceIndex);

  /// Handles dialog acceptance while guarding against unsaved changes.
  void accept() override;

  /// Handles dialog rejection while guarding against unsaved changes.
  void reject() override;

signals:
  void collectionSaved(const QList<CollectionConfig> &collections);
  void gridWidthChanged(int collectionIndex, int newGridWidth);
  void spacingChanged(int collectionIndex, int horizontalSpacing, int verticalSpacing);
  /// Emitted when changes requiring a database rescan have been saved
  void rescanRequired(int collectionIndex);
  /// Kartend-enq: emitted when the user changes the Settings Mode scope so
  /// dependent UI (e.g. Kartend-c06 field gating) can react.
  void settingsScopeChanged(SettingsScope scope);

protected:
  void showEvent(QShowEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private slots:
  void onTreeItemSelectionChanged();
  void onTreeItemChanged(QTreeWidgetItem *item, int column);
  void addCollection();
  void removeCollection();
  /// Kartend-f5i9: full-copy duplicate of the current collection. Prompts for
  /// a unique name and a parent collection (sibling/child/root all reachable
  /// via the parent combo). Every CollectionConfig field is copied — paths,
  /// extensions, launcher list, appearance — except runtime-only state
  /// (currentSubfolder) and the playlist markers (which mark virtual
  /// collections that aren't directly duplicable).
  void duplicateCollection();
  /// Kartend-iyk: pull-from-source. Opens ApplySettingsDialog in Pull mode
  /// so the user picks a source collection AND the field categories to
  /// copy onto the currently-edited collection. Marks the dialog dirty so
  /// the user can review in the form before committing with Save.
  void copySettingsFromOtherCollection();
  void browseLauncher();
  void browseCore();
  /// Kartend-bdl: Add/Edit/Remove handlers for the Additional Launchers list.
  void onAddAdditionalLauncher();
  void onEditAdditionalLauncher();
  void onRemoveAdditionalLauncher();
  /// Kartend-bdl: Reflect the active row in the default-launcher combo so the
  /// edit/remove buttons enable/disable in lockstep with the selection.
  void onAdditionalLauncherSelectionChanged();
  /// Kartend-p1jd: Add/Edit/Remove handlers for the global Launcher Presets
  /// list on the new "Launchers" tab.
  void onAddLauncherPreset();
  void onEditLauncherPreset();
  void onRemoveLauncherPreset();
  void onLauncherPresetSelectionChanged();
  void browseMediaDir();
  void browseArtworkDir();
  void browseVideoDir();
  void browseManualDir();
  void browsePlaceholderArtwork();
  void browseStartupVideo();
  void checkForChanges();
  void onContentDirectoryChanged();
  void onGridWidthChanged(int value);
  void onRecursiveImportContent();
  void onRecursiveImportArtwork();
  void onIncludeSubfoldersToggled(bool checked);
  /// Kartend-enq: react to user changes in the Settings Mode combo box.
  void onSettingsScopeChanged(int comboIndex);
  /// Kartend-j613: builds the tree's right-click context menu (Rename /
  /// Duplicate / Delete / expand-collapse helpers) at the requested point.
  void onTreeContextMenuRequested(const QPoint &pos);
  /// Kartend-j613: drag-drop reparenting completed. Walk the tree post-drop
  /// and resync parentCollectionIndex / isSubcollection on every collection.
  void onTreeRearranged();
  /// Kartend-gzmk: open the multi-select picker pre-checked with the
  /// current collection's additionalParentNames so the user can manage
  /// alias parents. Excludes self + descendants from the picker (cycles
  /// can't be expressed via the UI).
  void onEditLinkedParents();

private:
  enum class GamepadCaptureTarget { None, Confirm, Back, ToggleSidebar };

  void startGamepadButtonCapture(GamepadCaptureTarget target);
  void stopGamepadButtonCapture();
  void onGamepadCaptureButtonPressed(const QString &buttonName);
  void updateGamepadCaptureUi();

  void updateCollectionTreeWidget();
  void expandPathToCollection(int collectionIndex);
  void populateTreeWidget();
  QTreeWidgetItem *createTreeItem(int collectionIndex, QTreeWidgetItem *parent = nullptr);
  /// Kartend-gzmk: tree-population pass that walks every collection's
  /// additionalParentNames and creates italicised mirror items under each
  /// resolved parent. Called after the primary tree is fully populated so
  /// the parent rows it attaches to are guaranteed to exist.
  void populateLinkedAppearances();
  /// Kartend-gzmk: keep stale link references from breaking the tree when a
  /// collection is renamed or removed. Walks every other collection and
  /// rewrites/removes name entries in additionalParentNames to match.
  /// @param oldName  current name of the affected collection
  /// @param newName  new name after rename — pass empty to remove the entry
  void propagateCollectionNameChange(const QString &oldName, const QString &newName);
  void setupConnections();
  void setupButtonConnections();
  void setupBasicUIConnections();
  /// Saves the specified collection and optionally refreshes the tree widget.
  void handleSaveCollection(int editedIndex, bool refreshTree = true);
  void setupFormFieldConnections();
  void setupSpacingConnections();
  void handleSpacingChanged();
  void setupTreeWidgetConnections();
  void setupUIConstraints();
  [[nodiscard]] static auto spacingInternalToUi(int spacing) -> int;
  [[nodiscard]] static auto spacingUiToInternal(int spacing) -> int;
  void setupGeneralSettingsConnections();
  void loadCollectionToUI(int index);
  void clearCollectionUI();
  void saveCollectionFromUI(int index);
  [[nodiscard]] bool hasUnsavedChanges() const;
  void updateSaveButtonStyle();
  void updateDeleteButtonState();
  void updateUIForLauncherType(const QString &launcherPath);
  /// Populates and selects the parent collection combo box for the active
  /// collection.
  void updateParentCollectionComboBox(int currentIndex);
  [[nodiscard]] bool wouldCreateCircularReference(int childIndex, int potentialParentIndex) const;
  /// Kartend-j613: recursively expand or collapse @p item and every
  /// descendant under it (used by the context menu's "Expand subtree"
  /// / "Collapse subtree" entries).
  void setSubtreeExpanded(QTreeWidgetItem *item, bool expanded);
  void emitGridWidthChanged();
  void updateFieldVisibility();
  void updateExtractArchivesVisibility();
  void onExtractArchivesToggled(bool checked);
  void updateSidebarModeVisibility();
  void updateGridWidthLimits();
  void loadGeneralSettingsToUI();
  void saveGeneralSettingsFromUI();
  void performRecursiveImport(const QString &baseDir, bool isContentDir);
  void ensureRootCollectionExists();
  // Helper methods for removeCollection refactoring
  auto validateRemovalPreconditions() -> bool;
  auto captureExpandedStates() -> QList<int>;
  auto performCollectionRemoval(int index) -> void;
  auto updateParentReferences(int removedIndex) -> void;
  auto rebuildParentIndices() -> void;
  auto restoreExpandedStates(const QList<int> &expandedBefore, int removedIndex) -> void;
  auto selectTargetAfterRemoval(int parentIdx, int removedIndex) -> void;
  // Helper methods for saveCollectionFromUI refactoring
  auto extractUIFieldValues() -> CollectionConfig;
  auto updateParentCollectionFromUI(CollectionConfig &collection, int index) -> void;
  // Kartend-bdl: load/save/refresh helpers for the multi-launcher controls.
  void loadAdditionalLaunchersToUI(const CollectionConfig &config);
  void clearAdditionalLaunchersUI();
  // Kartend-gzmk: helpers for the alias-parent ("Linked Parents") control.
  void loadLinkedParentsToUI(const CollectionConfig &config);
  void clearLinkedParentsUI();
  void updateLinkedParentsButtonLabel();
  [[nodiscard]] auto checkLinkedParentsChanges() const -> bool;
  void rebuildDefaultLauncherCombo(int preferredIndex);
  void updateAdditionalLauncherButtonsState();
  // Kartend-p1jd: launcher-presets tab helpers. Preset edits live in
  // m_generalSettings.launcherPresets (already loaded at dialog open time)
  // and are flushed via the standard saveGeneralSettings() path.
  void loadLauncherPresetsToUI();
  void refreshLauncherPresetsList();
  void updateLauncherPresetButtonsState();
  // Helper methods for hasUnsavedChanges refactoring
  auto checkBasicFieldChanges() const -> bool;
  auto checkExtensionChanges() const -> bool;
  auto checkTreeNameChanges() const -> bool;
  auto checkParentCollectionChanges() const -> bool;
  auto checkDimensionChanges() const -> bool;
  auto checkColorChanges() const -> bool;
  auto checkListModeChanges() const -> bool;
  auto checkBackgroundChanges() const -> bool;
  auto checkGeneralSettingsChanges() const -> bool;
  /// Prompts the user to resolve unsaved changes for the specified action.
  auto promptUnsavedChanges(const QString &actionDescription) -> QMessageBox::StandardButton;
  /// Restores the current collection to its last saved state.
  void revertCurrentCollectionEdits();
  /// Resolves unsaved changes prior to executing an action.
  auto resolveUnsavedChanges(const QString &actionDescription, bool refreshTreeAfterSave) -> bool;

  /// Kartend-enq: silent variant used by the Settings Mode auto-propagation
  /// path. Skips the per-category dialog and the post-apply summary because
  /// the user already opted in by selecting a non-`Current` mode — and the
  /// mode itself is a coarse-grained switch, so the full curated subset is
  /// always copied. Returns the number of collections actually mutated.
  int propagateAppearanceToIndicesSilently(const QList<int> &targetIndices);

  /// Kartend-c06: enable/disable form fields that don't participate in
  /// scope propagation. When the active Settings Mode is anything other
  /// than `Current`, the curated appearance/layout subset is the only data
  /// that gets copied to the wider scope on Save — so we gray out the
  /// non-propagatable controls (paths, extensions, launcher, scan/extract
  /// flags, parent linkage) to make it obvious that editing them only
  /// affects the currently-selected collection. Controls remain visible so
  /// the user can still inspect their values.
  void applyScopeFieldGating();

  Ui::SettingsDialog *ui;
  CollectionTreeWidget *collectionTreeWidget;
  QTreeWidgetItem *currentTreeItem;
  QHash<QTreeWidgetItem *, int> itemToCollectionIndex;
  /// Primary appearance per collection — the one that owns the row's name
  /// edit, drag handle, and parent linkage. Most settings-dialog code paths
  /// (expandPathToCollection, onTreeRearranged, removeCollection, ...) act
  /// on the primary item only. See collectionIndexToLinkedItems for alias
  /// (Kartend-gzmk) appearances that hang off the same QTreeWidget.
  QHash<int, QTreeWidgetItem *> collectionIndexToItem;
  /// Kartend-gzmk: per-collection list of linked-appearance QTreeWidgetItems.
  /// Empty for collections with no additionalParentNames. Linked items are
  /// italicised, non-editable, non-draggable, and exist purely to mirror
  /// the collection under additional parents in the visible tree.
  QHash<int, QList<QTreeWidgetItem *>> collectionIndexToLinkedItems;
  QList<CollectionConfig> collections;
  int originalCurrentCollectionIndex;
  CollectionConfig originalCollection;
  int currentCollectionIndex;
  QList<CollectionConfig> m_workingCollections;
  bool m_gridWidthChangedForActiveCollection;
  int m_newGridWidthForActiveCollection;
  bool m_collectionSaved;
  QList<int> m_parentCollectionMapping;
  bool m_isLoading;
  /// Kartend-bdl: working copy of the active collection's additional
  /// launchers — kept in sync with the QListWidget so dirty-checking and the
  /// default-launcher combo can read structured data instead of strings.
  QList<LauncherConfig> m_workingAdditionalLaunchers;
  /// Kartend-gzmk: working copy of the active collection's alias-parent
  /// names. Edited via the Linked Parents picker; flushed back to the
  /// collection on Save and reset on collection switch.
  QStringList m_workingAdditionalParentNames;
  GeneralSettings m_generalSettings;
  GeneralSettings m_originalGeneralSettings;
  /// Tracks collection indices that need a rescan due to database-affecting
  /// changes
  QSet<int> m_rescanRequired;

  GamepadCaptureTarget m_gamepadCaptureTarget = GamepadCaptureTarget::None;
  QMetaObject::Connection m_gamepadCaptureConnection;

  /// Kartend-enq: active scope for Save actions. Defaults to `Current` so
  /// legacy behavior is preserved until the user explicitly opts into a
  /// broader scope.
  SettingsScope m_settingsScope = SettingsScope::Current;

  // Unsaved-changes indicator (Kartend-9f6): a pulsing drop-shadow glow
  // applied to the save button while hasUnsavedChanges() is true. Owned by
  // the button once attached (Qt manages lifetime via QWidget::setGraphicsEffect).
  QGraphicsDropShadowEffect *m_saveButtonGlow = nullptr;
  QPropertyAnimation *m_saveButtonGlowAnim = nullptr;

  bool eventFilter(QObject *obj, QEvent *event) override;
};

#endif
