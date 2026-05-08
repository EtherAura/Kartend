// per-collection background video. Mirrors VideoPreviewWidget's
// QVideoSink-based pipeline (frames decoded into a QImage, painted via
// paintEvent) but is tuned for the items viewport: always muted, cover-fit
// scaling, mouse-event transparent, and no QAudioOutput attached so the
// decoder doesn't spool up an audio stream.
#include "backgroundvideowidget.h"

#include <QEvent>
#include <QHideEvent>
#include <QMediaPlayer>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

BackgroundVideoWidget::BackgroundVideoWidget(QWidget *parent) : QWidget(parent) {
  // Pure visual surface: never eat mouse events meant for the grid above.
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  setAutoFillBackground(false);

  m_player = new QMediaPlayer(this);
  m_sink = new QVideoSink(this);
  m_player->setVideoSink(m_sink);
  m_player->setLoops(QMediaPlayer::Infinite);
  // No QAudioOutput attached on purpose — background video is ambient and
  // the decoder shouldn't spin up an audio path we'd just have to mute.

  connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
    if (!frame.isValid() || m_currentPath.isEmpty()) {
      return;
    }
    QImage img = frame.toImage();
    if (img.isNull()) {
      return;
    }
    m_currentImage = std::move(img);
    update();
  });

  // Track the parent's size so the video keeps filling the viewport even
  // when the user resizes the main window. Matches the geometry on the
  // first installEventFilter call so the widget appears at the right place
  // before any resize event fires.
  if (auto *p = qobject_cast<QWidget *>(parent)) {
    p->installEventFilter(this);
    setGeometry(p->rect());
  }
}

BackgroundVideoWidget::~BackgroundVideoWidget() {
  if (m_player) {
    m_player->stop();
    m_player->setSource(QUrl());
  }
}

void BackgroundVideoWidget::setVideoPath(const QString &filePath) {
  if (filePath.isEmpty()) {
    clear();
    return;
  }
  if (filePath == m_currentPath && m_player &&
      m_player->playbackState() == QMediaPlayer::PlayingState) {
    return;
  }
  m_currentPath = filePath;
  m_currentImage = QImage();
  if (m_player) {
    m_player->stop();
    m_player->setSource(QUrl::fromLocalFile(filePath));
    if (isVisible()) {
      m_player->play();
    }
  }
  update();
}

void BackgroundVideoWidget::clear() {
  m_currentPath.clear();
  m_currentImage = QImage();
  if (m_player) {
    m_player->stop();
    m_player->setSource(QUrl());
  }
  update();
}

void BackgroundVideoWidget::hideEvent(QHideEvent *event) {
  // Free decoder time whenever the items page isn't visible (detail page
  // overlay, settings dialog, etc.). showEvent resumes playback.
  if (m_player && m_player->playbackState() == QMediaPlayer::PlayingState) {
    m_player->pause();
  }
  QWidget::hideEvent(event);
}

void BackgroundVideoWidget::showEvent(QShowEvent *event) {
  // Re-sync to parent rect on every show — guards against stale geometry
  // when the items page becomes visible for the first time after a
  // stacked-widget switch. See HeaderLogoOverlay::showEvent for the same
  // pattern.
  if (auto *p = qobject_cast<QWidget *>(parent())) {
    setGeometry(p->rect());
  }
  if (m_player && !m_currentPath.isEmpty() &&
      m_player->playbackState() != QMediaPlayer::PlayingState) {
    m_player->play();
  }
  QWidget::showEvent(event);
}

void BackgroundVideoWidget::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);
  if (m_currentImage.isNull() || rect().isEmpty()) {
    return;
  }
  QPainter painter(this);
  // Cover-fit: scale by the LARGER dimension so the rect is fully filled,
  // possibly cropping the longer side. Centered. Matches CSS
  // `background-size: cover; background-position: center`.
  const QSize target = rect().size();
  QImage scaled =
      m_currentImage.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
  const int x = (target.width() - scaled.width()) / 2;
  const int y = (target.height() - scaled.height()) / 2;
  painter.drawImage(x, y, scaled);
}

bool BackgroundVideoWidget::eventFilter(QObject *watched, QEvent *event) {
  if (watched == parent() && event->type() == QEvent::Resize) {
    if (auto *p = qobject_cast<QWidget *>(parent())) {
      setGeometry(p->rect());
    }
  }
  return QWidget::eventFilter(watched, event);
}
