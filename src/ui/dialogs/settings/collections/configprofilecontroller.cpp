// ConfigProfileController — configuration backup/restore controls.
// Extracted from SettingsDialog (settingsdialoggeneral.cpp seam).
#include "configprofilecontroller.h"
#include "errorutils.h"
#include "imainwindow.h"
#include "settingsutils.h"
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStringList>
#include <QTimer>

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
} // namespace

ConfigProfileController::ConfigProfileController(QObject *parent) : QObject(parent) {}

void ConfigProfileController::setupControls(const Setup &setup) {
  m_dialogParent = setup.dialogParent;
  m_importConfigComboBox = setup.importConfigComboBox;
  m_importConfigButton = setup.importConfigButton;
  m_exportConfigButton = setup.exportConfigButton;

  // configuration backup — Export saves the live config to a
  // named .cfg in the Kartend config directory; the Load combo lists every
  // other .cfg in that directory and replaces kartend.cfg with the selection.
  populateImportConfigComboBox();
  if (m_importConfigButton) {
    m_importConfigButton->setEnabled(m_importConfigComboBox && m_importConfigComboBox->count() > 0);
  }
  if (m_importConfigComboBox) {
    connect(m_importConfigComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int /*index*/) {
              if (m_importConfigButton) {
                m_importConfigButton->setEnabled(m_importConfigComboBox->count() > 0 &&
                                                 m_importConfigComboBox->currentIndex() >= 0);
              }
            });
  }

  if (m_exportConfigButton) {
    connect(m_exportConfigButton, &QPushButton::clicked, this,
            &ConfigProfileController::exportConfig);
  }

  if (m_importConfigButton) {
    connect(m_importConfigButton, &QPushButton::clicked, this,
            &ConfigProfileController::importConfig);
  }
}

void ConfigProfileController::populateImportConfigComboBox() {
  QComboBox *combo = m_importConfigComboBox;
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

void ConfigProfileController::exportConfig() {
  // Prompt for a profile name so the export lands inside the config dir
  // and shows up in the Load dropdown next to other backups.
  bool ok = false;
  const QString rawName =
      QInputDialog::getText(m_dialogParent, tr("Export Configuration"),
                            tr("Profile name (saved as <name>.cfg in the config directory):"),
                            QLineEdit::Normal, QString(), &ok);
  if (!ok) {
    return;
  }
  QString name = rawName.trimmed();
  if (name.isEmpty()) {
    QMessageBox::warning(m_dialogParent, tr("Export Configuration"),
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
    QMessageBox::warning(m_dialogParent, tr("Export Configuration"),
                         tr("\"kartend\" is reserved for the active configuration. "
                            "Please choose a different name."));
    return;
  }

  const QDir configDir = QFileInfo(SettingsUtils::getConfigPath()).absoluteDir();
  const QString destPath = configDir.absoluteFilePath(name + ".cfg");

  if (QFile::exists(destPath)) {
    const auto choice = QMessageBox::question(
        m_dialogParent, tr("Export Configuration"),
        tr("A profile named \"%1.cfg\" already exists. Overwrite it?").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice != QMessageBox::Yes) {
      return;
    }
  }

  const auto result = SettingsUtils::exportConfig(destPath);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    QMessageBox::critical(m_dialogParent, tr("Export Configuration"),
                          tr("Failed to export configuration:\n%1").arg(result.error().message));
    return;
  }
  // Refresh the dropdown so the new file appears, then select it.
  populateImportConfigComboBox();
  if (m_importConfigComboBox) {
    const int idx = m_importConfigComboBox->findData(destPath);
    if (idx >= 0) {
      m_importConfigComboBox->setCurrentIndex(idx);
    }
  }
  if (m_importConfigButton) {
    m_importConfigButton->setEnabled(m_importConfigComboBox && m_importConfigComboBox->count() > 0);
  }
  QMessageBox::information(m_dialogParent, tr("Export Configuration"),
                           tr("Configuration exported to:\n%1").arg(destPath));
}

void ConfigProfileController::importConfig() {
  if (!m_importConfigComboBox || m_importConfigComboBox->currentIndex() < 0) {
    return;
  }
  const QString sourcePath = m_importConfigComboBox->currentData().toString();
  const QString sourceName = m_importConfigComboBox->currentText();
  if (sourcePath.isEmpty()) {
    return;
  }

  const auto confirm = QMessageBox::warning(
      m_dialogParent, tr("Load Configuration"),
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
    QMessageBox::critical(m_dialogParent, tr("Load Configuration"),
                          tr("Failed to load configuration:\n%1").arg(result.error().message));
    return;
  }

  // kartend.cfg now holds the imported profile. Tell the main window before
  // scheduling the quit below: the shutdown path otherwise persists the OLD
  // in-memory state over the imported file (ApplicationManager::shutdown's
  // saveCollections removes every collection section not in the stale list,
  // and a pending debounced general-settings save would clobber [General]).
  // The dialog's QObject parent is the main window — the same host
  // resolution SettingsDialog itself uses.
  QObject *hostCandidate = m_dialogParent ? m_dialogParent->parent() : nullptr;
  if (auto *host = dynamic_cast<IMainWindow *>(hostCandidate)) {
    host->markConfigReplacedOnDisk();
  }

  QMessageBox::information(m_dialogParent, tr("Load Configuration"),
                           tr("Configuration loaded from \"%1\".\n\nKartend will now exit. Restart "
                              "the application to apply the new configuration.")
                               .arg(sourceName));
  // Defer the quit until after the message box / event loop unwinds so
  // the dialog can finish closing cleanly.
  QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}
