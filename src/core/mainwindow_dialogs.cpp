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
// Includes are the dialog/wizard headers each launcher names plus the
// managers and value-container headers their bodies dereference. Keep the
// list narrow — anything used only by setup* code belongs in
// mainwindow_setup.cpp's include block, not here.

#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>

#include "applicationmanager.h"
#include "artworkcandidates.h"
#include "artworkmanager.h"
#include "artworkwizarddialog.h"
#include "bindingvisualizerdialog.h"
#include "bulkedit.h"
#include "bulkeditdialog.h"
#include "cachemanager.h"
#include "collection/collectioncontext.h"
#include "collection/presentationprofile.h"
#include "collection/themepreset.h"
#include "collection/typehelpers.h"
#include "collection/validationhelpers.h"
#include "collectionhealth.h"
#include "collectionhealthdialog.h"
#include "commandpalettedialog.h"
#include "detailpagemanager.h"
#include "dialogcontroller.h"
#include "idatabasemanager.h"
#include "interactionmanager.h"
#include "itemartwork.h"
#include "kartmanager.h"
#include "launchmanager.h"
#include "layoutprofilesdialog.h"
#include "libraryonboardingwizard.h"
#include "mainwindow.h"
#include "marqueecontroller.h"
#include "menucontroller.h"
#include "metadataqueue.h"
#include "metadatareviewdialog.h"
#include "navigationmanager.h"
#include "pathutils.h"
#include "playlistmanager.h"
#include "presentationprofilesdialog.h"
#include "scraperprovidersdialog.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "toolbarcontroller.h"
#include "variantgrouping.h"
#include "variantgroupingdialog.h"

#include "isettingsmanager.h"
#include "sessionmanager.h"
#include "settingsutils.h"
#include "stringutils.h"
#include "ui_mainwindow.h"
#include "uiconstants/dialog.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcMainWindow)

void MainWindow::openSettingsDialog(SettingsPage initialPage) {
  auto *settings = m_appManager ? m_appManager->getSettingsManager() : nullptr;
  if (!settings) {
    return;
  }
  SettingsDialogContext context;
  context.parent = this;
  context.collections = &m_collections;
  context.currentCollectionIndex = &currentCollectionIndex;
  context.detailsPaneManager = m_appManager->getDetailsPaneManager();
  context.scrollManager = m_appManager->getScrollManager();
  context.navigationManager = m_appManager->getNavigationManager();
  context.databaseManager = m_appManager->getDatabaseManager();
  context.createSettingsDialog = DialogController::makeSettingsDialogFactory(&m_appContext);
  context.initialPage = initialPage;
  settings->openSettingsDialog(context);
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

void MainWindow::showFirstRunWizard() {
  const auto result = m_dialogController->runFirstRunWizard();

  if (result.accepted && !result.pickedConfig.mediaDirectory.isEmpty()) {
    // Mirrors the post-add sequence in setupInitialTimersEmptyCollections:
    // append → save → rebuild hierarchy → navigate. Skipping any of these
    // would leave the freshly-created collection invisible until the next
    // restart.
    m_collections.append(result.pickedConfig);
    if (m_appManager->getSettingsManager()) {
      m_appManager->getSettingsManager()->saveCollections(m_collections);
    }
    rebuildHierarchyCache();
    if (m_appManager->getNavigationManager()) {
      currentCollectionIndex = m_collections.size() - 1;
      m_appManager->getNavigationManager()->showCollectionItems(currentCollectionIndex);
    }
  }

  // Always flip firstRunComplete — even when the user skipped without
  // picking a folder. They saw the wizard; auto-launching it again would
  // be obnoxious. Re-running stays available via Help → Setup Wizard…
  m_generalSettings.startup.firstRunComplete = true;
  if (m_appManager->getSettingsManager()) {
    m_appManager->getSettingsManager()->saveGeneralSettings(m_generalSettings);
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
    m_appManager->getSettingsManager()->saveCollections(m_collections);
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
      m_appManager->getSettingsManager()->saveCollections(m_collections);
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

void MainWindow::showCollectionHealthInteractive() {
  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_collections.size()) {
    QMessageBox::information(this, tr("Collection health"),
                             tr("Open a collection before running the health audit."));
    return;
  }
  const CollectionConfig &cfg = m_collections[currentCollectionIndex];
  // Resolve the uuid the same way every other per-item code path does so
  // the item enumeration finds the rows the rest of the app sees.
  const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
  const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
  if (uuid.isEmpty()) {
    QMessageBox::warning(this, tr("Collection health"),
                         tr("Could not resolve this collection's identity. "
                            "Check the media directory in settings."));
    return;
  }

  IDatabaseManager *db = m_appManager->getDatabaseManager();
  if (!db) {
    return;
  }
  const auto rows = db->loadAllItemPathsForCollection(uuid);

  // Convert to CollectionHealth::ItemPath wire shape. Decoupling the
  // analyzer's input from IDatabaseManager keeps the analyzer
  // unit-testable without a real DB.
  QList<CollectionHealth::ItemPath> healthItems;
  healthItems.reserve(rows.size());
  for (const IDatabaseManager::ItemPathRow &row : rows) {
    healthItems.append({row.path, row.artworkPath});
  }

  // Launcher validator: defer to LaunchManager's existing path resolver
  // so what counts as "found" matches what the launch pipeline would
  // accept at run time.
  auto validator = [](const QString &path) {
    return LaunchManager::validateLauncherPath(path).isOk();
  };
  const CollectionHealth::Report report = CollectionHealth::analyze(cfg, healthItems, validator);

  CollectionHealthDialog dialog(this);
  dialog.setReport(cfg.name, report);
  dialog.exec();
}

void MainWindow::showVariantGroupingInteractive() {
  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_collections.size()) {
    QMessageBox::information(this, tr("Duplicates and variants"),
                             tr("Open a collection before scanning for variants."));
    return;
  }
  const CollectionConfig &cfg = m_collections[currentCollectionIndex];
  const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
  const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
  if (uuid.isEmpty()) {
    QMessageBox::warning(this, tr("Duplicates and variants"),
                         tr("Could not resolve this collection's identity. "
                            "Check the media directory in settings."));
    return;
  }
  IDatabaseManager *db = m_appManager->getDatabaseManager();
  if (!db) return;

  const auto rows = db->loadAllItemPathsForCollection(uuid);
  QStringList paths;
  paths.reserve(rows.size());
  for (const IDatabaseManager::ItemPathRow &row : rows) {
    paths.append(row.path);
  }
  const auto groups = VariantGrouping::groupByBaseName(paths);
  if (groups.isEmpty()) {
    QMessageBox::information(this, tr("Duplicates and variants"),
                             tr("No same-name variants were detected in '%1' — every item has a "
                                "unique basename.")
                                 .arg(cfg.name));
    return;
  }

  VariantGroupingDialog dialog(groups, this);
  dialog.setLaunchHandler([this](const QString &path) {
    if (path.isEmpty() || !m_appManager) return;
    // Defer to InteractionManager so launcher selection, kart-archive
    // unpacking, and play-count bumps stay in one place. Resolve the
    // owning collection via the DB instead of trusting the currently-viewed
    // one — a user can navigate elsewhere while the dialog is up.
    IDatabaseManager *db = m_appManager->getDatabaseManager();
    InteractionManager *im = m_appManager->getInteractionManager();
    if (!db || !im) return;
    const int owningIndex = db->getCollectionIndexForFile(path);
    if (!CollectionUtils::isValidIndex(owningIndex, &m_collections)) return;
    im->launchItemWithCollection(path, owningIndex);
  });
  dialog.setSelectHandler([this](const QString &path) { navigateToItem(path); });
  dialog.exec();
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
  if (auto *im = m_appManager->getInteractionManager()) {
    auto *conn = new QMetaObject::Connection;
    *conn = connect(db, &IDatabaseManager::visualIndexForPathLoaded, this,
                    [conn, filePath, im](int visualIndex, const QString &resultPath) {
                      if (resultPath != filePath) {
                        return; // some other path's result — ignore
                      }
                      QObject::disconnect(*conn);
                      delete conn;
                      if (visualIndex >= 0) {
                        im->selectItemByIndex(visualIndex, /*allowHorizontalScroll=*/true);
                      }
                    });
    db->fetchVisualIndexForPath(context, m_collections, filePath);
  }
}

void MainWindow::bulkEditInteractive() {
  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_collections.size()) {
    QMessageBox::information(this, tr("Bulk edit"),
                             tr("Open a collection before bulk-editing items."));
    return;
  }
  const CollectionConfig &cfg = m_collections[currentCollectionIndex];
  IDatabaseManager *db = m_appManager->getDatabaseManager();
  if (!db) {
    return;
  }
  const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
  const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
  if (uuid.isEmpty()) {
    return;
  }
  const auto rows = db->loadAllItemPathsForCollection(uuid);
  if (rows.isEmpty()) {
    QMessageBox::information(this, tr("Bulk edit"), tr("This collection has no items to edit."));
    return;
  }

  BulkEditDialog dialog(this);
  dialog.setScope(cfg.name, rows.size());
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const auto choice = dialog.result();

  // Confirmation gate — describes the change in user-readable terms
  // before persistence. The dialog disables Apply when the input is
  // invalid (e.g. empty tag), but a final yes/no still surfaces here so
  // an accidental Enter on the picker isn't destructive.
  const QString actionDescription =
      BulkEdit::actionRequiresParameter(choice.action)
          ? tr("%1 \"%2\"").arg(BulkEdit::actionLabel(choice.action), choice.parameter)
          : BulkEdit::actionLabel(choice.action);
  const auto confirmation = QMessageBox::question(
      this, tr("Bulk edit"),
      tr("%1 across %2 item(s) in \"%3\"?").arg(actionDescription).arg(rows.size()).arg(cfg.name),
      QMessageBox::Apply | QMessageBox::Cancel, QMessageBox::Cancel);
  if (confirmation != QMessageBox::Apply) {
    return;
  }

  QStringList paths;
  paths.reserve(rows.size());
  for (const IDatabaseManager::ItemPathRow &row : rows) {
    paths.append(row.path);
  }
  const auto loaded = db->loadItemMetadataBatch(uuid, paths);

  QList<ItemMetadataStore::ItemMetadata> batch;
  batch.reserve(paths.size());
  for (const QString &path : paths) {
    auto it = loaded.find(path);
    ItemMetadataStore::ItemMetadata md =
        it != loaded.end() ? it.value() : ItemMetadataStore::ItemMetadata{};
    md.collectionUuid = uuid;
    md.path = path;
    batch.append(md);
  }

  const auto changes = BulkEdit::applyAction(choice.action, choice.parameter, batch);
  int writes = 0;
  for (const auto &change : changes) {
    if (!change.changed) {
      continue;
    }
    if (db->saveItemMetadata(change.metadata)) {
      ++writes;
    }
  }

  // Soft reload so the new state (tags, flags, ratings) surfaces in the
  // sidebar without a collection switch. The grid rendering isn't
  // affected by these flags until Kartend-elte ships, but the details
  // pane already reads them.
  if (m_appManager->getNavigationManager()) {
    m_appManager->getNavigationManager()->safeReloadCollection(currentCollectionIndex);
  }
  QMessageBox::information(this, tr("Bulk edit"),
                           tr("Applied to %1 of %2 item(s).").arg(writes).arg(rows.size()));
}

void MainWindow::openCommandPalette() {
  QList<CommandPaletteDialog::Command> commands;

  // Collection switch entries. Skip playlists / smart playlists from
  // the suggestions — they have their own access paths and the palette
  // would otherwise be dominated by reserved/auto-generated rows.
  for (int i = 0; i < m_collections.size(); ++i) {
    const CollectionConfig &cfg = m_collections.at(i);
    if (cfg.isPlaylist) continue;
    const int idx = i;
    commands.append({tr("Collection"), cfg.name, [this, idx]() {
                       if (m_appManager->getNavigationManager()) {
                         m_appManager->getNavigationManager()->showCollectionItems(idx);
                       }
                     }});
  }

  // View-mode toggles. Map the live ViewType enum to two simple verbs;
  // the toolbar already syncs visually once the new mode is set.
  commands.append(
      {tr("View"), tr("Switch to grid view"), [this]() {
         if (auto *menu = m_menuController.get()) {
           menu->syncLayoutActions(ViewType::Grid);
         }
         if (currentCollectionIndex >= 0 && currentCollectionIndex < m_collections.size()) {
           m_collections[currentCollectionIndex].viewType = ViewType::Grid;
           if (m_appManager->getSettingsManager()) {
             m_appManager->getSettingsManager()->saveCollections(m_collections);
           }
           if (m_appManager->getNavigationManager()) {
             m_appManager->getNavigationManager()->safeReloadCollection(currentCollectionIndex);
           }
         }
       }});
  commands.append(
      {tr("View"), tr("Switch to list view"), [this]() {
         if (auto *menu = m_menuController.get()) {
           menu->syncLayoutActions(ViewType::List);
         }
         if (currentCollectionIndex >= 0 && currentCollectionIndex < m_collections.size()) {
           m_collections[currentCollectionIndex].viewType = ViewType::List;
           if (m_appManager->getSettingsManager()) {
             m_appManager->getSettingsManager()->saveCollections(m_collections);
           }
           if (m_appManager->getNavigationManager()) {
             m_appManager->getNavigationManager()->safeReloadCollection(currentCollectionIndex);
           }
         }
       }});

  // Common tool entries already reachable via menus — surfaced here so
  // the palette is the single keyboard-driven entry point users learn.
  commands.append({tr("Tools"), tr("Open settings"), [this]() {
                     // Mirror ctx.onOpenSettings (mainwindow_setup createMenuBar) so the
                     // palette opens the same dialog the Settings menu entry uses.
                     if (auto *settings = m_appManager->getSettingsManager()) {
                       SettingsDialogContext context;
                       context.parent = this;
                       context.collections = &m_collections;
                       context.currentCollectionIndex = &currentCollectionIndex;
                       context.detailsPaneManager = m_appManager->getDetailsPaneManager();
                       context.scrollManager = m_appManager->getScrollManager();
                       context.navigationManager = m_appManager->getNavigationManager();
                       context.databaseManager = m_appManager->getDatabaseManager();
                       context.createSettingsDialog =
                           DialogController::makeSettingsDialogFactory(&m_appContext);
                       settings->openSettingsDialog(context);
                     }
                   }});
  commands.append({tr("Tools"), tr("Rescan current collection"), [this]() {
                     if (currentCollectionIndex >= 0 && m_appManager->getNavigationManager()) {
                       m_appManager->getNavigationManager()->safeReloadCollection(
                           currentCollectionIndex);
                     }
                   }});
  commands.append({tr("Tools"), tr("Show collection health…"),
                   [this]() { showCollectionHealthInteractive(); }});
  commands.append({tr("Tools"), tr("Bulk edit items…"), [this]() { bulkEditInteractive(); }});
  commands.append(
      {tr("Tools"), tr("Layout profiles…"), [this]() { manageLayoutProfilesInteractive(); }});

  CommandPaletteDialog dialog(this);
  dialog.setCommands(std::move(commands));
  dialog.exec();
}

void MainWindow::reviewMissingMetadataInteractive() {
  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_collections.size()) {
    QMessageBox::information(this, tr("Review missing metadata"),
                             tr("Open a collection before running the review."));
    return;
  }
  const CollectionConfig &cfg = m_collections[currentCollectionIndex];
  IDatabaseManager *db = m_appManager->getDatabaseManager();
  if (!db) return;
  const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
  const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
  if (uuid.isEmpty()) return;

  const auto rows = db->loadAllItemPathsForCollection(uuid);
  if (rows.isEmpty()) {
    QMessageBox::information(this, tr("Review missing metadata"),
                             tr("This collection has no items to review."));
    return;
  }
  QList<MetadataQueue::InputRow> inputs;
  inputs.reserve(rows.size());
  for (const IDatabaseManager::ItemPathRow &row : rows) {
    MetadataQueue::InputRow input;
    input.filePath = row.path;
    input.hasArtworkOnDisk = !row.artworkPath.trimmed().isEmpty();
    input.itemName = QFileInfo(row.path).completeBaseName();
    inputs.append(input);
  }
  const auto entries = MetadataQueue::build(uuid, inputs, [db](const QString &u, const QString &p) {
    return db->loadItemMetadata(u, p);
  });
  if (entries.isEmpty()) {
    QMessageBox::information(
        this, tr("Review missing metadata"),
        tr("Every item in \"%1\" already has the core metadata fields.").arg(cfg.name));
    return;
  }

  // Edit closure: open the existing per-item editor through the
  // interaction manager (it owns the closure that pops EditMetadataDialog
  // and persists the result). Returns true when something changed.
  auto onEdit = [this](const QString &filePath, const QString &itemName) {
    if (auto *im = m_appManager->getInteractionManager()) {
      // editItemMetadata is fire-and-forget; we can't tell from its
      // signature whether the user actually saved. Conservative: assume
      // an edit attempt counts and let reevaluate filter the queue.
      im->editItemMetadata(filePath, itemName);
      return true;
    }
    return false;
  };
  auto onReload = [db](const QString &u, const QString &p) { return db->loadItemMetadata(u, p); };

  MetadataReviewDialog dialog(this);
  dialog.setQueue(entries, std::move(onEdit), std::move(onReload));
  dialog.exec();

  // Refresh the sidebar so any edits made during the review are visible
  // without a separate collection switch.
  if (m_appManager->getNavigationManager()) {
    m_appManager->getNavigationManager()->safeReloadCollection(currentCollectionIndex);
  }
}

void MainWindow::artworkWizardInteractive() {
  if (currentCollectionIndex < 0 || currentCollectionIndex >= m_collections.size()) {
    QMessageBox::information(this, tr("Assign missing artwork"),
                             tr("Open a collection before running the wizard."));
    return;
  }
  const CollectionConfig &cfg = m_collections[currentCollectionIndex];
  IDatabaseManager *db = m_appManager->getDatabaseManager();
  if (!db) return;
  const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
  const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
  if (uuid.isEmpty()) return;
  const QString artworkDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);
  if (artworkDir.trimmed().isEmpty()) {
    QMessageBox::information(this, tr("Assign missing artwork"),
                             tr("This collection has no artwork directory configured."));
    return;
  }

  // Queue = items where items.artwork_path is empty / NULL. Pull the
  // path list, filter, and build the wizard's per-entry record.
  const auto rows = db->loadAllItemPathsForCollection(uuid);
  QList<ArtworkWizardDialog::Entry> queue;
  for (const IDatabaseManager::ItemPathRow &row : rows) {
    if (!row.artworkPath.trimmed().isEmpty()) {
      continue;
    }
    ArtworkWizardDialog::Entry entry;
    entry.filePath = row.path;
    entry.collectionUuid = uuid;
    entry.itemName = QFileInfo(row.path).completeBaseName();
    queue.append(entry);
  }
  if (queue.isEmpty()) {
    QMessageBox::information(this, tr("Assign missing artwork"),
                             tr("Every item in \"%1\" already has artwork.").arg(cfg.name));
    return;
  }

  // Snapshot the artwork directory listing once. The wizard ranks
  // candidates per item but re-walking the directory for every entry
  // would be wasteful — these directories typically contain hundreds of
  // files and the listing is stable for the wizard's lifetime.
  QStringList directoryFiles;
  {
    QDir dir(artworkDir);
    const QStringList entries = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    directoryFiles.reserve(entries.size());
    for (const QString &name : entries) {
      directoryFiles.append(dir.absoluteFilePath(name));
    }
  }

  auto candidatesFor = [directoryFiles](const ArtworkWizardDialog::Entry &entry) {
    return ArtworkCandidates::rank(entry.itemName, directoryFiles);
  };
  auto onPick = [db](const ArtworkWizardDialog::Entry &entry, const QString &chosenFilePath) {
    ItemArtworkStore::ItemArtwork row;
    row.collectionUuid = entry.collectionUuid;
    row.path = entry.filePath;
    // "Front" is the cross-provider primary-cover slot used by the
    // sidebar gallery and tile renderer; saving here makes the picked
    // image surface as the item's main artwork immediately.
    row.artworkType = ItemArtworkStore::StandardTypes::Front;
    row.manualPath = chosenFilePath;
    return db->saveItemArtwork(row);
  };

  ArtworkWizardDialog dialog(this);
  dialog.setQueue(queue, std::move(candidatesFor), std::move(onPick));
  dialog.exec();

  // safeReloadCollection so newly-assigned artwork surfaces in the grid
  // and sidebar without requiring a collection switch.
  if (m_appManager->getNavigationManager()) {
    m_appManager->getNavigationManager()->safeReloadCollection(currentCollectionIndex);
  }
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
  // Same persist-and-navigate sequence the first-run wizard uses:
  // append → save → rebuild hierarchy → navigate.
  m_collections.append(result.pickedConfig);
  if (m_appManager->getSettingsManager()) {
    m_appManager->getSettingsManager()->saveCollections(m_collections);
  }
  rebuildHierarchyCache();
  if (m_appManager->getNavigationManager()) {
    currentCollectionIndex = m_collections.size() - 1;
    m_appManager->getNavigationManager()->showCollectionItems(currentCollectionIndex);
  }
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
      m_appManager->getSettingsManager()->saveGeneralSettings(m_generalSettings);
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
