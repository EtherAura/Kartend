// Collection configuration dialog with tree-based hierarchy editing and live
// preview.
#include <algorithm>
#include <functional>
#include <QAbstractItemView>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFontDialog>
#include <QAction>
#include <QInputDialog>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmapCache>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <set>

#include "extensionutils.h"
#include "gamepadmanager.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "mainwindow.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "ui_settingsdialog.h"
#include "uiconstants.h"

SettingsDialog::SettingsDialog(QWidget *parent, const QList<CollectionConfig> &initialCollections,
                               int initialIndex)
    : QDialog(parent), ui(new Ui::SettingsDialog), collectionTreeWidget(nullptr),
      currentTreeItem(nullptr), collections(initialCollections),
      originalCurrentCollectionIndex(initialIndex), currentCollectionIndex(initialIndex),
      m_workingCollections(initialCollections), m_gridWidthChangedForActiveCollection(false),
      m_newGridWidthForActiveCollection(0), m_collectionSaved(true), m_isLoading(false) {
  ui->setupUi(this);
  setWindowTitle(tr("Settings"));
  setModal(true);

  collectionTreeWidget = ui->collectionTreeWidget;

  installEventFilter(this);
  if (collectionTreeWidget) {
    collectionTreeWidget->installEventFilter(this);
    collectionTreeWidget->setFocusPolicy(Qt::WheelFocus);
  }

  // Check if there's no root collection and prompt to create one
  ensureRootCollectionExists();

  loadGeneralSettingsToUI();
  setupConnections();
  updateCollectionTreeWidget();

  if (currentCollectionIndex >= 0 && currentCollectionIndex < m_workingCollections.size()) {
    expandPathToCollection(currentCollectionIndex);
  }

  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_workingCollections.size()) {
    currentCollectionIndex = m_workingCollections.isEmpty() ? -1 : 0;
  }
  if (currentCollectionIndex >= 0 && currentCollectionIndex < m_workingCollections.size()) {
    loadCollectionToUI(currentCollectionIndex);
    originalCollection = m_workingCollections[currentCollectionIndex];
    if (collectionIndexToItem.contains(currentCollectionIndex)) {
      collectionTreeWidget->setCurrentItem(collectionIndexToItem[currentCollectionIndex]);
      currentTreeItem = collectionIndexToItem[currentCollectionIndex];
    }
  }

  originalCurrentCollectionIndex = currentCollectionIndex;
  loadGeneralSettingsToUI();

  // Initialize button states after setup is complete
  m_collectionSaved = true;
  updateSaveButtonStyle();
  updateDeleteButtonState();
}

// Handles wheel routing and whitespace click to allow deselection
auto SettingsDialog::eventFilter(QObject *obj, QEvent *event) -> bool {
  if (event->type() == QEvent::Wheel) {
    auto *wheelEvent = static_cast<QWheelEvent *>(event);

    if (obj == collectionTreeWidget) {
      return QDialog::eventFilter(obj, event);
    }

    if ((collectionTreeWidget) && collectionTreeWidget->underMouse()) {
      QWheelEvent forwardedEvent(
          collectionTreeWidget->mapFromGlobal(wheelEvent->globalPosition().toPoint()),
          wheelEvent->globalPosition().toPoint(), wheelEvent->pixelDelta(),
          wheelEvent->angleDelta(), wheelEvent->buttons(), wheelEvent->modifiers(),
          wheelEvent->phase(), wheelEvent->inverted());
      QApplication::sendEvent(collectionTreeWidget, &forwardedEvent);
      event->accept();
      return true;
    }

    event->accept();
    return true;
  }

  if ((collectionTreeWidget) &&
      (obj == collectionTreeWidget || obj == collectionTreeWidget->viewport()) &&
      event->type() == QEvent::MouseButtonPress) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    auto *src = static_cast<QWidget *>(obj);
    QPoint vpPos = collectionTreeWidget->viewport()->mapFrom(src, mouseEvent->pos());
    const QTreeWidgetItem *hit = collectionTreeWidget->itemAt(vpPos);
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

SettingsDialog::~SettingsDialog() {
  delete ui;
}

void SettingsDialog::accept() {
  stopGamepadButtonCapture();
  if (!resolveUnsavedChanges(tr("closing the dialog"), true)) {
    return;
  }
  saveGeneralSettingsFromUI();

  // Prompt user to rescan if database-affecting changes were saved
  if (!m_rescanRequired.isEmpty()) {
    QMessageBox::StandardButton reply =
        QMessageBox::question(this, tr("Rescan Required"),
                              tr("Some changes affect the database and require a rescan to take "
                                 "effect.\n\n"
                                 "Would you like to rescan now?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

    if (reply == QMessageBox::Yes) {
      // Only rescan the currently viewed collection to avoid concurrent
      // database operations If the user modified multiple collections, they can
      // manually rescan others
      if (m_rescanRequired.contains(originalCurrentCollectionIndex)) {
        emit rescanRequired(originalCurrentCollectionIndex);
      } else if (!m_rescanRequired.isEmpty()) {
        // Fall back to first affected collection if current wasn't modified
        emit rescanRequired(*m_rescanRequired.begin());
      }
    }
    m_rescanRequired.clear();
  }

  QDialog::accept();
}

void SettingsDialog::reject() {
  stopGamepadButtonCapture();
  if (!resolveUnsavedChanges(tr("closing the dialog"), true)) {
    return;
  }
  QDialog::reject();
}

void SettingsDialog::setupButtonConnections() {
  connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);

  connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

  connect(ui->addCollectionButton, &QPushButton::clicked, this, &SettingsDialog::addCollection);
  connect(ui->removeCollectionButton, &QPushButton::clicked, this,
          &SettingsDialog::removeCollection);
  // Kartend-63o: populate the "apply to..." tool button's dropdown menu so
  // users can propagate the current collection's appearance/layout settings
  // to all collections or to subcollections only. Menu is owned by the
  // button so Qt cleans it up with the dialog.
  if (ui->applyToButton) {
    auto *menu = new QMenu(ui->applyToButton);
    QAction *allAction = menu->addAction(tr("Apply to All Collections..."));
    QAction *subAction = menu->addAction(tr("Apply to Subcollections Only..."));
    connect(allAction, &QAction::triggered, this,
            &SettingsDialog::applyCurrentSettingsToAllCollections);
    connect(subAction, &QAction::triggered, this,
            &SettingsDialog::applyCurrentSettingsToSubcollections);
    ui->applyToButton->setMenu(menu);
  }
  // Kartend-enq: wire the Settings Mode selector. Default is `Current` to
  // preserve legacy single-collection save behavior.
  if (ui->settingsScopeComboBox) {
    ui->settingsScopeComboBox->setCurrentIndex(static_cast<int>(m_settingsScope));
    connect(ui->settingsScopeComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::onSettingsScopeChanged);
  }
  connect(ui->browseLauncherButton, &QPushButton::clicked, this, &SettingsDialog::browseLauncher);
  connect(ui->browseCoreButton, &QPushButton::clicked, this, &SettingsDialog::browseCore);
  connect(ui->browseMediaDirButton, &QPushButton::clicked, this, &SettingsDialog::browseMediaDir);
  connect(ui->browseArtworkDirButton, &QPushButton::clicked, this,
          &SettingsDialog::browseArtworkDir);
  if (ui->browseVideoDirButton) {
    connect(ui->browseVideoDirButton, &QPushButton::clicked, this,
            &SettingsDialog::browseVideoDir);
  }
  if (ui->browsePlaceholderArtworkButton) {
    connect(ui->browsePlaceholderArtworkButton, &QPushButton::clicked, this,
            &SettingsDialog::browsePlaceholderArtwork);
  }
  if (ui->recursiveImportContentButton) {
    connect(ui->recursiveImportContentButton, &QPushButton::clicked, this,
            &SettingsDialog::onRecursiveImportContent);
  }
}

void SettingsDialog::setupConnections() {
  setupButtonConnections();
  setupBasicUIConnections();
  setupFormFieldConnections();
  setupSpacingConnections();
  setupTreeWidgetConnections();
  setupUIConstraints();
  setupGeneralSettingsConnections();
}

void SettingsDialog::onSettingsScopeChanged(int comboIndex) {
  // Kartend-enq: clamp combo index defensively in case the .ui file is
  // edited and adds/removes entries; only emit when the scope actually
  // changes so dependent UI doesn't churn.
  SettingsScope newScope = SettingsScope::Current;
  switch (comboIndex) {
  case 1:
    newScope = SettingsScope::CurrentAndSubcollections;
    break;
  case 2:
    newScope = SettingsScope::All;
    break;
  default:
    newScope = SettingsScope::Current;
    break;
  }
  if (newScope == m_settingsScope) {
    return;
  }
  m_settingsScope = newScope;
  applyScopeFieldGating();
  emit settingsScopeChanged(m_settingsScope);
}

void SettingsDialog::applyScopeFieldGating() {
  // Kartend-c06: when the user picks a wider scope, edits to fields outside
  // the curated propagation subset only ever affect the currently-selected
  // collection. Disable those controls so the UI matches the propagation
  // behavior — paths, extensions, launcher/core, extract & scan flags, and
  // parent linkage. The list mirrors copyAppearanceAndLayoutFields()'s
  // exclusions in settingsdialogtree.cpp.
  const bool enabled = (m_settingsScope == SettingsScope::Current);
  QWidget *const gatedFields[] = {
      ui->parentCollectionComboBox,
      ui->mediaDirLineEdit,
      ui->browseMediaDirButton,
      ui->recursiveImportContentButton,
      ui->artworkDirLineEdit,
      ui->browseArtworkDirButton,
      ui->videoDirLineEdit,
      ui->browseVideoDirButton,
      ui->placeholderArtworkLineEdit,
      ui->browsePlaceholderArtworkButton,
      ui->fileExtensionsLineEdit,
      ui->launcherLineEdit,
      ui->browseLauncherButton,
      ui->coreLineEdit,
      ui->browseCoreButton,
      ui->launchParamsLineEdit,
      ui->extractArchivesCheckBox,
      ui->extractedExtensionLineEdit,
      ui->includeContentSubfoldersCheckBox,
      ui->includeArtworkSubfoldersCheckBox,
      ui->showAllSubcollectionItemsCheckBox,
      ui->showAllSubfolderItemsCheckBox,
      ui->hideSubfolderTitlesCheckBox,
      ui->showHiddenFoldersCheckBox,
  };
  for (QWidget *w : gatedFields) {
    if (w) {
      w->setEnabled(enabled);
    }
  }
}

auto SettingsDialog::spacingInternalToUi(int spacing) -> int {
  return spacing - UIConstants::Viewport::SPACING_MIN;
}

auto SettingsDialog::spacingUiToInternal(int spacing) -> int {
  return spacing + UIConstants::Viewport::SPACING_MIN;
}

// Add a new collection, optionally inheriting from current selection;
// initialize defaults.
auto SettingsDialog::promptUnsavedChanges(const QString &actionDescription)
    -> QMessageBox::StandardButton {
  QMessageBox messageBox(this);
  messageBox.setIcon(QMessageBox::Warning);
  messageBox.setWindowTitle(tr("Unsaved Changes"));
  messageBox.setText(tr("Save changes before %1?").arg(actionDescription.trimmed()));
  messageBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
  messageBox.setDefaultButton(QMessageBox::Save);
  return static_cast<QMessageBox::StandardButton>(messageBox.exec());
}

void SettingsDialog::emitGridWidthChanged() {
  if (m_gridWidthChangedForActiveCollection && originalCurrentCollectionIndex >= 0 &&
      originalCurrentCollectionIndex < collections.size()) {
    emit gridWidthChanged(originalCurrentCollectionIndex, m_newGridWidthForActiveCollection);
    m_gridWidthChangedForActiveCollection = false;
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
