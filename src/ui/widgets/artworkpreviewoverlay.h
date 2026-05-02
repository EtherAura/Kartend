#ifndef ARTWORKPREVIEWOVERLAY_H
#define ARTWORKPREVIEWOVERLAY_H

#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
QT_END_NAMESPACE

class VideoPreviewWidget;

/**
 * @brief Modal overlay widget that displays artwork or video for a selected
 * item in a centered popup.
 *
 * Originally artwork-only (Kartend-un3l); extended in Kartend-ljey to host a
 * looping muted video preview as well. The two display modes are mutually
 * exclusive — switching media stops/clears the inactive widget. The overlay
 * covers the parent widget and dismisses on Escape, click-outside, or the
 * close button.
 */
class ArtworkPreviewOverlay : public QWidget {
  Q_OBJECT

public:
  explicit ArtworkPreviewOverlay(QWidget *parent = nullptr);
  ~ArtworkPreviewOverlay() override;

  /// Show the overlay with artwork for the given file path.
  void showArtworkForFile(const QString &filePath, const QString &artworkDirectory);

  /// Show the overlay with the artwork file at the given absolute path.
  /// Used by the sidebar artwork gallery (Kartend-un3l) when the path was
  /// resolved by ItemArtworkStore and there is no need to re-search a
  /// directory. `launchRequested` is still emitted on Enter / double-click;
  /// callers that only want preview behavior should leave that signal
  /// unconnected.
  void showArtworkAtPath(const QString &absoluteArtworkPath);

  /// Show the overlay playing the video at @p absoluteVideoPath. The video
  /// loops muted at ~80% of the parent size. Closing the overlay (Escape,
  /// click-outside, or the close button) stops playback.
  void showVideoAtPath(const QString &absoluteVideoPath);

  /// Convenience entry point for video-first preview (Kartend-ljey). Tries
  /// VideoUtils::findVideoForFile() against @p videoDirectory first; falls
  /// back to artwork lookup if no video is found. Returns true if either
  /// medium was shown, false if neither resolved.
  bool showMediaForFile(const QString &filePath, const QString &artworkDirectory,
                        const QString &videoDirectory);

  /// Hide the overlay and stop any video playback.
  void hideOverlay();

  /// File path of the currently-previewed item, or empty if none. Set by
  /// showArtworkForFile and showMediaForFile (gallery-style showAtPath
  /// callers leave it empty since they don't have an associated file).
  [[nodiscard]] QString currentFilePath() const { return m_currentFilePath; }

signals:
  /// Emitted when the user activates (Enter / double-click) while the
  /// overlay is visible. Used by expand-mode to launch the previewed item
  /// on the second activation. @p filePath is the path of the item the
  /// overlay is displaying (may be empty for gallery thumbnail previews
  /// that don't carry an associated media path).
  void launchRequested(const QString &filePath);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  QLabel *m_artworkLabel = nullptr;
  QPushButton *m_closeButton = nullptr;
  VideoPreviewWidget *m_videoPreview = nullptr;
  // Whichever of m_artworkLabel / m_videoPreview is currently displaying
  // content. Drives geometry centering, click-hit testing, and the close
  // button's anchor.
  QWidget *m_displayWidget = nullptr;
  QString m_currentFilePath;

  void setupUI();
  void ensureVideoPreview();
  void centerContent();
  void displayPixmap(const QPixmap &pixmap);
  void displayVideo(const QString &absoluteVideoPath);
};

#endif // ARTWORKPREVIEWOVERLAY_H
