// Sibling translation unit for SettingsDialog: form/spacing connections,
// field extraction, change detection, browse helpers, load/save.
#include <QAbstractItemView>
#include <QColorDialog>
#include <QDir>
#include <QFileDialog>
#include <QFontDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPixmapCache>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <algorithm>
#include <functional>
#include <set>

#include "extensionutils.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "mainwindow.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "ui_settingsdialog.h"
#include "uiconstants.h"

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

  // Check if database-affecting fields changed before saving
  // These fields affect the UUID or database content and require a rescan
  QString newName = originalCollection.name;
  if (collectionIndexToItem.contains(editedIndex) &&
      collectionIndexToItem[editedIndex]) {
    newName = collectionIndexToItem[editedIndex]->text(0);
  }
  QString newMediaDir = ui->mediaDirLineEdit
                            ? ui->mediaDirLineEdit->text().trimmed()
                            : originalCollection.mediaDirectory;
  QString newExtensions = ui->fileExtensionsLineEdit
                              ? ui->fileExtensionsLineEdit->text().trimmed()
                              : originalCollection.extensions.join(", ");
  bool newIncludeSubfolders =
      ui->includeContentSubfoldersCheckBox
          ? ui->includeContentSubfoldersCheckBox->isChecked()
          : originalCollection.includeContentSubfolders;

  bool databaseFieldsChanged =
      (newName != originalCollection.name) ||
      (newMediaDir != originalCollection.mediaDirectory) ||
      (newExtensions != originalCollection.extensions.join(", ")) ||
      (newIncludeSubfolders != originalCollection.includeContentSubfolders);

  if (databaseFieldsChanged) {
    m_rescanRequired.insert(editedIndex);
  }

  saveCollectionFromUI(editedIndex);
  originalCollection = collections[editedIndex];

  // Also save general settings (e.g., titleTintSaturation, titleTintLightness)
  saveGeneralSettingsFromUI();
  m_originalGeneralSettings = m_generalSettings;

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
  if (ui->extractArchivesCheckBox) {
    connect(ui->extractArchivesCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
    connect(ui->extractArchivesCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::onExtractArchivesToggled);
  }
  if (ui->extractedExtensionLineEdit) {
    connect(ui->extractedExtensionLineEdit, &QLineEdit::textChanged, this,
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
    connect(ui->includeContentSubfoldersCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::onIncludeSubfoldersToggled);
  }
  if (ui->showAllSubfolderItemsCheckBox) {
    connect(ui->showAllSubfolderItemsCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->hideSubfolderTitlesCheckBox) {
    connect(ui->hideSubfolderTitlesCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->showHiddenFoldersCheckBox) {
    connect(ui->showHiddenFoldersCheckBox, &QCheckBox::toggled, this,
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
  if (ui->viewTypeComboBox) {
    connect(ui->viewTypeComboBox,
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
  if (ui->cornerRadiusSpinBox) {
    connect(ui->cornerRadiusSpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  // Color field connections
  if (ui->primaryColorEdit) {
    connect(ui->primaryColorEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->tileColorEdit) {
    connect(ui->tileColorEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->selectionColorEdit) {
    connect(ui->selectionColorEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  // List mode field connections
  if (ui->listFontSizeSpinBox) {
    connect(ui->listFontSizeSpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->listRowHeightSpinBox) {
    connect(ui->listRowHeightSpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->listRowColorEdit) {
    connect(ui->listRowColorEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->listAltRowColorEdit) {
    connect(ui->listAltRowColorEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  // Background field connections
  if (ui->backgroundValueEdit) {
    connect(ui->backgroundValueEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->backgroundColorRadio) {
    connect(ui->backgroundColorRadio, &QRadioButton::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->backgroundImageRadio) {
    connect(ui->backgroundImageRadio, &QRadioButton::toggled, this,
            &SettingsDialog::checkForChanges);
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
  if (!ui->horizontalSpacingSpinBox || !ui->verticalSpacingSpinBox) {
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
    emit spacingChanged(currentCollectionIndex, internalHorizontalSpacing,
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
  if (ui->cornerRadiusSpinBox) {
    ui->cornerRadiusSpinBox->setMinimum(UIConstants::Item::MIN_CORNER_RADIUS);
    ui->cornerRadiusSpinBox->setMaximum(UIConstants::Item::MAX_CORNER_RADIUS);
    ui->cornerRadiusSpinBox->setSingleStep(1);
  }
}

void SettingsDialog::setupGeneralSettingsConnections() {
  connect(ui->rememberSelectionCheckBox, &QCheckBox::toggled, this,
          [this](bool checked) {
            auto *mainWindow = qobject_cast<MainWindow *>(parent());
            if ((mainWindow) && (mainWindow->getSettingsManager())) {
              mainWindow->m_generalSettings.rememberSelection = checked;
              mainWindow->getSettingsManager()->saveGeneralSettings(
                  mainWindow->m_generalSettings);
              m_generalSettings = mainWindow->m_generalSettings;
            }
          });

  connect(ui->wrapNavigationCheckBox, &QCheckBox::toggled, this,
          [this](bool checked) {
            auto *mainWindow = qobject_cast<MainWindow *>(parent());
            if ((mainWindow) && (mainWindow->getSettingsManager())) {
              mainWindow->m_generalSettings.wrapNavigation = checked;
              mainWindow->getSettingsManager()->saveGeneralSettings(
                  mainWindow->m_generalSettings);
              m_generalSettings = mainWindow->m_generalSettings;
            }
          });

  // Browse font button for per-collection custom font
  connect(ui->browseFontButton, &QPushButton::clicked, this, [this]() {
    bool ok;
    QFont currentFont = QApplication::font();
    // Initialize font size from the font size spinbox (grid mode font size)
    if (ui->fontSizeSpinBox) {
      currentFont.setPointSize(ui->fontSizeSpinBox->value());
    }
    QString currentFamily = ui->customFontEdit->text().trimmed();
    if (!currentFamily.isEmpty()) {
      currentFont.setFamily(currentFamily);
    }
    QFont font =
        QFontDialog::getFont(&ok, currentFont, this, tr("Select Font"));
    if (ok) {
      ui->customFontEdit->setText(font.family());
      checkForChanges();
    }
  });

  connect(ui->browseColorButton, &QPushButton::clicked, this, [this]() {
    QColor currentColor = Qt::white;
    if (!m_generalSettings.titleBaseColor.isEmpty()) {
      currentColor = QColor(m_generalSettings.titleBaseColor);
    }
    QColor color =
        QColorDialog::getColor(currentColor, this, tr("Select Base Color"));
    if (color.isValid()) {
      ui->baseColorEdit->setText(color.name());
      // Save immediately like checkbox settings
      auto *mainWindow = qobject_cast<MainWindow *>(parent());
      if (mainWindow && mainWindow->getSettingsManager()) {
        mainWindow->m_generalSettings.titleBaseColor = color.name();
        mainWindow->getSettingsManager()->saveGeneralSettings(
            mainWindow->m_generalSettings);
        m_generalSettings = mainWindow->m_generalSettings;
        // Apply to ItemWidget immediately
        ItemWidget::setTitleBaseColor(color.name());
      }
    }
  });

  // Connect customFontEdit to change tracking (per-collection setting)
  if (ui->customFontEdit) {
    connect(ui->customFontEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::checkForChanges);
  }

  connect(ui->baseColorEdit, &QLineEdit::editingFinished, this, [this]() {
    auto *mainWindow = qobject_cast<MainWindow *>(parent());
    if (mainWindow && mainWindow->getSettingsManager()) {
      QString baseColor = ui->baseColorEdit->text().trimmed();
      if (baseColor != mainWindow->m_generalSettings.titleBaseColor) {
        mainWindow->m_generalSettings.titleBaseColor = baseColor;
        mainWindow->getSettingsManager()->saveGeneralSettings(
            mainWindow->m_generalSettings);
        m_generalSettings = mainWindow->m_generalSettings;
        ItemWidget::setTitleBaseColor(baseColor);
      }
    }
  });

  // Background type radio buttons - update button text based on selection
  connect(ui->backgroundColorRadio, &QRadioButton::toggled, this,
          [this](bool checked) {
            if (checked) {
              ui->browseBackgroundButton->setText(tr("Pick..."));
              ui->browseBackgroundButton->setToolTip(
                  tr("Select background color"));
              ui->backgroundValueEdit->setPlaceholderText(tr("Default"));
              ui->backgroundValueEdit->setToolTip(
                  tr("Background color (hex format like #FF5500)"));
            }
          });

  connect(ui->backgroundImageRadio, &QRadioButton::toggled, this,
          [this](bool checked) {
            if (checked) {
              ui->browseBackgroundButton->setText(tr("Browse..."));
              ui->browseBackgroundButton->setToolTip(
                  tr("Select background image"));
              ui->backgroundValueEdit->setPlaceholderText(tr("None"));
              ui->backgroundValueEdit->setToolTip(
                  tr("Path to background image file"));
            }
          });

  // Background picker button - opens color dialog or file dialog based on radio
  // selection
  connect(ui->browseBackgroundButton, &QPushButton::clicked, this, [this]() {
    if (ui->backgroundColorRadio->isChecked()) {
      // Color picker
      QColor currentColor = Qt::white;
      QString currentValue = ui->backgroundValueEdit->text().trimmed();
      if (!currentValue.isEmpty()) {
        currentColor = QColor(currentValue);
      }
      QColor color = QColorDialog::getColor(currentColor, this,
                                            tr("Select Background Color"));
      if (color.isValid()) {
        ui->backgroundValueEdit->setText(color.name());
      }
    } else {
      // Image file picker
      QString currentPath = ui->backgroundValueEdit->text().trimmed();
      QString startDir = currentPath.isEmpty()
                             ? QDir::homePath()
                             : QFileInfo(currentPath).absolutePath();
      QString filePath = QFileDialog::getOpenFileName(
          this, tr("Select Background Image"), startDir,
          tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*)"));
      if (!filePath.isEmpty()) {
        ui->backgroundValueEdit->setText(filePath);
      }
    }
  });

  // Primary color picker button
  connect(ui->browsePrimaryColorButton, &QPushButton::clicked, this, [this]() {
    QColor currentColor = Qt::gray;
    QString currentValue = ui->primaryColorEdit->text().trimmed();
    if (!currentValue.isEmpty() && QColor::isValidColorName(currentValue)) {
      currentColor = QColor(currentValue);
    }
    QColor color =
        QColorDialog::getColor(currentColor, this, tr("Select Primary Color"));
    if (color.isValid()) {
      ui->primaryColorEdit->setText(color.name());
    }
  });

  // Tile color picker button
  connect(ui->browseTileColorButton, &QPushButton::clicked, this, [this]() {
    QColor currentColor = Qt::gray;
    QString currentValue = ui->tileColorEdit->text().trimmed();
    if (!currentValue.isEmpty() && QColor::isValidColorName(currentValue)) {
      currentColor = QColor(currentValue);
    }
    QColor color =
        QColorDialog::getColor(currentColor, this, tr("Select Tile Color"));
    if (color.isValid()) {
      ui->tileColorEdit->setText(color.name());
    }
  });

  // Selection color picker button
  connect(
      ui->browseSelectionColorButton, &QPushButton::clicked, this, [this]() {
        QColor currentColor = Qt::gray;
        QString currentValue = ui->selectionColorEdit->text().trimmed();
        if (!currentValue.isEmpty() && QColor::isValidColorName(currentValue)) {
          currentColor = QColor(currentValue);
        }
        QColor color = QColorDialog::getColor(currentColor, this,
                                              tr("Select Selection Color"));
        if (color.isValid()) {
          ui->selectionColorEdit->setText(color.name());
        }
      });

  // List mode row color picker button
  connect(ui->browseListRowColorButton, &QPushButton::clicked, this, [this]() {
    QColor currentColor = Qt::gray;
    QString currentValue = ui->listRowColorEdit->text().trimmed();
    if (!currentValue.isEmpty() && QColor::isValidColorName(currentValue)) {
      currentColor = QColor(currentValue);
    }
    QColor color =
        QColorDialog::getColor(currentColor, this, tr("Select Row Color"));
    if (color.isValid()) {
      ui->listRowColorEdit->setText(color.name());
    }
  });

  // List mode alternate row color picker button
  connect(
      ui->browseListAltRowColorButton, &QPushButton::clicked, this, [this]() {
        QColor currentColor = Qt::gray;
        QString currentValue = ui->listAltRowColorEdit->text().trimmed();
        if (!currentValue.isEmpty() && QColor::isValidColorName(currentValue)) {
          currentColor = QColor(currentValue);
        }
        QColor color = QColorDialog::getColor(currentColor, this,
                                              tr("Select Alternate Row Color"));
        if (color.isValid()) {
          ui->listAltRowColorEdit->setText(color.name());
        }
      });

  auto markChanged = [this]() { checkForChanges(); };
  if (ui->keyNavUpEdit) {
    connect(ui->keyNavUpEdit, &QKeySequenceEdit::keySequenceChanged, this,
            markChanged);
  }
  if (ui->keyNavDownEdit) {
    connect(ui->keyNavDownEdit, &QKeySequenceEdit::keySequenceChanged, this,
            markChanged);
  }
  if (ui->keyNavLeftEdit) {
    connect(ui->keyNavLeftEdit, &QKeySequenceEdit::keySequenceChanged, this,
            markChanged);
  }
  if (ui->keyNavRightEdit) {
    connect(ui->keyNavRightEdit, &QKeySequenceEdit::keySequenceChanged, this,
            markChanged);
  }
  if (ui->keyConfirmEdit) {
    connect(ui->keyConfirmEdit, &QKeySequenceEdit::keySequenceChanged, this,
            markChanged);
  }
  if (ui->keyBackEdit) {
    connect(ui->keyBackEdit, &QKeySequenceEdit::keySequenceChanged, this,
            markChanged);
  }
  if (ui->keySearchEdit) {
    connect(ui->keySearchEdit, &QKeySequenceEdit::keySequenceChanged, this,
            markChanged);
  }

  if (ui->gamepadConfirmButtonLineEdit) {
    connect(ui->gamepadConfirmButtonLineEdit, &QLineEdit::textChanged, this,
            markChanged);
  }
  if (ui->gamepadBackButtonLineEdit) {
    connect(ui->gamepadBackButtonLineEdit, &QLineEdit::textChanged, this,
            markChanged);
  }
  if (ui->gamepadToggleSidebarButtonLineEdit) {
    connect(ui->gamepadToggleSidebarButtonLineEdit, &QLineEdit::textChanged,
            this, markChanged);
  }
  if (ui->detectGamepadConfirmButtonButton) {
    connect(
        ui->detectGamepadConfirmButtonButton, &QPushButton::clicked, this,
        [this]() { startGamepadButtonCapture(GamepadCaptureTarget::Confirm); });
  }
  if (ui->detectGamepadBackButtonButton) {
    connect(
        ui->detectGamepadBackButtonButton, &QPushButton::clicked, this,
        [this]() { startGamepadButtonCapture(GamepadCaptureTarget::Back); });
  }
  if (ui->detectGamepadToggleSidebarButtonButton) {
    connect(ui->detectGamepadToggleSidebarButtonButton, &QPushButton::clicked,
            this, [this]() {
              startGamepadButtonCapture(GamepadCaptureTarget::ToggleSidebar);
            });
  }
  if (ui->gamepadUseDpadCheckBox) {
    connect(ui->gamepadUseDpadCheckBox, &QCheckBox::toggled, this, markChanged);
  }
  if (ui->gamepadUseLeftStickCheckBox) {
    connect(ui->gamepadUseLeftStickCheckBox, &QCheckBox::toggled, this,
            markChanged);
  }

  // General settings spinbox connections for change detection
  if (ui->pixmapCacheSpinBox) {
    connect(ui->pixmapCacheSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::checkForChanges);
  }
  if (ui->keyboardSpeedSpinBox) {
    connect(ui->keyboardSpeedSpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->keyboardRepeatDelaySpinBox) {
    connect(ui->keyboardRepeatDelaySpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->clickHoldDelaySpinBox) {
    connect(ui->clickHoldDelaySpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->clickHoldRepeatIntervalSpinBox) {
    connect(ui->clickHoldRepeatIntervalSpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->listKeyboardRepeatSpinBox) {
    connect(ui->listKeyboardRepeatSpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->listClickHoldRepeatSpinBox) {
    connect(ui->listClickHoldRepeatSpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->mouseWheelSpeedSpinBox) {
    connect(ui->mouseWheelSpeedSpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->scrollAnimationSpeedSpinBox) {
    connect(ui->scrollAnimationSpeedSpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->titleSaturationSpinBox) {
    connect(ui->titleSaturationSpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->titleLightnessSpinBox) {
    connect(ui->titleLightnessSpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
}

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

  config.launcherPath = (ui->launcherLineEdit) ? ui->launcherLineEdit->text()
                                               : config.launcherPath;
  config.corePath =
      (ui->coreLineEdit) ? ui->coreLineEdit->text() : config.corePath;
  config.launchParameters = (ui->launchParamsLineEdit)
                                ? ui->launchParamsLineEdit->text()
                                : config.launchParameters;
  config.extractArchives = (ui->extractArchivesCheckBox)
                               ? ui->extractArchivesCheckBox->isChecked()
                               : config.extractArchives;
  config.extractedExtension = (ui->extractedExtensionLineEdit)
                                  ? ui->extractedExtensionLineEdit->text()
                                  : config.extractedExtension;
  config.mediaDirectory = (ui->mediaDirLineEdit) ? ui->mediaDirLineEdit->text()
                                                 : config.mediaDirectory;
  config.artworkDirectory = (ui->artworkDirLineEdit)
                                ? ui->artworkDirLineEdit->text()
                                : config.artworkDirectory;
  config.includeContentSubfolders =
      (ui->includeContentSubfoldersCheckBox)
          ? ui->includeContentSubfoldersCheckBox->isChecked()
          : config.includeContentSubfolders;
  config.showAllSubfolderItems =
      (ui->showAllSubfolderItemsCheckBox)
          ? ui->showAllSubfolderItemsCheckBox->isChecked()
          : config.showAllSubfolderItems;
  config.hideSubfolderTitles =
      (ui->hideSubfolderTitlesCheckBox)
          ? ui->hideSubfolderTitlesCheckBox->isChecked()
          : config.hideSubfolderTitles;
  config.showHiddenFolders = (ui->showHiddenFoldersCheckBox)
                                 ? ui->showHiddenFoldersCheckBox->isChecked()
                                 : config.showHiddenFolders;
  config.includeArtworkSubfolders =
      (ui->includeArtworkSubfoldersCheckBox)
          ? ui->includeArtworkSubfoldersCheckBox->isChecked()
          : config.includeArtworkSubfolders;
  config.itemWidth =
      (ui->itemWidthSpinBox) ? ui->itemWidthSpinBox->value() : config.itemWidth;
  config.itemHeight = (ui->itemHeightSpinBox) ? ui->itemHeightSpinBox->value()
                                              : config.itemHeight;
  config.fontSize =
      (ui->fontSizeSpinBox) ? ui->fontSizeSpinBox->value() : config.fontSize;
  config.cornerRadius = (ui->cornerRadiusSpinBox)
                            ? ui->cornerRadiusSpinBox->value()
                            : config.cornerRadius;
  config.extensions = (ui->fileExtensionsLineEdit)
                          ? ExtensionUtils::parseUserExtensionList(
                                ui->fileExtensionsLineEdit->text())
                          : config.extensions;
  config.gridWidth =
      (ui->gridWidthSpinBox) ? ui->gridWidthSpinBox->value() : config.gridWidth;
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
  config.viewType =
      (ui->viewTypeComboBox)
          ? static_cast<ViewType>(ui->viewTypeComboBox->currentIndex())
          : config.viewType;
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

  // Background settings
  if (ui->backgroundImageRadio && ui->backgroundColorRadio) {
    config.backgroundType = ui->backgroundImageRadio->isChecked()
                                ? BackgroundType::Image
                                : BackgroundType::Color;
  }
  if (ui->backgroundValueEdit) {
    QString value = ui->backgroundValueEdit->text().trimmed();
    if (config.backgroundType == BackgroundType::Image) {
      config.backgroundImage = value;
      // Clear color when switching to image mode
      config.backgroundColor.clear();
    } else {
      config.backgroundColor = value;
      // Clear image when switching to color mode
      config.backgroundImage.clear();
    }
  }

  // Primary color setting
  config.primaryColor = (ui->primaryColorEdit)
                            ? ui->primaryColorEdit->text().trimmed()
                            : config.primaryColor;

  // Tile color setting
  config.tileColor = (ui->tileColorEdit) ? ui->tileColorEdit->text().trimmed()
                                         : config.tileColor;

  // Selection color setting
  config.selectionColor = (ui->selectionColorEdit)
                              ? ui->selectionColorEdit->text().trimmed()
                              : config.selectionColor;

  // List mode settings
  config.listFontSize = (ui->listFontSizeSpinBox)
                            ? ui->listFontSizeSpinBox->value()
                            : config.listFontSize;
  config.listRowHeight = (ui->listRowHeightSpinBox)
                             ? ui->listRowHeightSpinBox->value()
                             : config.listRowHeight;
  config.listRowColor = (ui->listRowColorEdit)
                            ? ui->listRowColorEdit->text().trimmed()
                            : config.listRowColor;
  config.listAltRowColor = (ui->listAltRowColorEdit)
                               ? ui->listAltRowColorEdit->text().trimmed()
                               : config.listAltRowColor;

  // Custom font family (per-collection)
  config.customFontFamily = (ui->customFontEdit)
                                ? ui->customFontEdit->text().trimmed()
                                : config.customFontFamily;

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
      ((ui->extractArchivesCheckBox) &&
       ui->extractArchivesCheckBox->isChecked() !=
           originalConfig.extractArchives) ||
      ((ui->extractedExtensionLineEdit) &&
       ui->extractedExtensionLineEdit->text() !=
           originalConfig.extractedExtension) ||
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
      ((ui->viewTypeComboBox) &&
       ui->viewTypeComboBox->currentIndex() !=
           static_cast<int>(originalConfig.viewType)) ||
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
      ((ui->includeContentSubfoldersCheckBox) &&
       ui->includeContentSubfoldersCheckBox->isChecked() !=
           originalConfig.includeContentSubfolders) ||
      ((ui->showAllSubfolderItemsCheckBox) &&
       ui->showAllSubfolderItemsCheckBox->isChecked() !=
           originalConfig.showAllSubfolderItems) ||
      ((ui->hideSubfolderTitlesCheckBox) &&
       ui->hideSubfolderTitlesCheckBox->isChecked() !=
           originalConfig.hideSubfolderTitles) ||
      ((ui->showHiddenFoldersCheckBox) &&
       ui->showHiddenFoldersCheckBox->isChecked() !=
           originalConfig.showHiddenFolders) ||
      ((ui->includeArtworkSubfoldersCheckBox) &&
       ui->includeArtworkSubfoldersCheckBox->isChecked() !=
           originalConfig.includeArtworkSubfolders) ||
      ((ui->fontSizeSpinBox) &&
       ui->fontSizeSpinBox->value() != originalConfig.fontSize) ||
      ((ui->cornerRadiusSpinBox) &&
       ui->cornerRadiusSpinBox->value() != originalConfig.cornerRadius));
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
          ui->itemHeightSpinBox->value() != originalCollection.itemHeight ||
          (ui->cornerRadiusSpinBox && ui->cornerRadiusSpinBox->value() !=
                                          originalCollection.cornerRadius));
}

// Checks color field changes
auto SettingsDialog::checkColorChanges() const -> bool {
  const CollectionConfig &originalConfig = originalCollection;
  return (
      ((ui->primaryColorEdit) &&
       ui->primaryColorEdit->text().trimmed() != originalConfig.primaryColor) ||
      ((ui->tileColorEdit) &&
       ui->tileColorEdit->text().trimmed() != originalConfig.tileColor) ||
      ((ui->selectionColorEdit) && ui->selectionColorEdit->text().trimmed() !=
                                       originalConfig.selectionColor));
}

// Checks list mode field changes
auto SettingsDialog::checkListModeChanges() const -> bool {
  const CollectionConfig &originalConfig = originalCollection;
  return (
      ((ui->listFontSizeSpinBox) &&
       ui->listFontSizeSpinBox->value() != originalConfig.listFontSize) ||
      ((ui->listRowHeightSpinBox) &&
       ui->listRowHeightSpinBox->value() != originalConfig.listRowHeight) ||
      ((ui->listRowColorEdit) &&
       ui->listRowColorEdit->text().trimmed() != originalConfig.listRowColor) ||
      ((ui->listAltRowColorEdit) && ui->listAltRowColorEdit->text().trimmed() !=
                                        originalConfig.listAltRowColor) ||
      ((ui->customFontEdit) && ui->customFontEdit->text().trimmed() !=
                                   originalConfig.customFontFamily));
}

// Checks background field changes
auto SettingsDialog::checkBackgroundChanges() const -> bool {
  const CollectionConfig &originalConfig = originalCollection;
  // Check background type
  if (ui->backgroundImageRadio && ui->backgroundColorRadio) {
    BackgroundType currentType = ui->backgroundImageRadio->isChecked()
                                     ? BackgroundType::Image
                                     : BackgroundType::Color;
    if (currentType != originalConfig.backgroundType) {
      return true;
    }
  }
  // Check background value
  if (ui->backgroundValueEdit) {
    QString currentValue = ui->backgroundValueEdit->text().trimmed();
    // Compare against the appropriate original field based on type
    if (ui->backgroundImageRadio && ui->backgroundImageRadio->isChecked()) {
      if (currentValue != originalConfig.backgroundImage) {
        return true;
      }
    } else {
      if (currentValue != originalConfig.backgroundColor) {
        return true;
      }
    }
  }
  return false;
}

auto SettingsDialog::checkGeneralSettingsChanges() const -> bool {
  // Check text appearance settings (saturation, lightness, base color)
  if (ui->titleSaturationSpinBox &&
      ui->titleSaturationSpinBox->value() !=
          m_originalGeneralSettings.titleTintSaturation) {
    return true;
  }
  if (ui->titleLightnessSpinBox &&
      ui->titleLightnessSpinBox->value() !=
          m_originalGeneralSettings.titleTintLightness) {
    return true;
  }
  if (ui->baseColorEdit && ui->baseColorEdit->text().trimmed() !=
                               m_originalGeneralSettings.titleBaseColor) {
    return true;
  }
  return false;
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

void SettingsDialog::saveCollectionFromUI(int index) {
  if (!CollectionUtils::isValidIndex(index, m_workingCollections)) {
    return;
  }

  CollectionConfig collection = extractUIFieldValues();

  // Validate paths for security before saving
  // Check each path that could be used for file operations
  auto validatePath = [this](const QString &path,
                             const QString &fieldName) -> bool {
    if (path.isEmpty()) {
      return true; // Empty paths are allowed (optional fields)
    }
    auto result = PathUtils::validatePathSecurity(path);
    if (result.isError()) {
      QMessageBox::warning(
          this, tr("Invalid Path"),
          tr("The %1 contains invalid characters:\n\n%2\n\n"
             "Please remove shell metacharacters, backslashes, "
             "or other special characters.")
              .arg(fieldName, result.error().message));
      return false;
    }
    return true;
  };

  if (!validatePath(collection.mediaDirectory, tr("Media Directory"))) {
    return;
  }
  if (!validatePath(collection.artworkDirectory, tr("Artwork Directory"))) {
    return;
  }
  if (!validatePath(collection.launcherPath, tr("Launcher Path"))) {
    return;
  }
  if (!validatePath(collection.corePath, tr("Core Path"))) {
    return;
  }
  if (!validatePath(collection.backgroundImage, tr("Background Image"))) {
    return;
  }

  updateParentCollectionFromUI(collection, index);

  m_workingCollections[index] = collection;
  collections = m_workingCollections;
  originalCollection = collection;
  m_collectionSaved = true;
  updateSaveButtonStyle();
}

auto SettingsDialog::hasUnsavedChanges() const -> bool {
  // General settings can be changed without a collection selected
  if (checkGeneralSettingsChanges()) {
    return true;
  }

  if (currentCollectionIndex < 0 ||
      currentCollectionIndex >= collections.size()) {
    return false;
  }

  return checkBasicFieldChanges() || checkExtensionChanges() ||
         checkTreeNameChanges() || checkParentCollectionChanges() ||
         checkDimensionChanges() || checkColorChanges() ||
         checkListModeChanges() || checkBackgroundChanges();
}

void SettingsDialog::updateSaveButtonStyle() {
  ui->saveCollectionButton->setEnabled(hasUnsavedChanges());
}

void SettingsDialog::updateDeleteButtonState() {
  if (ui->removeCollectionButton) {
    // Enable delete when there's a valid collection selected
    bool hasSelection = currentTreeItem != nullptr &&
                        itemToCollectionIndex.contains(currentTreeItem);
    ui->removeCollectionButton->setEnabled(hasSelection &&
                                           !collections.isEmpty());
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

  // Update extract archives visibility based on launcher type
  updateExtractArchivesVisibility();
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

  // Artwork directory is always visible - shell collections can set artwork
  // for subcollections that inherit from the parent
  ui->label_artworkDir->setVisible(true);
  ui->artworkDirLineEdit->setVisible(true);
  ui->browseArtworkDirButton->setVisible(true);

  if (hasContentDir) {
    updateUIForLauncherType(ui->launcherLineEdit->text());
    updateExtractArchivesVisibility();
  } else {
    ui->label_core->setVisible(false);
    ui->coreLineEdit->setVisible(false);
    ui->browseCoreButton->setVisible(false);
    ui->label_extractArchives->setVisible(false);
    ui->extractArchivesCheckBox->setVisible(false);
    ui->label_extractedExtension->setVisible(false);
    ui->extractedExtensionLineEdit->setVisible(false);
  }

  ui->label_sidebarMode->setVisible(true);
  ui->sidebarModeComboBox->setVisible(true);
}

void SettingsDialog::updateExtractArchivesVisibility() {
  bool isRetroArch =
      ui->launcherLineEdit->text().contains("retroarch", Qt::CaseInsensitive);
  bool extractEnabled = ui->extractArchivesCheckBox->isChecked();

  // Show extract archives option only for RetroArch launchers
  ui->label_extractArchives->setVisible(isRetroArch);
  ui->extractArchivesCheckBox->setVisible(isRetroArch);

  // Show extracted extension field only when extraction is enabled
  ui->label_extractedExtension->setVisible(isRetroArch && extractEnabled);
  ui->extractedExtensionLineEdit->setVisible(isRetroArch && extractEnabled);
}

void SettingsDialog::onExtractArchivesToggled(bool checked) {
  Q_UNUSED(checked)
  updateExtractArchivesVisibility();
}

void SettingsDialog::updateSidebarModeVisibility() {
  ui->label_sidebarMode->setVisible(true);
  ui->sidebarModeComboBox->setVisible(true);
}

void SettingsDialog::updateGridWidthLimits() {
  if (!ui->gridWidthSpinBox) {
    return;
  }
  int preservedValue = ui->gridWidthSpinBox->value();
  ui->gridWidthSpinBox->setMaximum(UIConstants::Grid::MAX_WIDTH);
  ui->gridWidthSpinBox->setValue(preservedValue);
}

void SettingsDialog::onGridWidthChanged(int value) {
  Q_UNUSED(value)
  checkForChanges();
  if (!m_isLoading &&
      currentCollectionIndex == originalCurrentCollectionIndex &&
      originalCurrentCollectionIndex >= 0 &&
      originalCurrentCollectionIndex < collections.size()) {
    emit gridWidthChanged(currentCollectionIndex, value);
  }
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
  if (ui->keyboardSpeedSpinBox) {
    ui->keyboardSpeedSpinBox->blockSignals(true);
    ui->keyboardSpeedSpinBox->setValue(
        m_generalSettings.keyboardRepeatIntervalMs);
    ui->keyboardSpeedSpinBox->blockSignals(false);
  }
  if (ui->keyboardRepeatDelaySpinBox) {
    ui->keyboardRepeatDelaySpinBox->blockSignals(true);
    ui->keyboardRepeatDelaySpinBox->setValue(
        m_generalSettings.keyboardRepeatDelayMs);
    ui->keyboardRepeatDelaySpinBox->blockSignals(false);
  }
  if (ui->clickHoldDelaySpinBox) {
    ui->clickHoldDelaySpinBox->blockSignals(true);
    ui->clickHoldDelaySpinBox->setValue(m_generalSettings.clickHoldDelayMs);
    ui->clickHoldDelaySpinBox->blockSignals(false);
  }
  if (ui->clickHoldRepeatIntervalSpinBox) {
    ui->clickHoldRepeatIntervalSpinBox->blockSignals(true);
    ui->clickHoldRepeatIntervalSpinBox->setValue(
        m_generalSettings.clickHoldRepeatIntervalMs);
    ui->clickHoldRepeatIntervalSpinBox->blockSignals(false);
  }
  if (ui->listKeyboardRepeatSpinBox) {
    ui->listKeyboardRepeatSpinBox->blockSignals(true);
    ui->listKeyboardRepeatSpinBox->setValue(
        m_generalSettings.listKeyboardRepeatIntervalMs);
    ui->listKeyboardRepeatSpinBox->blockSignals(false);
  }
  if (ui->listClickHoldRepeatSpinBox) {
    ui->listClickHoldRepeatSpinBox->blockSignals(true);
    ui->listClickHoldRepeatSpinBox->setValue(
        m_generalSettings.listClickHoldRepeatIntervalMs);
    ui->listClickHoldRepeatSpinBox->blockSignals(false);
  }
  if (ui->mouseWheelSpeedSpinBox) {
    ui->mouseWheelSpeedSpinBox->blockSignals(true);
    ui->mouseWheelSpeedSpinBox->setValue(m_generalSettings.mouseWheelRows);
    ui->mouseWheelSpeedSpinBox->blockSignals(false);
  }
  if (ui->scrollAnimationSpeedSpinBox) {
    ui->scrollAnimationSpeedSpinBox->blockSignals(true);
    ui->scrollAnimationSpeedSpinBox->setValue(
        m_generalSettings.scrollAnimationDurationMs);
    ui->scrollAnimationSpeedSpinBox->blockSignals(false);
  }
  if (ui->titleSaturationSpinBox) {
    ui->titleSaturationSpinBox->blockSignals(true);
    ui->titleSaturationSpinBox->setValue(m_generalSettings.titleTintSaturation);
    ui->titleSaturationSpinBox->blockSignals(false);
  }
  if (ui->titleLightnessSpinBox) {
    ui->titleLightnessSpinBox->blockSignals(true);
    ui->titleLightnessSpinBox->setValue(m_generalSettings.titleTintLightness);
    ui->titleLightnessSpinBox->blockSignals(false);
  }
  if (ui->baseColorEdit) {
    ui->baseColorEdit->blockSignals(true);
    ui->baseColorEdit->setText(m_generalSettings.titleBaseColor);
    ui->baseColorEdit->blockSignals(false);
  }
  // Note: customFontEdit is now loaded per-collection in loadCollectionFields()

  auto setKeyEdit = [](QKeySequenceEdit *edit, int key) {
    if (!edit) {
      return;
    }
    edit->blockSignals(true);
    edit->setKeySequence(QKeySequence(key));
    edit->blockSignals(false);
  };

  setKeyEdit(ui->keyNavUpEdit, m_generalSettings.keyNavUp);
  setKeyEdit(ui->keyNavDownEdit, m_generalSettings.keyNavDown);
  setKeyEdit(ui->keyNavLeftEdit, m_generalSettings.keyNavLeft);
  setKeyEdit(ui->keyNavRightEdit, m_generalSettings.keyNavRight);
  setKeyEdit(ui->keyConfirmEdit, m_generalSettings.keyConfirm);
  setKeyEdit(ui->keyBackEdit, m_generalSettings.keyBack);
  setKeyEdit(ui->keySearchEdit, m_generalSettings.keySearch);

  if (ui->gamepadUseDpadCheckBox) {
    ui->gamepadUseDpadCheckBox->blockSignals(true);
    ui->gamepadUseDpadCheckBox->setChecked(m_generalSettings.gamepadUseDpad);
    ui->gamepadUseDpadCheckBox->blockSignals(false);
  }
  if (ui->gamepadUseLeftStickCheckBox) {
    ui->gamepadUseLeftStickCheckBox->blockSignals(true);
    ui->gamepadUseLeftStickCheckBox->setChecked(
        m_generalSettings.gamepadUseLeftStick);
    ui->gamepadUseLeftStickCheckBox->blockSignals(false);
  }
  if (ui->gamepadConfirmButtonLineEdit) {
    ui->gamepadConfirmButtonLineEdit->blockSignals(true);
    ui->gamepadConfirmButtonLineEdit->setText(
        m_generalSettings.gamepadConfirmButton);
    ui->gamepadConfirmButtonLineEdit->blockSignals(false);
  }
  if (ui->gamepadBackButtonLineEdit) {
    ui->gamepadBackButtonLineEdit->blockSignals(true);
    ui->gamepadBackButtonLineEdit->setText(m_generalSettings.gamepadBackButton);
    ui->gamepadBackButtonLineEdit->blockSignals(false);
  }
  if (ui->gamepadToggleSidebarButtonLineEdit) {
    ui->gamepadToggleSidebarButtonLineEdit->blockSignals(true);
    ui->gamepadToggleSidebarButtonLineEdit->setText(
        m_generalSettings.gamepadToggleSidebarButton);
    ui->gamepadToggleSidebarButtonLineEdit->blockSignals(false);
  }

  // Store original general settings for change detection
  m_originalGeneralSettings = m_generalSettings;

  updateGamepadCaptureUi();
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
    if (ui->keyboardSpeedSpinBox) {
      mainWindow->m_generalSettings.keyboardRepeatIntervalMs =
          ui->keyboardSpeedSpinBox->value();
    }
    if (ui->keyboardRepeatDelaySpinBox) {
      mainWindow->m_generalSettings.keyboardRepeatDelayMs =
          ui->keyboardRepeatDelaySpinBox->value();
    }
    if (ui->clickHoldDelaySpinBox) {
      mainWindow->m_generalSettings.clickHoldDelayMs =
          ui->clickHoldDelaySpinBox->value();
    }
    if (ui->clickHoldRepeatIntervalSpinBox) {
      mainWindow->m_generalSettings.clickHoldRepeatIntervalMs =
          ui->clickHoldRepeatIntervalSpinBox->value();
    }
    if (ui->listKeyboardRepeatSpinBox) {
      mainWindow->m_generalSettings.listKeyboardRepeatIntervalMs =
          ui->listKeyboardRepeatSpinBox->value();
    }
    if (ui->listClickHoldRepeatSpinBox) {
      mainWindow->m_generalSettings.listClickHoldRepeatIntervalMs =
          ui->listClickHoldRepeatSpinBox->value();
    }
    if (ui->mouseWheelSpeedSpinBox) {
      mainWindow->m_generalSettings.mouseWheelRows =
          ui->mouseWheelSpeedSpinBox->value();
    }
    if (ui->scrollAnimationSpeedSpinBox) {
      mainWindow->m_generalSettings.scrollAnimationDurationMs =
          ui->scrollAnimationSpeedSpinBox->value();
    }
    if (ui->titleSaturationSpinBox) {
      mainWindow->m_generalSettings.titleTintSaturation =
          ui->titleSaturationSpinBox->value();
      // Apply to ItemWidget static settings
      ItemWidget::setTitleTintSaturation(ui->titleSaturationSpinBox->value());
    }
    if (ui->titleLightnessSpinBox) {
      mainWindow->m_generalSettings.titleTintLightness =
          ui->titleLightnessSpinBox->value();
      // Apply to ItemWidget static settings
      ItemWidget::setTitleTintLightness(ui->titleLightnessSpinBox->value());
    }
    if (ui->baseColorEdit) {
      mainWindow->m_generalSettings.titleBaseColor =
          ui->baseColorEdit->text().trimmed();
      // Apply to ItemWidget static settings
      ItemWidget::setTitleBaseColor(ui->baseColorEdit->text().trimmed());
    }
    // Note: customFontFamily is now saved per-collection, not in general
    // settings

    auto singleKeyFromEdit = [](QKeySequenceEdit *edit,
                                int fallbackKey) -> int {
      if (!edit) {
        return fallbackKey;
      }
      const QKeySequence seq = edit->keySequence();
      if (seq.isEmpty()) {
        return fallbackKey;
      }
      const auto combo = seq[0];
      const int keyOnly = static_cast<int>(combo.key());
      return (keyOnly != 0) ? keyOnly : fallbackKey;
    };

    mainWindow->m_generalSettings.keyNavUp = singleKeyFromEdit(
        ui->keyNavUpEdit, mainWindow->m_generalSettings.keyNavUp);
    mainWindow->m_generalSettings.keyNavDown = singleKeyFromEdit(
        ui->keyNavDownEdit, mainWindow->m_generalSettings.keyNavDown);
    mainWindow->m_generalSettings.keyNavLeft = singleKeyFromEdit(
        ui->keyNavLeftEdit, mainWindow->m_generalSettings.keyNavLeft);
    mainWindow->m_generalSettings.keyNavRight = singleKeyFromEdit(
        ui->keyNavRightEdit, mainWindow->m_generalSettings.keyNavRight);
    mainWindow->m_generalSettings.keyConfirm = singleKeyFromEdit(
        ui->keyConfirmEdit, mainWindow->m_generalSettings.keyConfirm);
    mainWindow->m_generalSettings.keyBack = singleKeyFromEdit(
        ui->keyBackEdit, mainWindow->m_generalSettings.keyBack);
    mainWindow->m_generalSettings.keySearch = singleKeyFromEdit(
        ui->keySearchEdit, mainWindow->m_generalSettings.keySearch);

    if (ui->gamepadUseDpadCheckBox) {
      mainWindow->m_generalSettings.gamepadUseDpad =
          ui->gamepadUseDpadCheckBox->isChecked();
    }
    if (ui->gamepadUseLeftStickCheckBox) {
      mainWindow->m_generalSettings.gamepadUseLeftStick =
          ui->gamepadUseLeftStickCheckBox->isChecked();
    }
    if (ui->gamepadConfirmButtonLineEdit) {
      const QString v = ui->gamepadConfirmButtonLineEdit->text().trimmed();
      if (!v.isEmpty()) {
        mainWindow->m_generalSettings.gamepadConfirmButton = v;
      }
    }
    if (ui->gamepadBackButtonLineEdit) {
      const QString v = ui->gamepadBackButtonLineEdit->text().trimmed();
      if (!v.isEmpty()) {
        mainWindow->m_generalSettings.gamepadBackButton = v;
      }
    }
    if (ui->gamepadToggleSidebarButtonLineEdit) {
      const QString v =
          ui->gamepadToggleSidebarButtonLineEdit->text().trimmed();
      if (!v.isEmpty()) {
        mainWindow->m_generalSettings.gamepadToggleSidebarButton = v;
      }
    }
    mainWindow->getSettingsManager()->saveGeneralSettings(
        mainWindow->m_generalSettings);
    m_generalSettings = mainWindow->m_generalSettings;

    // Refresh all visible widgets to apply text appearance changes immediately
    ScrollManager *scrollManager = mainWindow->getScrollManager();
    if (scrollManager) {
      const auto &activeWidgets = scrollManager->getActiveWidgets();
      for (auto it = activeWidgets.constBegin(); it != activeWidgets.constEnd();
           ++it) {
        ItemWidget *widget = it.value();
        if (widget) {
          widget->applyTitleTint();
        }
      }
    }
  }
}

void SettingsDialog::checkForChanges() {
  // Skip change detection during programmatic loading to avoid
  // false positives before originalCollection is set
  if (m_isLoading) {
    return;
  }
  m_collectionSaved = !hasUnsavedChanges();
  updateSaveButtonStyle();
}

void SettingsDialog::browseLauncher() {
  QString fileName = QFileDialog::getOpenFileName(this, tr("Select Launcher"),
                                                  "", tr("All Files (*)"));
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
  QString dirName =
      QFileDialog::getExistingDirectory(this, tr("Select Media Directory"), "");
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

void SettingsDialog::onIncludeSubfoldersToggled(bool checked) {
  if (ui->subfolderOptionsWidget) {
    ui->subfolderOptionsWidget->setVisible(checked);
  }
}

void SettingsDialog::performRecursiveImport(const QString &baseDir,
                                            bool isContentDir) {
  QDir dir(baseDir);
  if (!dir.exists()) {
    QMessageBox::warning(this, tr("Recursive Import"),
                         tr("The specified directory does not exist."));
    return;
  }

  // Get list of subdirectories
  QStringList subdirs =
      dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  if (subdirs.isEmpty()) {
    QMessageBox::information(
        this, tr("Recursive Import"),
        tr("No subdirectories found in the specified directory."));
    return;
  }

  // Show confirmation dialog
  QString message = tr("This will create %1 subcollection(s) based on the "
                       "directory structure:\n\n")
                        .arg(subdirs.size());
  for (int i = 0; i < qMin(subdirs.size(), 10); ++i) {
    message += QString("  • %1\n").arg(subdirs[i]);
  }
  if (subdirs.size() > 10) {
    message += tr("  ... and %1 more\n").arg(subdirs.size() - 10);
  }
  message += tr(
      "\nEach subcollection will inherit the current collection's settings.\n");
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
  if (currentCollectionIndex < 0 ||
      currentCollectionIndex >= m_workingCollections.size()) {
    return;
  }

  // Save current collection first
  handleSaveCollection(currentCollectionIndex, false);

  // Copy by value to avoid reference invalidation when m_workingCollections is
  // appended to
  const CollectionConfig templateConfig =
      m_workingCollections[currentCollectionIndex];
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

  QMessageBox::information(
      this, tr("Recursive Import"),
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
  if (ui->extractArchivesCheckBox) {
    ui->extractArchivesCheckBox->setChecked(config.extractArchives);
  }
  if (ui->extractedExtensionLineEdit) {
    ui->extractedExtensionLineEdit->setText(config.extractedExtension);
  }
  if (ui->mediaDirLineEdit) {
    ui->mediaDirLineEdit->setText(config.mediaDirectory);
  }
  if (ui->artworkDirLineEdit) {
    ui->artworkDirLineEdit->setText(config.artworkDirectory);
  }
  if (ui->includeContentSubfoldersCheckBox) {
    ui->includeContentSubfoldersCheckBox->setChecked(
        config.includeContentSubfolders);
  }
  if (ui->showAllSubfolderItemsCheckBox) {
    ui->showAllSubfolderItemsCheckBox->setChecked(config.showAllSubfolderItems);
  }
  if (ui->hideSubfolderTitlesCheckBox) {
    ui->hideSubfolderTitlesCheckBox->setChecked(config.hideSubfolderTitles);
  }
  if (ui->showHiddenFoldersCheckBox) {
    ui->showHiddenFoldersCheckBox->setChecked(config.showHiddenFolders);
  }
  if (ui->subfolderOptionsWidget) {
    ui->subfolderOptionsWidget->setVisible(config.includeContentSubfolders);
  }
  if (ui->includeArtworkSubfoldersCheckBox) {
    ui->includeArtworkSubfoldersCheckBox->setChecked(
        config.includeArtworkSubfolders);
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
  if (ui->viewTypeComboBox) {
    ui->viewTypeComboBox->setCurrentIndex(static_cast<int>(config.viewType));
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
    ui->hideVerticalScrollbarCheckBox->setChecked(config.hideVerticalScrollbar);
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
  if (ui->cornerRadiusSpinBox) {
    ui->cornerRadiusSpinBox->setValue(config.cornerRadius);
  }

  // Background settings
  if (ui->backgroundColorRadio && ui->backgroundImageRadio) {
    if (config.backgroundType == BackgroundType::Image) {
      ui->backgroundImageRadio->setChecked(true);
    } else {
      ui->backgroundColorRadio->setChecked(true);
    }
  }
  if (ui->backgroundValueEdit) {
    if (config.backgroundType == BackgroundType::Image) {
      ui->backgroundValueEdit->setText(config.backgroundImage);
    } else {
      ui->backgroundValueEdit->setText(config.backgroundColor);
    }
  }

  // Primary color setting
  if (ui->primaryColorEdit) {
    ui->primaryColorEdit->setText(config.primaryColor);
  }

  // Tile color setting
  if (ui->tileColorEdit) {
    ui->tileColorEdit->setText(config.tileColor);
  }

  // Selection color setting
  if (ui->selectionColorEdit) {
    ui->selectionColorEdit->setText(config.selectionColor);
  }

  // List mode settings
  if (ui->listFontSizeSpinBox) {
    ui->listFontSizeSpinBox->setValue(config.listFontSize);
  }
  if (ui->listRowHeightSpinBox) {
    ui->listRowHeightSpinBox->setValue(config.listRowHeight);
  }
  if (ui->listRowColorEdit) {
    ui->listRowColorEdit->setText(config.listRowColor);
  }
  if (ui->listAltRowColorEdit) {
    ui->listAltRowColorEdit->setText(config.listAltRowColor);
  }

  // Custom font family (per-collection)
  if (ui->customFontEdit) {
    ui->customFontEdit->setText(config.customFontFamily);
  }

  updateParentCollectionComboBox(index);
  updateFieldVisibility();
  updateGridWidthLimits();
  m_isLoading = false;
}

void SettingsDialog::clearCollectionUI() {
  m_isLoading = true;

  if (ui->launcherLineEdit)
    ui->launcherLineEdit->clear();
  if (ui->coreLineEdit)
    ui->coreLineEdit->clear();
  if (ui->launchParamsLineEdit)
    ui->launchParamsLineEdit->clear();
  if (ui->mediaDirLineEdit)
    ui->mediaDirLineEdit->clear();
  if (ui->artworkDirLineEdit)
    ui->artworkDirLineEdit->clear();
  if (ui->fileExtensionsLineEdit)
    ui->fileExtensionsLineEdit->clear();
  if (ui->backgroundValueEdit)
    ui->backgroundValueEdit->clear();
  if (ui->primaryColorEdit)
    ui->primaryColorEdit->clear();
  if (ui->tileColorEdit)
    ui->tileColorEdit->clear();
  if (ui->selectionColorEdit)
    ui->selectionColorEdit->clear();
  if (ui->listRowColorEdit)
    ui->listRowColorEdit->clear();
  if (ui->listAltRowColorEdit)
    ui->listAltRowColorEdit->clear();
  if (ui->customFontEdit)
    ui->customFontEdit->clear();

  if (ui->includeContentSubfoldersCheckBox)
    ui->includeContentSubfoldersCheckBox->setChecked(false);
  if (ui->showAllSubfolderItemsCheckBox)
    ui->showAllSubfolderItemsCheckBox->setChecked(false);
  if (ui->hideSubfolderTitlesCheckBox)
    ui->hideSubfolderTitlesCheckBox->setChecked(false);
  if (ui->showHiddenFoldersCheckBox)
    ui->showHiddenFoldersCheckBox->setChecked(false);
  if (ui->includeArtworkSubfoldersCheckBox)
    ui->includeArtworkSubfoldersCheckBox->setChecked(false);
  if (ui->showAllSubcollectionItemsCheckBox)
    ui->showAllSubcollectionItemsCheckBox->setChecked(false);
  if (ui->hideHorizontalScrollbarCheckBox)
    ui->hideHorizontalScrollbarCheckBox->setChecked(false);
  if (ui->hideVerticalScrollbarCheckBox)
    ui->hideVerticalScrollbarCheckBox->setChecked(false);
  if (ui->hideTitlesCheckBox)
    ui->hideTitlesCheckBox->setChecked(false);
  if (ui->hideSubcollectionTitlesCheckBox)
    ui->hideSubcollectionTitlesCheckBox->setChecked(false);

  if (ui->gridWidthSpinBox)
    ui->gridWidthSpinBox->setValue(UIConstants::Grid::DEFAULT_WIDTH);
  if (ui->horizontalSpacingSpinBox)
    ui->horizontalSpacingSpinBox->setValue(70);
  if (ui->verticalSpacingSpinBox)
    ui->verticalSpacingSpinBox->setValue(0);
  if (ui->itemWidthSpinBox)
    ui->itemWidthSpinBox->setValue(200);
  if (ui->itemHeightSpinBox)
    ui->itemHeightSpinBox->setValue(300);
  if (ui->fontSizeSpinBox)
    ui->fontSizeSpinBox->setValue(12);
  if (ui->cornerRadiusSpinBox)
    ui->cornerRadiusSpinBox->setValue(0);

  if (ui->horizontalAlignmentComboBox)
    ui->horizontalAlignmentComboBox->setCurrentIndex(0);
  if (ui->sidebarModeComboBox)
    ui->sidebarModeComboBox->setCurrentIndex(0);
  if (ui->viewTypeComboBox)
    ui->viewTypeComboBox->setCurrentIndex(0);
  if (ui->parentCollectionComboBox)
    ui->parentCollectionComboBox->clear();

  if (ui->backgroundColorRadio)
    ui->backgroundColorRadio->setChecked(true);
  if (ui->subfolderOptionsWidget)
    ui->subfolderOptionsWidget->setVisible(false);

  m_isLoading = false;
}
