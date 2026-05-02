#ifndef METADATASIDEBAR_H
#define METADATASIDEBAR_H

#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include "ui_metadatasidebar.h"

class VideoPreviewWidget;
QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

class MetadataSidebar : public QWidget {
  Q_OBJECT
public:
  explicit MetadataSidebar(QWidget *parent = nullptr);
  ~MetadataSidebar();
  void setMetadata(const QString &filePath, const QString &itemName,
                   const QString &artworkDirectory = QString(),
                   const QString &videoDirectory = QString());
  void clearMetadata();
  void setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy policy);

private:
  void setupUI();
  void updateFileInfo(const QString &filePath);
  void loadArtwork(const QString &baseName, const QString &artworkDirectory);
  void schedulePreviewVideo(const QString &videoPath);
  void showArtworkOnly();
  [[nodiscard]] static QString formatFileSize(qint64 bytes);
  Ui::MetadataSidebar *ui;
  VideoPreviewWidget *m_videoPreview = nullptr;
  QTimer *m_videoStartTimer = nullptr;
  QString m_pendingVideoPath;
};

#endif