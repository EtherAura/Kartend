#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

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
class QTreeWidget;
class QShowEvent;
class QResizeEvent;
class QGraphicsDropShadowEffect;
class QPropertyAnimation;
QT_END_NAMESPACE

class SidebarManager;
class ScrollManager;
class NavigationManager;

class SettingsDialog : public QDialog {
  Q_OBJECT

public:
  explicit SettingsDialog(QWidget *parent, const QList<CollectionConfig> &initialCollections,
                          int initialIndex = -1);
  ~SettingsDialog() override;

  [[nodiscard]] const QList<CollectionConfig> &getCollections() const { return collections; }

  /// Returns the index of the collection currently selected in the tree.
  [[nodiscard]] int getSelectedCollectionIndex() const { return currentCollectionIndex; }

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

protected:
  void showEvent(QShowEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private slots:
  void onTreeItemSelectionChanged();
  void onTreeItemChanged(QTreeWidgetItem *item, int column);
  void addCollection();
  void removeCollection();
  /// Kartend-63o: propagate the currently-edited collection's appearance and
  /// layout settings to every other collection in the list. Prompts for
  /// confirmation first.
  void applyCurrentSettingsToAllCollections();
  /// Kartend-63o: propagate the currently-edited collection's appearance and
  /// layout settings to its (recursive) descendants only.
  void applyCurrentSettingsToSubcollections();
  void browseLauncher();
  void browseCore();
  void browseMediaDir();
  void browseArtworkDir();
  void checkForChanges();
  void onContentDirectoryChanged();
  void onGridWidthChanged(int value);
  void onRecursiveImportContent();
  void onRecursiveImportArtwork();
  void onIncludeSubfoldersToggled(bool checked);

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

  /// Kartend-63o: propagate the current collection's appearance & layout
  /// settings (grid/spacing/colors/fonts/view type, etc. — never
  /// paths/extensions/behavior flags that would require a rescan) to every
  /// index in @p targetIndices. Shows a confirmation QMessageBox first,
  /// persists the modified collections, and refreshes the tree so the user
  /// sees the change immediately.
  void applyCurrentSettingsToIndices(const QList<int> &targetIndices, const QString &scopeLabel);

  Ui::SettingsDialog *ui;
  QTreeWidget *collectionTreeWidget;
  QTreeWidgetItem *currentTreeItem;
  QHash<QTreeWidgetItem *, int> itemToCollectionIndex;
  QHash<int, QTreeWidgetItem *> collectionIndexToItem;
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
  GeneralSettings m_generalSettings;
  GeneralSettings m_originalGeneralSettings;
  /// Tracks collection indices that need a rescan due to database-affecting
  /// changes
  QSet<int> m_rescanRequired;

  GamepadCaptureTarget m_gamepadCaptureTarget = GamepadCaptureTarget::None;
  QMetaObject::Connection m_gamepadCaptureConnection;

  // Unsaved-changes indicator (Kartend-9f6): a pulsing drop-shadow glow
  // applied to the save button while hasUnsavedChanges() is true. Owned by
  // the button once attached (Qt manages lifetime via QWidget::setGraphicsEffect).
  QGraphicsDropShadowEffect *m_saveButtonGlow = nullptr;
  QPropertyAnimation *m_saveButtonGlowAnim = nullptr;

  bool eventFilter(QObject *obj, QEvent *event) override;
};

#endif
