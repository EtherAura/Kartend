// Sibling translation unit for SettingsDialog: setupGeneralSettingsConnections.
// Extracted from settingsdialogsetup.cpp during LOC-reduction refactor.
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFontDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QtGlobal>

#include "extensionutils.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "mainwindow.h"
#include "scrollmanager.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "ui_settingsdialog.h"
#include "uiconstants.h"

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

  if (ui->startupCollectionComboBox) {
    connect(ui->startupCollectionComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int /*index*/) {
              auto *mainWindow = qobject_cast<MainWindow *>(parent());
              if ((mainWindow) && (mainWindow->getSettingsManager())) {
                mainWindow->m_generalSettings.startupCollection =
                    ui->startupCollectionComboBox->currentData().toString();
                mainWindow->getSettingsManager()->saveGeneralSettings(
                    mainWindow->m_generalSettings);
                m_generalSettings = mainWindow->m_generalSettings;
              }
            });
  }

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

