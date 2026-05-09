// One-shot fullscreen startup video. Reuses VideoPreviewWidget's
// QVideoSink → QLabel rendering pattern to avoid the QVideoWidget Wayland
// quirks documented in videopreviewwidget.cpp's banner. Distinct class from
// VideoPreviewWidget because that one is built around looping muted preview
// playback inside small embedded surfaces; the startup overlay is a one-shot,
// fullscreen, dismissible thing.
#include "startupvideooverlay.h"

#include <QAudioOutput>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>

#include "videopreviewwidget.h"

StartupVideoOverlay::StartupVideoOverlay(QWidget *parent) : QWidget(parent) {
  // Frameless modal child: covers the parent geometry and intercepts all
  // input so menu shortcuts and toolbar clicks can't fire underneath while
  // the clip is playing.
  setAttribute(Qt::WA_DeleteOnClose, false);
  setFocusPolicy(Qt::StrongFocus);
  setAutoFillBackground(false);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  m_imageLabel = new QLabel(this);
  m_imageLabel->setAlignment(Qt::AlignCenter);
  m_imageLabel->setMinimumSize(1, 1);
  m_imageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  m_imageLabel->setScaledContents(false);
  layout->addWidget(m_imageLabel);

  m_player = new QMediaPlayer(this);
  m_audioOutput = new QAudioOutput(this);
  // Track the global preview-volume slider so a user who muted previews
  // doesn't get blasted by a startup-video logo sting.
  m_audioOutput->setMuted(false);
  m_audioOutput->setVolume(static_cast<qreal>(VideoPreviewWidget::globalVolume()) / 100.0);
  m_player->setAudioOutput(m_audioOutput);

  m_sink = new QVideoSink(this);
  m_player->setVideoSink(m_sink);
  // Explicitly one-shot; the whole point of this class is "play and go".
  m_player->setLoops(1);

  connect(m_sink, &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) { renderFrame(frame); });
  connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
          [this](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::EndOfMedia || status == QMediaPlayer::InvalidMedia) {
              dismiss();
            }
          });
}

StartupVideoOverlay::~StartupVideoOverlay() {
  if (m_player) {
    m_player->stop();
    m_player->setSource(QUrl());
  }
}

bool StartupVideoOverlay::playVideo(const QString &absolutePath) {
  if (absolutePath.isEmpty()) {
    return false;
  }
  QFileInfo info(absolutePath);
  if (!info.exists() || !info.isReadable()) {
    return false;
  }
  m_player->setSource(QUrl::fromLocalFile(absolutePath));
  m_player->play();
  setFocus(Qt::OtherFocusReason);
  return true;
}

void StartupVideoOverlay::renderFrame(const QVideoFrame &frame) {
  if (!frame.isValid() || !m_imageLabel || m_dismissed) {
    return;
  }
  QImage img = frame.toImage();
  if (img.isNull()) {
    return;
  }
  m_currentImage = img;
  paintCurrentImageScaled();
}

void StartupVideoOverlay::paintCurrentImageScaled() {
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

void StartupVideoOverlay::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event)
  // Solid black backdrop so letterboxed clips (or the brief delay before
  // the first decoded frame arrives) never leak parent-window content.
  QPainter painter(this);
  painter.fillRect(rect(), Qt::black);
}

void StartupVideoOverlay::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  paintCurrentImageScaled();
}

void StartupVideoOverlay::mousePressEvent(QMouseEvent *event) {
  event->accept();
  dismiss();
}

void StartupVideoOverlay::keyPressEvent(QKeyEvent *event) {
  // Any key dismisses — Escape is the obvious one but the issue says
  // "skippable via key/click" without restriction so don't filter.
  event->accept();
  dismiss();
}

void StartupVideoOverlay::dismiss() {
  if (m_dismissed) {
    return;
  }
  m_dismissed = true;
  if (m_player) {
    m_player->stop();
  }
  hide();
  emit dismissed();
  deleteLater();
}
