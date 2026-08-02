// Looping preview video with audio (+).
//
// Renders frames into a QLabel via QVideoSink rather than using
// QVideoWidget. QVideoWidget's native-window backend plays audio fine but
// fails to show frames on Wayland / certain GL drivers when the top-level
// window isn't maximized — the subsurface for the video output never gets
// realized for the parent's current geometry. Going through QLabel keeps
// frames in Qt's normal paint pipeline and works regardless of window
// state.
#include "videopreviewwidget.h"

#include <algorithm>
#include <utility>

#include <QAudioOutput>
#include <QHideEvent>
#include <QImage>
#include <QLabel>
#include <QLoggingCategory>
#include <QMediaPlayer>
#include <QPixmap>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>

int VideoPreviewWidget::s_globalVolume = 100;
QList<VideoPreviewWidget *> VideoPreviewWidget::s_instances;

VideoPreviewWidget::VideoPreviewWidget(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  m_imageLabel = new QLabel(this);
  m_imageLabel->setAlignment(Qt::AlignCenter);
  // Allow the label to shrink below its sizeHint inside layouts (it will
  // otherwise try to keep the last pixmap's natural size). Pixmap scaling
  // happens in renderFrame so the label never owns the scaling logic.
  m_imageLabel->setMinimumSize(1, 1);
  m_imageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  m_imageLabel->setScaledContents(false);
  layout->addWidget(m_imageLabel);

  // QMediaPlayer / QAudioOutput / QVideoSink are constructed lazily on the
  // first playVideo() call; see ensureMediaPipeline().
  s_instances.append(this);
}

void VideoPreviewWidget::ensureMediaPipeline() {
  if (m_player) {
    return;
  }
  m_player = new QMediaPlayer(this);
  m_audioOutput = new QAudioOutput(this);
  // Audio is unmuted by default; volume comes from s_globalVolume so a
  // toolbar slider controls every preview surface uniformly.
  m_audioOutput->setMuted(false);
  m_player->setAudioOutput(m_audioOutput);
  applyGlobalVolume();

  m_sink = new QVideoSink(this);
  m_player->setVideoSink(m_sink);
  m_player->setLoops(QMediaPlayer::Infinite);

  connect(m_sink, &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) { renderFrame(frame); });

  // Diagnostic logging — every QMediaPlayer state transition + any
  // error gets one line so a non-playing preview is debuggable
  // without running through gdb. Enable via
  // `QT_LOGGING_RULES=kartend.video.debug=true`.
  static const QLoggingCategory lcVideo("kartend.video", QtWarningMsg);
  connect(m_player, &QMediaPlayer::errorOccurred, this,
          [](QMediaPlayer::Error err, const QString &msg) {
            qCDebug(lcVideo) << "QMediaPlayer error:" << err << msg;
          });
  connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [](QMediaPlayer::MediaStatus s) {
    qCDebug(lcVideo) << "QMediaPlayer mediaStatus:" << s;
  });
  connect(m_player, &QMediaPlayer::playbackStateChanged, this, [](QMediaPlayer::PlaybackState s) {
    qCDebug(lcVideo) << "QMediaPlayer playbackState:" << s;
  });
}

VideoPreviewWidget::~VideoPreviewWidget() {
  s_instances.removeOne(this);
  if (m_player) {
    m_player->stop();
    m_player->setSource(QUrl());
  }
}

void VideoPreviewWidget::setGlobalVolume(int percent) {
  s_globalVolume = std::clamp(percent, 0, 100);
  for (VideoPreviewWidget *w : s_instances) {
    if (w) {
      w->applyGlobalVolume();
    }
  }
}

void VideoPreviewWidget::applyGlobalVolume() {
  if (!m_audioOutput) {
    return;
  }
  m_audioOutput->setVolume(static_cast<qreal>(s_globalVolume) / 100.0);
}

void VideoPreviewWidget::renderFrame(const QVideoFrame &frame) {
  static const QLoggingCategory lcVideo("kartend.video", QtWarningMsg);
  static int frameCounter = 0;
  if (!frame.isValid() || !m_imageLabel) {
    qCDebug(lcVideo) << "renderFrame skipped: validFrame=" << frame.isValid()
                     << "label=" << (m_imageLabel != nullptr);
    return;
  }
  // Drop frames arriving after stop() has cleared the source. Without
  // this, a residual frame in the sink's queue could repaint the label
  // after the user has navigated away.
  if (m_currentPath.isEmpty()) {
    qCDebug(lcVideo) << "renderFrame skipped: currentPath is empty (post-stop)";
    return;
  }
  // Drop frames while a previous frame is still waiting for its coalesced
  // apply below — BEFORE the toImage() conversion, which is the expensive
  // part. When the GUI thread keeps up, every frame is applied; when it
  // falls behind, decoding continues but the per-frame convert + scale cost
  // stops piling onto the event loop. One frame of lag is invisible at
  // preview frame rates.
  if (m_frameApplyPending) {
    return;
  }
  QImage img = frame.toImage();
  if (img.isNull()) {
    qCDebug(lcVideo) << "renderFrame skipped: frame.toImage() returned null"
                     << "pixelFormat=" << frame.pixelFormat() << "size=" << frame.size();
    return;
  }
  m_currentImage = img;
  // Log periodically (first frame + every 60th after) so the log
  // isn't drowned by a steady-state 60 fps stream. Also dumps widget
  // geometry + ancestor visibility so a "frame rendered but user
  // can't see it" outcome is debuggable (window position outside
  // the parent's clip rect, parent widget hidden, etc.).
  if (frameCounter == 0 || frameCounter % 60 == 0) {
    QString ancestry;
    QWidget *w = this;
    int depth = 0;
    while (w && depth < 6) {
      ancestry += QStringLiteral(" -> [%1 visible=%2 geom=%3,%4 %5x%6]")
                      .arg(w->metaObject()->className())
                      .arg(w->isVisible())
                      .arg(w->x())
                      .arg(w->y())
                      .arg(w->width())
                      .arg(w->height());
      w = w->parentWidget();
      ++depth;
    }
    qCDebug(lcVideo) << "renderFrame ok counter=" << frameCounter << "size=" << img.size()
                     << "labelSize=" << m_imageLabel->size()
                     << "labelVisible=" << m_imageLabel->isVisible() << ancestry;
  }
  ++frameCounter;
  m_frameApplyPending = true;
  // Coalesce the label update to the next event-loop turn so a burst of
  // deliveries paints once; the pending flag just set drops further frames
  // until this apply has run.
  QTimer::singleShot(0, this, [this]() {
    m_frameApplyPending = false;
    paintCurrentImageScaled();
  });
}

void VideoPreviewWidget::paintCurrentImageScaled() {
  if (m_currentImage.isNull() || !m_imageLabel) {
    return;
  }
  const QSize target = m_imageLabel->size();
  if (target.width() <= 0 || target.height() <= 0) {
    return;
  }
  // Fast scaling while frames are streaming — the next frame replaces the
  // pixmap in ~16-33ms anyway, so smooth filtering per intermediate frame
  // buys nothing visible and costs GUI-thread time. Paused / stopped (the
  // frame the user actually looks at, including the post-pause repaint and
  // a resize while paused) gets SmoothTransformation.
  const bool streaming = m_player && m_player->playbackState() == QMediaPlayer::PlayingState;

  // Kartend-q7w3a: scale first, convert once — and hand the result over as an
  // rvalue.
  //
  // This used to convert to a QPixmap at FULL frame size and then scale that,
  // which costs two allocations per frame (the larger of them full-size) at up
  // to 60fps per active preview. Scaling the QImage first makes the single
  // remaining allocation the small one.
  //
  // The std::move matters as much as the ordering: QPixmap::fromImage(QImage&&)
  // routes to fromImageInPlace(), whereas the const-ref overload deep-copies
  // whenever the source is still referenced elsewhere and a format re-tag is
  // needed — the same sharing trap that made the artwork path expensive
  // (Kartend-7z067). `scaled` is a local with no other owner, so in-place
  // conversion is safe here.
  QImage frame = m_currentImage.scaled(
      target, Qt::KeepAspectRatio, streaming ? Qt::FastTransformation : Qt::SmoothTransformation);
  m_imageLabel->setPixmap(QPixmap::fromImage(std::move(frame)));
}

void VideoPreviewWidget::playVideo(const QString &filePath) {
  if (filePath.isEmpty()) {
    stop();
    return;
  }
  ensureMediaPipeline();
  if (filePath == m_currentPath && m_player &&
      m_player->playbackState() == QMediaPlayer::PlayingState) {
    return;
  }
  m_currentPath = filePath;
  // Loading a new clip resets the user's manual pause — the user is asking
  // to play this new video, not pause it.
  m_userPaused = false;
  m_player->stop();
  m_player->setSource(QUrl::fromLocalFile(filePath));
  m_player->play();
}

void VideoPreviewWidget::stop() {
  if (!m_player) {
    return;
  }
  m_player->stop();
  m_player->setSource(QUrl());
  m_currentPath.clear();
  m_currentImage = QImage();
  m_userPaused = false;
  if (m_imageLabel) {
    m_imageLabel->clear();
  }
}

bool VideoPreviewWidget::togglePauseResume() {
  if (!m_player || m_currentPath.isEmpty()) {
    return false;
  }
  if (m_player->playbackState() == QMediaPlayer::PlayingState) {
    m_player->pause();
    m_userPaused = true;
    // The paused frame is the one the user will actually study — replace the
    // fast-scaled streaming pixmap with a smooth-scaled render of it.
    paintCurrentImageScaled();
  } else {
    m_player->play();
    m_userPaused = false;
  }
  return m_userPaused;
}

bool VideoPreviewWidget::isPaused() const {
  if (!m_player || m_currentPath.isEmpty()) {
    return false;
  }
  return m_player->playbackState() == QMediaPlayer::PausedState;
}

void VideoPreviewWidget::hideEvent(QHideEvent *event) {
  // Pause (not stop) while hidden: decoding halts, so no CPU is spent on a
  // video the user cannot see, but the source and position are preserved on
  // purpose — sidebar previews resume from where they left off when
  // showEvent() runs (a stop() here would drop the loaded source and restart
  // playback from the beginning on every collapse/expand cycle).
  if (m_player) {
    m_player->pause();
  }
  QWidget::hideEvent(event);
}

void VideoPreviewWidget::showEvent(QShowEvent *event) {
  // Resume playback if a source is loaded and we were previously paused
  // by hideEvent() — but skip when the user has explicitly paused via
  // togglePauseResume(), so their intent survives a hide/show cycle.
  if (m_player && !m_currentPath.isEmpty() && !m_userPaused &&
      m_player->playbackState() != QMediaPlayer::PlayingState) {
    m_player->play();
  }
  QWidget::showEvent(event);
}

void VideoPreviewWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  // Re-scale the last received frame to match the new geometry so the
  // label doesn't briefly show a wrong-aspect pixmap until the next
  // frame arrives from the decoder.
  paintCurrentImageScaled();
}
