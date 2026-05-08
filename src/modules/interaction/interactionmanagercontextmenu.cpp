// Sibling translation unit for InteractionManager.
// Right-click context menu implementation.
// These remain InteractionManager members; this is a translation-unit split.
#include "interactionmanager.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>

#include "collectionutils.h"
#include "customfieldsdialog.h"
#include "databasemanager.h"
#include "detailspane.h"
#include "detailspanemanager.h"
#include "itemmetadata.h"
#include "itemwidget.h"
#include "launcherchooserdialog.h"
#include "launchmanager.h"
#include "navigationmanager.h"
#include "pathutils.h"
#include "playlistmanager.h"
#include "scrollmanager.h"
#include "selectionmanager.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcInteractionManager)

void InteractionManager::showContextMenu(ItemWidget *widget, int visualIndex,
                                         const QPoint &globalPos) {
  if (!widget || visualIndex < 0) {
    return;
  }

  // Select the right-clicked item so the user sees which item is targeted
  selectItemByIndex(visualIndex, true);

  const bool isSubcollection = widget->isSubcollection();
  const bool isVirtualFolder = widget->isVirtualFolder();
  const bool isMediaItem = !isSubcollection && !isVirtualFolder;
  const QString filePath = widget->getFilePath();

  QMenu menu;

  // --- Launch action (only for media items with a file path) ---
  if (isMediaItem && !filePath.isEmpty()) {
    QAction *launchAction = menu.addAction(tr("Launch"));
    QObject::connect(launchAction, &QAction::triggered, this, [this, filePath]() {
      if (!m_databaseManager || !m_currentCollectionIndex) {
        return;
      }
      const int cIdx = m_databaseManager->getCollectionIndexForFile(filePath);
      const int ownerIdx = (cIdx >= 0 ? cIdx : *m_currentCollectionIndex);
      launchItemWithCollection(filePath, ownerIdx);
    });
  }

  // --- Open (enter) action for subcollections and virtual folders ---
  if (isSubcollection || isVirtualFolder) {
    QAction *openAction = menu.addAction(tr("Open"));
    QObject::connect(openAction, &QAction::triggered, this, [this]() {
      if (m_scrollManager) {
        const int totalItems = m_scrollManager->getTotalItems();
        processEnterOrReturnKey(totalItems);
      }
    });
  }

  menu.addSeparator();

  // --- Toggle sidebar (properties) action ---
  QAction *propertiesAction = menu.addAction(tr("Properties"));
  QObject::connect(propertiesAction, &QAction::triggered, this, [this]() {
    if (m_detailsPaneManager) {
      m_detailsPaneManager->toggleSidebar();
    }
  });

  // --- Refresh action (soft reload of current collection) ---
  QAction *refreshAction = menu.addAction(tr("Refresh"));
  QObject::connect(refreshAction, &QAction::triggered, this, [this]() {
    if (m_navigationManager && m_currentCollectionIndex && *m_currentCollectionIndex >= 0) {
      m_navigationManager->safeReloadCollection(*m_currentCollectionIndex);
    }
  });

  // --- Edit custom fields (, media items only) ---
  if (isMediaItem && !filePath.isEmpty() && m_databaseManager && m_collections &&
      m_currentCollectionIndex) {
    menu.addSeparator();
    QAction *customFieldsAction = menu.addAction(tr("Edit custom fields..."));
    const QString itemName = widget->getItemName();
    QObject::connect(customFieldsAction, &QAction::triggered, this,
                     [this, filePath, itemName]() { editCustomFields(filePath, itemName); });

    // --- Set / clear per-item manual override ---
    // Show "Set manual file..." always (lets the user point at any file).
    // Show "Clear manual override" only when an override is currently set,
    // mirroring how custom fields silently no-op when none exist.
    QAction *setManualAction = menu.addAction(tr("Set manual file..."));
    QObject::connect(setManualAction, &QAction::triggered, this, [this, filePath]() {
      const QString picked = QFileDialog::getOpenFileName(
          QApplication::activeWindow(), tr("Select Manual File"), QString(),
          tr("Manual Files (*.pdf *.epub *.cbr *.cbz *.djvu *.txt *.md *.html *.htm "
             "*.rtf *.doc *.docx *.odt *.png *.jpg *.jpeg);;All Files (*)"));
      if (picked.isEmpty()) {
        return;
      }
      setItemManualPath(filePath, picked);
    });

    // Only offer "Clear" when a manual_path override actually exists for the
    // selected item; querying the DB here keeps the menu honest about what
    // it can do (vs. always offering an action that may no-op).
    int owningIndex = m_databaseManager->getCollectionIndexForFile(filePath);
    if (owningIndex < 0) {
      owningIndex = *m_currentCollectionIndex;
    }
    if (CollectionUtils::isValidIndex(owningIndex, m_collections)) {
      const CollectionConfig &owning = (*m_collections)[owningIndex];
      const QString expandedMediaDir =
          PathUtils::validateAndExpandPath(owning.mediaDirectory, owning.name);
      const QString uuid = CollectionUtils::computeCollectionUuid(owning.name, expandedMediaDir);
      if (!uuid.isEmpty()) {
        const ItemMetadataStore::ItemMetadata md =
            m_databaseManager->loadItemMetadata(uuid, filePath);
        if (!md.manualPath.isEmpty()) {
          QAction *clearManualAction = menu.addAction(tr("Clear manual override"));
          QObject::connect(clearManualAction, &QAction::triggered, this,
                           [this, filePath]() { setItemManualPath(filePath, QString()); });
        }
      }

      // --- Per-item launcher override ---
      // Only meaningful when the owning collection has more than one
      // launcher — pinning a single-launcher collection to "launcher 0"
      // would be a no-op masquerading as a configuration choice.
      if (owning.launcherCount() > 1) {
        QAction *setLauncherAction = menu.addAction(tr("Always launch with..."));
        const int launcherCount = owning.launcherCount();
        const QString collectionName = owning.name;
        QStringList launcherNames;
        launcherNames.reserve(launcherCount);
        for (int i = 0; i < launcherCount; ++i) {
          launcherNames << owning.launcherDisplayName(i);
        }
        const int currentOverride =
            uuid.isEmpty() ? -1 : m_databaseManager->loadItemMetadata(uuid, filePath).launcherIndex;
        const int defaultIndex =
            currentOverride >= 0 && currentOverride < launcherCount
                ? currentOverride
                : std::clamp(owning.defaultLauncherIndex, 0, launcherCount - 1);
        QObject::connect(setLauncherAction, &QAction::triggered, this,
                         [this, filePath, collectionName, launcherNames, defaultIndex]() {
                           const int chosen = LauncherChooserDialog::choose(
                               QApplication::activeWindow(), collectionName, launcherNames,
                               defaultIndex);
                           if (chosen < 0) {
                             return; // User cancelled.
                           }
                           setItemLauncherOverride(filePath, chosen);
                         });
        if (currentOverride >= 0) {
          QAction *clearLauncherAction = menu.addAction(tr("Clear launcher override"));
          QObject::connect(clearLauncherAction, &QAction::triggered, this,
                           [this, filePath]() { setItemLauncherOverride(filePath, -1); });
        }
      }
    }
  }

  // ─── Playlist actions ──────────────────────────────────────
  // Two surfaces:
  //   (a) On any media item — "Add to playlist ▶ <list> | New playlist…"
  //       lets the user assemble playlists from anywhere in the library.
  //   (b) Inside a playlist — "Remove from playlist" so the chooser dialog
  //       isn't the only way out, plus rename/delete on the playlist itself.
  // The playlist itself is identified via the synthesized CollectionConfig at
  // m_currentCollectionIndex (isPlaylist=true) so the surrounding code keeps
  // treating playlists as ordinary virtual collections.
  if (m_playlistManager && m_collections && m_currentCollectionIndex && m_databaseManager) {
    const bool insideCollection =
        CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections);
    const bool insidePlaylist =
        insideCollection && (*m_collections)[*m_currentCollectionIndex].isPlaylist;

    if (isMediaItem && !filePath.isEmpty()) {
      // Resolve the source collection's uuid (using the file→collection map
      // built during the items range fetch). Add-to-playlist needs the uuid to
      // store a stable (uuid, path) reference that survives item id
      // renumbering across rescans.
      int owningIdx = m_databaseManager->getCollectionIndexForFile(filePath);
      if (owningIdx < 0 && !insidePlaylist) {
        owningIdx = *m_currentCollectionIndex;
      }
      QString srcUuid;
      if (CollectionUtils::isValidIndex(owningIdx, m_collections)) {
        const CollectionConfig &owning = (*m_collections)[owningIdx];
        const QString expandedMediaDir =
            PathUtils::validateAndExpandPath(owning.mediaDirectory, owning.name);
        srcUuid = CollectionUtils::computeCollectionUuid(owning.name, expandedMediaDir);
      }

      menu.addSeparator();

      // top-level favorites toggle. Faster than navigating into
      // the "Add to playlist" submenu and locating the favorites entry, since
      // starring is the most common per-item playlist action. The label flips
      // based on current membership so a single click is always meaningful.
      const QString favouritesId = m_playlistManager->ensureFavoritesPlaylist();
      if (!favouritesId.isEmpty() && !srcUuid.isEmpty()) {
        const bool alreadyFavorite =
            m_playlistManager->containsItem(favouritesId, srcUuid, filePath);
        QAction *favAction =
            menu.addAction(alreadyFavorite ? tr("Remove from Favorites") : tr("Add to Favorites"));
        QObject::connect(
            favAction, &QAction::triggered, this,
            [this, favouritesId, srcUuid, filePath, alreadyFavorite]() {
              if (!m_playlistManager) {
                return;
              }
              if (alreadyFavorite) {
                m_playlistManager->removeItem(favouritesId, srcUuid, filePath);
              } else {
                m_playlistManager->addItem(favouritesId, srcUuid, filePath);
              }
              // When the user is currently inside the favorites
              // playlist, the count just changed — reload so the
              // grid drops/appends the affected tile right away
              // rather than waiting for the next manual refresh.
              if (m_navigationManager && m_currentCollectionIndex &&
                  CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections) &&
                  (*m_collections)[*m_currentCollectionIndex].playlistId == favouritesId) {
                m_navigationManager->safeReloadCollection(*m_currentCollectionIndex);
              }
            });
      }

      QMenu *addToMenu = menu.addMenu(tr("Add to playlist"));

      QAction *newPlaylistAction = addToMenu->addAction(tr("New playlist…"));
      QObject::connect(newPlaylistAction, &QAction::triggered, this,
                       [this, srcUuid, filePath]() { addItemToNewPlaylist(srcUuid, filePath); });

      // import a playlist from a JSON or M3U file. Lives next
      // to "New playlist…" rather than under a separate top-level entry so
      // the discovery surface for "create a playlist" is one place.
      QAction *importAction = addToMenu->addAction(tr("Import playlist from file…"));
      QObject::connect(importAction, &QAction::triggered, this,
                       [this]() { importPlaylistFromFile(); });

      const QList<PlaylistRow> playlists = m_playlistManager->loadAll();
      if (!playlists.isEmpty()) {
        addToMenu->addSeparator();
        // The chooser silently no-ops when the (uuid, path) pair is already in
        // the playlist (containsItem inside addItem) — keeps the menu honest
        // without requiring a separate "already added" disabled state.
        for (const PlaylistRow &row : playlists) {
          QString label = row.name.isEmpty() ? tr("(unnamed)") : row.name;
          QAction *action = addToMenu->addAction(label);
          const QString playlistId = row.id;
          QObject::connect(action, &QAction::triggered, this,
                           [this, playlistId, srcUuid, filePath]() {
                             addItemToPlaylist(playlistId, srcUuid, filePath);
                           });
        }
      }

      // Inside a playlist, also offer the inverse action.
      if (insidePlaylist) {
        const QString playlistId = (*m_collections)[*m_currentCollectionIndex].playlistId;
        QAction *removeAction = menu.addAction(tr("Remove from playlist"));
        QObject::connect(removeAction, &QAction::triggered, this,
                         [this, playlistId, srcUuid, filePath]() {
                           if (m_playlistManager) {
                             m_playlistManager->removeItem(playlistId, srcUuid, filePath);
                             // playlistsChanged → MainWindow re-syncs the
                             // CollectionConfigs; the safeReload below picks
                             // up the new (smaller) item count.
                             if (m_navigationManager && m_currentCollectionIndex) {
                               m_navigationManager->safeReloadCollection(*m_currentCollectionIndex);
                             }
                           }
                         });
      }
    }

    // Playlist-level actions when the user is currently viewing one. Kept off
    // the bottom of the menu so the per-item actions above remain the
    // primary affordance.
    if (insidePlaylist) {
      menu.addSeparator();
      const QString playlistId = (*m_collections)[*m_currentCollectionIndex].playlistId;
      const QString currentName = (*m_collections)[*m_currentCollectionIndex].name;
      // built-in playlists keep rename (so users can localize
      // the label) but hide delete — PlaylistManager refuses the call anyway,
      // and surfacing a button that always errors is worse UX than just
      // omitting it.
      const bool isReserved =
          !(*m_collections)[*m_currentCollectionIndex].playlistReservedKind.isEmpty();

      QAction *renameAction = menu.addAction(tr("Rename playlist…"));
      QObject::connect(renameAction, &QAction::triggered, this, [this, playlistId, currentName]() {
        renamePlaylistDialog(playlistId, currentName);
      });

      // export the current playlist. The submenu houses both
      // formats so the menu stays scannable; M3U for cross-app interop, JSON
      // for lossless Kartend round-trip.
      QMenu *exportMenu = menu.addMenu(tr("Export playlist"));
      QAction *exportJsonAction = exportMenu->addAction(tr("As JSON…"));
      QObject::connect(exportJsonAction, &QAction::triggered, this,
                       [this, playlistId, currentName]() {
                         exportPlaylistToFile(playlistId, currentName, /*asJson=*/true);
                       });
      QAction *exportM3uAction = exportMenu->addAction(tr("As M3U…"));
      QObject::connect(exportM3uAction, &QAction::triggered, this,
                       [this, playlistId, currentName]() {
                         exportPlaylistToFile(playlistId, currentName, /*asJson=*/false);
                       });

      if (!isReserved) {
        QAction *deleteAction = menu.addAction(tr("Delete playlist…"));
        QObject::connect(
            deleteAction, &QAction::triggered, this,
            [this, playlistId, currentName]() { deletePlaylistConfirm(playlistId, currentName); });
      }
    }
  }

  menu.exec(globalPos);
}

void InteractionManager::addItemToNewPlaylist(const QString &srcUuid, const QString &filePath) {
  if (!m_playlistManager) {
    return;
  }
  bool ok = false;
  const QString name =
      QInputDialog::getText(QApplication::activeWindow(), tr("New Playlist"), tr("Playlist name:"),
                            QLineEdit::Normal, QString(), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  auto created = m_playlistManager->createPlaylist(name);
  if (created.isError()) {
    return;
  }
  const QString &newId = created.value();
  if (!filePath.isEmpty()) {
    m_playlistManager->addItem(newId, srcUuid, filePath);
  }
}

void InteractionManager::addItemToPlaylist(const QString &playlistId, const QString &srcUuid,
                                           const QString &filePath) {
  if (!m_playlistManager || playlistId.isEmpty() || filePath.isEmpty()) {
    return;
  }
  m_playlistManager->addItem(playlistId, srcUuid, filePath);
}

void InteractionManager::renamePlaylistDialog(const QString &playlistId,
                                              const QString &currentName) {
  if (!m_playlistManager || playlistId.isEmpty()) {
    return;
  }
  bool ok = false;
  const QString newName =
      QInputDialog::getText(QApplication::activeWindow(), tr("Rename Playlist"), tr("New name:"),
                            QLineEdit::Normal, currentName, &ok);
  if (!ok || newName.trimmed().isEmpty() || newName == currentName) {
    return;
  }
  m_playlistManager->renamePlaylist(playlistId, newName);
}

void InteractionManager::deletePlaylistConfirm(const QString &playlistId,
                                               const QString &currentName) {
  if (!m_playlistManager || playlistId.isEmpty()) {
    return;
  }
  // Confirm because the operation cascades to playlist_items and there's no
  // undo for the row removal (the source items themselves are untouched).
  const auto choice = QMessageBox::question(
      QApplication::activeWindow(), tr("Delete Playlist"),
      tr("Delete the playlist \"%1\"? Its items will not be removed from their source "
         "collections.")
          .arg(currentName),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (choice != QMessageBox::Yes) {
    return;
  }
  if (m_playlistManager->deletePlaylist(playlistId) && m_navigationManager) {
    // Pop back to the parent so the now-deleted playlist isn't left visible
    // mid-fetch. The synthesized CollectionConfig will be removed on the
    // next resync triggered by playlistsChanged.
    m_navigationManager->safeReloadCollection(0);
  }
}

void InteractionManager::editCustomFields(const QString &filePath, const QString &itemName) {
  if (!m_databaseManager || !m_collections || !m_currentCollectionIndex) {
    return;
  }
  // The owning collection of the file may differ from the displayed one in
  // showAllSubcollectionItems mode; mirror DetailsPaneManager and prefer the
  // file's resolved collection so the metadata row's UUID matches across
  // navigation modes.
  int owningIndex = m_databaseManager->getCollectionIndexForFile(filePath);
  if (owningIndex < 0) {
    owningIndex = *m_currentCollectionIndex;
  }
  if (!CollectionUtils::isValidIndex(owningIndex, m_collections)) {
    return;
  }
  const CollectionConfig &owning = (*m_collections)[owningIndex];
  const QString expandedMediaDir =
      PathUtils::validateAndExpandPath(owning.mediaDirectory, owning.name);
  const QString uuid = CollectionUtils::computeCollectionUuid(owning.name, expandedMediaDir);
  if (uuid.isEmpty()) {
    return;
  }

  ItemMetadataStore::ItemMetadata metadata = m_databaseManager->loadItemMetadata(uuid, filePath);
  metadata.collectionUuid = uuid;
  metadata.path = filePath;

  CustomFieldsDialog dialog(QApplication::activeWindow());
  dialog.setItemTitle(itemName);
  dialog.setFields(ItemMetadataStore::parseCustomFields(metadata.customFields));
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  metadata.customFields = ItemMetadataStore::serializeCustomFields(dialog.fields());
  // Mark the row as user-edited so future scraper integrations can decide
  // whether to overwrite. Existing rows from a scraper keep their source
  // until the user touches them via this dialog.
  metadata.source = QStringLiteral("user");
  if (!m_databaseManager->saveItemMetadata(metadata)) {
    return;
  }

  // Refresh the sidebar so the new fields render immediately.
  if (m_detailsPaneManager) {
    m_detailsPaneManager->updateSidebarMetadata(
        m_selectionManager ? m_selectionManager->selectedWidget() : nullptr);
  }
}

void InteractionManager::setItemManualPath(const QString &filePath, const QString &manualPath) {
  if (!m_databaseManager || !m_collections || !m_currentCollectionIndex) {
    return;
  }
  // Resolve the owning collection the same way editCustomFields does so the
  // (uuid, path) key matches across showAllSubcollectionItems navigation.
  int owningIndex = m_databaseManager->getCollectionIndexForFile(filePath);
  if (owningIndex < 0) {
    owningIndex = *m_currentCollectionIndex;
  }
  if (!CollectionUtils::isValidIndex(owningIndex, m_collections)) {
    return;
  }
  const CollectionConfig &owning = (*m_collections)[owningIndex];
  const QString expandedMediaDir =
      PathUtils::validateAndExpandPath(owning.mediaDirectory, owning.name);
  const QString uuid = CollectionUtils::computeCollectionUuid(owning.name, expandedMediaDir);
  if (uuid.isEmpty()) {
    return;
  }

  ItemMetadataStore::ItemMetadata metadata = m_databaseManager->loadItemMetadata(uuid, filePath);
  metadata.collectionUuid = uuid;
  metadata.path = filePath;
  metadata.manualPath = manualPath;
  // User-driven edit: stamp the source so future scrapers know this row was
  // touched by the user (matches editCustomFields behavior).
  metadata.source = QStringLiteral("user");
  if (!m_databaseManager->saveItemMetadata(metadata)) {
    return;
  }
  if (m_detailsPaneManager) {
    m_detailsPaneManager->updateSidebarMetadata(
        m_selectionManager ? m_selectionManager->selectedWidget() : nullptr);
  }
}

void InteractionManager::setItemLauncherOverride(const QString &filePath, int launcherIndex) {
  if (!m_databaseManager || !m_collections || !m_currentCollectionIndex) {
    return;
  }
  // Mirror setItemManualPath's owner-resolution so the (uuid, path) key
  // matches across showAllSubcollectionItems navigation. Negative indices
  // mean "clear the override".
  int owningIndex = m_databaseManager->getCollectionIndexForFile(filePath);
  if (owningIndex < 0) {
    owningIndex = *m_currentCollectionIndex;
  }
  if (!CollectionUtils::isValidIndex(owningIndex, m_collections)) {
    return;
  }
  const CollectionConfig &owning = (*m_collections)[owningIndex];
  const QString expandedMediaDir =
      PathUtils::validateAndExpandPath(owning.mediaDirectory, owning.name);
  const QString uuid = CollectionUtils::computeCollectionUuid(owning.name, expandedMediaDir);
  if (uuid.isEmpty()) {
    return;
  }

  ItemMetadataStore::ItemMetadata metadata = m_databaseManager->loadItemMetadata(uuid, filePath);
  metadata.collectionUuid = uuid;
  metadata.path = filePath;
  // Clamp incoming overrides into the visible launcher range so a stale UI
  // pick can never pin past the end. -1 stays -1 to clear.
  metadata.launcherIndex =
      launcherIndex < 0 ? -1 : std::clamp(launcherIndex, 0, owning.launcherCount() - 1);
  metadata.source = QStringLiteral("user");
  if (!m_databaseManager->saveItemMetadata(metadata)) {
    return;
  }
  if (m_detailsPaneManager) {
    m_detailsPaneManager->updateSidebarMetadata(
        m_selectionManager ? m_selectionManager->selectedWidget() : nullptr);
  }
}

void InteractionManager::exportPlaylistToFile(const QString &playlistId, const QString &currentName,
                                              bool asJson) {
  if (!m_playlistManager || playlistId.isEmpty()) {
    return;
  }
  const QString defaultExt = asJson ? QStringLiteral(".json") : QStringLiteral(".m3u");
  // Suggest a filename that pre-fills the save dialog with the playlist's
  // current name + the format-appropriate extension. Sanitisation is left to
  // the platform file dialog — Qt's QFileDialog handles platform-native
  // illegal-character handling per OS.
  const QString suggestion = currentName.trimmed().isEmpty()
                                 ? QStringLiteral("playlist") + defaultExt
                                 : currentName.trimmed() + defaultExt;
  const QString filterJson = tr("Kartend Playlist (*.json)");
  const QString filterM3u = tr("M3U Playlist (*.m3u)");
  const QString chosen =
      QFileDialog::getSaveFileName(QApplication::activeWindow(), tr("Export Playlist"), suggestion,
                                   asJson ? filterJson : filterM3u);
  if (chosen.isEmpty()) {
    return; // User cancelled.
  }

  // Append the format extension when the user didn't include one — keeps the
  // file recognisable to the format-detector in importPlaylistFromFile.
  QString outPath = chosen;
  if (!outPath.endsWith(defaultExt, Qt::CaseInsensitive)) {
    outPath += defaultExt;
  }

  auto result = asJson ? m_playlistManager->exportToJson(playlistId, outPath)
                       : m_playlistManager->exportToM3U(playlistId, outPath);
  if (result.isError()) {
    QMessageBox::warning(QApplication::activeWindow(), tr("Export Failed"), result.error().message);
    return;
  }
  QMessageBox::information(QApplication::activeWindow(), tr("Export Complete"),
                           tr("Wrote %1 item(s) to %2").arg(result.value()).arg(outPath));
}

void InteractionManager::importPlaylistFromFile() {
  if (!m_playlistManager) {
    return;
  }
  const QString chosen = QFileDialog::getOpenFileName(
      QApplication::activeWindow(), tr("Import Playlist"), QString(),
      tr("Playlist Files (*.json *.m3u);;Kartend Playlist (*.json);;M3U Playlist (*.m3u);;"
         "All Files (*)"));
  if (chosen.isEmpty()) {
    return;
  }

  // Sniff format from the extension. JSON is the lossless Kartend format;
  // anything else is assumed to be M3U so unusual extensions (e.g. .pls) at
  // least try the path-per-line parser instead of failing out.
  const bool isJson = chosen.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive);
  if (isJson) {
    auto result = m_playlistManager->importFromJson(chosen);
    if (result.isError()) {
      QMessageBox::warning(QApplication::activeWindow(), tr("Import Failed"),
                           result.error().message);
    }
    return;
  }

  int skipped = 0;
  auto result =
      m_playlistManager->importFromM3U(chosen, QFileInfo(chosen).completeBaseName(), &skipped);
  if (result.isError()) {
    QMessageBox::warning(QApplication::activeWindow(), tr("Import Failed"), result.error().message);
    return;
  }
  if (skipped > 0) {
    // Surface the skipped count in a single completion dialog so the user
    // knows their imported playlist may be shorter than the source — without
    // forcing them through one warning per missing entry.
    QMessageBox::information(
        QApplication::activeWindow(), tr("Import Complete"),
        tr("Imported playlist; %1 entries skipped (no matching items in the library).")
            .arg(skipped));
  }
}
