// Sibling translation unit for DetailsPane: preview-video control. Holds
// the resolve-and-apply step (applyPreviewVideo) plus the thin pause /
// resume / toggle / schedule shims that route through DetailsPaneArtwork.
// Split out of detailspane.cpp so the (network-free, but selection-hot)
// video-resolution logic lives next to its siblings instead of inflating
// setMetadata.
#include "detailspane.h"

#include "detailspaneartwork.h"
#include "videopreviewwidget.h"
#include "videoutils.h"

#include <QDir>
#include <QLoggingCategory>
#include <QString>

void DetailsPane::pausePreviewVideo() {
  if (m_artworkController) m_artworkController->pausePreviewVideo();
}

void DetailsPane::resumePreviewVideo() {
  if (m_artworkController) m_artworkController->resumePreviewVideo();
}

bool DetailsPane::togglePreviewVideoPause() {
  return m_artworkController && m_artworkController->togglePreviewVideoPause();
}

void DetailsPane::schedulePreviewVideo(const QString &videoPath) {
  if (m_artworkController) m_artworkController->schedulePreviewVideo(videoPath);
}

void DetailsPane::applyPreviewVideo(const QString &filePath, const QString &artworkDirectory,
                                    const QString &videoDirectory) {
  // Resolve preview video. Lookup priority:
  // (1) the collection's `videoDirectory` (power-user override), then
  // (2) `{artworkDirectory}/video/` — where scraped videos land under the
  // single-root layout.
  QString videoPath;
  if (!videoDirectory.isEmpty()) {
    videoPath = VideoUtils::findVideoForFile(filePath, videoDirectory);
  }
  if (videoPath.isEmpty() && !artworkDirectory.isEmpty()) {
    videoPath = VideoUtils::findVideoForFile(filePath, QDir(artworkDirectory).filePath("video"));
  }
  // Static category at warning level so this trace stays silent during
  // normal navigation (fires on every selection move). Opt in via
  // `KARTEND_LOG_RULES=kartend.video.debug=true`.
  static const QLoggingCategory lcVideo("kartend.video", QtWarningMsg);
  qCDebug(lcVideo) << "preview lookup item=" << filePath << "videoDir=" << videoDirectory
                   << "artworkDir=" << artworkDirectory << "resolved=" << videoPath;

  // Only churn the video preview when the resolved path actually changed
  // for THIS item. setMetadata fires multiple times for the same selection
  // (manager refreshes after artwork load, post-scrape updates, hover
  // events, etc.); without this guard each invocation hides the video
  // widget, which triggers VideoPreviewWidget's hideEvent -> m_player->
  // pause(), then the 500ms debounce re-shows + re-plays — a glitchy
  // ping-pong the user sees as "video stops playing". When the path is
  // unchanged AND the widget is currently showing the same video, we leave
  // the playback alone entirely.
  const QString currentVideoPath =
      m_videoPlayback.videoPreview ? m_videoPlayback.videoPreview->currentVideoPath() : QString();
  const bool videoUnchanged = !videoPath.isEmpty() && videoPath == currentVideoPath &&
                              m_videoPlayback.videoPreview &&
                              m_videoPlayback.videoPreview->isVisible();
  if (!videoUnchanged) {
    // Kartend-4wxmp: forwarder removed — call the artwork controller directly.
    if (m_artworkController) m_artworkController->showArtworkOnly();
    schedulePreviewVideo(videoPath);
  }
}
