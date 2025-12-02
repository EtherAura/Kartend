#ifndef METADATASIDEBAR_H
#define METADATASIDEBAR_H

#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include "ui_metadatasidebar.h"

class MetadataSidebar : public QWidget {
  Q_OBJECT
public:
  explicit MetadataSidebar(QWidget *parent = nullptr);
  ~MetadataSidebar();
  void setMetadata(const QString &filePath, const QString &itemName);
  void clearMetadata();
  void setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy policy);

private:
  void setupUI();
  void updateFileInfo(const QString &filePath);
  [[nodiscard]] static QString formatFileSize(qint64 bytes);
  Ui::MetadataSidebar *ui;
};

#endif