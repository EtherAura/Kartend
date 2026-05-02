#ifndef METADATASIDEBAR_H
#define METADATASIDEBAR_H

#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include "itemmetadata.h"
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
  /// Renders extended metadata in a Details section. Hides the section
  /// entirely when `metadata.isEmpty()` so empty rows do not clutter the UI.
  void setExtendedMetadata(const ItemMetadataStore::ItemMetadata &metadata);
  void clearMetadata();
  void setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy policy);

private:
  void setupUI();
  void updateFileInfo(const QString &filePath);
  void loadArtwork(const QString &baseName, const QString &artworkDirectory);
  void schedulePreviewVideo(const QString &videoPath);
  void showArtworkOnly();
  void ensureDetailsSection();
  void clearDetailsSection();
  void appendDetailRow(const QString &label, const QString &value, bool wrap = false);
  [[nodiscard]] static QString formatFileSize(qint64 bytes);
  [[nodiscard]] static QString formatRuntime(int seconds);
  [[nodiscard]] static QString formatTags(const QString &raw);
  Ui::MetadataSidebar *ui;
  VideoPreviewWidget *m_videoPreview = nullptr;
  QTimer *m_videoStartTimer = nullptr;
  QString m_pendingVideoPath;

  // Dynamically-built "Details" section appended to the content layout.
  // Built lazily on first use so existing layouts (and tests that don't show
  // the sidebar) are unaffected.
  QWidget *m_detailsContainer = nullptr;
  QVBoxLayout *m_detailsLayout = nullptr;
  QLabel *m_detailsTitle = nullptr;
};

#endif