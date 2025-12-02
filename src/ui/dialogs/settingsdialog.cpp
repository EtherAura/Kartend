// Collection configuration dialog with tree-based hierarchy editing and live preview.
#include <QAbstractItemView>
#include <QDir>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmapCache>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSet>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <set>

#include "extensionutils.h"
#include "mainwindow.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "ui_settingsdialog.h"
#include "uiconstants.h"

SettingsDialog::SettingsDialog(
    QWidget *parent, const QList<CollectionConfig> &initialCollections,
    int initialIndex)
    : QDialog(parent), ui(new Ui::SettingsDialog),
      collectionTreeWidget(nullptr), currentTreeItem(nullptr),
      collections(initialCollections),
      originalCurrentCollectionIndex(initialIndex),
      currentCollectionIndex(initialIndex),
      m_workingCollections(initialCollections),
      m_gridWidthChangedForActiveCollection(false),
      m_newGridWidthForActiveCollection(0), m_collectionSaved(true),
      m_isLoading(false) {
  ui->setupUi(this);
  setWindowTitle(tr("Settings"));
  setModal(true);

  collectionTreeWidget = ui->collectionTreeWidget;

  installEventFilter(this);
  if (collectionTreeWidget) {
    collectionTreeWidget->installEventFilter(this);
    collectionTreeWidget->setFocusPolicy(Qt::WheelFocus);
  }

  loadGeneralSettingsToUI();
  setupConnections();
  updateCollectionTreeWidget();

  if (currentCollectionIndex >= 0 &&
      currentCollectionIndex < m_workingCollections.size()) {
    expandPathToCollection(currentCollectionIndex);
  }

  if (currentCollectionIndex < 0 ||
      currentCollectionIndex >= m_workingCollections.size()) {
    currentCollectionIndex = m_workingCollections.isEmpty() ? -1 : 0;
  }
  if (currentCollectionIndex >= 0 &&
      currentCollectionIndex < m_workingCollections.size()) {
    loadCollectionToUI(currentCollectionIndex);
    originalCollection = m_workingCollections[currentCollectionIndex];
    if (collectionIndexToItem.contains(currentCollectionIndex)) {
      collectionTreeWidget->setCurrentItem(
          collectionIndexToItem[currentCollectionIndex]);
    }
  }

  originalCurrentCollectionIndex = currentCollectionIndex;
  loadGeneralSettingsToUI();
}

// Handles wheel routing and whitespace click to allow deselection
auto SettingsDialog::eventFilter(QObject *obj, QEvent *event) -> bool {
  if (event->type() == QEvent::Wheel) {
    auto *wheelEvent = static_cast<QWheelEvent *>(event);

    if (obj == collectionTreeWidget) {
      return QDialog::eventFilter(obj, event);
    }

    if ((collectionTreeWidget) &&
        collectionTreeWidget->underMouse()) {
      QWheelEvent forwardedEvent(
          collectionTreeWidget->mapFromGlobal(
              wheelEvent->globalPosition().toPoint()),
          wheelEvent->globalPosition().toPoint(), wheelEvent->pixelDelta(),
          wheelEvent->angleDelta(), wheelEvent->buttons(),
          wheelEvent->modifiers(), wheelEvent->phase(), wheelEvent->inverted());
      QApplication::sendEvent(collectionTreeWidget, &forwardedEvent);
      event->accept();
      return true;
    }

    event->accept();
    return true;
  }

  if ((collectionTreeWidget) &&
      (obj == collectionTreeWidget ||
       obj == collectionTreeWidget->viewport()) &&
      event->type() == QEvent::MouseButtonPress) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    auto *src = static_cast<QWidget *>(obj);
    QPoint vpPos =
        collectionTreeWidget->viewport()->mapFrom(src, mouseEvent->pos());
    QTreeWidgetItem *hit = collectionTreeWidget->itemAt(vpPos);
    if (!hit) {
      collectionTreeWidget->clearSelection();
      collectionTreeWidget->setCurrentItem(nullptr);
      currentTreeItem = nullptr;
      currentCollectionIndex = -1;
      event->accept();
      return true;
    }
  }

  return QDialog::eventFilter(obj, event);
}

SettingsDialog::~SettingsDialog() { delete ui; }

void SettingsDialog::accept() {
  if (!resolveUnsavedChanges(tr("closing the dialog"), true)) {
    return;
  }
  saveGeneralSettingsFromUI();
  QDialog::accept();
}

void SettingsDialog::reject() {
  if (!resolveUnsavedChanges(tr("closing the dialog"), true)) {
    return;
  }
  QDialog::reject();
}

void SettingsDialog::updateCollectionTreeWidget() {
  if (!collectionTreeWidget) {
    return;
  }
  collectionTreeWidget->clear();
  itemToCollectionIndex.clear();
  collectionIndexToItem.clear();
  populateTreeWidget();
  if (ui->removeCollectionButton) {
    ui->removeCollectionButton->setEnabled(collections.size() > 1);
  }
}

void SettingsDialog::expandPathToCollection(int collectionIndex) {
  if (!CollectionUtils::isValidIndex(collectionIndex, &collections)) {
    return;
  }
  if (!collectionIndexToItem.contains(collectionIndex)) {
    return;
  }

  QList<int> pathIndices;
  int currentIndex = collectionIndex;
  while (CollectionUtils::isValidIndex(currentIndex, &collections)) {
    pathIndices.prepend(currentIndex);
    const CollectionConfig &config = collections[currentIndex];
    currentIndex = config.parentCollectionIndex;
  }
  for (int i = 0; i < pathIndices.size() - 1; ++i) {
    int index = pathIndices[i];
    if (collectionIndexToItem.contains(index)) {
      QTreeWidgetItem *item = collectionIndexToItem[index];
      if (item) {
        item->setExpanded(true);
      }
    }
  }
}

void SettingsDialog::populateTreeWidget() {
  for (int i = 0; i < collections.size(); ++i) {
    if (collections[i].parentCollectionIndex == -1) {
      createTreeItem(i);
    }
  }

  bool foundSubcollection = true;
  int maxIterations = collections.size();
  int iteration = 0;

  while (foundSubcollection && iteration < maxIterations) {
    foundSubcollection = false;
    iteration++;

    for (int i = 0; i < collections.size(); ++i) {
      if (collectionIndexToItem.contains(i)) {
        continue;
      }
      int parentIndex = collections[i].parentCollectionIndex;
      if (parentIndex >= 0 && parentIndex < collections.size()) {
        if (collectionIndexToItem.contains(parentIndex)) {
          QTreeWidgetItem *parentItem = collectionIndexToItem[parentIndex];
          createTreeItem(i, parentItem);
          foundSubcollection = true;
        }
      }
    }
  }

  for (int i = 0; i < collections.size(); ++i) {
    if (!collectionIndexToItem.contains(i)) {
      collections[i].parentCollectionIndex = -1;
      collections[i].isSubcollection = false;
      createTreeItem(i);
    }
  }
}

auto SettingsDialog::createTreeItem(int collectionIndex,
                                    QTreeWidgetItem *parent)
    -> QTreeWidgetItem * {
  QTreeWidgetItem *item = (parent)
                              ? new QTreeWidgetItem(parent)
                              : new QTreeWidgetItem(collectionTreeWidget);
  item->setText(0, collections[collectionIndex].name);
  item->setFlags(item->flags() | Qt::ItemIsEditable);
  itemToCollectionIndex[item] = collectionIndex;
  collectionIndexToItem[collectionIndex] = item;
  return item;
}

// Handles selection changes; supports deselection state
void SettingsDialog::onTreeItemSelectionChanged() {
  QList<QTreeWidgetItem *> selectedItems =
      collectionTreeWidget->selectedItems();
  if (selectedItems.isEmpty()) {
    currentTreeItem = nullptr;
    currentCollectionIndex = -1;
    return;
  }

  QTreeWidgetItem *item = selectedItems.first();
  if (!itemToCollectionIndex.contains(item)) {
    return;
  }

  int newIndex = itemToCollectionIndex[item];
  if (newIndex == currentCollectionIndex) {
    return;
  }

  const int previousIndex = currentCollectionIndex;
  if (previousIndex >= 0 &&
      previousIndex < m_workingCollections.size() &&
      !resolveUnsavedChanges(tr("switching collections"), true)) {
    if (collectionIndexToItem.contains(previousIndex)) {
      QSignalBlocker blocker(collectionTreeWidget);
      if (auto *previousItem = collectionIndexToItem[previousIndex]) {
        collectionTreeWidget->setCurrentItem(previousItem);
        previousItem->setSelected(true);
      }
    }
    return;
  }

  if (collectionIndexToItem.contains(newIndex)) {
    item = collectionIndexToItem[newIndex];
  }

  if (collectionTreeWidget && item) {
    QSignalBlocker blocker(collectionTreeWidget);
    collectionTreeWidget->setCurrentItem(item);
    item->setSelected(true);
  }

  currentCollectionIndex = newIndex;
  currentTreeItem = item;
  if (newIndex >= 0 && newIndex < m_workingCollections.size()) {
    originalCollection = m_workingCollections[newIndex];
  } else {
    originalCollection = CollectionConfig();
  }
  loadCollectionToUI(newIndex);
  m_collectionSaved = true;
  updateSaveButtonStyle();
}

void SettingsDialog::onTreeItemChanged(QTreeWidgetItem *item, int column) {
  if (column != 0 || !itemToCollectionIndex.contains(item)) {
    return;
  }
  int collectionIndex = itemToCollectionIndex[item];
  if (!CollectionUtils::isValidIndex(collectionIndex, &collections) ||
      !CollectionUtils::isValidIndex(collectionIndex, &m_workingCollections)) {
    return;
  }

  QString newName = item->text(0);
  const QString oldName = collections[collectionIndex].name;

  if (newName != oldName) {
    collections[collectionIndex].name = newName;
    m_workingCollections[collectionIndex].name = newName;

    bool revertedToOriginal = (collectionIndex == currentCollectionIndex &&
                               newName == originalCollection.name);
    m_collectionSaved = revertedToOriginal && !hasUnsavedChanges();
    if (!revertedToOriginal) {
      m_collectionSaved = false;
    }

    updateSaveButtonStyle();
  }
}

void SettingsDialog::setupButtonConnections() {
  connect(ui->buttonBox, &QDialogButtonBox::accepted, this,
          &SettingsDialog::accept);

  connect(ui->buttonBox, &QDialogButtonBox::rejected, this,
          &SettingsDialog::reject);

  connect(ui->addCollectionButton, &QPushButton::clicked, this,
          &SettingsDialog::addCollection);
  connect(ui->removeCollectionButton, &QPushButton::clicked, this,
          &SettingsDialog::removeCollection);
  connect(ui->browseLauncherButton, &QPushButton::clicked, this,
          &SettingsDialog::browseLauncher);
  connect(ui->browseCoreButton, &QPushButton::clicked, this,
          &SettingsDialog::browseCore);
  connect(ui->browseMediaDirButton, &QPushButton::clicked, this,
          &SettingsDialog::browseMediaDir);
  connect(ui->browseArtworkDirButton, &QPushButton::clicked, this,
          &SettingsDialog::browseArtworkDir);
  if (ui->recursiveImportContentButton) {
    connect(ui->recursiveImportContentButton, &QPushButton::clicked, this,
            &SettingsDialog::onRecursiveImportContent);
  }
}

void SettingsDialog::setupBasicUIConnections() {
  connect(ui->saveCollectionButton, &QPushButton::clicked, this, [this]() {
    if (currentCollectionIndex < 0 ||
        currentCollectionIndex >= collections.size()) {
      return;
    }
    if (!ui->gridWidthSpinBox) {
      return;
    }

    int editedIndex = currentCollectionIndex;
    handleSaveCollection(editedIndex);
  });
}

void SettingsDialog::handleSaveCollection(int editedIndex, bool refreshTree) {
  int newGridWidth = ui->gridWidthSpinBox->value();
  bool isActive = (editedIndex == originalCurrentCollectionIndex &&
                   originalCurrentCollectionIndex >= 0 &&
                   originalCurrentCollectionIndex < collections.size());
  bool gridWidthChangedFlag = (newGridWidth != originalCollection.gridWidth);
  if (isActive && gridWidthChangedFlag) {
    m_gridWidthChangedForActiveCollection = true;
    m_newGridWidthForActiveCollection = newGridWidth;
  }

  saveCollectionFromUI(editedIndex);
  originalCollection = collections[editedIndex];
  m_collectionSaved = true;
  updateSaveButtonStyle();
  emit collectionSaved(collections);
  if (isActive && gridWidthChangedFlag) {
    emitGridWidthChanged();
  }

  if (!refreshTree) {
    return;
  }

  QSignalBlocker blocker(collectionTreeWidget);
  // Rebuild tree to reflect parent changes immediately and reselect the
  // edited collection
  updateCollectionTreeWidget();
  expandPathToCollection(editedIndex);
  if (collectionIndexToItem.contains(editedIndex)) {
    QTreeWidgetItem *item = collectionIndexToItem[editedIndex];
    if (item) {
      collectionTreeWidget->setCurrentItem(item);
      item->setSelected(true);
    }
  }
  loadCollectionToUI(editedIndex);
}

void SettingsDialog::setupFormFieldConnections() {
  if (ui->launcherLineEdit) {
    connect(ui->launcherLineEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
    connect(ui->launcherLineEdit, &QLineEdit::textChanged, this,
            [this](const QString &text) { updateUIForLauncherType(text); });
  }
  if (ui->coreLineEdit) {
    connect(ui->coreLineEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->launchParamsLineEdit) {
    connect(ui->launchParamsLineEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->mediaDirLineEdit) {
    connect(ui->mediaDirLineEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
    connect(ui->mediaDirLineEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::onContentDirectoryChanged);
  }
  if (ui->artworkDirLineEdit) {
    connect(ui->artworkDirLineEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->includeContentSubfoldersCheckBox) {
    connect(ui->includeContentSubfoldersCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->includeArtworkSubfoldersCheckBox) {
    connect(ui->includeArtworkSubfoldersCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->fileExtensionsLineEdit) {
    connect(ui->fileExtensionsLineEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->gridWidthSpinBox) {
    connect(ui->gridWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::onGridWidthChanged);
  }
  if (ui->showAllSubcollectionItemsCheckBox) {
    connect(ui->showAllSubcollectionItemsCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->horizontalAlignmentComboBox) {
    connect(ui->horizontalAlignmentComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->parentCollectionComboBox) {
    connect(ui->parentCollectionComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->sidebarModeComboBox) {
    connect(ui->sidebarModeComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->hideHorizontalScrollbarCheckBox) {
    connect(ui->hideHorizontalScrollbarCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->hideVerticalScrollbarCheckBox) {
    connect(ui->hideVerticalScrollbarCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->hideTitlesCheckBox) {
    connect(ui->hideTitlesCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->hideSubcollectionTitlesCheckBox) {
    connect(ui->hideSubcollectionTitlesCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->itemWidthSpinBox) {
    connect(ui->itemWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::checkForChanges);
  }
  if (ui->itemHeightSpinBox) {
    connect(ui->itemHeightSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::checkForChanges);
  }
  if (ui->fontSizeSpinBox) {
    connect(ui->fontSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::checkForChanges);
  }
}

void SettingsDialog::setupSpacingConnections() {
  if (ui->horizontalSpacingSpinBox) {
    connect(ui->horizontalSpacingSpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
    connect(ui->horizontalSpacingSpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this]() { handleSpacingChanged(); });
  }
  if (ui->verticalSpacingSpinBox) {
    connect(ui->verticalSpacingSpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
    connect(ui->verticalSpacingSpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this]() { handleSpacingChanged(); });
  }
}

void SettingsDialog::handleSpacingChanged() {
  if (!ui->horizontalSpacingSpinBox ||
      !ui->verticalSpacingSpinBox) {
    return;
  }
  if (currentCollectionIndex < 0 ||
      currentCollectionIndex >= collections.size()) {
    return;
  }
  if (originalCurrentCollectionIndex < 0 ||
      originalCurrentCollectionIndex >= collections.size()) {
    return;
  }
  if (currentCollectionIndex == originalCurrentCollectionIndex) {
    // Rebase horizontal spacing: UI value 20 corresponds to internal -50
    // Internal = UI - 70
    int internalHorizontalSpacing = ui->horizontalSpacingSpinBox->value() - 70;
    emit spacingChanged(currentCollectionIndex,
                        internalHorizontalSpacing,
                        ui->verticalSpacingSpinBox->value());
  }
}

void SettingsDialog::setupTreeWidgetConnections() {
  if (collectionTreeWidget) {
    connect(collectionTreeWidget, &QTreeWidget::itemSelectionChanged, this,
            &SettingsDialog::onTreeItemSelectionChanged);
    connect(collectionTreeWidget, &QTreeWidget::itemChanged, this,
            &SettingsDialog::onTreeItemChanged);
    collectionTreeWidget->setEditTriggers(QAbstractItemView::EditKeyPressed |
                                          QAbstractItemView::DoubleClicked);
  }
}

void SettingsDialog::setupUIConstraints() {
  if (ui->horizontalSpacingSpinBox) {
    // Rebase horizontal spacing: UI range 0 to 150 maps to internal -100 to 50
    // Internal = UI - 70.
    // Min UI = -100 + 70 = -30? No.
    // User wants "20" to be "-50".
    // UI = Internal + 70.
    // Min Internal = -100. Min UI = -30.
    // Max Internal = 50. Max UI = 120.
    ui->horizontalSpacingSpinBox->setMinimum(-30);
    ui->horizontalSpacingSpinBox->setMaximum(120);
    ui->horizontalSpacingSpinBox->setSingleStep(1);
  }
  if (ui->verticalSpacingSpinBox) {
    ui->verticalSpacingSpinBox->setMinimum(UIConstants::Viewport::SPACING_MIN);
    ui->verticalSpacingSpinBox->setMaximum(UIConstants::Viewport::SPACING_MAX);
    ui->verticalSpacingSpinBox->setSingleStep(1);
  }
  if (ui->gridWidthSpinBox) {
    ui->gridWidthSpinBox->setMinimum(UIConstants::Grid::MIN_WIDTH);
    ui->gridWidthSpinBox->setMaximum(UIConstants::Grid::MAX_WIDTH);
    ui->gridWidthSpinBox->setSingleStep(1);
  }
  if (ui->fontSizeSpinBox) {
    ui->fontSizeSpinBox->setMinimum(UIConstants::Item::MIN_FONT_SIZE);
    ui->fontSizeSpinBox->setMaximum(UIConstants::Item::MAX_FONT_SIZE);
    ui->fontSizeSpinBox->setSingleStep(1);
  }
}

void SettingsDialog::setupGeneralSettingsConnections() {
  connect(ui->rememberSelectionCheckBox, &QCheckBox::toggled, this,
          [this](bool checked) {
            auto *mainWindow = qobject_cast<MainWindow *>(parent());
            if ((mainWindow) &&
                (mainWindow->getSettingsManager())) {
              mainWindow->m_generalSettings.rememberSelection = checked;
              mainWindow->getSettingsManager()->saveGeneralSettings(
                  mainWindow->m_generalSettings);
              m_generalSettings = mainWindow->m_generalSettings;
            }
          });

  connect(ui->wrapNavigationCheckBox, &QCheckBox::toggled, this,
          [this](bool checked) {
            auto *mainWindow = qobject_cast<MainWindow *>(parent());
            if ((mainWindow) &&
                (mainWindow->getSettingsManager())) {
              mainWindow->m_generalSettings.wrapNavigation = checked;
              mainWindow->getSettingsManager()->saveGeneralSettings(
                  mainWindow->m_generalSettings);
              m_generalSettings = mainWindow->m_generalSettings;
            }
          });
}

// Sets up signal/slot connections and ensures tree updates immediately after
// saving changes
void SettingsDialog::setupConnections() {
  setupButtonConnections();
  setupBasicUIConnections();
  setupFormFieldConnections();
  setupSpacingConnections();
  setupTreeWidgetConnections();
  setupUIConstraints();
  setupGeneralSettingsConnections();
}

// Add a new collection, optionally inheriting from current selection;
// initialize defaults.
void SettingsDialog::addCollection() {
  bool parseOk;
  QString name = QInputDialog::getText(
      this, "Add Collection", "Enter collection name:", QLineEdit::Normal, "",
      &parseOk);
  if (!parseOk || name.isEmpty()) {
    return;
  }

  if (currentCollectionIndex >= 0 &&
      currentCollectionIndex < collections.size()) {
    saveCollectionFromUI(currentCollectionIndex);
  }

  CollectionConfig newCollection;
  newCollection.name = name;
  newCollection.launcherPath = "";
  newCollection.corePath = "";
  newCollection.launchParameters = "";
  newCollection.mediaDirectory = "";
  newCollection.artworkDirectory = "";
  newCollection.extensions = QStringList();
  newCollection.gridWidth = UIConstants::Grid::DEFAULT_WIDTH;
  newCollection.sidebarVisible = false;
  newCollection.parentCollectionIndex = -1;
  newCollection.isSubcollection = false;
  newCollection.showAllSubcollectionItems = false;
  newCollection.horizontalAlignment = HorizontalAlignment::Center;
  newCollection.fontSize = UIConstants::Item::DEFAULT_FONT_SIZE;
  newCollection.hideTitles = false;
  newCollection.hideSubcollectionTitles = false;

  int parentIdx = (currentCollectionIndex >= 0 &&
                   currentCollectionIndex < m_workingCollections.size())
                      ? currentCollectionIndex
                      : -1;
  if (parentIdx >= 0) {
    const CollectionConfig &parent = m_workingCollections[parentIdx];
    newCollection.parentCollectionIndex = parentIdx;
    newCollection.isSubcollection = true;
    newCollection.gridWidth = parent.gridWidth;
    newCollection.horizontalSpacing = parent.horizontalSpacing;
    newCollection.verticalSpacing = parent.verticalSpacing;
    newCollection.itemWidth = parent.itemWidth;
    newCollection.itemHeight = parent.itemHeight;
    newCollection.fontSize = parent.fontSize;
    newCollection.hideHorizontalScrollbar = parent.hideHorizontalScrollbar;
    newCollection.hideVerticalScrollbar = parent.hideVerticalScrollbar;
    newCollection.sidebarMode = parent.sidebarMode;
    newCollection.showAllSubcollectionItems = parent.showAllSubcollectionItems;
    newCollection.horizontalAlignment = parent.horizontalAlignment;
    newCollection.hideTitles = parent.hideTitles;
    newCollection.hideSubcollectionTitles = parent.hideSubcollectionTitles;
  }

  collections.append(newCollection);
  m_workingCollections.append(newCollection);
  int newIndex = collections.size() - 1;
  currentCollectionIndex = newIndex;

  updateCollectionTreeWidget();
  expandPathToCollection(newIndex);

  if (collectionIndexToItem.contains(newIndex)) {
    QTreeWidgetItem *item = collectionIndexToItem[newIndex];
    if (item) {
      collectionTreeWidget->setCurrentItem(item);
      item->setSelected(true);
    }
  }

  loadCollectionToUI(newIndex);
  originalCollection = m_workingCollections[newIndex];
  m_collectionSaved = true;
  updateSaveButtonStyle();
}

// Validates preconditions for collection removal
auto SettingsDialog::validateRemovalPreconditions() -> bool {
  if ((!currentTreeItem) ||
      !itemToCollectionIndex.contains(currentTreeItem)) {
    return false;
  }
  int index = itemToCollectionIndex[currentTreeItem];
  if (!CollectionUtils::isValidIndex(index, collections)) {
    return false;
  }
  if (collections.size() <= 1) {
    QMessageBox::warning(this, "Cannot Remove Collection",
                         "You cannot remove the last collection.",
                         QMessageBox::Ok);
    return false;
  }
  return true;
}

// Captures currently expanded tree states before removal
auto SettingsDialog::captureExpandedStates() -> QList<int> {
  QList<int> expandedBefore;
  for (auto it = collectionIndexToItem.begin();
       it != collectionIndexToItem.end(); ++it) {
    if ((it.value()) && it.value()->isExpanded()) {
      expandedBefore.append(it.key());
    }
  }
  return expandedBefore;
}

// Performs the actual collection removal from data structures
auto SettingsDialog::performCollectionRemoval(int index) -> void {
  collections.removeAt(index);
  if (index >= 0 && index < m_workingCollections.size()) {
    m_workingCollections.removeAt(index);
  }
}

// Updates parent references after collection removal
auto SettingsDialog::updateParentReferences(int removedIndex) -> void {
  for (int i = 0; i < collections.size(); ++i) {
    if (collections[i].parentCollectionIndex > removedIndex) {
      collections[i].parentCollectionIndex -= 1;
      m_workingCollections[i].parentCollectionIndex =
          collections[i].parentCollectionIndex;
    } else if (collections[i].parentCollectionIndex == removedIndex) {
      collections[i].parentCollectionIndex = -1;
      m_workingCollections[i].parentCollectionIndex = -1;
      collections[i].isSubcollection = false;
      m_workingCollections[i].isSubcollection = false;
    }
  }
}

// Restores expanded states after tree rebuild, adjusting for removed index
auto SettingsDialog::restoreExpandedStates(const QList<int> &expandedBefore,
                                           int removedIndex) -> void {
  for (int expIdx : expandedBefore) {
    if (expIdx == removedIndex) {
      continue;
    }
    int adjustedIdx = (expIdx > removedIndex) ? expIdx - 1 : expIdx;
    if (collectionIndexToItem.contains(adjustedIdx) &&
        (collectionIndexToItem[adjustedIdx])) {
      collectionIndexToItem[adjustedIdx]->setExpanded(true);
    }
  }
}

// Selects appropriate target collection after removal
auto SettingsDialog::selectTargetAfterRemoval(int parentIdx, int removedIndex)
    -> void {
  if (parentIdx >= removedIndex) {
    parentIdx -= 1;
  }
  int targetIndex =
      (parentIdx >= 0 && parentIdx < collections.size()) ? parentIdx : 0;
  currentCollectionIndex = (collections.isEmpty())
                               ? -1
                               : qBound(0, targetIndex, collections.size() - 1);

  if (currentCollectionIndex >= 0 &&
      collectionIndexToItem.contains(currentCollectionIndex)) {
    collectionTreeWidget->setCurrentItem(
        collectionIndexToItem[currentCollectionIndex]);
    collectionIndexToItem[currentCollectionIndex]->setSelected(true);
    expandPathToCollection(currentCollectionIndex);
    loadCollectionToUI(currentCollectionIndex);
    originalCollection = m_workingCollections[currentCollectionIndex];
  } else {
    originalCollection = CollectionConfig();
  }
}

// Extracts all UI field values into a collection config
auto SettingsDialog::extractUIFieldValues() -> CollectionConfig {
  CollectionConfig config;
  if (currentCollectionIndex >= 0 &&
      currentCollectionIndex < m_workingCollections.size()) {
    config = m_workingCollections[currentCollectionIndex];
  }

  if (collectionIndexToItem.contains(currentCollectionIndex) &&
      (collectionIndexToItem[currentCollectionIndex])) {
    QString treeName = collectionIndexToItem[currentCollectionIndex]->text(0);
    if (!treeName.isEmpty()) {
      config.name = treeName;
    }
  }

  config.launcherPath = (ui->launcherLineEdit)
                            ? ui->launcherLineEdit->text()
                            : config.launcherPath;
  config.corePath = (ui->coreLineEdit) ? ui->coreLineEdit->text()
                                                  : config.corePath;
  config.launchParameters = (ui->launchParamsLineEdit)
                                ? ui->launchParamsLineEdit->text()
                                : config.launchParameters;
  config.mediaDirectory = (ui->mediaDirLineEdit)
                              ? ui->mediaDirLineEdit->text()
                              : config.mediaDirectory;
  config.artworkDirectory = (ui->artworkDirLineEdit)
                                ? ui->artworkDirLineEdit->text()
                                : config.artworkDirectory;
  config.includeContentSubfolders =
      (ui->includeContentSubfoldersCheckBox)
          ? ui->includeContentSubfoldersCheckBox->isChecked()
          : config.includeContentSubfolders;
  config.includeArtworkSubfolders =
      (ui->includeArtworkSubfoldersCheckBox)
          ? ui->includeArtworkSubfoldersCheckBox->isChecked()
          : config.includeArtworkSubfolders;
  config.itemWidth = (ui->itemWidthSpinBox)
                         ? ui->itemWidthSpinBox->value()
                         : config.itemWidth;
  config.itemHeight = (ui->itemHeightSpinBox)
                          ? ui->itemHeightSpinBox->value()
                          : config.itemHeight;
  config.fontSize = (ui->fontSizeSpinBox)
                        ? ui->fontSizeSpinBox->value()
                        : config.fontSize;
  config.extensions = (ui->fileExtensionsLineEdit)
                          ? ExtensionUtils::parseUserExtensionList(
                                ui->fileExtensionsLineEdit->text())
                          : config.extensions;
  config.gridWidth = (ui->gridWidthSpinBox)
                         ? ui->gridWidthSpinBox->value()
                         : config.gridWidth;
  config.showAllSubcollectionItems =
      (ui->showAllSubcollectionItemsCheckBox)
          ? ui->showAllSubcollectionItemsCheckBox->isChecked()
          : config.showAllSubcollectionItems;
  config.horizontalAlignment =
      (ui->horizontalAlignmentComboBox)
          ? static_cast<HorizontalAlignment>(
                ui->horizontalAlignmentComboBox->currentIndex())
          : config.horizontalAlignment;
  config.sidebarMode =
      (ui->sidebarModeComboBox)
          ? static_cast<SidebarMode>(ui->sidebarModeComboBox->currentIndex())
          : config.sidebarMode;
  // Rebase horizontal spacing: Internal = UI - 70
  config.horizontalSpacing = (ui->horizontalSpacingSpinBox)
                                 ? ui->horizontalSpacingSpinBox->value() - 70
                                 : config.horizontalSpacing;
  config.verticalSpacing = (ui->verticalSpacingSpinBox)
                               ? ui->verticalSpacingSpinBox->value()
                               : config.verticalSpacing;
  config.hideHorizontalScrollbar =
      (ui->hideHorizontalScrollbarCheckBox)
          ? ui->hideHorizontalScrollbarCheckBox->isChecked()
          : config.hideHorizontalScrollbar;
  config.hideVerticalScrollbar =
      (ui->hideVerticalScrollbarCheckBox)
          ? ui->hideVerticalScrollbarCheckBox->isChecked()
          : config.hideVerticalScrollbar;
  config.hideTitles = (ui->hideTitlesCheckBox)
                          ? ui->hideTitlesCheckBox->isChecked()
                          : config.hideTitles;
  config.hideSubcollectionTitles =
      (ui->hideSubcollectionTitlesCheckBox)
          ? ui->hideSubcollectionTitlesCheckBox->isChecked()
          : config.hideSubcollectionTitles;
  return config;
}

// Updates parent collection settings from UI
auto SettingsDialog::updateParentCollectionFromUI(CollectionConfig &collection,
                                                  int index) -> void {
  if (ui->parentCollectionComboBox) {
    int dropdownIndex = ui->parentCollectionComboBox->currentIndex();
    if (dropdownIndex >= 0 &&
        dropdownIndex < m_parentCollectionMapping.size()) {
      int newParentIndex = m_parentCollectionMapping[dropdownIndex];
      if (newParentIndex >= 0 && newParentIndex < m_workingCollections.size() &&
          newParentIndex != index) {
        collection.parentCollectionIndex = newParentIndex;
        collection.isSubcollection = true;
      } else {
        collection.parentCollectionIndex = -1;
        collection.isSubcollection = false;
      }
    } else {
      collection.parentCollectionIndex = -1;
      collection.isSubcollection = false;
    }
  }
}

// Checks basic field changes against original configuration
auto SettingsDialog::checkBasicFieldChanges() const -> bool {
  const CollectionConfig &originalConfig = originalCollection;

  return (
      ((ui->launcherLineEdit) &&
       ui->launcherLineEdit->text() != originalConfig.launcherPath) ||
      ((ui->coreLineEdit) &&
       ui->coreLineEdit->text() != originalConfig.corePath) ||
      ((ui->launchParamsLineEdit) &&
       ui->launchParamsLineEdit->text() != originalConfig.launchParameters) ||
      ((ui->mediaDirLineEdit) &&
       ui->mediaDirLineEdit->text() != originalConfig.mediaDirectory) ||
      ((ui->artworkDirLineEdit) &&
       ui->artworkDirLineEdit->text() != originalConfig.artworkDirectory) ||
      ((ui->gridWidthSpinBox) &&
       ui->gridWidthSpinBox->value() != originalConfig.gridWidth) ||
      ((ui->showAllSubcollectionItemsCheckBox) &&
       ui->showAllSubcollectionItemsCheckBox->isChecked() !=
           originalConfig.showAllSubcollectionItems) ||
      ((ui->horizontalAlignmentComboBox) &&
       ui->horizontalAlignmentComboBox->currentIndex() !=
           static_cast<int>(originalConfig.horizontalAlignment)) ||
      ((ui->sidebarModeComboBox) &&
       ui->sidebarModeComboBox->currentIndex() !=
           static_cast<int>(originalConfig.sidebarMode)) ||
      ((ui->horizontalSpacingSpinBox) &&
       (ui->horizontalSpacingSpinBox->value() - 70) !=
           originalConfig.horizontalSpacing) ||
      ((ui->verticalSpacingSpinBox) &&
       ui->verticalSpacingSpinBox->value() != originalConfig.verticalSpacing) ||
      ((ui->hideHorizontalScrollbarCheckBox) &&
       ui->hideHorizontalScrollbarCheckBox->isChecked() !=
           originalConfig.hideHorizontalScrollbar) ||
      ((ui->hideVerticalScrollbarCheckBox) &&
       ui->hideVerticalScrollbarCheckBox->isChecked() !=
           originalConfig.hideVerticalScrollbar) ||
      ((ui->hideTitlesCheckBox) &&
       ui->hideTitlesCheckBox->isChecked() != originalConfig.hideTitles) ||
      ((ui->hideSubcollectionTitlesCheckBox) &&
       ui->hideSubcollectionTitlesCheckBox->isChecked() !=
           originalConfig.hideSubcollectionTitles) ||
      ((ui->fontSizeSpinBox) &&
       ui->fontSizeSpinBox->value() != originalConfig.fontSize));
}

// Checks extension list changes
auto SettingsDialog::checkExtensionChanges() const -> bool {
  QStringList currentExtensions = (ui->fileExtensionsLineEdit)
                                      ? ExtensionUtils::parseUserExtensionList(
                                            ui->fileExtensionsLineEdit->text())
                                      : originalCollection.extensions;
  return currentExtensions != originalCollection.extensions;
}

// Checks tree name changes
auto SettingsDialog::checkTreeNameChanges() const -> bool {
  QString currentTreeName = originalCollection.name;
  if (collectionIndexToItem.contains(currentCollectionIndex)) {
    QTreeWidgetItem *item = collectionIndexToItem[currentCollectionIndex];
    if (item) {
      currentTreeName = item->text(0);
    }
  }
  return currentTreeName != originalCollection.name;
}

// Checks parent collection changes
auto SettingsDialog::checkParentCollectionChanges() const -> bool {
  int dropdownIndex = (ui->parentCollectionComboBox)
                          ? ui->parentCollectionComboBox->currentIndex()
                          : -1;
  int currentParentIndex = -1;
  if (dropdownIndex >= 0 && dropdownIndex < m_parentCollectionMapping.size()) {
    currentParentIndex = m_parentCollectionMapping[dropdownIndex];
  }
  return currentParentIndex != originalCollection.parentCollectionIndex;
}

// Checks dimension changes
auto SettingsDialog::checkDimensionChanges() const -> bool {
  return (ui->itemWidthSpinBox->value() != originalCollection.itemWidth ||
          ui->itemHeightSpinBox->value() != originalCollection.itemHeight);
}

auto SettingsDialog::promptUnsavedChanges(const QString &actionDescription)
    -> QMessageBox::StandardButton {
  QMessageBox messageBox(this);
  messageBox.setIcon(QMessageBox::Warning);
  messageBox.setWindowTitle(tr("Unsaved Changes"));
  messageBox.setText(
      tr("Save changes before %1?").arg(actionDescription.trimmed()));
  messageBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard |
                                QMessageBox::Cancel);
  messageBox.setDefaultButton(QMessageBox::Save);
  return static_cast<QMessageBox::StandardButton>(messageBox.exec());
}

void SettingsDialog::revertCurrentCollectionEdits() {
  if (currentCollectionIndex < 0 ||
      currentCollectionIndex >= m_workingCollections.size()) {
    return;
  }

  m_workingCollections[currentCollectionIndex] = originalCollection;
  if (currentCollectionIndex >= 0 &&
      currentCollectionIndex < collections.size()) {
    collections[currentCollectionIndex] = originalCollection;
  }

  if (collectionIndexToItem.contains(currentCollectionIndex)) {
    if (auto *item = collectionIndexToItem[currentCollectionIndex]) {
      item->setText(0, originalCollection.name);
    }
  }

  loadCollectionToUI(currentCollectionIndex);
  m_collectionSaved = true;
  updateSaveButtonStyle();
}

auto SettingsDialog::resolveUnsavedChanges(const QString &actionDescription,
                                           bool refreshTreeAfterSave) -> bool {
  if (!hasUnsavedChanges()) {
    m_collectionSaved = true;
    return true;
  }

  const QMessageBox::StandardButton decision =
      promptUnsavedChanges(actionDescription);
  if (decision == QMessageBox::Cancel) {
    return false;
  }
  if (decision == QMessageBox::Save) {
    if (currentCollectionIndex >= 0 &&
        currentCollectionIndex < m_workingCollections.size()) {
      handleSaveCollection(currentCollectionIndex, refreshTreeAfterSave);
    }
    return true;
  }

  revertCurrentCollectionEdits();
  return true;
}

void SettingsDialog::removeCollection() {
  if (!validateRemovalPreconditions()) {
    return;
  }

  int index = itemToCollectionIndex[currentTreeItem];
  int parentIdx = collections[index].parentCollectionIndex;

  QMessageBox::StandardButton reply = QMessageBox::question(
      this, "Remove Collection", "Remove " + collections[index].name + "?",
      QMessageBox::Yes | QMessageBox::No);
  if (reply != QMessageBox::Yes) {
    return;
  }

  QList<int> expandedBefore = captureExpandedStates();
  performCollectionRemoval(index);
  updateParentReferences(index);
  updateCollectionTreeWidget();
  restoreExpandedStates(expandedBefore, index);
  selectTargetAfterRemoval(parentIdx, index);

  m_collectionSaved = true;
  updateSaveButtonStyle();
}

// Saves current collection UI edits (including name) into working and live
// collections
void SettingsDialog::saveCollectionFromUI(int index) {
  if (!CollectionUtils::isValidIndex(index, m_workingCollections)) {
    return;
  }

  CollectionConfig collection = extractUIFieldValues();
  updateParentCollectionFromUI(collection, index);

  m_workingCollections[index] = collection;
  collections = m_workingCollections;
  originalCollection = collection;
  m_collectionSaved = true;
  updateSaveButtonStyle();
}

auto SettingsDialog::hasUnsavedChanges() const -> bool {
  if (currentCollectionIndex < 0 ||
      currentCollectionIndex >= collections.size()) {
    return false;
  }

  return checkBasicFieldChanges() || checkExtensionChanges() ||
         checkTreeNameChanges() || checkParentCollectionChanges() ||
         checkDimensionChanges();
}

void SettingsDialog::updateSaveButtonStyle() {
  if (hasUnsavedChanges()) {
    ui->saveCollectionButton->setStyleSheet("QPushButton {"
                                            "  border: 1px solid red;"
                                            "  padding: 5px;"
                                            "}");
  } else {
    ui->saveCollectionButton->setStyleSheet("");
  }
}

void SettingsDialog::updateUIForLauncherType(const QString &launcherPath) {
  bool hasContentDir = !ui->mediaDirLineEdit->text().trimmed().isEmpty();
  bool isRetroArch = launcherPath.contains("retroarch", Qt::CaseInsensitive);
  bool showCore = hasContentDir && isRetroArch;
  ui->coreLineEdit->setVisible(showCore);
  ui->browseCoreButton->setVisible(showCore);
  ui->label_core->setVisible(showCore);
  if (isRetroArch) {
    ui->coreLineEdit->setToolTip(
        "Path to RetroArch core file (.so/.dll/.dylib)");
    ui->launchParamsLineEdit->setToolTip("Additional RetroArch parameters");
  } else {
    ui->launchParamsLineEdit->setToolTip(
        "Additional command-line parameters for the launcher");
  }
}

void SettingsDialog::updateParentCollectionComboBox(int currentIndex) {
  if (!ui->parentCollectionComboBox) {
    return;
  }

  QSignalBlocker blocker(ui->parentCollectionComboBox);
  ui->parentCollectionComboBox->clear();
  ui->parentCollectionComboBox->addItem("None");
  m_parentCollectionMapping.clear();
  m_parentCollectionMapping.append(-1);

  for (int i = 0; i < collections.size(); ++i) {
    if (i == currentIndex) {
      continue;
    }
    if (wouldCreateCircularReference(currentIndex, i)) {
      continue;
    }
    ui->parentCollectionComboBox->addItem(collections[i].name);
    m_parentCollectionMapping.append(i);
  }

  int desiredParentIndex =
      (currentIndex >= 0 && currentIndex < collections.size())
          ? collections[currentIndex].parentCollectionIndex
          : -1;
  int targetDropdownIndex =
      m_parentCollectionMapping.indexOf(desiredParentIndex);
  if (targetDropdownIndex < 0) {
    targetDropdownIndex = 0;
  }
  ui->parentCollectionComboBox->setCurrentIndex(targetDropdownIndex);
}

auto SettingsDialog::wouldCreateCircularReference(
    int childIndex, int potentialParentIndex) const -> bool {
  if (childIndex < 0 || childIndex >= collections.size() ||
      potentialParentIndex < 0 || potentialParentIndex >= collections.size()) {
    return true;
  }
  if (potentialParentIndex == childIndex) {
    return true;
  }

  int currentParent = collections[potentialParentIndex].parentCollectionIndex;
  std::set<int> visited;
  while (currentParent >= 0 && currentParent < collections.size()) {
    if (visited.contains(currentParent)) {
      return true;
    }
    visited.insert(currentParent);
    if (currentParent == childIndex) {
      return true;
    }
    currentParent = collections[currentParent].parentCollectionIndex;
  }
  return false;
}

void SettingsDialog::emitGridWidthChanged() {
  if (m_gridWidthChangedForActiveCollection &&
      originalCurrentCollectionIndex >= 0 &&
      originalCurrentCollectionIndex < collections.size()) {
    emit gridWidthChanged(originalCurrentCollectionIndex,
                          m_newGridWidthForActiveCollection);
    m_gridWidthChangedForActiveCollection = false;
  }
}

void SettingsDialog::onContentDirectoryChanged() {
  updateFieldVisibility();
  checkForChanges();
}

void SettingsDialog::updateFieldVisibility() {
  bool hasContentDir = !ui->mediaDirLineEdit->text().trimmed().isEmpty();

  ui->label_launcher->setVisible(hasContentDir);
  ui->launcherLineEdit->setVisible(hasContentDir);
  ui->browseLauncherButton->setVisible(hasContentDir);
  ui->label_launchParams->setVisible(hasContentDir);
  ui->launchParamsLineEdit->setVisible(hasContentDir);
  ui->label_fileExtensions->setVisible(hasContentDir);
  ui->fileExtensionsLineEdit->setVisible(hasContentDir);
  ui->label_artworkDir->setVisible(hasContentDir);
  ui->artworkDirLineEdit->setVisible(hasContentDir);
  ui->browseArtworkDirButton->setVisible(hasContentDir);

  if (hasContentDir) {
    updateUIForLauncherType(ui->launcherLineEdit->text());
  } else {
    ui->label_core->setVisible(false);
    ui->coreLineEdit->setVisible(false);
    ui->browseCoreButton->setVisible(false);
  }

  ui->label_sidebarMode->setVisible(true);
  ui->sidebarModeComboBox->setVisible(true);
}

void SettingsDialog::updateSidebarModeVisibility() {
  ui->label_sidebarMode->setVisible(true);
  ui->sidebarModeComboBox->setVisible(true);
}

auto SettingsDialog::calculateMaxGridWidth() const -> int {
  int viewportWidth = UIConstants::Viewport::DEFAULT_WIDTH;
  if (QWidget *parentWindow = this->parentWidget()) {
    auto *itemScrollArea =
        parentWindow->findChild<QScrollArea *>("itemScrollArea");
    if ((itemScrollArea) &&
        (itemScrollArea->viewport())) {
      viewportWidth = itemScrollArea->viewport()->width();
      QScrollBar *vScrollBar = itemScrollArea->verticalScrollBar();
      if ((vScrollBar) && vScrollBar->isVisible()) {
        viewportWidth -= vScrollBar->width();
      }
    }
  }
  if (viewportWidth < UIConstants::Viewport::MIN_WIDTH) {
    viewportWidth = UIConstants::Viewport::DEFAULT_WIDTH;
  }

  int itemWidth = (ui->itemWidthSpinBox)
                      ? ui->itemWidthSpinBox->value()
                      : originalCollection.itemWidth;
  if (itemWidth <= 0) {
    itemWidth = UIConstants::Item::DEFAULT_WIDTH;
  }

  int horizontalSpacing = originalCollection.horizontalSpacing;
  int margins = UIConstants::Grid::MARGINS;
  const int totalMargins = margins * 2;
  const int stride = itemWidth + horizontalSpacing;
  int itemsFit =
      stride > 0 ? (viewportWidth - totalMargins + horizontalSpacing) / stride
                 : UIConstants::Grid::MIN_WIDTH;
  itemsFit = std::max(UIConstants::Grid::MIN_WIDTH, itemsFit);
  itemsFit += 1;
  return std::min(itemsFit, UIConstants::Grid::MAX_WIDTH);
}

void SettingsDialog::updateGridWidthLimits() {
  if (!ui->gridWidthSpinBox) {
    return;
  }
  int preservedValue = ui->gridWidthSpinBox->value();
  int calculatedMax = calculateMaxGridWidth();
  ui->gridWidthSpinBox->setMaximum(UIConstants::Grid::MAX_WIDTH);
  ui->gridWidthSpinBox->setValue(preservedValue);
  if (preservedValue > calculatedMax) {
    ui->gridWidthSpinBox->setToolTip(
        QString("Current: %1, Recommended max: %2, Absolute max: %3")
            .arg(preservedValue)
            .arg(calculatedMax)
            .arg(UIConstants::Grid::MAX_WIDTH));
  } else {
    ui->gridWidthSpinBox->setToolTip(
        QString("Items per row (Recommended max: %1, Absolute max: %2)")
            .arg(calculatedMax)
            .arg(UIConstants::Grid::MAX_WIDTH));
  }
}

void SettingsDialog::onGridWidthChanged(int value) {
  if (!ui->gridWidthSpinBox) {
    return;
  }
  int maxWidth = calculateMaxGridWidth();
  if (value > maxWidth) {
    // Delay tooltip to avoid showing during rapid spin box changes
    QTimer::singleShot(
        UIConstants::Timing::MEDIUM_DELAY_MS, this, [this, maxWidth, value]() {
          QToolTip::showText(
              ui->gridWidthSpinBox->mapToGlobal(QPoint(0, 0)),
              QString("Width %1 may overflow. Recommended max: %2.")
                  .arg(value)
                  .arg(maxWidth),
              ui->gridWidthSpinBox, QRect(), UIConstants::Timing::TOOLTIP_DISPLAY_MS);
        });
  }
  checkForChanges();
  if (!m_isLoading &&
      currentCollectionIndex == originalCurrentCollectionIndex &&
      originalCurrentCollectionIndex >= 0 &&
      originalCurrentCollectionIndex < collections.size()) {
    emit gridWidthChanged(currentCollectionIndex, value);
  }
}

void SettingsDialog::showEvent(QShowEvent *event) {
  QDialog::showEvent(event);
  // Delay grid width calculation until dialog geometry is finalized
  QTimer::singleShot(UIConstants::Timing::LONG_DELAY_MS, this,
                     &SettingsDialog::updateGridWidthLimits);
}

void SettingsDialog::resizeEvent(QResizeEvent *event) {
  QDialog::resizeEvent(event);
  // Delay recalculation until resize animation/settling completes
  QTimer::singleShot(UIConstants::Timing::MEDIUM_DELAY_MS, this,
                     &SettingsDialog::updateGridWidthLimits);
}

void SettingsDialog::loadGeneralSettingsToUI() {
  auto *mainWindow = qobject_cast<MainWindow *>(parent());
  if (mainWindow) {
    m_generalSettings = mainWindow->m_generalSettings;
  }
  if (ui->rememberSelectionCheckBox) {
    ui->rememberSelectionCheckBox->blockSignals(true);
    ui->rememberSelectionCheckBox->setChecked(
        m_generalSettings.rememberSelection);
    ui->rememberSelectionCheckBox->blockSignals(false);
  }
  if (ui->wrapNavigationCheckBox) {
    ui->wrapNavigationCheckBox->blockSignals(true);
    ui->wrapNavigationCheckBox->setChecked(m_generalSettings.wrapNavigation);
    ui->wrapNavigationCheckBox->blockSignals(false);
  }
  if (ui->pixmapCacheSpinBox) {
    ui->pixmapCacheSpinBox->blockSignals(true);
    ui->pixmapCacheSpinBox->setValue(m_generalSettings.pixmapCacheSizeMB);
    ui->pixmapCacheSpinBox->blockSignals(false);
  }
}

void SettingsDialog::saveGeneralSettingsFromUI() {
  auto *mainWindow = qobject_cast<MainWindow *>(parent());
  if ((mainWindow) && (mainWindow->getSettingsManager())) {
    if (ui->rememberSelectionCheckBox) {
      mainWindow->m_generalSettings.rememberSelection =
          ui->rememberSelectionCheckBox->isChecked();
    }
    if (ui->wrapNavigationCheckBox) {
      mainWindow->m_generalSettings.wrapNavigation =
          ui->wrapNavigationCheckBox->isChecked();
    }
    if (ui->pixmapCacheSpinBox) {
      int newCacheSize = ui->pixmapCacheSpinBox->value();
      mainWindow->m_generalSettings.pixmapCacheSizeMB = newCacheSize;
      // Apply immediately (in KB)
      QPixmapCache::setCacheLimit(newCacheSize * 1024);
    }
    mainWindow->getSettingsManager()->saveGeneralSettings(
        mainWindow->m_generalSettings);
    m_generalSettings = mainWindow->m_generalSettings;
  }
}

void SettingsDialog::checkForChanges() {
  m_collectionSaved = !hasUnsavedChanges();
  updateSaveButtonStyle();
}

void SettingsDialog::browseLauncher() {
  QString fileName = QFileDialog::getOpenFileName(
      this, tr("Select Launcher"), "",
      tr("All Files (*)"));
  if (!fileName.isEmpty() && ui->launcherLineEdit) {
    ui->launcherLineEdit->setText(fileName);
  }
}

void SettingsDialog::browseCore() {
  QString fileName = QFileDialog::getOpenFileName(
      this, tr("Select Core"), "",
      tr("Core Files (*.so *.dll *.dylib);;All Files (*)"));
  if (!fileName.isEmpty() && ui->coreLineEdit) {
    ui->coreLineEdit->setText(fileName);
  }
}

void SettingsDialog::browseMediaDir() {
  QString dirName = QFileDialog::getExistingDirectory(
      this, tr("Select Media Directory"), "");
  if (!dirName.isEmpty() && ui->mediaDirLineEdit) {
    ui->mediaDirLineEdit->setText(dirName);
  }
}

void SettingsDialog::browseArtworkDir() {
  QString dirName = QFileDialog::getExistingDirectory(
      this, tr("Select Artwork Directory"), "");
  if (!dirName.isEmpty() && ui->artworkDirLineEdit) {
    ui->artworkDirLineEdit->setText(dirName);
  }
}

void SettingsDialog::onRecursiveImportContent() {
  if (!ui->mediaDirLineEdit) {
    return;
  }
  QString baseDir = ui->mediaDirLineEdit->text().trimmed();
  if (baseDir.isEmpty()) {
    QMessageBox::warning(this, tr("Recursive Import"),
                         tr("Please specify a content directory first."));
    return;
  }
  performRecursiveImport(baseDir, true);
}

void SettingsDialog::onRecursiveImportArtwork() {
  if (!ui->artworkDirLineEdit) {
    return;
  }
  QString baseDir = ui->artworkDirLineEdit->text().trimmed();
  if (baseDir.isEmpty()) {
    QMessageBox::warning(this, tr("Recursive Import"),
                         tr("Please specify an artwork directory first."));
    return;
  }
  performRecursiveImport(baseDir, false);
}

void SettingsDialog::performRecursiveImport(const QString &baseDir, bool isContentDir) {
  QDir dir(baseDir);
  if (!dir.exists()) {
    QMessageBox::warning(this, tr("Recursive Import"),
                         tr("The specified directory does not exist."));
    return;
  }

  // Get list of subdirectories
  QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  if (subdirs.isEmpty()) {
    QMessageBox::information(this, tr("Recursive Import"),
                             tr("No subdirectories found in the specified directory."));
    return;
  }

  // Show confirmation dialog
  QString message = tr("This will create %1 subcollection(s) based on the directory structure:\n\n").arg(subdirs.size());
  for (int i = 0; i < qMin(subdirs.size(), 10); ++i) {
    message += QString("  • %1\n").arg(subdirs[i]);
  }
  if (subdirs.size() > 10) {
    message += tr("  ... and %1 more\n").arg(subdirs.size() - 10);
  }
  message += tr("\nEach subcollection will inherit the current collection's settings.\n");
  if (isContentDir) {
    message += tr("Content directories will be set automatically.");
  } else {
    message += tr("Artwork directories will be set automatically.");
  }

  QMessageBox::StandardButton reply = QMessageBox::question(
      this, tr("Confirm Recursive Import"), message,
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (reply != QMessageBox::Yes) {
    return;
  }

  // Get current collection as template
  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_workingCollections.size()) {
    return;
  }
  
  // Save current collection first
  handleSaveCollection(currentCollectionIndex, false);
  
  const CollectionConfig &templateConfig = m_workingCollections[currentCollectionIndex];
  int parentIndex = currentCollectionIndex;

  // Create subcollections for each subdirectory
  for (const QString &subdir : subdirs) {
    CollectionConfig newCollection = templateConfig;
    newCollection.name = subdir;
    newCollection.parentCollectionIndex = parentIndex;
    newCollection.isSubcollection = true;
    
    // Set the directory paths
    if (isContentDir) {
      newCollection.mediaDirectory = dir.absoluteFilePath(subdir);
      // Try to match artwork directory structure if it exists
      if (!templateConfig.artworkDirectory.isEmpty()) {
        QDir artworkBase(templateConfig.artworkDirectory);
        QString potentialArtworkDir = artworkBase.absoluteFilePath(subdir);
        if (QDir(potentialArtworkDir).exists()) {
          newCollection.artworkDirectory = potentialArtworkDir;
        }
      }
    } else {
      newCollection.artworkDirectory = dir.absoluteFilePath(subdir);
      // Try to match content directory structure if it exists
      if (!templateConfig.mediaDirectory.isEmpty()) {
        QDir mediaBase(templateConfig.mediaDirectory);
        QString potentialMediaDir = mediaBase.absoluteFilePath(subdir);
        if (QDir(potentialMediaDir).exists()) {
          newCollection.mediaDirectory = potentialMediaDir;
        }
      }
    }
    
    // Clear the virtual subfolder state
    newCollection.currentSubfolder.clear();
    
    m_workingCollections.append(newCollection);
    collections.append(newCollection);
  }

  // Refresh the tree widget
  updateCollectionTreeWidget();
  expandPathToCollection(currentCollectionIndex);
  
  // Reselect current collection
  if (collectionIndexToItem.contains(currentCollectionIndex)) {
    QTreeWidgetItem *item = collectionIndexToItem[currentCollectionIndex];
    if (item) {
      collectionTreeWidget->setCurrentItem(item);
      item->setSelected(true);
      item->setExpanded(true);
    }
  }
  
  emit collectionSaved(collections);
  
  QMessageBox::information(this, tr("Recursive Import"),
                           tr("Successfully created %1 subcollection(s).").arg(subdirs.size()));
}

void SettingsDialog::loadCollectionToUI(int index) {
  if (!CollectionUtils::isValidIndex(index, m_workingCollections)) {
    return;
  }
  m_isLoading = true;
  const CollectionConfig &config = m_workingCollections[index];

  if (ui->launcherLineEdit) {
    ui->launcherLineEdit->setText(config.launcherPath);
  }
  if (ui->coreLineEdit) {
    ui->coreLineEdit->setText(config.corePath);
  }
  if (ui->launchParamsLineEdit) {
    ui->launchParamsLineEdit->setText(config.launchParameters);
  }
  if (ui->mediaDirLineEdit) {
    ui->mediaDirLineEdit->setText(config.mediaDirectory);
  }
  if (ui->artworkDirLineEdit) {
    ui->artworkDirLineEdit->setText(config.artworkDirectory);
  }
  if (ui->includeContentSubfoldersCheckBox) {
    ui->includeContentSubfoldersCheckBox->setChecked(config.includeContentSubfolders);
  }
  if (ui->includeArtworkSubfoldersCheckBox) {
    ui->includeArtworkSubfoldersCheckBox->setChecked(config.includeArtworkSubfolders);
  }
  if (ui->fileExtensionsLineEdit) {
    ui->fileExtensionsLineEdit->setText(config.extensions.join(", "));
  }
  if (ui->gridWidthSpinBox) {
    ui->gridWidthSpinBox->setValue(config.gridWidth);
  }
  if (ui->showAllSubcollectionItemsCheckBox) {
    ui->showAllSubcollectionItemsCheckBox->setChecked(
        config.showAllSubcollectionItems);
  }
  if (ui->horizontalAlignmentComboBox) {
    ui->horizontalAlignmentComboBox->setCurrentIndex(
        static_cast<int>(config.horizontalAlignment));
  }
  if (ui->sidebarModeComboBox) {
    ui->sidebarModeComboBox->setCurrentIndex(
        static_cast<int>(config.sidebarMode));
  }
  if (ui->horizontalSpacingSpinBox) {
    // Rebase horizontal spacing: UI = Internal + 70
    ui->horizontalSpacingSpinBox->setValue(config.horizontalSpacing + 70);
  }
  if (ui->verticalSpacingSpinBox) {
    ui->verticalSpacingSpinBox->setValue(config.verticalSpacing);
  }
  if (ui->hideHorizontalScrollbarCheckBox) {
    ui->hideHorizontalScrollbarCheckBox->setChecked(
        config.hideHorizontalScrollbar);
  }
  if (ui->hideVerticalScrollbarCheckBox) {
    ui->hideVerticalScrollbarCheckBox->setChecked(
        config.hideVerticalScrollbar);
  }
  if (ui->hideTitlesCheckBox) {
    ui->hideTitlesCheckBox->setChecked(config.hideTitles);
  }
  if (ui->hideSubcollectionTitlesCheckBox) {
    ui->hideSubcollectionTitlesCheckBox->setChecked(
        config.hideSubcollectionTitles);
  }
  if (ui->itemWidthSpinBox) {
    ui->itemWidthSpinBox->setValue(config.itemWidth);
  }
  if (ui->itemHeightSpinBox) {
    ui->itemHeightSpinBox->setValue(config.itemHeight);
  }
  if (ui->fontSizeSpinBox) {
    ui->fontSizeSpinBox->setValue(config.fontSize);
  }

  updateParentCollectionComboBox(index);
  updateFieldVisibility();
  updateGridWidthLimits();
  m_isLoading = false;
}