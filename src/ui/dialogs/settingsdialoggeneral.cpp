// Sibling translation unit for SettingsDialog: setupGeneralSettingsConnections.
// Extracted from settingsdialogsetup.cpp during LOC-reduction refactor.
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QtGlobal>
#include <QTimer>

#include "errorutils.h"
#include "extensionutils.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "mainwindow.h"
#include "scrollmanager.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "ui_settingsdialog.h"
#include "uiconstants.h"

namespace {
/// Kartend-88w: list every *.cfg in the Kartend config directory that isn't
/// the live kartend.cfg. Sorted alphabetically so the dropdown order is
/// stable across opens. Returns absolute paths.
QStringList listEligibleConfigProfiles() {
  const QString livePath = SettingsUtils::getConfigPath();
  const QDir configDir = QFileInfo(livePath).absoluteDir();
  QStringList paths;
  if (!configDir.exists()) {
    return paths;
  }
  const QFileInfoList entries =
      configDir.entryInfoList(QStringList() << "*.cfg", QDir::Files, QDir::Name);
  paths.reserve(entries.size());
  const QString liveCanonical = QFileInfo(livePath).canonicalFilePath();
  for (const QFileInfo &entry : entries) {
    // Skip the active config — loading it would be a no-op and only invites
    // confusion. Compare canonical paths so a symlink or relative form still
    // matches.
    if (!liveCanonical.isEmpty() && entry.canonicalFilePath() == liveCanonical) {
      continue;
    }
    paths.append(entry.absoluteFilePath());
  }
  return paths;
}

void populateImportConfigComboBox(QComboBox *combo) {
  if (!combo) {
    return;
  }
  QSignalBlocker blocker(combo);
  combo->clear();
  const QStringList paths = listEligibleConfigProfiles();
  for (const QString &path : paths) {
    combo->addItem(QFileInfo(path).fileName(), path);
  }
}
} // namespace

void SettingsDialog::setupGeneralSettingsConnections() {
  connect(ui->rememberSelectionCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
    auto *mainWindow = qobject_cast<MainWindow *>(parent());
    if ((mainWindow) && (mainWindow->getSettingsManager())) {
      mainWindow->m_generalSettings.rememberSelection = checked;
      mainWindow->getSettingsManager()->saveGeneralSettings(mainWindow->m_generalSettings);
      m_generalSettings = mainWindow->m_generalSettings;
    }
  });

  connect(ui->wrapNavigationCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
    auto *mainWindow = qobject_cast<MainWindow *>(parent());
    if ((mainWindow) && (mainWindow->getSettingsManager())) {
      mainWindow->m_generalSettings.wrapNavigation = checked;
      mainWindow->getSettingsManager()->saveGeneralSettings(mainWindow->m_generalSettings);
      m_generalSettings = mainWindow->m_generalSettings;
    }
  });

  connect(ui->selectItemOnHoverCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
    auto *mainWindow = qobject_cast<MainWindow *>(parent());
    if ((mainWindow) && (mainWindow->getSettingsManager())) {
      mainWindow->m_generalSettings.selectItemOnHover = checked;
      mainWindow->getSettingsManager()->saveGeneralSettings(mainWindow->m_generalSettings);
      m_generalSettings = mainWindow->m_generalSettings;
    }
  });

  if (ui->bootSplashCheckBox) {
    connect(ui->bootSplashCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
      auto *mainWindow = qobject_cast<MainWindow *>(parent());
      if ((mainWindow) && (mainWindow->getSettingsManager())) {
        mainWindow->m_generalSettings.bootSplashEnabled = checked;
        mainWindow->getSettingsManager()->saveGeneralSettings(mainWindow->m_generalSettings);
        m_generalSettings = mainWindow->m_generalSettings;
      }
    });
  }

  if (ui->resumeFocusSplashCheckBox) {
    connect(ui->resumeFocusSplashCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
      auto *mainWindow = qobject_cast<MainWindow *>(parent());
      if ((mainWindow) && (mainWindow->getSettingsManager())) {
        mainWindow->m_generalSettings.resumeFocusSplashEnabled = checked;
        mainWindow->getSettingsManager()->saveGeneralSettings(mainWindow->m_generalSettings);
        m_generalSettings = mainWindow->m_generalSettings;
      }
    });
  }

  if (ui->runtimeDetectionCheckBox) {
    connect(ui->runtimeDetectionCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
      auto *mainWindow = qobject_cast<MainWindow *>(parent());
      if ((mainWindow) && (mainWindow->getSettingsManager())) {
        mainWindow->m_generalSettings.runtimeDetectionEnabled = checked;
        mainWindow->getSettingsManager()->saveGeneralSettings(mainWindow->m_generalSettings);
        m_generalSettings = mainWindow->m_generalSettings;
      }
    });
  }

  // Kartend-fse: launch-history settings save immediately, mirroring the
  // other check/spinbox handlers above. Live save means the next launch
  // picks up the new gate/cap without round-tripping through OK/Apply.
  if (ui->historyEnabledCheckBox) {
    connect(ui->historyEnabledCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
      auto *mainWindow = qobject_cast<MainWindow *>(parent());
      if ((mainWindow) && (mainWindow->getSettingsManager())) {
        mainWindow->m_generalSettings.historyEnabled = checked;
        mainWindow->getSettingsManager()->saveGeneralSettings(mainWindow->m_generalSettings);
        m_generalSettings = mainWindow->m_generalSettings;
      }
    });
  }
  if (ui->historyMaxEntriesSpinBox) {
    connect(ui->historyMaxEntriesSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int value) {
              auto *mainWindow = qobject_cast<MainWindow *>(parent());
              if ((mainWindow) && (mainWindow->getSettingsManager())) {
                mainWindow->m_generalSettings.historyMaxEntries = value;
                mainWindow->getSettingsManager()->saveGeneralSettings(
                    mainWindow->m_generalSettings);
                m_generalSettings = mainWindow->m_generalSettings;
              }
            });
  }

  if (ui->startupCollectionComboBox) {
    connect(ui->startupCollectionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int /*index*/) {
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
    QFont font = QFontDialog::getFont(&ok, currentFont, this, tr("Select Font"));
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
    QColor color = QColorDialog::getColor(currentColor, this, tr("Select Base Color"));
    if (color.isValid()) {
      ui->baseColorEdit->setText(color.name());
      // Save immediately like checkbox settings
      auto *mainWindow = qobject_cast<MainWindow *>(parent());
      if (mainWindow && mainWindow->getSettingsManager()) {
        mainWindow->m_generalSettings.titleBaseColor = color.name();
        mainWindow->getSettingsManager()->saveGeneralSettings(mainWindow->m_generalSettings);
        m_generalSettings = mainWindow->m_generalSettings;
        // Apply to ItemWidget immediately
        ItemWidget::setTitleBaseColor(color.name());
      }
    }
  });

  // Connect customFontEdit to change tracking (per-collection setting)
  if (ui->customFontEdit) {
    connect(ui->customFontEdit, &QLineEdit::textChanged, this, &SettingsDialog::checkForChanges);
  }

  connect(ui->baseColorEdit, &QLineEdit::editingFinished, this, [this]() {
    auto *mainWindow = qobject_cast<MainWindow *>(parent());
    if (mainWindow && mainWindow->getSettingsManager()) {
      QString baseColor = ui->baseColorEdit->text().trimmed();
      if (baseColor != mainWindow->m_generalSettings.titleBaseColor) {
        mainWindow->m_generalSettings.titleBaseColor = baseColor;
        mainWindow->getSettingsManager()->saveGeneralSettings(mainWindow->m_generalSettings);
        m_generalSettings = mainWindow->m_generalSettings;
        ItemWidget::setTitleBaseColor(baseColor);
      }
    }
  });

  // Background type radio buttons - update button text based on selection
  connect(ui->backgroundColorRadio, &QRadioButton::toggled, this, [this](bool checked) {
    if (checked) {
      ui->browseBackgroundButton->setText(tr("Pick..."));
      ui->browseBackgroundButton->setToolTip(tr("Select background color"));
      ui->backgroundValueEdit->setPlaceholderText(tr("Default"));
      ui->backgroundValueEdit->setToolTip(tr("Background color (hex format like #FF5500)"));
    }
  });

  connect(ui->backgroundImageRadio, &QRadioButton::toggled, this, [this](bool checked) {
    if (checked) {
      ui->browseBackgroundButton->setText(tr("Browse..."));
      ui->browseBackgroundButton->setToolTip(tr("Select background image"));
      ui->backgroundValueEdit->setPlaceholderText(tr("None"));
      ui->backgroundValueEdit->setToolTip(tr("Path to background image file"));
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
      QColor color = QColorDialog::getColor(currentColor, this, tr("Select Background Color"));
      if (color.isValid()) {
        ui->backgroundValueEdit->setText(color.name());
      }
    } else {
      // Image file picker
      QString currentPath = ui->backgroundValueEdit->text().trimmed();
      QString startDir =
          currentPath.isEmpty() ? QDir::homePath() : QFileInfo(currentPath).absolutePath();
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
    QColor color = QColorDialog::getColor(currentColor, this, tr("Select Primary Color"));
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
    QColor color = QColorDialog::getColor(currentColor, this, tr("Select Tile Color"));
    if (color.isValid()) {
      ui->tileColorEdit->setText(color.name());
    }
  });

  // Selection color picker button
  connect(ui->browseSelectionColorButton, &QPushButton::clicked, this, [this]() {
    QColor currentColor = Qt::gray;
    QString currentValue = ui->selectionColorEdit->text().trimmed();
    if (!currentValue.isEmpty() && QColor::isValidColorName(currentValue)) {
      currentColor = QColor(currentValue);
    }
    QColor color = QColorDialog::getColor(currentColor, this, tr("Select Selection Color"));
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
    QColor color = QColorDialog::getColor(currentColor, this, tr("Select Row Color"));
    if (color.isValid()) {
      ui->listRowColorEdit->setText(color.name());
    }
  });

  // List mode alternate row color picker button
  connect(ui->browseListAltRowColorButton, &QPushButton::clicked, this, [this]() {
    QColor currentColor = Qt::gray;
    QString currentValue = ui->listAltRowColorEdit->text().trimmed();
    if (!currentValue.isEmpty() && QColor::isValidColorName(currentValue)) {
      currentColor = QColor(currentValue);
    }
    QColor color = QColorDialog::getColor(currentColor, this, tr("Select Alternate Row Color"));
    if (color.isValid()) {
      ui->listAltRowColorEdit->setText(color.name());
    }
  });

  auto markChanged = [this]() { checkForChanges(); };
  if (ui->keyNavUpEdit) {
    connect(ui->keyNavUpEdit, &QKeySequenceEdit::keySequenceChanged, this, markChanged);
  }
  if (ui->keyNavDownEdit) {
    connect(ui->keyNavDownEdit, &QKeySequenceEdit::keySequenceChanged, this, markChanged);
  }
  if (ui->keyNavLeftEdit) {
    connect(ui->keyNavLeftEdit, &QKeySequenceEdit::keySequenceChanged, this, markChanged);
  }
  if (ui->keyNavRightEdit) {
    connect(ui->keyNavRightEdit, &QKeySequenceEdit::keySequenceChanged, this, markChanged);
  }
  if (ui->keyConfirmEdit) {
    connect(ui->keyConfirmEdit, &QKeySequenceEdit::keySequenceChanged, this, markChanged);
  }
  if (ui->keyBackEdit) {
    connect(ui->keyBackEdit, &QKeySequenceEdit::keySequenceChanged, this, markChanged);
  }
  if (ui->keySearchEdit) {
    connect(ui->keySearchEdit, &QKeySequenceEdit::keySequenceChanged, this, markChanged);
  }

  if (ui->gamepadConfirmButtonLineEdit) {
    connect(ui->gamepadConfirmButtonLineEdit, &QLineEdit::textChanged, this, markChanged);
  }
  if (ui->gamepadBackButtonLineEdit) {
    connect(ui->gamepadBackButtonLineEdit, &QLineEdit::textChanged, this, markChanged);
  }
  if (ui->gamepadToggleSidebarButtonLineEdit) {
    connect(ui->gamepadToggleSidebarButtonLineEdit, &QLineEdit::textChanged, this, markChanged);
  }
  if (ui->detectGamepadConfirmButtonButton) {
    connect(ui->detectGamepadConfirmButtonButton, &QPushButton::clicked, this,
            [this]() { startGamepadButtonCapture(GamepadCaptureTarget::Confirm); });
  }
  if (ui->detectGamepadBackButtonButton) {
    connect(ui->detectGamepadBackButtonButton, &QPushButton::clicked, this,
            [this]() { startGamepadButtonCapture(GamepadCaptureTarget::Back); });
  }
  if (ui->detectGamepadToggleSidebarButtonButton) {
    connect(ui->detectGamepadToggleSidebarButtonButton, &QPushButton::clicked, this,
            [this]() { startGamepadButtonCapture(GamepadCaptureTarget::ToggleSidebar); });
  }
  if (ui->gamepadUseDpadCheckBox) {
    connect(ui->gamepadUseDpadCheckBox, &QCheckBox::toggled, this, markChanged);
  }
  if (ui->gamepadUseLeftStickCheckBox) {
    connect(ui->gamepadUseLeftStickCheckBox, &QCheckBox::toggled, this, markChanged);
  }
  if (ui->artworkCycleModifierComboBox) {
    connect(ui->artworkCycleModifierComboBox, &QComboBox::currentIndexChanged, this, markChanged);
  }

  // Attract mode connections for change detection
  if (ui->attractModeCheckBox) {
    connect(ui->attractModeCheckBox, &QCheckBox::toggled, this, &SettingsDialog::checkForChanges);
  }
  if (ui->attractIdleTimeoutSpinBox) {
    connect(ui->attractIdleTimeoutSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->attractScrollSpeedSpinBox) {
    connect(ui->attractScrollSpeedSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SettingsDialog::checkForChanges);
  }
  if (ui->attractAutoScrollCheckBox) {
    connect(ui->attractAutoScrollCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->attractAdvanceSelectionCheckBox) {
    connect(ui->attractAdvanceSelectionCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->attractAdvanceIntervalSpinBox) {
    connect(ui->attractAdvanceIntervalSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->attractAdvanceRandomCheckBox) {
    connect(ui->attractAdvanceRandomCheckBox, &QCheckBox::toggled, this,
            &SettingsDialog::checkForChanges);
  }

  // General settings spinbox connections for change detection
  if (ui->pixmapCacheSpinBox) {
    connect(ui->pixmapCacheSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->keyboardSpeedSpinBox) {
    connect(ui->keyboardSpeedSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->keyboardRepeatDelaySpinBox) {
    connect(ui->keyboardRepeatDelaySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->clickHoldDelaySpinBox) {
    connect(ui->clickHoldDelaySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->clickHoldRepeatIntervalSpinBox) {
    connect(ui->clickHoldRepeatIntervalSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->listKeyboardRepeatSpinBox) {
    connect(ui->listKeyboardRepeatSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->listClickHoldRepeatSpinBox) {
    connect(ui->listClickHoldRepeatSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->mouseWheelSpeedSpinBox) {
    connect(ui->mouseWheelSpeedSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->scrollAnimationSpeedSpinBox) {
    connect(ui->scrollAnimationSpeedSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->scrollVelocityMultiplierSpinBox) {
    connect(ui->scrollVelocityMultiplierSpinBox,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->titleSaturationSpinBox) {
    connect(ui->titleSaturationSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }
  if (ui->titleLightnessSpinBox) {
    connect(ui->titleLightnessSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::checkForChanges);
  }

  // Kartend-81o: customizable toolbar fields — every change marks the dialog
  // dirty so the user gets the standard Save / Discard / Cancel prompt.
  for (auto *box : {ui->toolbarGridViewVisibleCheckBox, ui->toolbarListViewVisibleCheckBox,
                    ui->toolbarHideSubcollectionsVisibleCheckBox,
                    ui->toolbarTypeFilterVisibleCheckBox, ui->toolbarTitleFilterVisibleCheckBox,
                    ui->toolbarSearchModeVisibleCheckBox, ui->toolbarSearchBarVisibleCheckBox}) {
    if (box) {
      connect(box, &QCheckBox::toggled, this, &SettingsDialog::checkForChanges);
    }
  }
  for (auto *edit : {ui->toolbarGridViewTextEdit, ui->toolbarListViewTextEdit,
                     ui->toolbarHideSubcollectionsTextEdit, ui->toolbarTitleFilterTextEdit}) {
    if (edit) {
      connect(edit, &QLineEdit::textChanged, this, &SettingsDialog::checkForChanges);
    }
  }

  // Kartend-88w: configuration backup — Export saves the live config to a
  // named .cfg in the Kartend config directory; the Load combo lists every
  // other .cfg in that directory and replaces kartend.cfg with the selection.
  populateImportConfigComboBox(ui->importConfigComboBox);
  if (ui->importConfigButton) {
    ui->importConfigButton->setEnabled(ui->importConfigComboBox &&
                                       ui->importConfigComboBox->count() > 0);
  }
  if (ui->importConfigComboBox) {
    connect(ui->importConfigComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int /*index*/) {
              if (ui->importConfigButton) {
                ui->importConfigButton->setEnabled(ui->importConfigComboBox->count() > 0 &&
                                                   ui->importConfigComboBox->currentIndex() >= 0);
              }
            });
  }

  if (ui->exportConfigButton) {
    connect(ui->exportConfigButton, &QPushButton::clicked, this, [this]() {
      // Prompt for a profile name so the export lands inside the config dir
      // and shows up in the Load dropdown next to other backups.
      bool ok = false;
      const QString rawName =
          QInputDialog::getText(this, tr("Export Configuration"),
                                tr("Profile name (saved as <name>.cfg in the config directory):"),
                                QLineEdit::Normal, QString(), &ok);
      if (!ok) {
        return;
      }
      QString name = rawName.trimmed();
      if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Export Configuration"),
                             tr("Please enter a non-empty profile name."));
        return;
      }
      // Strip .cfg if the user already typed it, then sanitize so the name
      // can't escape the config directory or collide with the live file.
      if (name.endsWith(".cfg", Qt::CaseInsensitive)) {
        name.chop(4);
      }
      static const QRegularExpression invalidChars(R"([\\/:*?"<>|])");
      name.replace(invalidChars, "_");
      if (name.compare("kartend", Qt::CaseInsensitive) == 0) {
        QMessageBox::warning(this, tr("Export Configuration"),
                             tr("\"kartend\" is reserved for the active configuration. "
                                "Please choose a different name."));
        return;
      }

      const QDir configDir = QFileInfo(SettingsUtils::getConfigPath()).absoluteDir();
      const QString destPath = configDir.absoluteFilePath(name + ".cfg");

      if (QFile::exists(destPath)) {
        const auto choice = QMessageBox::question(
            this, tr("Export Configuration"),
            tr("A profile named \"%1.cfg\" already exists. Overwrite it?").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (choice != QMessageBox::Yes) {
          return;
        }
      }

      const auto result = SettingsUtils::exportConfig(destPath);
      if (result.isError()) {
        ErrorUtils::logError(result.error());
        QMessageBox::critical(
            this, tr("Export Configuration"),
            tr("Failed to export configuration:\n%1").arg(result.error().message));
        return;
      }
      // Refresh the dropdown so the new file appears, then select it.
      populateImportConfigComboBox(ui->importConfigComboBox);
      if (ui->importConfigComboBox) {
        const int idx = ui->importConfigComboBox->findData(destPath);
        if (idx >= 0) {
          ui->importConfigComboBox->setCurrentIndex(idx);
        }
      }
      if (ui->importConfigButton) {
        ui->importConfigButton->setEnabled(ui->importConfigComboBox &&
                                           ui->importConfigComboBox->count() > 0);
      }
      QMessageBox::information(this, tr("Export Configuration"),
                               tr("Configuration exported to:\n%1").arg(destPath));
    });
  }

  if (ui->importConfigButton) {
    connect(ui->importConfigButton, &QPushButton::clicked, this, [this]() {
      if (!ui->importConfigComboBox || ui->importConfigComboBox->currentIndex() < 0) {
        return;
      }
      const QString sourcePath = ui->importConfigComboBox->currentData().toString();
      const QString sourceName = ui->importConfigComboBox->currentText();
      if (sourcePath.isEmpty()) {
        return;
      }

      const auto confirm = QMessageBox::warning(
          this, tr("Load Configuration"),
          tr("This will replace the active configuration with \"%1\". The current "
             "configuration will be saved as kartend.cfg.bak. Kartend must be restarted "
             "for the changes to take effect.\n\nContinue?")
              .arg(sourceName),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (confirm != QMessageBox::Yes) {
        return;
      }

      const auto result = SettingsUtils::importConfig(sourcePath);
      if (result.isError()) {
        ErrorUtils::logError(result.error());
        QMessageBox::critical(this, tr("Load Configuration"),
                              tr("Failed to load configuration:\n%1").arg(result.error().message));
        return;
      }

      QMessageBox::information(
          this, tr("Load Configuration"),
          tr("Configuration loaded from \"%1\".\n\nKartend will now exit. Restart "
             "the application to apply the new configuration.")
              .arg(sourceName));
      // Defer the quit until after the message box / event loop unwinds so
      // the dialog can finish closing cleanly.
      QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    });
  }
}
