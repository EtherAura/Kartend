// Looping muted preview video widget for the metadata sidebar.
#include "videopreviewwidget.h"

#include <QAudioOutput>
#include <QHideEvent>
#include <QMediaPlayer>
#include <QShowEvent>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

VideoPreviewWidget::VideoPreviewWidget(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  m_videoWidget = new QVideoWidget(this);
  m_videoWidget->setAspectRatioMode(Qt::KeepAspectRatio);
  layout->addWidget(m_videoWidget);

  m_player = new QMediaPlayer(this);
  m_audioOutput = new QAudioOutput(this);
  m_audioOutput->setMuted(true);
  m_audioOutput->setVolume(0.0);
  m_player->setAudioOutput(m_audioOutput);
  m_player->setVideoOutput(m_videoWidget);
  m_player->setLoops(QMediaPlayer::Infinite);
}

VideoPreviewWidget::~VideoPreviewWidget() {
  if (m_player) {
    m_player->stop();
    m_player->setSource(QUrl());
  }
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
  m_player->stop();
  m_player->setSource(QUrl::fromLocalFile(filePath));
  m_audioOutput->setMuted(true);
  m_player->play();
}

void VideoPreviewWidget::stop() {
  if (!m_player) {
    return;
  }
  m_player->stop();
  m_player->setSource(QUrl());
  m_currentPath.clear();
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
  // Resume playback if a source is loaded and we were previously paused by
  // hideEvent().
  if (m_player && !m_currentPath.isEmpty() &&
      m_player->playbackState() != QMediaPlayer::PlayingState) {
    m_player->play();
  }
  QWidget::showEvent(event);
}
