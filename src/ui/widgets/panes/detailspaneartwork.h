#ifndef DETAILSPANEARTWORK_H
#define DETAILSPANEARTWORK_H

#include <functional>
#include <QObject>
#include <QString>

class DetailsPane;

/// Owns the artwork-preview + per-item video-preview behaviour that lives in
/// the vertical (Left/Right dock) layout of DetailsPane: async artwork
/// resolution + decode, the debounced video-preview start timer, pause /
/// resume / toggle of the running QMediaPlayer, and the preview-tile sizing
/// math that scales the artwork QLabel against the sidebar's available
/// width. The host's other surfaces (gallery row, metadata rows,
/// horizontal-dock view) are unaffected.
///
/// Coupling: takes the host DetailsPane via setHost so the helper can reach
/// the .ui-owned widgets (ui->artworkDisplay, ui->scrollArea) and the
/// shared state members (m_videoPlayback, m_artworkSource,
/// m_primaryArtworkPath, m_artworkLoadGen) that the host's other helpers
/// (DetailsPaneGalleryView, the horizontal-dock view) also read. The state
/// is intentionally NOT moved off the host — it stays where ~100 existing
/// access sites already reference it. This class is a method-organising
/// extraction (Kartend-5nxz, first half) that pulls the artwork/video
/// implementation out of the 1628-LOC detailspane.cpp while keeping the
/// rest of the dock-orientation / appearance / metadata pipeline anchored
/// on DetailsPane.
class DetailsPaneArtwork : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(DetailsPaneArtwork)

public:
  explicit DetailsPaneArtwork(QObject *parent = nullptr);

  /// Bind to the owning DetailsPane. Idempotent — call once during
  /// DetailsPane's setup.
  void setHost(DetailsPane *host);

  /// Phase-1 candidate-path collection + Phase-2 async decode of the
  /// per-item artwork. Updates host's m_artworkSource and
  /// m_primaryArtworkPath when a candidate decodes successfully; older
  /// in-flight loads are dropped via the host's generation counter so a
  /// rapid selection burst can't deliver out-of-order results.
  void loadArtwork(const QString &baseName, const QString &artworkDirectory);

  /// Queue a video-preview start after the debounce interval. An empty
  /// path cancels any pending playback. Used by setMetadata when the
  /// selected item has a sidecar video file.
  void schedulePreviewVideo(const QString &videoPath);

  /// Soft-pause: hide the video preview widget (its hideEvent pauses the
  /// QMediaPlayer in place so resumePreviewVideo picks up at the same
  /// position) and re-show the static artwork. Idempotent.
  void pausePreviewVideo();
  /// Reverse of pausePreviewVideo: if a source was loaded, re-show the
  /// widget so the player resumes; if the prior pause caught the
  /// debounce mid-flight, re-arm the start timer so the original
  /// schedule runs again.
  void resumePreviewVideo();
  /// Toggle the sidebar preview video between playing and paused.
  /// Returns true if a toggle occurred (i.e. a video is loaded), false
  /// when there's nothing to toggle.
  bool togglePreviewVideoPause();

  /// Stop any running video preview and re-show the static artwork.
  /// Called by setMetadata before scheduling a new preview, and by
  /// clearMetadata when the selection goes empty.
  void showArtworkOnly();

  /// Scale the artwork + video preview boxes to the current sidebar
  /// content width. Re-renders the cached artwork pixmap centered in
  /// the new size; capped at the .ui's designer max so very wide
  /// sidebars don't show a giant preview.
  void applyPreviewSize();

  /// Install a predicate the video-preview start timer consults before
  /// firing playVideo. When the predicate returns true, the timer
  /// restarts itself with a short re-check interval instead of running
  /// playVideo's GUI-thread-blocking pipeline setup. Pass an empty
  /// predicate (default) to disable scroll-aware deferral.
  void setScrollIdlePredicate(std::function<bool()> predicate);
  /// True when no scroll/animation is currently active. Returns true
  /// when no predicate is installed (preserves pre-fix behaviour).
  [[nodiscard]] bool isScrollIdle() const;

private:
  /// Sidebar content-width-derived preview tile width (vertical dock).
  /// Stays a private helper because both applyPreviewSize and the host's
  /// horizontal-view code path need the same calculation.
  [[nodiscard]] int previewBoxSize() const;

  DetailsPane *m_host = nullptr;
};

#endif // DETAILSPANEARTWORK_H
