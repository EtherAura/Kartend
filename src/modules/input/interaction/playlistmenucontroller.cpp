// Playlist action handlers extracted verbatim from
// interactionmanager_contextactions.cpp (Kartend-5lmt7). Each method is a
// discrete user action reachable from the right-click context menu:
// add-to-playlist (existing or new), smart-playlist create/edit dialogs,
// rename / delete with confirmation, and the JSON/M3U import/export flows.

#include "playlistmenucontroller.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "inavigationmanager.h"
#include "iplaylistmanager.h"
#include "pathutils.h"

PlaylistMenuController::PlaylistMenuController(QObject *parent) : QObject(parent) {}

IPlaylistManager *PlaylistMenuController::playlistMgr() const {
  return m_ctx ? m_ctx->playlistManager() : nullptr;
}

INavigationManager *PlaylistMenuController::navMgr() const {
  return m_ctx ? m_ctx->navigationManager() : nullptr;
}

void PlaylistMenuController::setupReferences(const PlaylistMenuControllerSetup &setup) {
  m_ctx = setup.ctx;
  m_collections = setup.collections;
  m_currentCollectionIndex = setup.currentCollectionIndex;
  m_runSmartPlaylistDialog = setup.runSmartPlaylistDialog;
  m_dialogs = setup.dialogs;
}

// ── Kartend-sqoq0: runner-or-stock-fallback helpers ─────────────────────────
// Owner-supplied closures (DialogRunners, wired by MainWindow) take priority;
// a null runner falls back to the original direct Qt dialog construction so
// unwired contexts behave exactly as before. Headless tests stub the runners
// and never see a modal.

void PlaylistMenuController::warnDialog(const QString &title, const QString &text) const {
  if (m_dialogs.warn) {
    m_dialogs.warn(title, text);
    return;
  }
  QMessageBox::warning(QApplication::activeWindow(), title, text);
}

void PlaylistMenuController::infoDialog(const QString &title, const QString &text) const {
  if (m_dialogs.info) {
    m_dialogs.info(title, text);
    return;
  }
  QMessageBox::information(QApplication::activeWindow(), title, text);
}

bool PlaylistMenuController::confirmDialog(const QString &title, const QString &text) const {
  if (m_dialogs.confirm) {
    return m_dialogs.confirm(title, text);
  }
  return QMessageBox::question(QApplication::activeWindow(), title, text,
                               QMessageBox::Yes | QMessageBox::No,
                               QMessageBox::No) == QMessageBox::Yes;
}

std::optional<QString> PlaylistMenuController::getTextDialog(const QString &title,
                                                             const QString &label,
                                                             const QString &initialText) const {
  if (m_dialogs.getText) {
    return m_dialogs.getText(title, label, initialText);
  }
  bool ok = false;
  const QString out = QInputDialog::getText(QApplication::activeWindow(), title, label,
                                            QLineEdit::Normal, initialText, &ok);
  if (!ok) {
    return std::nullopt;
  }
  return out;
}

QString PlaylistMenuController::getOpenFileNameDialog(const QString &caption,
                                                      const QString &startPath,
                                                      const QString &filter) const {
  if (m_dialogs.getOpenFileName) {
    return m_dialogs.getOpenFileName(caption, startPath, filter);
  }
  return QFileDialog::getOpenFileName(QApplication::activeWindow(), caption, startPath, filter);
}

QString PlaylistMenuController::getSaveFileNameDialog(const QString &caption,
                                                      const QString &startPath,
                                                      const QString &filter) const {
  if (m_dialogs.getSaveFileName) {
    return m_dialogs.getSaveFileName(caption, startPath, filter);
  }
  return QFileDialog::getSaveFileName(QApplication::activeWindow(), caption, startPath, filter);
}

SmartPlaylistCollectionEntries
PlaylistMenuController::collectSmartPlaylistCollectionEntries() const {
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

void PlaylistMenuController::addItemToNewPlaylist(const QString &srcUuid, const QString &filePath) {
  if (!playlistMgr()) {
    return;
  }
  const std::optional<QString> name =
      getTextDialog(tr("New Playlist"), tr("Playlist name:"), QString());
  if (!name.has_value() || name->trimmed().isEmpty()) {
    return;
  }
  auto created = playlistMgr()->createPlaylist(*name);
  if (created.isError()) {
    return;
  }
  const QString &newId = created.value();
  if (!filePath.isEmpty()) {
    playlistMgr()->addItem(newId, srcUuid, filePath);
  }
}

void PlaylistMenuController::addItemToPlaylist(const QString &playlistId, const QString &srcUuid,
                                               const QString &filePath) {
  if (!playlistMgr() || playlistId.isEmpty() || filePath.isEmpty()) {
    return;
  }
  playlistMgr()->addItem(playlistId, srcUuid, filePath);
}

void PlaylistMenuController::createSmartPlaylistDialog() {
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
    warnDialog(tr("Could not create smart playlist"), created.error().message);
  }
  // The synthesizer + sidebar refresh happens via the playlistsChanged
  // signal that createSmartPlaylist already emitted; nothing else to do.
}

void PlaylistMenuController::editSmartPlaylistDialog(const QString &playlistId,
                                                     const QString &currentName) {
  if (!playlistMgr() || playlistId.isEmpty() || !m_runSmartPlaylistDialog) {
    return;
  }
  auto loaded = playlistMgr()->loadSmartFilter(playlistId);
  if (loaded.isError()) {
    warnDialog(tr("Could not load smart filter"), loaded.error().message);
    return;
  }
  auto edit = m_runSmartPlaylistDialog(currentName, loaded.value(),
                                       collectSmartPlaylistCollectionEntries());
  if (!edit.has_value()) {
    return;
  }
  if (!playlistMgr()->updateSmartFilter(playlistId, edit->filter)) {
    warnDialog(tr("Could not update smart filter"),
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

void PlaylistMenuController::renamePlaylistDialog(const QString &playlistId,
                                                  const QString &currentName) {
  if (!playlistMgr() || playlistId.isEmpty()) {
    return;
  }
  const std::optional<QString> newName =
      getTextDialog(tr("Rename Playlist"), tr("New name:"), currentName);
  if (!newName.has_value() || newName->trimmed().isEmpty() || *newName == currentName) {
    return;
  }
  playlistMgr()->renamePlaylist(playlistId, *newName);
}

void PlaylistMenuController::deletePlaylistConfirm(const QString &playlistId,
                                                   const QString &currentName) {
  if (!playlistMgr() || playlistId.isEmpty()) {
    return;
  }
  // Confirm because the operation cascades to playlist_items and there's no
  // undo for the row removal (the source items themselves are untouched).
  if (!confirmDialog(tr("Delete Playlist"),
                     tr("Delete the playlist \"%1\"? Its items will not be removed from their "
                        "source collections.")
                         .arg(currentName))) {
    return;
  }
  if (playlistMgr()->deletePlaylist(playlistId) && navMgr()) {
    // Pop back to the parent so the now-deleted playlist isn't left visible
    // mid-fetch. The synthesized CollectionConfig will be removed on the
    // next resync triggered by playlistsChanged.
    navMgr()->safeReloadCollection(0);
  }
}

void PlaylistMenuController::exportPlaylistToFile(const QString &playlistId,
                                                  const QString &currentName, bool asJson) {
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
      getSaveFileNameDialog(tr("Export Playlist"), suggestion, asJson ? filterJson : filterM3u);
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
    warnDialog(tr("Export Failed"), result.error().message);
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
  infoDialog(tr("Export Complete"), message);
}

void PlaylistMenuController::importPlaylistFromFile() {
  if (!playlistMgr()) {
    return;
  }
  const QString chosen = getOpenFileNameDialog(
      tr("Import Playlist"), QString(),
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
      warnDialog(tr("Import Failed"), result.error().message);
    }
    return;
  }

  int skipped = 0;
  auto result =
      playlistMgr()->importFromM3U(chosen, QFileInfo(chosen).completeBaseName(), &skipped);
  if (result.isError()) {
    warnDialog(tr("Import Failed"), result.error().message);
    return;
  }
  if (skipped > 0) {
    // Surface the skipped count in a single completion dialog so the user
    // knows their imported playlist may be shorter than the source — without
    // forcing them through one warning per missing entry.
    infoDialog(tr("Import Complete"),
               tr("Imported playlist; %1 entries skipped (no matching items in the library).")
                   .arg(skipped));
  }
}
