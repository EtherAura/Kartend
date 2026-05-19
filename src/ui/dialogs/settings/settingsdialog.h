#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "applysettingsdialog.h"
#include "collectionremover.h"
#include "collectionutils.h"
#include "isettingsdialog.h"
#include "settingsmodel.h"
#include <memory>
#include <QDialog>
#include <QHash>
#include <QList>
#include <QMessageBox>
#include <QMouseEvent>
#include <QTreeWidgetItem>

QT_BEGIN_NAMESPACE
namespace Ui {
class SettingsDialog;
}
class QShowEvent;
class QHideEvent;
class QResizeEvent;
class QGraphicsDropShadowEffect;
class QPropertyAnimation;
class QPushButton;
QT_END_NAMESPACE

class CollectionTreeWidget;
class ConfigProfileController;
class DetailsPaneManager;
class GamepadCaptureController;
class ScrollManager;
class NavigationManager;
class SidebarPanel;
class TreeManager;

class SettingsDialog : public QDialog, public CollectionRemoverHost, public ISettingsDialog {
  Q_OBJECT

public:
  /// scope selector for Save actions in the settings dialog.
  /// `Current` saves only the selected collection (default, legacy behavior).
  /// `CurrentAndSubcollections` propagates appearance/layout fields from the
  /// edited collection to its descendants on Save. `All` propagates them to
  /// every other collection. The propagation set mirrors the curated subset
  /// used by 's explicit "Apply to..." action — identity, paths,
  /// extensions, and scan-affecting flags are never copied.
  enum class SettingsScope { Current, CurrentAndSubcollections, All };
  Q_ENUM(SettingsScope)

  explicit SettingsDialog(QWidget *parent, const QList<CollectionConfig> &initialCollections,
                          int initialIndex = -1);
  ~SettingsDialog() override;

  // ISettingsDialog — neutral role interface for the data-layer controller.
  [[nodiscard]] const QList<CollectionConfig> &getCollections() const override {
    return collections;
  }

  /// Run the dialog modally. Thin forwarder so the single derived override
  /// satisfies both QDialog::exec() and the ISettingsDialog::exec() contract
  /// (otherwise the interface's pure virtual would leave SettingsDialog
  /// abstract).
  int exec() override { return QDialog::exec(); }

  /// Returns the index of the collection currently selected in the tree.
  [[nodiscard]] int getSelectedCollectionIndex() const { return currentCollectionIndex; }

  /// returns the active Settings Mode scope.
  [[nodiscard]] SettingsScope currentSettingsScope() const { return m_settingsScope; }

  /// Returns true when any tracked field on the currently-edited collection
  /// or general-settings tab differs from the original snapshot. Public so
  /// tests can drive widget mutations and observe the dirty state directly;
  /// production code observes this via the Save button enabled-state binding.
  [[nodiscard]] bool hasUnsavedChanges() const;

  /// low-level mask-aware copy. Public so tests can verify the
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

  /// True while any SettingsDialog instance is currently visible.
  /// Tracked by show/hide events so the main window can suppress its
  /// focus-return splash when the user dismisses the settings dialog
  /// (or alt-tabs back to it).
  [[nodiscard]] static bool isAnyInstanceVisible();

signals:
  void collectionSaved(const QList<CollectionConfig> &collections);
  void gridWidthChanged(int collectionIndex, int newGridWidth);
  void spacingChanged(int collectionIndex, int horizontalSpacing, int verticalSpacing);
  /// Emitted when changes requiring a database rescan have been saved
  void rescanRequired(int collectionIndex);
  /// emitted when the user changes the Settings Mode scope so
  /// dependent UI (e.g. field gating) can react.
  void settingsScopeChanged(SettingsScope scope);

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private slots:
  void onTreeItemSelectionChanged();
  void onTreeItemChanged(QTreeWidgetItem *item, int column);
  void addCollection();
  void removeCollection();
  /// full-copy duplicate of the current collection. Prompts for
  /// a unique name and a parent collection (sibling/child/root all reachable
  /// via the parent combo). Every CollectionConfig field is copied — paths,
  /// extensions, launcher list, appearance — except runtime-only state
  /// (currentSubfolder) and the playlist markers (which mark virtual
  /// collections that aren't directly duplicable).
  void duplicateCollection();
  /// pull-from-source. Opens ApplySettingsDialog in Pull mode
  /// so the user picks a source collection AND the field categories to
  /// copy onto the currently-edited collection. Marks the dialog dirty so
  /// the user can review in the form before committing with Save.
  void copySettingsFromOtherCollection();
  void browseLauncher();
  void browseCore();
  /// Add/Edit/Remove handlers for the Additional Launchers list.
  void onAddAdditionalLauncher();
  void onEditAdditionalLauncher();
  void onRemoveAdditionalLauncher();
  /// Reflect the active row in the default-launcher combo so the
  /// edit/remove buttons enable/disable in lockstep with the selection.
  void onAdditionalLauncherSelectionChanged();
  void browseMediaDir();
  void checkForChanges();
  void onContentDirectoryChanged();
  void onGridWidthChanged(int value);
  void onRecursiveImportContent();
  /// react to user changes in the Settings Mode combo box.
  void onSettingsScopeChanged(int comboIndex);
  /// builds the tree's right-click context menu (Rename /
  /// Duplicate / Delete / expand-collapse helpers) at the requested point.
  void onTreeContextMenuRequested(const QPoint &pos);
  /// drag-drop reparenting completed. Walk the tree post-drop
  /// and resync parentCollectionIndex / isSubcollection on every collection.
  void onTreeRearranged();
  /// open the multi-select picker pre-checked with the
  /// current collection's additionalParentNames so the user can manage
  /// alias parents. Excludes self + descendants from the picker (cycles
  /// can't be expressed via the UI).
  void onEditLinkedParents();
  /// Nav-rail row changed — swap the visible page in the QStackedWidget and
  /// refresh the context header.
  void onNavigationRowChanged();
  /// Settings-search text changed — filter the category list to rows whose
  /// label or indexed field text matches the query.
  void onSettingsSearchTextChanged(const QString &text);

private:
  void updateCollectionTreeWidget() override;
  void expandPathToCollection(int index) override;
  /// CollectionRemoverHost override — thunks through to m_treeManager so the
  /// remover sees one consistent name-propagation path.
  void propagateCollectionNameChange(const QString &oldName, const QString &newName) override;
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
  /// Builds the left-rail category list, wires it to the page stack, styles
  /// the context header, and indexes settings for search. Implementation in
  /// settingsdialognav.cpp.
  void setupNavigation();
  /// Refreshes the context header (icon + title + collection subtitle) for
  /// the currently selected nav row.
  void updateContextHeader();
  /// Indexes every page's visible label / button / group-box text so the
  /// search box can match settings by name. Stored on each category row.
  void buildSettingsSearchIndex();
  /// Restore / persist the dialog geometry and rail splitter position so the
  /// layout survives across runs.
  void restoreDialogState();
  void saveDialogState();
  void loadCollectionToUI(int index) override;
  void clearCollectionUI() override;
  void saveCollectionFromUI(int index);
  void updateSaveButtonStyle() override;
  void updateDeleteButtonState() override;
  void updateUIForLauncherType(const QString &launcherPath);
  /// Populates and selects the parent collection combo box for the active
  /// collection.
  void updateParentCollectionComboBox(int currentIndex);
  [[nodiscard]] bool wouldCreateCircularReference(int childIndex, int potentialParentIndex) const;
  void emitGridWidthChanged();
  void updateFieldVisibility();
  void updateExtractArchivesVisibility();
  void onExtractArchivesToggled(bool checked);
  void updateGridWidthLimits();
  void loadGeneralSettingsToUI();
  void saveGeneralSettingsFromUI();
  void performRecursiveImport(const QString &baseDir, bool isContentDir);
  void ensureRootCollectionExists() override;
  // The 7-step removal pipeline lives on CollectionRemover now.
  // Helper methods for saveCollectionFromUI refactoring
  auto extractUIFieldValues() -> CollectionConfig;
  auto updateParentCollectionFromUI(CollectionConfig &collection, int index) -> void;
  // load/save/refresh helpers for the multi-launcher controls.
  void loadAdditionalLaunchersToUI(const CollectionConfig &config);
  void clearAdditionalLaunchersUI();
  // helpers for the alias-parent ("Linked Parents") control.
  void loadLinkedParentsToUI(const CollectionConfig &config);
  void clearLinkedParentsUI();
  void updateLinkedParentsButtonLabel();
  [[nodiscard]] auto checkLinkedParentsChanges() const -> bool;
  void rebuildDefaultLauncherCombo(int preferredIndex);
  void updateAdditionalLauncherButtonsState();
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

  /// silent variant used by the Settings Mode auto-propagation
  /// path. Skips the per-category dialog and the post-apply summary because
  /// the user already opted in by selecting a non-`Current` mode — and the
  /// mode itself is a coarse-grained switch, so the full curated subset is
  /// always copied. Returns the number of collections actually mutated.
  int propagateAppearanceToIndicesSilently(const QList<int> &targetIndices);

  /// enable/disable form fields that don't participate in
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
  /// Owns the tree-population state (item↔collection index maps,
  /// linked-appearance mirrors) and the pure tree-domain operations
  /// (rebuild, expand-path, name propagation, subtree expand). Constructed
  /// after collectionTreeWidget is wired so it can take a non-owning
  /// pointer to it. Slots that wire user gestures still live on the dialog
  /// — they read this manager for tree state.
  std::unique_ptr<TreeManager> m_treeManager;
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
  /// working copy of the active collection's additional
  /// launchers — kept in sync with the QListWidget so dirty-checking and the
  /// default-launcher combo can read structured data instead of strings.
  QList<LauncherConfig> m_workingAdditionalLaunchers;
  /// working copy of the active collection's alias-parent
  /// names. Edited via the Linked Parents picker; flushed back to the
  /// collection on Save and reset on collection switch.
  QStringList m_workingAdditionalParentNames;
  GeneralSettings m_generalSettings;
  GeneralSettings m_originalGeneralSettings;
  /// Tracks collection indices that need a rescan due to database-affecting
  /// changes
  QSet<int> m_rescanRequired;

  /// Gamepad button-capture state machine — owns the active capture
  /// target, the GamepadManager binding-capture connection, and the
  /// per-tick UI sync (line edits, detect buttons, sibling checkbox
  /// lock state). Parented to this dialog.
  GamepadCaptureController *m_gamepadCapture = nullptr;
  /// Multi-step collection-removal pipeline — owns the seven discrete
  /// steps (validate / capture-expanded / perform-removal /
  /// update-parents / rebuild-indices / restore-expanded /
  /// select-target) and the orchestrator that strings them together
  /// with the user-confirm prompt. Parented to this dialog.
  CollectionRemover *m_collectionRemover = nullptr;

  /// Configuration backup/restore controller — owns the Export / Load
  /// config-profile controls and their .cfg export/import logic.
  /// Parented to this dialog. setupGeneralSettingsConnections() hands it
  /// the three borrowed widgets.
  ConfigProfileController *m_configProfileController = nullptr;

  /// active scope for Save actions. Defaults to `Current` so
  /// legacy behavior is preserved until the user explicitly opts into a
  /// broader scope.
  SettingsScope m_settingsScope = SettingsScope::Current;

  /// Persistent save icon — installed as the top-right corner widget of
  /// the dialog's main QTabWidget so it stays put regardless of which
  /// page is open. Saves the current collection (when one is selected)
  /// plus general settings; doesn't close the dialog.
  QPushButton *m_saveButton = nullptr;
  // Unsaved-changes indicator: a pulsing drop-shadow glow
  // applied to the save button while hasUnsavedChanges() is true. Owned by
  // the button once attached (Qt manages lifetime via QWidget::setGraphicsEffect).
  QGraphicsDropShadowEffect *m_saveButtonGlow = nullptr;
  QPropertyAnimation *m_saveButtonGlowAnim = nullptr;

  bool eventFilter(QObject *obj, QEvent *event) override;

  /// Non-owning aggregate of the dialog's editable data fields. Initialized
  /// in the constructor body once the underlying members exist; handed to
  /// CollectionRemover so the removal pipeline can mutate the data without
  /// reaching back into private dialog internals.
  SettingsModel m_model;

  // ── CollectionRemoverHost implementation ─────────────────────────────
  // Thin adapters that forward to the dialog's tree-state fields and
  // private UI helpers. Marked private — callers reach them via the
  // CollectionRemoverHost* slot only.
  [[nodiscard]] int selectedCollectionIndex() const override;
  [[nodiscard]] bool hasSelection() const override;
  void selectCollection(int index) override;
  void clearSelection() override;
  [[nodiscard]] QList<int> expandedCollectionIndices() const override;
  void expandCollectionAtIndex(int index) override;
  [[nodiscard]] QWidget *dialogWidget() override { return this; }
  void emitCollectionSaved(const QList<CollectionConfig> &collections) override {
    emit collectionSaved(collections);
  }
  // The remaining host overrides (expandPathToCollection, loadCollectionToUI,
  // propagateCollectionNameChange, updateCollectionTreeWidget,
  // clearCollectionUI, ensureRootCollectionExists, updateSaveButtonStyle,
  // updateDeleteButtonState) are the dialog's own private members declared
  // above; the `override` keyword is added on each declaration to make the
  // host wiring explicit.
};

#endif
