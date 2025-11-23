#ifndef METADATASIDEBAR_H
#define METADATASIDEBAR_H

#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include "ui_metadatasidebar.h"

class metadataSidebar : public QWidget {
  Q_OBJECT
public:
  explicit metadataSidebar(QWidget *parent = nullptr);
  ~metadataSidebar();
  void setmetadata(const QString &filePath, const QString &itemName);
  void clearMetadata();
  void setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy policy);

private:
  void setupUI();
  void updateFileInfo(const QString &filePath);
  static QString formatFileSize(qint64 bytes);
  Ui::metadataSidebar *ui;
};

#endif