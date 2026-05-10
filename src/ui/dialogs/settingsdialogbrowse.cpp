// Sibling translation unit for SettingsDialog.
// Extracted from settingsdialogform.cpp during LOC-reduction refactor.
// These remain SettingsDialog members; this is a translation-unit split.
#include <algorithm>
#include <functional>
#include <QAbstractItemView>
#include <QColorDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
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
#include <set>

#include "appearanceeffectspanel.h"
#include "appearancelayoutpanel.h"
#include "appearancelistpanel.h"
#include "appearancetitlespanel.h"
#include "appearancetoolbarpanel.h"
#include "artworktabpanel.h"
#include "collectiontreewidget.h"
#include "configurationpanel.h"
#include "extensionutils.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "launchertabpanel.h"
#include "mainwindow.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "sidebarpanel.h"
#include "subfolderspanel.h"
#include "treemanager.h"
#include "ui_settingsdialog.h"
#include "uiconstants.h"

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
  QString fileName =
      QFileDialog::getOpenFileName(this, tr("Select Launcher"), "", tr("All Files (*)"));
  if (!fileName.isEmpty() && ui->launcherPanel->launcherLineEdit()) {
    ui->launcherPanel->launcherLineEdit()->setText(fileName);
  }
}

void SettingsDialog::browseCore() {
  QString fileName = QFileDialog::getOpenFileName(
      this, tr("Select Core"), "", tr("Core Files (*.so *.dll *.dylib);;All Files (*)"));
  if (!fileName.isEmpty() && ui->launcherPanel->coreLineEdit()) {
    ui->launcherPanel->coreLineEdit()->setText(fileName);
  }
}

void SettingsDialog::browseMediaDir() {
  QString dirName = QFileDialog::getExistingDirectory(this, tr("Select Media Directory"), "");
  if (!dirName.isEmpty() && ui->configurationPanel->mediaDirLineEdit()) {
    ui->configurationPanel->mediaDirLineEdit()->setText(dirName);
  }
}

void SettingsDialog::onRecursiveImportContent() {
  if (!ui->configurationPanel->mediaDirLineEdit()) {
    return;
  }
  QString baseDir = ui->configurationPanel->mediaDirLineEdit()->text().trimmed();
  if (baseDir.isEmpty()) {
    QMessageBox::warning(this, tr("Recursive Import"),
                         tr("Please specify a content directory first."));
    return;
  }
  performRecursiveImport(baseDir, true);
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
  QString message = tr("This will create %1 subcollection(s) based on the "
                       "directory structure:\n\n")
                        .arg(subdirs.size());
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

  QMessageBox::StandardButton reply =
      QMessageBox::question(this, tr("Confirm Recursive Import"), message,
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

  // Copy by value to avoid reference invalidation when m_workingCollections is
  // appended to
  const CollectionConfig templateConfig = m_workingCollections[currentCollectionIndex];
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
  if (auto *item = m_treeManager ? m_treeManager->itemAt(currentCollectionIndex) : nullptr) {
    collectionTreeWidget->setCurrentItem(item);
    item->setSelected(true);
    item->setExpanded(true);
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

  ui->launcherPanel->load();
  loadAdditionalLaunchersToUI(config);
  loadLinkedParentsToUI(config);
  ui->configurationPanel->load();
  // Populate the type combo from the union of types in use across the
  // working list so the user can pick anything they've already tagged. The
  // combo stays editable so free-form values still survive a round-trip.
  {
    QStringList types{QString()};
    types += CollectionUtils::collectAllCollectionTypes(m_workingCollections);
    ui->configurationPanel->setKnownTypes(types, config.type);
  }
  ui->artworkPanel->load();
  ui->subfoldersPanel->load();
  ui->appearanceLayoutPanel->load();
  ui->sidebarPanel->load();
  ui->appearanceTitlesPanel->load();

  // Background / palette / list-row colors / vignette owned by
  // AppearanceColorsPanel.
  ui->appearanceColorsPanel->load();

  // List mode settings
  ui->appearanceListPanel->load();

  // header logo
  ui->appearanceToolbarPanel->load();

  // wallpaper parallax
  ui->appearanceEffectsPanel->load();

  // toolbar backdrop blur

  updateParentCollectionComboBox(index);
  updateFieldVisibility();
  updateGridWidthLimits();
  m_isLoading = false;
}

void SettingsDialog::clearCollectionUI() {
  m_isLoading = true;

  ui->launcherPanel->clear();
  clearAdditionalLaunchersUI();
  clearLinkedParentsUI();
  ui->configurationPanel->clear();
  ui->artworkPanel->clear();
  ui->appearanceColorsPanel->clear();
  ui->appearanceTitlesPanel->clear();
  ui->appearanceToolbarPanel->clear();
  ui->appearanceEffectsPanel->clear();

  ui->subfoldersPanel->clear();
  ui->appearanceLayoutPanel->clear();
  ui->sidebarPanel->clear();
  if (ui->configurationPanel->parentCollectionComboBox())
    ui->configurationPanel->parentCollectionComboBox()->clear();

  m_isLoading = false;
}
