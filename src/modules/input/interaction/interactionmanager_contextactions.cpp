// Sibling translation unit for InteractionManager.
// Context-menu *action handlers* — the slot-side of the right-click menu
// items showContextMenu() builds. These are split out of
// interactionmanager_contextmenu.cpp so that file can stay focused on the
// QMenu / action assembly. Each function below is a discrete user action:
// add-to-playlist, item-metadata edit, manual-path picker, launcher-override
// chooser, pin/hide/continue-later toggles, smart-playlist CRUD, playlist
// import/export, command-preview dialog.
//
// All remain InteractionManager members; this is purely a TU split.

#include "interactionmanager.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QUrl>

#include "collection/collectionconfig.h"
#include "collection/launcherpreset.h"
#include "collection/typehelpers.h"
#include "collection/validationhelpers.h"
// The two dialogs (createsmartplaylistdialog.h / editmetadatadialog.h) are
// launched via owner-supplied closures so the input layer doesn't need the
// ui/ dialog headers. EditMetadataPayload's full definition lives in
// itemmetadata.h, included below.
#include "idatabasemanager.h"
#include "idetailspanemanager.h"
#include "imainwindow.h"
#include "inavigationmanager.h"
#include "iplaylistmanager.h"
#include "iselectionmanager.h"
#include "itemmetadata.h"
#include "launchmanager.h"
#include "pathutils.h"

#include <algorithm>
#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcInteractionManager)

void InteractionManager::addItemToNewPlaylist(const QString &srcUuid, const QString &filePath) {
  if (!playlistMgr()) {
    return;
  }
  bool ok = false;
  const QString name =
      QInputDialog::getText(QApplication::activeWindow(), tr("New Playlist"), tr("Playlist name:"),
                            QLineEdit::Normal, QString(), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  auto created = playlistMgr()->createPlaylist(name);
  if (created.isError()) {
    return;
  }
  const QString &newId = created.value();
  if (!filePath.isEmpty()) {
    playlistMgr()->addItem(newId, srcUuid, filePath);
  }
}

void InteractionManager::addItemToPlaylist(const QString &playlistId, const QString &srcUuid,
                                           const QString &filePath) {
  if (!playlistMgr() || playlistId.isEmpty() || filePath.isEmpty()) {
    return;
  }
  playlistMgr()->addItem(playlistId, srcUuid, filePath);
}

void InteractionManager::createSmartPlaylistDialog() {
  if (!playlistMgr() || !m_runSmartPlaylistDialog) {
    return;
  }
  auto edit =
      m_runSmartPlaylistDialog(QString(), std::nullopt, collectSmartPlaylistCollectionEntries());
  if (!edit.has_value() || edit->name.isEmpty()) {
    return;
  }
  auto created = playlistMgr()->createSmartPlaylist(edit->name, edit->filter);
  if (created.isError()) {
    QMessageBox::warning(QApplication::activeWindow(), tr("Could not create smart playlist"),
                         created.error().message);
  }
  // The synthesizer + sidebar refresh happens via the playlistsChanged
  // signal that createSmartPlaylist already emitted; nothing else to do.
}

void InteractionManager::editSmartPlaylistDialog(const QString &playlistId,
                                                 const QString &currentName) {
  if (!playlistMgr() || playlistId.isEmpty() || !m_runSmartPlaylistDialog) {
    return;
  }
  auto loaded = playlistMgr()->loadSmartFilter(playlistId);
  if (loaded.isError()) {
    QMessageBox::warning(QApplication::activeWindow(), tr("Could not load smart filter"),
                         loaded.error().message);
    return;
  }
  auto edit = m_runSmartPlaylistDialog(currentName, loaded.value(),
                                       collectSmartPlaylistCollectionEntries());
  if (!edit.has_value()) {
    return;
  }
  if (!playlistMgr()->updateSmartFilter(playlistId, edit->filter)) {
    QMessageBox::warning(QApplication::activeWindow(), tr("Could not update smart filter"),
                         tr("The filter update failed. See logs for details."));
    return;
  }
  // Rename if the user changed the name. Done after the filter update so
  // a partial failure (rename ok, filter ok, but signal handlers race)
  // still lands the more important payload first.
  if (!edit->name.isEmpty() && edit->name != currentName) {
    playlistMgr()->renamePlaylist(playlistId, edit->name);
  }
  // Re-evaluate and re-render the current view so the new filter takes
  // effect immediately rather than on the next collection switch.
  if (navMgr() && m_currentCollectionIndex) {
    navMgr()->safeReloadCollection(*m_currentCollectionIndex);
  }
}

void InteractionManager::renamePlaylistDialog(const QString &playlistId,
                                              const QString &currentName) {
  if (!playlistMgr() || playlistId.isEmpty()) {
    return;
  }
  bool ok = false;
  const QString newName =
      QInputDialog::getText(QApplication::activeWindow(), tr("Rename Playlist"), tr("New name:"),
                            QLineEdit::Normal, currentName, &ok);
  if (!ok || newName.trimmed().isEmpty() || newName == currentName) {
    return;
  }
  playlistMgr()->renamePlaylist(playlistId, newName);
}

void InteractionManager::deletePlaylistConfirm(const QString &playlistId,
                                               const QString &currentName) {
  if (!playlistMgr() || playlistId.isEmpty()) {
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
  if (playlistMgr()->deletePlaylist(playlistId) && navMgr()) {
    // Pop back to the parent so the now-deleted playlist isn't left visible
    // mid-fetch. The synthesized CollectionConfig will be removed on the
    // next resync triggered by playlistsChanged.
    navMgr()->safeReloadCollection(0);
  }
}

void InteractionManager::editItemMetadata(const QString &filePath, const QString &itemName) {
  if (!databaseMgr() || !m_collections || !m_currentCollectionIndex) {
    return;
  }
  // The owning collection of the file may differ from the displayed one in
  // showAllSubcollectionItems mode; mirror DetailsPaneManager and prefer the
  // file's resolved collection so the metadata row's UUID matches across
  // navigation modes.
  int owningIndex = databaseMgr()->getCollectionIndexForFile(filePath);
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

  ItemMetadataStore::ItemMetadata metadata = databaseMgr()->loadItemMetadata(uuid, filePath);
  metadata.collectionUuid = uuid;
  metadata.path = filePath;

  if (!m_runEditMetadataDialog) {
    return;
  }

  EditMetadataPayload initial;
  initial.notes = metadata.notes;
  initial.tags = ItemMetadataStore::parseTags(metadata.tags);
  initial.rating = metadata.rating;
  initial.sourceUrl = metadata.sourceUrl;
  initial.customFields = ItemMetadataStore::parseCustomFields(metadata.customFields);

  auto edited = m_runEditMetadataDialog(itemName, initial);
  if (!edited.has_value()) {
    return;
  }

  metadata.notes = edited->notes;
  metadata.tags = ItemMetadataStore::serializeTags(edited->tags);
  metadata.rating = edited->rating;
  metadata.sourceUrl = edited->sourceUrl;
  metadata.customFields = ItemMetadataStore::serializeCustomFields(edited->customFields);
  // Mark the row as user-edited so future scraper integrations can decide
  // whether to overwrite. Existing rows from a scraper keep their source
  // until the user touches them via this dialog.
  metadata.source = QStringLiteral("user");
  if (!databaseMgr()->saveItemMetadata(metadata)) {
    return;
  }

  // Refresh the sidebar so the new fields render immediately. Bypass
  // the debounce — the user is staring at the dialog they just dismissed
  // and the metadata they edited is for the currently-displayed item.
  if (detailsPaneMgr()) {
    detailsPaneMgr()->refreshSidebarMetadataImmediate();
  }
}

void InteractionManager::setItemManualPath(const QString &filePath, const QString &manualPath) {
  if (!databaseMgr() || !m_collections || !m_currentCollectionIndex) {
    return;
  }
  // Resolve the owning collection the same way editItemMetadata does so the
  // (uuid, path) key matches across showAllSubcollectionItems navigation.
  int owningIndex = databaseMgr()->getCollectionIndexForFile(filePath);
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

  ItemMetadataStore::ItemMetadata metadata = databaseMgr()->loadItemMetadata(uuid, filePath);
  metadata.collectionUuid = uuid;
  metadata.path = filePath;
  metadata.manualPath = manualPath;
  // User-driven edit: stamp the source so future scrapers know this row was
  // touched by the user (matches editItemMetadata behavior).
  metadata.source = QStringLiteral("user");
  if (!databaseMgr()->saveItemMetadata(metadata)) {
    return;
  }
  if (detailsPaneMgr()) {
    detailsPaneMgr()->refreshSidebarMetadataImmediate();
  }
}

void InteractionManager::setItemLauncherOverride(const QString &filePath, int launcherIndex) {
  if (!databaseMgr() || !m_collections || !m_currentCollectionIndex) {
    return;
  }
  // Mirror setItemManualPath's owner-resolution so the (uuid, path) key
  // matches across showAllSubcollectionItems navigation. Negative indices
  // mean "clear the override".
  int owningIndex = databaseMgr()->getCollectionIndexForFile(filePath);
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

  ItemMetadataStore::ItemMetadata metadata = databaseMgr()->loadItemMetadata(uuid, filePath);
  metadata.collectionUuid = uuid;
  metadata.path = filePath;
  // Clamp incoming overrides into the visible launcher range so a stale UI
  // pick can never pin past the end. -1 stays -1 to clear.
  metadata.launcherIndex =
      launcherIndex < 0 ? -1 : std::clamp(launcherIndex, 0, owning.launcher.launcherCount() - 1);
  metadata.source = QStringLiteral("user");
  if (!databaseMgr()->saveItemMetadata(metadata)) {
    return;
  }
  if (detailsPaneMgr()) {
    detailsPaneMgr()->refreshSidebarMetadataImmediate();
  }
}

namespace {

/// Resolves the (collection_uuid, path) key for a media item, picking the
/// item's owning collection (which may differ from the displayed one in
/// showAllSubcollectionItems mode). Returns an empty string when the
/// resolution fails so callers can early-out instead of writing a row
/// keyed by a stale collection.
QString resolveOwningUuid(IDatabaseManager *db, QList<CollectionConfig> *collections,
                          int *currentCollectionIndex, const QString &filePath) {
  if (!db || !collections || !currentCollectionIndex) {
    return {};
  }
  int owningIndex = db->getCollectionIndexForFile(filePath);
  if (owningIndex < 0) {
    owningIndex = *currentCollectionIndex;
  }
  if (!CollectionUtils::isValidIndex(owningIndex, collections)) {
    return {};
  }
  const CollectionConfig &owning = (*collections)[owningIndex];
  const QString expandedMediaDir =
      PathUtils::validateAndExpandPath(owning.mediaDirectory, owning.name);
  return CollectionUtils::computeCollectionUuid(owning.name, expandedMediaDir);
}

} // namespace

SmartPlaylistCollectionEntries InteractionManager::collectSmartPlaylistCollectionEntries() const {
  SmartPlaylistCollectionEntries out;
  if (!m_collections) {
    return out;
  }
  out.reserve(m_collections->size());
  for (const CollectionConfig &cfg : *m_collections) {
    // Playlists are virtual collections — anchoring a smart filter on
    // their uuid would recurse through the smart-playlist evaluator and
    // produce surprising results. Skip them.
    if (cfg.isPlaylist) {
      continue;
    }
    const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
    const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
    if (uuid.isEmpty()) {
      continue;
    }
    out.append({cfg.name, uuid});
  }
  return out;
}

void InteractionManager::previewLaunchCommand(const QString &filePath, const QString &itemName) {
  if (!m_runLaunchPreviewDialog || !m_collections || !m_currentCollectionIndex) {
    return;
  }
  if (!databaseMgr()) {
    return;
  }
  // Resolve the owning collection the same way every other per-item action
  // does — the displayed collection may not own the file (e.g.
  // showAllSubcollectionItems mode).
  int owningIndex = databaseMgr()->getCollectionIndexForFile(filePath);
  if (owningIndex < 0) {
    owningIndex = *m_currentCollectionIndex;
  }
  if (!CollectionUtils::isValidIndex(owningIndex, m_collections)) {
    return;
  }
  const CollectionConfig &owning = (*m_collections)[owningIndex];

  // Pick the launcher index the same way launchItem would: per-item
  // override wins, then default-launcher, then 0. We don't pop the chooser
  // dialog here — the preview is meant to be a fast read-only surface, so
  // the user gets to see what would happen with the most likely launcher.
  // If they want a different one, they can pick via "Always launch with…".
  int launcherIndex = -1;
  if (owning.launcher.launcherCount() > 0) {
    const QString expandedMediaDir =
        PathUtils::validateAndExpandPath(owning.mediaDirectory, owning.name);
    const QString uuid = CollectionUtils::computeCollectionUuid(owning.name, expandedMediaDir);
    if (!uuid.isEmpty()) {
      const auto md = databaseMgr()->loadItemMetadata(uuid, filePath);
      if (md.launcherIndex >= 0 && md.launcherIndex < owning.launcher.launcherCount()) {
        launcherIndex = md.launcherIndex;
      }
    }
    if (launcherIndex < 0) {
      launcherIndex =
          std::clamp(owning.launcher.defaultLauncherIndex, 0, owning.launcher.launcherCount() - 1);
    }
  } else {
    launcherIndex = 0;
  }

  const LauncherConfig launcher = LauncherUtils::resolvePreset(
      owning.launcher.launcherAt(launcherIndex),
      m_generalSettings ? m_generalSettings->launchers.launcherPresets : QList<LauncherPreset>{});
  const QString launcherName = launcher.name.trimmed().isEmpty()
                                   ? owning.launcher.launcherDisplayName(launcherIndex)
                                   : launcher.name.trimmed();

  const auto preview = LaunchManager::previewLaunchCommand(owning, launcher, filePath);
  m_runLaunchPreviewDialog(itemName, launcherName, filePath, preview);
}

void InteractionManager::toggleItemPinned(const QString &filePath) {
  const QString uuid =
      resolveOwningUuid(databaseMgr(), m_collections, m_currentCollectionIndex, filePath);
  if (uuid.isEmpty()) {
    return;
  }
  ItemMetadataStore::ItemMetadata md = databaseMgr()->loadItemMetadata(uuid, filePath);
  md.collectionUuid = uuid;
  md.path = filePath;
  md.isPinned = !md.isPinned;
  md.source = QStringLiteral("user");
  if (databaseMgr()->saveItemMetadata(md) && detailsPaneMgr()) {
    detailsPaneMgr()->refreshSidebarMetadataImmediate();
  }
}

void InteractionManager::toggleItemHidden(const QString &filePath) {
  const QString uuid =
      resolveOwningUuid(databaseMgr(), m_collections, m_currentCollectionIndex, filePath);
  if (uuid.isEmpty()) {
    return;
  }
  ItemMetadataStore::ItemMetadata md = databaseMgr()->loadItemMetadata(uuid, filePath);
  md.collectionUuid = uuid;
  md.path = filePath;
  md.isHidden = !md.isHidden;
  md.source = QStringLiteral("user");
  if (databaseMgr()->saveItemMetadata(md) && detailsPaneMgr()) {
    detailsPaneMgr()->refreshSidebarMetadataImmediate();
  }
}

void InteractionManager::toggleItemContinueLater(const QString &filePath) {
  const QString uuid =
      resolveOwningUuid(databaseMgr(), m_collections, m_currentCollectionIndex, filePath);
  if (uuid.isEmpty()) {
    return;
  }
  ItemMetadataStore::ItemMetadata md = databaseMgr()->loadItemMetadata(uuid, filePath);
  md.collectionUuid = uuid;
  md.path = filePath;
  md.continueLater = !md.continueLater;
  md.source = QStringLiteral("user");
  if (databaseMgr()->saveItemMetadata(md) && detailsPaneMgr()) {
    detailsPaneMgr()->refreshSidebarMetadataImmediate();
  }
}

void InteractionManager::exportPlaylistToFile(const QString &playlistId, const QString &currentName,
                                              bool asJson) {
  if (!playlistMgr() || playlistId.isEmpty()) {
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

  auto result = asJson ? playlistMgr()->exportToJson(playlistId, outPath)
                       : playlistMgr()->exportToM3U(playlistId, outPath);
  if (result.isError()) {
    QMessageBox::warning(QApplication::activeWindow(), tr("Export Failed"), result.error().message);
    return;
  }
  // Kartend-o84pt: M3U is an interop format that carries no collection identity
  // or per-item titles, so a Kartend->M3U->Kartend round-trip is lossy (a path
  // in more than one collection can re-home on import). Point the user at the
  // JSON format for a lossless round-trip; the .json export preserves
  // source_collection_uuid.
  QString message = tr("Wrote %1 item(s) to %2").arg(result.value()).arg(outPath);
  if (!asJson) {
    message += QChar('\n');
    message += tr("Note: M3U is an interop format — collection identity and "
                  "titles aren't preserved. Export as Kartend Playlist (.json) "
                  "for a lossless round-trip.");
  }
  QMessageBox::information(QApplication::activeWindow(), tr("Export Complete"), message);
}

void InteractionManager::importPlaylistFromFile() {
  if (!playlistMgr()) {
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
    auto result = playlistMgr()->importFromJson(chosen);
    if (result.isError()) {
      QMessageBox::warning(QApplication::activeWindow(), tr("Import Failed"),
                           result.error().message);
    }
    return;
  }

  int skipped = 0;
  auto result =
      playlistMgr()->importFromM3U(chosen, QFileInfo(chosen).completeBaseName(), &skipped);
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
