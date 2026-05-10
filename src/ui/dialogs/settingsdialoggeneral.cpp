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
#include "gamepadcapturecontroller.h"
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
/// list every *.cfg in the Kartend config directory.
/// Sorted alphabetically so the dropdown order is stable across opens.
/// Returns absolute paths. The active config is included so the user can
/// see (and re-load) it from the same control.
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
  for (const QFileInfo &entry : entries) {
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
  const QString liveCanonical = QFileInfo(SettingsUtils::getConfigPath()).canonicalFilePath();
  int activeIdx = -1;
  for (const QString &path : paths) {
    QString label = QFileInfo(path).fileName();
    // tag the active config so the user can tell it apart
    // from the other profiles in the same control. Loading it is still
    // valid — it reloads the on-disk values, which is a useful "revert"
    // path when the user has made unintended in-memory changes.
    if (!liveCanonical.isEmpty() && QFileInfo(path).canonicalFilePath() == liveCanonical) {
      label += QObject::tr(" (active)");
      activeIdx = combo->count();
    }
    combo->addItem(label, path);
  }
  if (activeIdx >= 0) {
    combo->setCurrentIndex(activeIdx);
  }
}
} // namespace

void SettingsDialog::setupGeneralSettingsConnections() {
  // Selection & Display / Startup / Input & Scroll Timing / Performance &
  // History live-save handlers all moved to GeneralSettingsPanel; the panel
  // emits changed() and SettingsDialog mirrors + persists in its constructor
  // wiring (see constructor's connect for ui->generalSettingsPanel).
  //
  // The previously live-applied showTitleInPlaceholder side-effect
  // (ItemWidget::setShowTitleInPlaceholder + repaint of visible widgets) is
  // now applied on Save only — consistent with the rest of the dialog's
  // deferred-save fields.

  // Browse font button for per-collection custom font lives on
  // AppearanceTitlesPanel.

  // Background type radios + value edit, palette pickers (base / primary /
  // tile / selection), list-row pickers, title-tint spin boxes, and the
  // baseColor live-save with ItemWidget side-effect — all live on
  // AppearanceColorsPanel now. Host wires baseColorChanged() / changed() in
  // the constructor.

  // customFontEdit textChanged routing lives on AppearanceTitlesPanel.

  // Global application-font controls (family + size + picker) live in
  // FontsPanel now; the panel emits changed() and SettingsDialog handles
  // the live-save mirror in its constructor.

  // Keyboard / Gamepad / Mouse connections (including detect-button →
  // GamepadCaptureController dispatch) live on ControlsPanel now.

  // Startup video / home-view / selection-display / input-timing /
  // performance-history connections all live in GeneralSettingsPanel now.

  // Toolbar-customization connections owned by ToolbarPanel.

  // configuration backup — Export saves the live config to a
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
