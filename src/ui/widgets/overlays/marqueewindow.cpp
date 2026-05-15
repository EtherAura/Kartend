// Secondary-monitor topper window. Frameless, no input focus, displays
// either a centred pixmap scaled to fit OR a looping muted video.
// MainWindow owns one of these and feeds it artwork updates on
// selection / collection changes; the window has no awareness of
// Kartend's models.
#include "marqueewindow.h"

#include <QAudioOutput>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QMediaPlayer>
#include <QResizeEvent>
#include <QScreen>
#include <QStackedLayout>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

MarqueeWindow::MarqueeWindow(QScreen *targetScreen)
    : QWidget(nullptr,
              // Frameless + always-on-top of its own screen + no taskbar
              // entry. WindowDoesNotAcceptFocus keeps keyboard focus on
              // the main Kartend window so users navigating with arrows
              // don't suddenly find their input swallowed here.
              Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint |
                  Qt::WindowDoesNotAcceptFocus) {
  setAttribute(Qt::WA_ShowWithoutActivating, true);
  // Solid-black background — matches the "off marquee" aesthetic of real
  // arcade cabinets, and provides decent contrast for most cover art.
  setAutoFillBackground(true);
  QPalette pal = palette();
  pal.setColor(QPalette::Window, Qt::black);
  setPalette(pal);

  // Stacked layout so we can swap between image (QLabel) and video
  // (QVideoWidget) modes without rebuilding the window. Index sentinels
  // mirror the STACK_IMAGE / STACK_VIDEO constants in the header.
  m_stack = new QStackedLayout(this);
  m_stack->setContentsMargins(0, 0, 0, 0);
  m_stack->setSpacing(0);

  m_label = new QLabel(this);
  m_label->setAlignment(Qt::AlignCenter);
  // Black-on-black placeholder until setPixmap fires; without this the
  // first paint flashes the default-grey label background.
  m_label->setStyleSheet(QStringLiteral("background-color: black;"));
  m_stack->insertWidget(STACK_IMAGE, m_label);

  if (targetScreen) pinToScreen(targetScreen);
}

MarqueeWindow::~MarqueeWindow() = default;

void MarqueeWindow::pinToScreen(QScreen *screen) {
  QScreen *target = screen ? screen : QGuiApplication::primaryScreen();
  if (!target) return; // headless / no screens at all — give up quietly.
  // setScreen + setGeometry is the Qt-idiomatic way to move a top-level
  // to a specific screen; just calling move() into the screen's geometry
  // works on X11 but is unreliable on Wayland.
  setScreen(target);
  setGeometry(target->geometry());
}

void MarqueeWindow::setPixmap(const QPixmap &pixmap) {
  m_sourcePixmap = pixmap;
  // Switching back to image mode — stop any in-flight video so the
  // GPU isn't decoding frames the user can't see, and so the
  // QMediaPlayer doesn't keep the file handle open.
  if (m_mediaPlayer) {
    m_mediaPlayer->stop();
    m_currentVideoPath.clear();
  }
  m_stack->setCurrentIndex(STACK_IMAGE);
  rescaleLabel();
}

void MarqueeWindow::ensureVideoWidget() {
  if (m_videoWidget) return;
  m_videoWidget = new QVideoWidget(this);
  // Aspect-ratio fit matches the image-mode behaviour — letterbox over
  // the black background rather than cropping or stretching.
  m_videoWidget->setAspectRatioMode(Qt::KeepAspectRatio);
  m_videoWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_stack->insertWidget(STACK_VIDEO, m_videoWidget);

  m_mediaPlayer = new QMediaPlayer(this);
  m_mediaPlayer->setVideoOutput(m_videoWidget);
  // QMediaPlayer leaks audio device handles when constructed without
  // a sink on some Qt builds; instantiate a muted one rather than
  // hoping the player tolerates a null output.
  m_audioOutput = new QAudioOutput(this);
  m_audioOutput->setMuted(true);
  m_audioOutput->setVolume(0.0);
  m_mediaPlayer->setAudioOutput(m_audioOutput);

  // Loop the video by re-setting the source on EndOfMedia. Qt 6's
  // QMediaPlayer doesn't have a built-in loop count, so the
  // listen-and-restart pattern is the canonical workaround.
  connect(m_mediaPlayer, &QMediaPlayer::mediaStatusChanged, this,
          [this](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::EndOfMedia && !m_currentVideoPath.isEmpty()) {
              m_mediaPlayer->setPosition(0);
              m_mediaPlayer->play();
            }
          });
}

void MarqueeWindow::setVideo(const QString &videoPath) {
  // Stop-and-clear path. The label is reused to render the black
  // background while we switch back so the user doesn't see a flash
  // of stale video.
  if (videoPath.isEmpty()) {
    if (m_mediaPlayer) {
      m_mediaPlayer->stop();
      m_mediaPlayer->setSource(QUrl());
    }
    m_currentVideoPath.clear();
    m_sourcePixmap = QPixmap();
    m_stack->setCurrentIndex(STACK_IMAGE);
    rescaleLabel();
    return;
  }
  // Caller passed a non-existent path — same fallback as above. We log
  // through the parent's category so the marquee init path doesn't
  // drag in a logging category just for one warning line.
  if (!QFileInfo::exists(videoPath)) {
    setVideo(QString());
    return;
  }
  // Same source already playing? No-op so re-pushing the same item
  // doesn't restart the loop mid-frame and cause a visible jump.
  if (m_currentVideoPath == videoPath && m_mediaPlayer &&
      m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
    m_stack->setCurrentIndex(STACK_VIDEO);
    return;
  }
  ensureVideoWidget();
  m_currentVideoPath = videoPath;
  m_mediaPlayer->setSource(QUrl::fromLocalFile(videoPath));
  m_mediaPlayer->play();
  m_stack->setCurrentIndex(STACK_VIDEO);
}

void MarqueeWindow::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  rescaleLabel();
}

void MarqueeWindow::rescaleLabel() {
  if (!m_label) return;
  if (m_sourcePixmap.isNull()) {
    m_label->clear();
    return;
  }
  // Scale uniformly to fit the label, preserving aspect ratio. The label
  // is the full window so its size() matches the screen geometry. Smooth
  // transform is worth the cost — the marquee is essentially a still
  // image and the scale-up from a 600px cover to a 1920px marquee is
  // visible at viewing distance.
  m_label->setPixmap(
      m_sourcePixmap.scaled(m_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
