// Coordinates the item detail page (overlay + data load).
#include "detailpagemanager.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <QUrl>

#include "applicationcontext.h"
#include "artworkutils.h"
#include "databasemanager.h"
#include "detailpageoverlay.h"
#include "detailspanemanager.h"
#include "itemartwork.h"
#include "videoutils.h"

DetailPageManager::DetailPageManager(QObject *parent) : QObject(parent) {}
DetailPageManager::~DetailPageManager() = default;

void DetailPageManager::setupReferences(const DetailPageManagerSetup &setup) {
  m_ctx = setup.ctx;
  m_overlay = setup.overlay;

  if (m_overlay) {
    // QDesktopServices is the same hand-off the sidebar uses for manuals so
    // both surfaces respect the user's xdg-open / Finder default handler.
    connect(m_overlay, &DetailPageOverlay::manualRequested, this, [](const QString &manualPath) {
      if (!manualPath.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(manualPath));
      }
    });
  }
}

void DetailPageManager::showForCurrentSelection() {
  DetailsPaneManager *detailsPane = m_ctx ? m_ctx->detailsPaneManager() : nullptr;
  IDatabaseManager *db = m_ctx ? m_ctx->databaseManager() : nullptr;
  if (!m_overlay || !detailsPane) {
    return;
  }
  const auto &itemCtx = detailsPane->currentItemContext();
  if (!itemCtx.isValid()) {
    // No selection (or the sidebar hasn't resolved one yet): silently ignore
    // so the user can mash the key without seeing an error.
    return;
  }

  DetailPageOverlay::Payload payload;
  payload.filePath = itemCtx.filePath;
  payload.itemName = itemCtx.itemName;

  const QString baseName = QFileInfo(itemCtx.filePath).completeBaseName();

  // ── Metadata + manual + usage ────────────────────────────────────────
  if (db && !itemCtx.uuid.isEmpty()) {
    payload.metadata = db->loadItemMetadata(itemCtx.uuid, itemCtx.filePath);
    payload.usage = db->loadItemUsageStats(itemCtx.uuid, itemCtx.filePath);
    // Title falls back to the metadata's scraped title when the sidebar
    // received a file-stem-derived itemName but the DB has a real title.
    if (!payload.metadata.title.isEmpty()) {
      payload.itemName = payload.metadata.title;
    }
    payload.manualPath = ItemMetadataStore::resolveManualFile(payload.metadata.manualPath, baseName,
                                                              itemCtx.manualDir);
  }

  // ── Artwork tiles (every standard type + any custom override the user
  // configured). Mirrors DetailsPaneManager::updateSidebarMetadata's gallery
  // build so the same set of artworks shows up here. ───────────────────
  QHash<QString, QString> overridesByType;
  QStringList customOrder;
  if (db && !itemCtx.uuid.isEmpty()) {
    const auto rows = db->loadItemArtwork(itemCtx.uuid, itemCtx.filePath);
    for (const auto &row : rows) {
      overridesByType.insert(row.artworkType, row.manualPath);
      if (!ItemArtworkStore::isStandardType(row.artworkType)) {
        customOrder.append(row.artworkType);
      }
    }
  }

  auto pushArtwork = [&](const QString &type, const QString &label) {
    const QString resolved = ItemArtworkStore::resolveArtworkPath(
        overridesByType.value(type), baseName, itemCtx.artworkDir, type);
    if (!resolved.isEmpty()) {
      payload.artwork.append({label, resolved});
    }
  };
  for (const QString &type : ItemArtworkStore::standardTypes()) {
    pushArtwork(type, ItemArtworkStore::standardTypeDisplayName(type));
  }
  for (const QString &type : customOrder) {
    pushArtwork(type, type);
  }

  // Fall back to the flat-layout artwork the sidebar's preview pane shows
  // ({artworkDir}/{baseName}.{ext}). Most users haven't migrated to the
  // type-subdirectory layout that ItemArtworkStore::resolveArtworkPath
  // expects, so without this the hero pane would be empty for the common
  // case. Prepend so it's the first thing shown; only added when no typed
  // artwork already covered the same file.
  if (!itemCtx.artworkDir.isEmpty()) {
    const QString fallback = ArtworkUtils::findArtworkForFile(
        QFileInfo(itemCtx.filePath).fileName(), itemCtx.artworkDir);
    if (!fallback.isEmpty()) {
      bool alreadyListed = false;
      for (const auto &entry : payload.artwork) {
        if (entry.path == fallback) {
          alreadyListed = true;
          break;
        }
      }
      if (!alreadyListed) {
        payload.artwork.prepend({tr("Cover"), fallback, /*isVideo=*/false});
      }
    }
  }

  // Prepend the video tile so the gallery follows the video-first ordering
  // the sidebar uses. Mirrors DetailsPaneManager::updateSidebarMetadata's
  // video lookup; an empty videoDir or missing file just yields no tile.
  if (!itemCtx.videoDir.isEmpty()) {
    const QString videoPath = VideoUtils::findVideoForFile(itemCtx.filePath, itemCtx.videoDir);
    if (!videoPath.isEmpty()) {
      payload.artwork.prepend({tr("Video"), videoPath, /*isVideo=*/true});
    }
  }

  // ── File info ────────────────────────────────────────────────────────
  QFileInfo info(itemCtx.filePath);
  if (info.exists()) {
    payload.fileSize = info.size();
    payload.fileModified = info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    payload.fileExtension =
        info.suffix().isEmpty() ? QString() : QStringLiteral(".%1").arg(info.suffix().toLower());
  }

  m_overlay->showWith(payload);
}

void DetailPageManager::hideOverlay() {
  if (m_overlay) {
    m_overlay->hideOverlay();
  }
}
