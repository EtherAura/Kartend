// Sibling translation unit for DetailsPane: thin delegates that route the
// public gallery API (setArtworkGallery, setArtworkEditEnabled,
// applyGalleryThumbSize) through DetailsPaneGalleryView. The helper owns
// every gallery widget the previous in-line implementation kept on
// DetailsPane plus the click-to-preview overlay.

#include "detailspane.h"
#include "detailspanegalleryview.h"
#include "uiconstants.h"
#include "videopreviewwidget.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QPixmap>
#include <QPushButton>

#include "loggingcategories.h"

void DetailsPane::setArtworkGallery(const QList<GalleryEntry> &entries) {
  // Inner instrumentation (Kartend-5ux9 follow-up) — outer galleryUi
  // showed a 2.3s startup outlier even after rebuildThumbs and
  // rebuildHorizontalGallery were both made async. Cost has to be in
  // setEntries' lazy ensureSection (widget construction + stylesheet
  // apply) or in rebuildHorizontalGallery's button creation, since
  // the actual pixmap loads now defer. Split the timer so the next
  // perf run says which.
  const bool perfTrace = qEnvironmentVariableIsSet("KARTEND_PERF_TRACE");
  qint64 perfSetEntriesMs = 0, perfHorizMs = 0;
  if (m_galleryView) {
    QElapsedTimer t;
    if (perfTrace) t.start();
    m_galleryView->setEntries(entries, m_primaryArtworkPath, m_activeTab);
    if (perfTrace) perfSetEntriesMs = t.elapsed();
  }
  // Keep the dedicated horizontal view's gallery in sync.
  {
    QElapsedTimer t;
    if (perfTrace) t.start();
    rebuildHorizontalGallery();
    if (perfTrace) perfHorizMs = t.elapsed();
  }
  if (perfTrace && (perfSetEntriesMs + perfHorizMs) > 5) {
    qCDebug(lcPerfTrace).nospace()
        << "DetailsPane::setArtworkGallery: setEntries=" << perfSetEntriesMs
        << " horizontalGallery=" << perfHorizMs << " entries=" << entries.size();
  }
}

QList<DetailsPane::GalleryEntry> DetailsPane::currentGalleryEntries() const {
  if (!m_galleryView) return {};
  return m_galleryView->entries();
}

void DetailsPane::setArtworkEditEnabled(bool enabled) {
  if (m_galleryView) {
    m_galleryView->setEditEnabled(enabled, m_activeTab);
  }
  // Mirror the trailing horizontal-view edit button so both track the
  // per-item edit permission in lockstep.
  if (m_hEditButton) {
    m_hEditButton->setVisible(enabled);
  }
  rebuildHorizontalGallery();
}

void DetailsPane::applyGalleryThumbSize() {
  if (m_galleryView) {
    m_galleryView->applyThumbSize(currentGalleryThumbSize());
  }
}

void DetailsPane::showMainPreviewForEntry(const GalleryEntry &entry) {
  // Swap the big artwork tile (or the video-preview tile) to the
  // gallery thumb the user just clicked / cycled to. Two flavours:
  //   * Image: load the file into the static artwork QLabel via
  //     loadArtwork() and stop any running preview video so the
  //     image is what the user sees in the slot.
  //   * Video: route through the existing debounced scheduler so
  //     start/stop behaviour matches setMetadata's first-load path
  //     (artwork hides, video shows).
  // Failures (missing file, decode error) fall back to leaving the
  // current tile in place — same shape as loadArtwork's own
  // "no usable pixmap" branch.
  if (entry.path.isEmpty() || !QFile::exists(entry.path)) return;
  if (entry.isVideo) {
    // Image-side widgets get reset back to the artwork tile so the
    // video swap is a clean state transition. schedulePreviewVideo's
    // debounce timer fires playVideo + the show/hide dance.
    schedulePreviewVideo(entry.path);
    return;
  }
  // Image swap: tear down any running video so the static tile is
  // what's on screen, then load the chosen pixmap into the artwork
  // display. loadArtwork resolves by basename inside a directory; we
  // pass the parent dir + completeBaseName to avoid re-implementing
  // the extension-cycle here.
  schedulePreviewVideo(QString()); // cancels any pending video start
  if (m_videoPreview) m_videoPreview->stop();
  showArtworkOnly();
  const QFileInfo info(entry.path);
  loadArtwork(info.completeBaseName(), info.absolutePath());
}

void DetailsPane::cycleMainPreview(int direction) {
  if (!m_galleryView) return;
  const auto &entries = m_galleryView->entries();
  if (entries.size() < 2) return;
  // Locate the currently-displayed entry. Match on file path —
  // m_primaryArtworkPath tracks the static tile; m_pendingVideoPath
  // tracks the video tile. If neither matches anything in the
  // gallery (e.g. the current display is the flat-root mirror at
  // {artwork}/{base}.png while the gallery contains only typed
  // copies), fall back to position 0 so the first cycle step is
  // deterministic.
  int currentIndex = -1;
  for (int i = 0; i < entries.size(); ++i) {
    if (entries[i].path == m_primaryArtworkPath ||
        (entries[i].isVideo && entries[i].path == m_pendingVideoPath)) {
      currentIndex = i;
      break;
    }
  }
  if (currentIndex < 0) currentIndex = 0;
  // direction is +1 or -1; the modular step handles negatives + wraps.
  const int n = entries.size();
  const int nextIndex = ((currentIndex + direction) % n + n) % n;
  showMainPreviewForEntry(entries[nextIndex]);
}
