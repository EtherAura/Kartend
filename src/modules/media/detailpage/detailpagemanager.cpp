// Coordinates the item detail page (overlay + async data load).
#include "detailpagemanager.h"

#include "applicationcontext.h"
#include "detailpagehelpers.h"
#include "idatabasemanager.h"
#include "idetailspanemanager.h"
#include "itemartwork.h"

DetailPageManager::DetailPageManager(QObject *parent) : QObject(parent) {}
DetailPageManager::~DetailPageManager() = default;

void DetailPageManager::setupReferences(const DetailPageManagerSetup &setup) {
  m_ctx = setup.ctx;
  m_overlay = setup.overlay;
  // Kartend-n8kh: the `manualRequested` -> QDesktopServices::openUrl wiring
  // used to live here, but it required #including the concrete
  // DetailPageOverlay header for the signal symbol. The connection is now
  // made in MainWindow alongside the overlay's other UI-layer wiring;
  // DetailPageManager just builds payloads and drives showWith().

  // Kartend-4p8o: receive the off-thread detail-page load result. The DB lives
  // for the app lifetime, so a one-time UniqueConnection here needs no teardown.
  if (IDatabaseManager *db = m_ctx ? m_ctx->databaseManager() : nullptr) {
    connect(db, &IDatabaseManager::itemDetailLoaded, this, &DetailPageManager::onItemDetailLoaded,
            Qt::UniqueConnection);
  }
}

void DetailPageManager::showForCurrentSelection() {
  IDetailsPaneManager *detailsPane = m_ctx ? m_ctx->detailsPaneManager() : nullptr;
  if (!m_overlay || !detailsPane) {
    return;
  }
  const auto &itemCtx = detailsPane->currentItemContext();
  if (!itemCtx.isValid()) {
    // No selection (or the sidebar hasn't resolved one yet): silently ignore
    // so the user can mash the key without seeing an error.
    return;
  }

  // Kartend-4p8o: show the overlay immediately with the cheap fields (no DB,
  // no filesystem) so the open feels instant, then load metadata + artwork +
  // file info off the UI thread and refresh when it lands. The previous inline
  // path issued three synchronous DB reads plus uncached directory scans before
  // the overlay ever appeared, stalling the UI on slow / cold storage.
  IDetailPageOverlay::Payload cheap;
  cheap.filePath = itemCtx.filePath;
  cheap.itemName = itemCtx.itemName;
  m_overlay->showWith(cheap);

  IDatabaseManager *db = m_ctx ? m_ctx->databaseManager() : nullptr;
  if (!db) {
    // No async source (e.g. a bare test fixture): the cheap overlay stands.
    return;
  }

  // Stash the cheap context — filePath/itemName aren't carried in
  // ItemDetailData — and bump the token so a result for a previously-selected
  // item (the user moved on before the load finished) is ignored on arrival.
  m_pendingFilePath = itemCtx.filePath;
  m_pendingItemName = itemCtx.itemName;
  const int token = ++m_detailLoadToken;
  db->loadItemDetailAsync(token, itemCtx.uuid, itemCtx.filePath, itemCtx.artworkDir,
                          itemCtx.videoDir, itemCtx.manualDir);
}

void DetailPageManager::onItemDetailLoaded(const ItemDetailData &data, int requestToken) {
  // Drop a stale result: the user opened another item while this one loaded.
  if (requestToken != m_detailLoadToken || !m_overlay || !data.valid) {
    return;
  }

  IDetailPageOverlay::Payload payload;
  payload.filePath = m_pendingFilePath;
  payload.itemName =
      DetailPagePayloadBuilder::pickDisplayTitle(m_pendingItemName, data.metadata.title);
  payload.metadata = data.metadata;
  payload.usage = data.usage;
  payload.manualPath = data.manualPath;
  payload.fileSize = data.fileSize;
  payload.fileModified = data.fileModified;
  payload.fileExtension = data.fileExtension;

  // Typed tiles in standard-then-custom order; the worker resolved the paths,
  // the UI owns the labels (standard display name, or the custom type id).
  for (const auto &[type, path] : data.typedArtwork) {
    const QString label = ItemArtworkStore::standardTypeDisplayName(type);
    payload.artwork.append({label.isEmpty() ? type : label, path, /*isVideo=*/false});
  }
  // Flat-layout fallback cover, prepended so it leads — but only when no typed
  // tile already covers the same file (mirrors the prior ordering).
  if (!data.fallbackCoverPath.isEmpty()) {
    bool alreadyListed = false;
    for (const auto &entry : payload.artwork) {
      if (entry.path == data.fallbackCoverPath) {
        alreadyListed = true;
        break;
      }
    }
    if (!alreadyListed) {
      payload.artwork.prepend({tr("Cover"), data.fallbackCoverPath, /*isVideo=*/false});
    }
  }
  // Video tile leads the gallery (video-first ordering the sidebar uses).
  if (!data.videoPath.isEmpty()) {
    payload.artwork.prepend({tr("Video"), data.videoPath, /*isVideo=*/true});
  }

  m_overlay->showWith(payload);
}

void DetailPageManager::hideOverlay() {
  // Invalidate any in-flight async load: without the bump, a result arriving
  // after the dismissal still matches the token and showWith()s the overlay
  // the user just closed.
  ++m_detailLoadToken;
  if (m_overlay) {
    m_overlay->hideOverlay();
  }
}

bool DetailPageManager::isOverlayActive() const {
  return m_overlay && m_overlay->isActive();
}
