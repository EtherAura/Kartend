// Looping preview video with audio (Kartend-ahg + Kartend-ljey).
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

#include <QAudioOutput>
#include <QHideEvent>
#include <QImage>
#include <QLabel>
#include <QMediaPlayer>
#include <QPixmap>
#include <QShowEvent>
#include <QSizePolicy>
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

  m_player = new QMediaPlayer(this);
  m_audioOutput = new QAudioOutput(this);
  // Audio is unmuted by default; volume comes from s_globalVolume so a
  // toolbar slider (Kartend-3m01) controls every preview surface uniformly.
  m_audioOutput->setMuted(false);
  m_player->setAudioOutput(m_audioOutput);
  applyGlobalVolume();

  m_sink = new QVideoSink(this);
  m_player->setVideoSink(m_sink);
  m_player->setLoops(QMediaPlayer::Infinite);

  connect(m_sink, &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) { renderFrame(frame); });

  s_instances.append(this);
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
  if (!frame.isValid() || !m_imageLabel) {
    return;
  }
  // Drop frames arriving after stop() has cleared the source. Without
  // this, a residual frame in the sink's queue could repaint the label
  // after the user has navigated away.
  if (m_currentPath.isEmpty()) {
    return;
  }
  QImage img = frame.toImage();
  if (img.isNull()) {
    return;
  }
  m_currentImage = img;
  paintCurrentImageScaled();
}

void VideoPreviewWidget::paintCurrentImageScaled() {
  if (m_currentImage.isNull() || !m_imageLabel) {
    return;
  }
  const QSize target = m_imageLabel->size();
  if (target.width() <= 0 || target.height() <= 0) {
    return;
  }
  QPixmap pix = QPixmap::fromImage(m_currentImage);
  pix = pix.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  m_imageLabel->setPixmap(pix);
}

void VideoPreviewWidget::playVideo(const QString &filePath) {
  if (filePath.isEmpty()) {
    stop();
    return;
  }
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
  // Free decoder resources whenever the sidebar (or this widget) becomes
  // hidden — there is no point decoding a video the user cannot see.
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
