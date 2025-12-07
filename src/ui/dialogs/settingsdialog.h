#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "collectionutils.h"
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
class QTreeWidget;
class QShowEvent;
class QResizeEvent;
QT_END_NAMESPACE

class SidebarManager;
class ScrollManager;
class NavigationManager;

class SettingsDialog : public QDialog {
  Q_OBJECT

public:
  explicit SettingsDialog(QWidget *parent,
                          const QList<CollectionConfig> &initialCollections,
                          int initialIndex = -1);
  ~SettingsDialog();

  QList<CollectionConfig> getCollections() const { return collections; }

  /// Handles dialog acceptance while guarding against unsaved changes.
  void accept() override;

  /// Handles dialog rejection while guarding against unsaved changes.
  void reject() override;

signals:
  void collectionSaved(const QList<CollectionConfig> &collections);
  void gridWidthChanged(int collectionIndex, int newGridWidth);
  void spacingChanged(int collectionIndex, int horizontalSpacing,
                      int verticalSpacing);

protected:
  void showEvent(QShowEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private slots:
  void onTreeItemSelectionChanged();
  void onTreeItemChanged(QTreeWidgetItem *item, int column);
  void addCollection();
  void removeCollection();
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
  void updateCollectionTreeWidget();
  void expandPathToCollection(int collectionIndex);
  void populateTreeWidget();
  QTreeWidgetItem *createTreeItem(int collectionIndex,
                                  QTreeWidgetItem *parent = nullptr);
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
  void setupGeneralSettingsConnections();
  void loadCollectionToUI(int index);
  void saveCollectionFromUI(int index);
  [[nodiscard]] bool hasUnsavedChanges() const;
  void updateSaveButtonStyle();
  void updateUIForLauncherType(const QString &launcherPath);
  /// Populates and selects the parent collection combo box for the active collection.
  void updateParentCollectionComboBox(int currentIndex);
  [[nodiscard]] bool wouldCreateCircularReference(int childIndex,
                                    int potentialParentIndex) const;
  void emitGridWidthChanged();
  void updateFieldVisibility();
  void updateSidebarModeVisibility();
  void updateGridWidthLimits();
  void loadGeneralSettingsToUI();
  void saveGeneralSettingsFromUI();
  void performRecursiveImport(const QString &baseDir, bool isContentDir);
  // Helper methods for removeCollection refactoring
  auto validateRemovalPreconditions() -> bool;
  auto captureExpandedStates() -> QList<int>;
  auto performCollectionRemoval(int index) -> void;
  auto updateParentReferences(int removedIndex) -> void;
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
  /// Prompts the user to resolve unsaved changes for the specified action.
  auto promptUnsavedChanges(const QString &actionDescription)
      -> QMessageBox::StandardButton;
  /// Restores the current collection to its last saved state.
  void revertCurrentCollectionEdits();
  /// Resolves unsaved changes prior to executing an action.
  auto resolveUnsavedChanges(const QString &actionDescription,
                             bool refreshTreeAfterSave) -> bool;

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
  bool eventFilter(QObject *obj, QEvent *event) override;
};

#endif