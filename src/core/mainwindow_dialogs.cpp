// Sibling TU of mainwindow_setup.cpp: dialog / wizard launchers.
//
// Holds the void MainWindow::*Interactive() and show*() entry points that
// the menu controller (and one or two settings panes) call to open a modal
// dialog or wizard. Moved here from mainwindow_setup.cpp to keep that file
// focused on the UI-bring-up phase (setupUI / setupUIReferences /
// initializeAppContext / createMenuBar / setupSidebar / setupArtworkManager /
// setupLastSelectedIndices / setupEventFilters / refreshCollectionFilesystemWatcher /
// applyPixmapCacheBudget / adjustGridWidth / setViewType / showEvent).
//
// Charter (docs/dev/mainwindow-partials.md): every function here is a thin
// wrapper — construct the dialog/wizard, populate its inputs from MainWindow
// state, exec(), write results back. Substantive tool flows live in
// controllers: the per-collection library tools in LibraryToolsController,
// the command-palette registry in MenuController::buildPaletteCommands.
//
// Includes are the dialog/wizard headers each launcher names plus the
// managers and value-container headers their bodies dereference. Keep the
// list narrow — anything used only by setup* code belongs in
// mainwindow_setup.cpp's include block, not here.

#include <memory>

#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QMessageBox>
#include <QPair>

#include "applicationmanager.h"
#include "bindingvisualizerdialog.h"
#include "collection/collectioncontext.h"
#include "collection/presentationprofile.h"
#include "collection/themepreset.h"
#include "collection/typehelpers.h"
#include "collection/validationhelpers.h"
#include "commandpalettedialog.h"
#include "createcollectiondialog.h"
#include "dialogcontroller.h"
#include "dialogrunners.h"
#include "errorpresentation.h"
#include "idatabasemanager.h"
#include "interactionmanager.h"
#include "isettingsmanager.h"
#include "layoutprofilesdialog.h"
#include "libraryonboardingwizard.h"
#include "librarytoolscontroller.h"
#include "mainwindow.h"
#include "menucontroller.h"
#include "navigationmanager.h"
#include "pathutils.h"
#include "presentationprofilesdialog.h"
#include "scraperprovidersdialog.h"
#include "scrollmanager.h"
#include "settingsdialogcontroller.h"
#include "settingsutils.h"
#include "uiconstants/dialog.h"
#include "uiconstants/grid.h"
#include "uiconstants/item.h"

SettingsDialogController *MainWindow::settingsDialogController() {
  if (!m_settingsDialogController) {
    // QObject-parented to this window — parent() is the runtime lifetime
    // guard. Long-lived (not per-open) because the controller tracks pending
    // "Collection Added" scan summaries that resolve after the dialog closes.
    m_settingsDialogController = new SettingsDialogController(&m_appContext, this);
  }
  return m_settingsDialogController;
}

SettingsDialogContext MainWindow::makeSettingsDialogContext() {
  SettingsDialogContext context;
  context.parent = this;
  context.collections = &m_collections;
  context.currentCollectionIndex = &currentCollectionIndex;
  context.detailsPaneManager = m_appManager->getDetailsPaneManager();
  context.scrollManager = m_appManager->getScrollManager();
  context.navigationManager = m_appManager->getNavigationManager();
  context.databaseManager = m_appManager->getDatabaseManager();
  context.createSettingsDialog = DialogController::makeSettingsDialogFactory(&m_appContext);
  // Kartend-sqoq0: generic runners for the "Collection Added" scan-summary
  // message boxes SettingsManager used to construct directly.
  context.dialogs = makeDialogRunners();
  return context;
}

void MainWindow::openSettingsDialog(SettingsPage initialPage) {
  // Without a settings manager there is nothing for the dialog flow to save
  // against — keep the historical guard even though the controller itself
  // reaches SettingsManager through the ApplicationContext.
  auto *settings = m_appManager ? m_appManager->getSettingsManager() : nullptr;
  if (!settings) {
    return;
  }
  SettingsDialogContext context = makeSettingsDialogContext();
  context.initialPage = initialPage;
  settingsDialogController()->openSettingsDialog(context);
  // Settings may have flipped watchFilesystem on/off or changed a mediaDirectory;
  // reconcile the watch set so the next file event lands on the right collection.
  refreshCollectionFilesystemWatcher();
}

void MainWindow::showAbout() {
  QString appName = APP_DISPLAY_NAME;
  QString appVersion = APP_VERSION;
  QString appAuthor = APP_AUTHOR;

  QString buildDate = BUILD_DATE;
  buildDate.replace("_SPACE_", " ");

  if (buildDate.isEmpty()) {
    buildDate = "[BUILD_DATE not set]";
  }

  QString aboutText = QString("<h3>%1 <span style='font-size: medium; "
                              "font-weight: normal;'>v%2</span></h3>"
                              "<p>Founded by %3</p>"
                              "<p>Build Date: %4</p>")
                          .arg(appName, appVersion, appAuthor, buildDate);

  QMessageBox msgBox(this);
  msgBox.setWindowTitle("About");
  msgBox.setText(aboutText);
  msgBox.setTextFormat(Qt::RichText);
  msgBox.setStandardButtons(QMessageBox::Ok);
  msgBox.resize(UIConstants::Dialog::ABOUT_WIDTH, UIConstants::Dialog::ABOUT_HEIGHT);
  msgBox.exec();
}

void MainWindow::appendCollectionAndPersist(const CollectionConfig &config, bool navigate) {
  // append → save → rebuild hierarchy → (optionally) navigate. Skipping any
  // step leaves the freshly-created collection invisible until restart.
  m_collections.append(config);
  if (m_appManager->getSettingsManager()) {
    ErrorPresentation::reportSaveResult(
        m_appManager->getSettingsManager()->saveCollections(m_collections), "collections", true);
  }
  rebuildHierarchyCache();
  if (navigate && m_appManager->getNavigationManager()) {
    currentCollectionIndex = m_collections.size() - 1;
    m_appManager->getNavigationManager()->showCollectionItems(currentCollectionIndex);
  }
}

void MainWindow::showFirstRunWizard() {
  const auto result = m_dialogController->runFirstRunWizard();

  if (result.accepted && !result.pickedConfig.mediaDirectory.isEmpty()) {
    // Mirrors the post-add sequence in setupInitialTimersEmptyCollections.
    appendCollectionAndPersist(result.pickedConfig, /*navigate=*/true);
  }

  // Always flip firstRunComplete — even when the user skipped without
  // picking a folder. They saw the wizard; auto-launching it again would
  // be obnoxious. Re-running stays available via Help → Setup Wizard…
  m_generalSettings.startup.firstRunComplete = true;
  if (m_appManager->getSettingsManager()) {
    ErrorPresentation::reportSaveResult(
        m_appManager->getSettingsManager()->saveGeneralSettings(m_generalSettings),
        "general settings", true);
  }
}

void MainWindow::importThemeInteractive() {
  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_collections.size()) {
    QMessageBox::information(this, tr("Import Theme"),
                             tr("Open a collection before importing a theme."));
    return;
  }
  const QString filePath = QFileDialog::getOpenFileName(
      this, tr("Import Theme"), QString(),
      tr("Kartend Theme (*.kartend-theme.json);;JSON (*.json);;All Files (*)"));
  if (filePath.isEmpty()) {
    return;
  }
  auto imported = ThemePresetIO::importFromFile(filePath);
  if (imported.isError()) {
    QMessageBox::warning(this, tr("Import Theme — Failed"), imported.error().message);
    return;
  }
  const ThemePreset preset = imported.value();
  const CollectionConfig &target = m_collections[currentCollectionIndex];
  const QStringList changes = ThemePresetIO::describeChanges(preset, target);

  // Confirmation surface: list the clusters that will change so the user
  // doesn't apply blindly. Empty list short-circuits to a friendly no-op.
  if (changes.isEmpty()) {
    QMessageBox::information(this, tr("Import Theme"),
                             tr("The selected theme matches the current collection — nothing to "
                                "change."));
    return;
  }
  QString summary = tr("Apply theme \"%1\" to \"%2\"?\n\nWill change:\n  • %3")
                        .arg(preset.name.isEmpty() ? tr("(unnamed)") : preset.name)
                        .arg(target.name)
                        .arg(changes.join(QStringLiteral("\n  • ")));
  const auto choice =
      QMessageBox::question(this, tr("Import Theme"), summary,
                            QMessageBox::Apply | QMessageBox::Cancel, QMessageBox::Cancel);
  if (choice != QMessageBox::Apply) {
    return;
  }
  CollectionConfig &mutableTarget = m_collections[currentCollectionIndex];
  ThemePresetIO::applyTo(preset, mutableTarget);
  if (m_appManager->getSettingsManager()) {
    ErrorPresentation::reportSaveResult(
        m_appManager->getSettingsManager()->saveCollections(m_collections), "collections", true);
  }
  // Soft reload so the new appearance values surface immediately without
  // the user having to switch collections.
  if (m_appManager->getNavigationManager()) {
    m_appManager->getNavigationManager()->safeReloadCollection(currentCollectionIndex);
  }
}

void MainWindow::exportThemeInteractive() {
  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_collections.size()) {
    QMessageBox::information(this, tr("Export Theme"),
                             tr("Open a collection before exporting a theme."));
    return;
  }
  const CollectionConfig &source = m_collections[currentCollectionIndex];
  const ThemePreset preset = ThemePresetIO::fromCollection(source);
  const QString suggestion =
      (source.name.isEmpty() ? QStringLiteral("theme") : source.name.trimmed()) +
      QStringLiteral(".kartend-theme.json");
  const QString filePath =
      QFileDialog::getSaveFileName(this, tr("Export Theme"), suggestion,
                                   tr("Kartend Theme (*.kartend-theme.json);;JSON (*.json)"));
  if (filePath.isEmpty()) {
    return;
  }
  QString outPath = filePath;
  // Tag with the canonical extension when the user typed a bare path so
  // the import-file-filter recognises it next round.
  if (!outPath.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)) {
    outPath += QStringLiteral(".kartend-theme.json");
  }
  auto exported = ThemePresetIO::exportToFile(preset, outPath);
  if (exported.isError()) {
    QMessageBox::warning(this, tr("Export Theme — Failed"), exported.error().message);
    return;
  }
  QMessageBox::information(this, tr("Export Theme"), tr("Theme exported to:\n%1").arg(outPath));
}

void MainWindow::manageLayoutProfilesInteractive() {
  const QString registryPath = SettingsUtils::getLayoutProfilesPath();
  auto loaded = ThemePresetIO::loadRegistry(registryPath);
  if (loaded.isError()) {
    QMessageBox::warning(this, tr("Layout profiles — could not load"), loaded.error().message);
    return;
  }
  QList<ThemePreset> profiles = loaded.value();

  // Snapshot of profiles before the dialog so we can detect mutations and
  // only persist when something actually changed. Avoids unnecessary disk
  // writes when the user just opens the dialog to browse.
  const QList<ThemePreset> originalProfiles = profiles;

  // Apply closure: writes the picked preset onto the current collection,
  // saves the INI, and triggers a soft-reload so the new layout is
  // visible without a restart. Lives on MainWindow because it has access
  // to m_collections / SettingsManager / NavigationManager — the dialog
  // intentionally doesn't touch any of those directly.
  auto onApply = [this](const ThemePreset &preset) {
    if (currentCollectionIndex < 0 || currentCollectionIndex >= m_collections.size()) {
      return;
    }
    CollectionConfig &target = m_collections[currentCollectionIndex];
    ThemePresetIO::applyTo(preset, target);
    if (m_appManager->getSettingsManager()) {
      ErrorPresentation::reportSaveResult(
          m_appManager->getSettingsManager()->saveCollections(m_collections), "collections", true);
    }
    if (m_appManager->getNavigationManager()) {
      m_appManager->getNavigationManager()->safeReloadCollection(currentCollectionIndex);
    }
  };

  LayoutProfilesDialog dialog(this);
  const CollectionConfig *current =
      (currentCollectionIndex >= 0 && currentCollectionIndex < m_collections.size())
          ? &m_collections[currentCollectionIndex]
          : nullptr;
  dialog.setRegistry(&profiles, current, std::move(onApply));
  dialog.exec();

  if (profiles != originalProfiles) {
    auto saved = ThemePresetIO::saveRegistry(profiles, registryPath);
    if (saved.isError()) {
      QMessageBox::warning(this, tr("Layout profiles — could not save"), saved.error().message);
    }
  }
}

// The five per-collection tool flows (Collection Health, Duplicates &
// Variants, Bulk Edit, Review Missing Metadata, Artwork Wizard) and their
// shared withActiveCollectionItems() skeleton live in
// librarytoolscontroller.{h,cpp}. The one-line delegations below keep the
// MainWindow surface the menu / palette callbacks bind to.

void MainWindow::showCollectionHealthInteractive() {
  if (m_libraryToolsController) m_libraryToolsController->showCollectionHealthInteractive();
}

void MainWindow::showVariantGroupingInteractive() {
  if (m_libraryToolsController) m_libraryToolsController->showVariantGroupingInteractive();
}

void MainWindow::navigateToItem(const QString &filePath) {
  if (filePath.isEmpty()) {
    return;
  }
  IDatabaseManager *db = m_appManager->getDatabaseManager();
  if (!db) {
    return;
  }
  const int owningIndex = db->getCollectionIndexForFile(filePath);
  if (!CollectionUtils::isValidIndex(owningIndex, &m_collections)) {
    // Row survives in items table but the owning collection was removed
    // from kartend.cfg — no live collection to navigate to.
    QMessageBox::information(
        this, tr("Open item"),
        tr("The collection that owns this item is no longer in your library."));
    return;
  }
  // Switch collections only when we're not already viewing the target —
  // showCollectionItems unconditionally triggers an items reload that
  // would needlessly bounce the current view.
  if (owningIndex != currentCollectionIndex && m_appManager->getNavigationManager()) {
    m_appManager->getNavigationManager()->showCollectionItems(owningIndex);
  }
  // Ask the DB worker for the item's visual index, then select via the
  // existing one-shot connection. Using the async path avoids a
  // synchronous load of the entire collection just to compute one index.
  CollectionContext context;
  context.currentIndex = owningIndex;
  context.config = m_collections[owningIndex];
  context.config.mediaDirectory =
      PathUtils::validateAndExpandPath(context.config.mediaDirectory, context.config.name);
  context.config.artworkDirectory =
      PathUtils::validateAndExpandPath(context.config.artworkDirectory, context.config.name);
  context.artworkDirectory = context.config.artworkDirectory;
  if (m_appManager->getInteractionManager()) {
    // Kartend-swyk: own the Connection handle via shared_ptr instead of a
    // manual new/delete. The delete previously ran only on the matching-path
    // branch, so if the worker never emitted a result for filePath (collection
    // emptied, path normalized, item removed, shutdown) the handle + captured
    // lambda leaked and the dead comparison ran on every future emission. The
    // connection is bound to `this`, so on teardown Qt disconnects it and frees
    // the lambda, dropping the last shared_ptr ref to the handle. The signal is
    // shared across paths (hence the resultPath filter), so Qt::SingleShot-
    // Connection is unusable — it would fire on the first non-matching result.
    // The InteractionManager is re-resolved at dispatch time (matching the
    // controller-ctx closures) rather than captured raw: the connection's
    // lifetime tracks MainWindow/DatabaseManager, not the sibling manager, so
    // a raw capture could dangle if a nested event loop dispatched the result
    // mid-teardown.
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(db, &IDatabaseManager::visualIndexForPathLoaded, this,
                    [this, conn, filePath](int visualIndex, const QString &resultPath) {
                      if (resultPath != filePath) {
                        return; // some other path's result — ignore
                      }
                      QObject::disconnect(*conn);
                      if (visualIndex < 0) {
                        return;
                      }
                      if (auto *im = m_appManager->getInteractionManager()) {
                        im->selectItemByIndex(visualIndex, /*allowHorizontalScroll=*/true);
                      }
                    });
    db->fetchVisualIndexForPath(context, m_collections, filePath);
  }
}

void MainWindow::bulkEditInteractive() {
  if (m_libraryToolsController) m_libraryToolsController->bulkEditInteractive();
}

void MainWindow::openCommandPalette() {
  // Registry building lives in MenuController::buildPaletteCommands — it owns
  // the command metadata (Tools callbacks, layout-action sync, manager
  // getters) the entries mirror. Built fresh per open so live collections /
  // view-mode / settings entries reflect the current state.
  if (!m_menuController) {
    return;
  }
  CommandPaletteDialog dialog(this);
  dialog.setCommands(m_menuController->buildPaletteCommands());
  dialog.exec();
}

void MainWindow::reviewMissingMetadataInteractive() {
  if (m_libraryToolsController) m_libraryToolsController->reviewMissingMetadataInteractive();
}

void MainWindow::artworkWizardInteractive() {
  if (m_libraryToolsController) m_libraryToolsController->artworkWizardInteractive();
}

void MainWindow::showBindingVisualizer() {
  // Pull the live gamepad manager so capture-mode hand-off is wired
  // automatically. Settings come from m_generalSettings (the live
  // user-edited copy) so the dialog reflects rebinds without a restart.
  GamepadManager *gp = nullptr;
  if (auto *interaction = m_appManager->getInteractionManager()) {
    gp = interaction->gamepadManager();
  }
  BindingVisualizerDialog dialog(&m_generalSettings, gp, this);
  dialog.exec();
}

void MainWindow::runNewLibraryWizard() {
  LibraryOnboardingWizard wizard(this);
  if (wizard.exec() != QDialog::Accepted) {
    return;
  }
  auto result = wizard.result();
  if (!result.accepted || result.pickedConfig.mediaDirectory.isEmpty()) {
    return;
  }
  // Same persist-and-navigate sequence the first-run wizard uses.
  appendCollectionAndPersist(result.pickedConfig, /*navigate=*/true);
}

QString MainWindow::createCollectionForDat(const QString &datPath) {
  CreateCollectionDialog dialog(this);
  dialog.setRetroarchConfigOverride(m_generalSettings.launchers.retroarchConfigPath);
  dialog.setIntroText(
      tr("Create a collection for the catalogue \"%1\". It will be attached to the new "
         "collection; point it at the folder holding the matching files.")
          .arg(QFileInfo(datPath).fileName()));
  // Offer the existing (non-playlist) collections as possible parents so a
  // DAT-seeded collection can be nested (Kartend-m6qsb.19). uuid → index map
  // resolves the pick back to a parentCollectionIndex below.
  QList<QPair<QString, QString>> parentOptions;
  QHash<QString, int> uuidToIndex;
  for (int i = 0; i < m_collections.size(); ++i) {
    const CollectionConfig &existing = m_collections.at(i);
    if (existing.isPlaylist) {
      continue;
    }
    const QString uuid = CollectionUtils::computeCollectionUuid(existing);
    parentOptions.append({existing.name, uuid});
    uuidToIndex.insert(uuid, i);
  }
  dialog.setParentCollectionOptions(parentOptions);

  if (dialog.exec() != QDialog::Accepted) {
    return QString();
  }

  // Mirror SettingsDialog::addCollection's field set so a DAT-seeded
  // collection is indistinguishable from a hand-made one, then pre-attach the
  // catalogue (linked audit profiles derive their DAT list from this).
  CollectionConfig c;
  c.name = dialog.collectionName();
  c.type = dialog.collectionType();
  c.scraperOverrides.scraperProviderId = dialog.scraperProviderId();
  c.scraperOverrides.screenscraperSystemId = dialog.screenscraperSystemId();
  c.launcher.launcherPath = dialog.launcherPath();
  c.launcher.corePath = dialog.corePath();
  c.mediaDirectory = dialog.contentPath();
  c.artworkDirectory = dialog.artworkDirectory();
  c.extensions = QStringList();
  c.gridLayout.gridWidth = UIConstants::Grid::DEFAULT_WIDTH;
  c.gridLayout.fontSize = UIConstants::Item::DEFAULT_FONT_SIZE;
  c.scraperOverrides.datFilePaths = QStringList{datPath};

  // Apply the chosen parent, inheriting the layout/sidebar fields a
  // subcollection takes from its parent — same set SettingsDialog::addCollection
  // copies so a DAT-seeded subcollection looks consistent (Kartend-m6qsb.19).
  const int parentIdx = uuidToIndex.value(dialog.parentCollectionUuid(), -1);
  if (parentIdx >= 0) {
    const CollectionConfig &parent = m_collections.at(parentIdx);
    c.parentCollectionIndex = parentIdx;
    c.isSubcollection = true;
    c.gridLayout = parent.gridLayout;
    c.sidebar.sidebarMode = parent.sidebar.sidebarMode;
    c.viewType = parent.viewType;
    c.showAllSubcollectionItems = parent.showAllSubcollectionItems;
    c.horizontalAlignment = parent.horizontalAlignment;
    c.hideTitles = parent.hideTitles;
    c.hideSubcollectionTitles = parent.hideSubcollectionTitles;
  }

  // Same persist sequence runNewLibraryWizard uses, but no navigate: the user
  // is mid DAT-library review.
  appendCollectionAndPersist(c, /*navigate=*/false);

  return CollectionUtils::computeCollectionUuid(c);
}

void MainWindow::managePresentationProfilesInteractive() {
  const QString registryPath = SettingsUtils::getPresentationProfilesPath();
  auto loaded = PresentationProfileIO::loadRegistry(registryPath);
  if (loaded.isError()) {
    QMessageBox::warning(this, tr("Presentation profiles — could not load"),
                         loaded.error().message);
    return;
  }
  QList<PresentationProfile> profiles = loaded.value();
  const QList<PresentationProfile> original = profiles;

  // Apply closure: write the picked profile onto m_generalSettings,
  // persist via SettingsManager, and reapply marquee + attract
  // behaviour so the change is visible immediately.
  auto onApply = [this](const PresentationProfile &profile) {
    PresentationProfileIO::applyTo(profile, m_generalSettings);
    if (m_appManager->getSettingsManager()) {
      ErrorPresentation::reportSaveResult(
          m_appManager->getSettingsManager()->saveGeneralSettings(m_generalSettings),
          "general settings", true);
    }
    // Marquee window respects the live settings on each (re)apply; we
    // call the same hook the Settings dialog uses so the change is
    // visible without restart.
    applyMarqueeSettings();
  };

  PresentationProfilesDialog dialog(this);
  dialog.setRegistry(&profiles, &m_generalSettings, std::move(onApply));
  dialog.exec();

  if (profiles != original) {
    auto saved = PresentationProfileIO::saveRegistry(profiles, registryPath);
    if (saved.isError()) {
      QMessageBox::warning(this, tr("Presentation profiles — could not save"),
                           saved.error().message);
    }
  }
}

void MainWindow::showScraperProvidersInteractive() {
  ScraperProvidersDialog dialog(&m_generalSettings, this);
  dialog.exec();
}
