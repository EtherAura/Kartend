// Implementation for DetailsPaneArtwork — the artwork + video-preview half
// of DetailsPane. Methods here read / write host state (m_videoPlayback,
// m_artworkSource, m_primaryArtworkPath, m_artworkLoadGen) and host UI
// widgets (ui->artworkDisplay, ui->scrollArea, ui->contentWidget) via the
// friend-class privilege declared on DetailsPane. See detailspaneartwork.h
// for the rationale of the friend pattern vs moving state.
#include "detailspaneartwork.h"

#include "collection/enumstringhelpers.h"
#include "detailspane.h"
#include "extensionutils.h"
#include "itemartwork.h"
#include "ui_detailspane.h"
#include "uiconstants/detailspaneconstants.h"
#include "uiconstants/metadata.h"
#include "videopreviewwidget.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QScrollArea>
#include <QtConcurrent/QtConcurrentRun>
#include <QTimer>

DetailsPaneArtwork::DetailsPaneArtwork(QObject *parent) : QObject(parent) {}

void DetailsPaneArtwork::setHost(DetailsPane *host) {
  m_host = host;
}

int DetailsPaneArtwork::previewBoxSize() const {
  if (!m_host || !m_host->ui) {
    return UIConstants::Metadata::ARTWORK_SIZE;
  }
  // Vertical dock (the only case the original .ui artwork preview is
  // shown in — the dedicated horizontal view has its own preview
  // tile). Tracks the scroll area's viewport width minus the
  // contentLayout's left+right margins (10+10=20) so the tile uses
  // the full pane width edge-to-edge. Capped at ARTWORK_SIZE_MAX so
  // very wide sidebars don't blow the tile up beyond a reasonable
  // limit.
  auto *ui = m_host->ui;
  int viewportW =
      (ui->scrollArea && ui->scrollArea->viewport()) ? ui->scrollArea->viewport()->width() : 0;
  if (viewportW <= 0 && ui->contentWidget) {
    viewportW = ui->contentWidget->width();
  }
  return qBound(80, viewportW - 20, UIConstants::Metadata::ARTWORK_SIZE_MAX);
}

void DetailsPaneArtwork::applyPreviewSize() {
  if (!m_host) return;
  // in horizontal dock the dedicated horizontal view owns the
  // sizing of its own preview tile (video only). The artworkDisplay stays
  // in the hidden vertical scrollArea so its size is irrelevant here.
  const bool horizontal = CollectionUtils::isDetailsPaneHorizontal(m_host->m_position);
  if (horizontal) {
    if (m_host->m_horizontalView && m_host->m_videoPlayback.videoPreview) {
      const int previewSize = m_host->horizontalPreviewSize();
      m_host->m_videoPlayback.videoPreview->setFixedSize(previewSize, previewSize);
    }
    return;
  }
  auto *ui = m_host->ui;
  const int width = previewBoxSize();
  // Tile shape is fixed (same dimensions for every item) so the layout
  // sidebar's available room; height is derived from the pane viewport
  // so very tall sidebars don't stretch the tile to dominate the
  // pane. 3:4 portrait works well for the most common case (game box
  // art); landscape art letterboxes top + bottom but a fixed tile
  // shape beats a dynamic one for visual stability.
  int viewportH = 0;
  if (ui->scrollArea && ui->scrollArea->viewport()) {
    viewportH = ui->scrollArea->viewport()->height();
  }
  if (viewportH <= 0 && ui->contentWidget) {
    viewportH = ui->contentWidget->height();
  }
  // Cap the tile at ~45% of the pane height so the description + the
  // metadata card below it still get usable space without scrolling.
  const int idealHeight = (width * 4) / 3;
  const int height = viewportH > 0 ? std::min(idealHeight, viewportH * 45 / 100) : idealHeight;

  // Video has no known aspect ratio until the first frame loads, so it
  // takes the same tile and the sink letterboxes internally.
  if (m_host->m_videoPlayback.videoPreview) {
    m_host->m_videoPlayback.videoPreview->setFixedSize(width, height);
  }
  // Vertical dock: scale gallery thumbs alongside the preview.
  m_host->applyGalleryThumbSize();
  if (!ui || !ui->artworkDisplay) {
    return;
  }
  // Hide the tile entirely when there's nothing to show — the
  // viewport-derived size grows the empty rectangle past 300px tall, which
  // dominates the unscraped Item tab as a giant Mid-colored slab.
  if (m_host->m_artworkSource.isNull()) {
    ui->artworkDisplay->setPixmap(QPixmap());
    ui->artworkDisplay->hide();
    return;
  }
  ui->artworkDisplay->setFixedSize(width, height);
  // Vertical-dock artwork and the video preview share one slot. When a
  // preview video is currently playing it owns the slot, so keep the
  // freshly-sized artwork loaded (pixmap set below) but hidden. Mirrors the
  // guard in DetailsPane::setArtworkSectionVisible — without it, re-entry
  // here re-shows the artwork stacked under the live video on refresh paths
  // that don't restart the video: a same-item reselect (setMetadata's
  // videoUnchanged guard skips the normal hide) or a tab-switch re-push where
  // loadArtwork's async decode completes after the video is already showing.
  // showArtworkOnly() re-shows the artwork when the video stops.
  const bool videoPlaying = m_host->m_videoPlayback.videoPreview &&
                            m_host->m_videoPlayback.videoPreview->isVisible() &&
                            !m_host->m_videoPlayback.videoPreview->currentVideoPath().isEmpty();
  ui->artworkDisplay->setVisible(!videoPlaying);
  QPixmap scaled =
      m_host->m_artworkSource.scaled(width, height, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QPixmap centered(width, height);
  centered.fill(m_host->palette().color(QPalette::Base));
  QPainter painter(&centered);
  const int x = (width - scaled.width()) / 2;
  const int y = (height - scaled.height()) / 2;
  painter.drawPixmap(x, y, scaled);
  painter.end();
  ui->artworkDisplay->setPixmap(centered);
}

void DetailsPaneArtwork::pausePreviewVideo() {
  if (!m_host) return;
  // Cancel any debounced start so a delayed playVideo() doesn't fire under
  // the overlay. Soft-pause via hide() — the widget's hideEvent pauses the
  // QMediaPlayer but keeps the loaded source so resumePreviewVideo() can
  // pick up at the same playback position. m_videoPlayback.pendingVideoPath is preserved
  // for the same reason: the resume path uses it when the prior schedule
  // was caught mid-debounce (path set but not yet loaded into the widget).
  if (m_host->m_videoPlayback.videoStartTimer) {
    m_host->m_videoPlayback.videoStartTimer->stop();
  }
  if (m_host->m_videoPlayback.videoPreview) {
    m_host->m_videoPlayback.videoPreview->hide();
  }
  // Restore the static artwork display so the sidebar isn't a black square
  // while the overlay is on top.
  m_host->ui->artworkDisplay->show();
}

void DetailsPaneArtwork::resumePreviewVideo() {
  if (!m_host) return;
  if (!m_host->m_videoPlayback.videoPreview) {
    return;
  }
  if (m_host->m_videoPlayback.videoPreview->hasLoadedSource()) {
    // showEvent inside VideoPreviewWidget calls QMediaPlayer::play() — and
    // the player was left in PausedState by hideEvent, so play() resumes
    // from the same position rather than restarting. Hide the static
    // artwork in vertical dock since the two share the same slot; in
    // horizontal dock they sit side-by-side and artwork visibility is
    // owned by the scrollArea (already hidden by applyDockOrientation).
    if (!CollectionUtils::isDetailsPaneHorizontal(m_host->m_position)) {
      m_host->ui->artworkDisplay->hide();
    }
    m_host->m_videoPlayback.videoPreview->show();
  } else if (!m_host->m_videoPlayback.pendingVideoPath.isEmpty() &&
             m_host->m_videoPlayback.videoStartTimer) {
    // The pause caught us mid-debounce. Re-arm the timer so the original
    // play schedule fires again on the now-visible widget.
    m_host->m_videoPlayback.videoStartTimer->start();
  }
}

bool DetailsPaneArtwork::togglePreviewVideoPause() {
  if (!m_host) return false;
  if (!m_host->m_videoPlayback.videoPreview ||
      !m_host->m_videoPlayback.videoPreview->hasLoadedSource()) {
    return false;
  }
  m_host->m_videoPlayback.videoPreview->togglePauseResume();
  return true;
}

void DetailsPaneArtwork::loadArtwork(const QString &baseName, const QString &artworkDirectory) {
  if (!m_host) return;
  QDir artworkDir(artworkDirectory);
  if (!artworkDir.exists()) {
    return;
  }

  // Phase 1 (synchronous, on main thread): collect ALL candidate paths
  // that exist on disk, in priority order. Each QFile::exists is a stat
  // call (cheap). We can't commit to a single resolved path here because
  // some candidates may be present but undecodable — e.g. a 0-byte
  // failed-scrape `.jpg` shadowing a valid `.png`. The original
  // synchronous loop would QPixmap-construct each candidate inline and
  // continue on null; this version preserves that contract by handing
  // the whole list to the worker so it can do the decode-to-find
  // walk off-thread.
  QStringList candidatePaths;
  for (const QString &ext : ExtensionUtils::imageBaseExtensions()) {
    QString lowerCandidate = artworkDir.absoluteFilePath(baseName + "." + ext);
    if (QFile::exists(lowerCandidate)) {
      candidatePaths.append(lowerCandidate);
    }
    QString upperCandidate = artworkDir.absoluteFilePath(baseName + "." + ext.toUpper());
    if (upperCandidate != lowerCandidate && QFile::exists(upperCandidate)) {
      candidatePaths.append(upperCandidate);
    }
  }
  // Fall back to typed-subdir scraped art when no top-level mirror exists.
  // Long-standing behaviour gap (Kartend-wppu): for collections whose
  // artworkDirectory points at a "wide" Artwork/ tree (parent of typed
  // subdirs) rather than a single typed subdir like Artwork/covers/, items
  // without a top-level {baseName}.{ext} mirror would render with a blank
  // primary tile even though scraped art existed under front/, box/,
  // screenshot/, etc. Walk the standard types in gallery display priority
  // (front first) and use the first available file. ItemArtworkStore
  // already owns the {artworkDirectory}/{type}/{baseName}.{ext} probe so
  // we just iterate.
  if (candidatePaths.isEmpty()) {
    for (const QString &type : ItemArtworkStore::standardTypes()) {
      const QString sub = ItemArtworkStore::findStandardArtwork(baseName, artworkDirectory, type);
      if (!sub.isEmpty()) {
        candidatePaths.append(sub);
        break;
      }
    }
  }
  if (candidatePaths.isEmpty()) {
    return; // Nothing to load — placeholder stays.
  }
  // Tentative primary-artwork path so setArtworkGallery (called later in
  // the same refresh pass) can synthesize the primary-cover thumb in the
  // gallery strip. If the worker ends up choosing a different candidate
  // (because the tentative one fails to decode), the callback updates
  // m_primaryArtworkPath to match the actual resolved path.
  m_host->m_primaryArtworkPath = candidatePaths.first();

  // Phase 2 (async, on QThreadPool): walk the candidate list and return
  // the first one that successfully decodes. Pre-fix this was a
  // synchronous QPixmap(path) on the main thread (50-280ms per fresh
  // selection on slow filesystems). Moving the walk off-thread keeps the
  // UI responsive; the user sees the placeholder briefly then the real
  // cover. Same pattern as ScrapeResultDialog::appendThumbAsync
  // (Kartend-j5lc). QImage is thread-safe; QPixmap::fromImage runs on
  // the main thread in the watcher's finished slot.
  //
  // Generation counter handles rapid-click races: if a newer loadArtwork
  // call has bumped m_artworkLoadGen by the time this worker finishes,
  // the older result is dropped instead of overwriting the currently
  // displayed item's pixmap. QFutureWatcher parented to `this` so a
  // pane destroy auto-disconnects the lambda and drops the result.
  using LoadResult = QPair<QString, QImage>;
  const quint64 myGen = ++m_host->m_artworkLoadGen;
  auto *watcher = new QFutureWatcher<LoadResult>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, myGen]() {
    watcher->deleteLater();
    if (!m_host || myGen != m_host->m_artworkLoadGen) return;
    const LoadResult res = watcher->result();
    if (res.second.isNull()) return;
    // The worker picks the first decodable candidate; update
    // m_primaryArtworkPath to match in case the tentative path
    // we set above wasn't the chosen one.
    m_host->m_primaryArtworkPath = res.first;
    // Cache the original-resolution pixmap so applyPreviewSize
    // can re-render at the new dimension on sidebar resize
    // without re-reading from disk.
    m_host->m_artworkSource = QPixmap::fromImage(res.second);
    applyPreviewSize();
    m_host->updateHorizontalView();
  });
  watcher->setFuture(QtConcurrent::run([candidatePaths]() -> LoadResult {
    for (const QString &p : candidatePaths) {
      QImage img(p);
      if (!img.isNull()) {
        return {p, img};
      }
    }
    return {QString(), QImage()};
  }));
}

void DetailsPaneArtwork::showArtworkOnly() {
  if (!m_host) return;
  if (m_host->m_videoPlayback.videoPreview) {
    // Skip the stop()+hide() when there's nothing to stop. Pre-fix the
    // unconditional QMediaPlayer::stop() blocked the GUI thread for
    // ~85-100ms per selection on items where the player had been left in
    // a pipeline-initialized state by a previous schedulePreviewVideo —
    // GStreamer's pipeline state transition isn't free. The user perceived
    // this as a per-click stutter (Kartend-2c7c). isVisible OR a non-empty
    // currentVideoPath means the player has something to tear down;
    // otherwise the call is a no-op anyway.
    const bool needsStop = m_host->m_videoPlayback.videoPreview->isVisible() ||
                           !m_host->m_videoPlayback.videoPreview->currentVideoPath().isEmpty();
    if (needsStop) {
      m_host->m_videoPlayback.videoPreview->stop();
      m_host->m_videoPlayback.videoPreview->hide();
    }
  }
  m_host->ui->artworkDisplay->show();
}

void DetailsPaneArtwork::schedulePreviewVideo(const QString &videoPath) {
  if (!m_host) return;
  m_host->m_videoPlayback.pendingVideoPath = videoPath;
  if (!m_host->m_videoPlayback.videoStartTimer) {
    return;
  }
  m_host->m_videoPlayback.videoStartTimer->stop();
  if (!videoPath.isEmpty()) {
    // Explicit interval — the timer callback may shorten the next
    // re-arm to 50ms when a scroll-active deferral happens
    // (Kartend-9q8d round 6). Without specifying the interval here,
    // schedulePreviewVideo would inherit the deferral interval and
    // start firing every 50ms instead of waiting the full debounce.
    m_host->m_videoPlayback.videoStartTimer->start(
        UIConstants::DetailsPane::VIDEO_PREVIEW_DEBOUNCE_MS);
  }
}

void DetailsPaneArtwork::setScrollIdlePredicate(std::function<bool()> predicate) {
  if (!m_host) return;
  m_host->m_videoPlayback.scrollIdlePredicate = std::move(predicate);
}

bool DetailsPaneArtwork::isScrollIdle() const {
  if (!m_host) return true;
  return !m_host->m_videoPlayback.scrollIdlePredicate ||
         m_host->m_videoPlayback.scrollIdlePredicate();
}
