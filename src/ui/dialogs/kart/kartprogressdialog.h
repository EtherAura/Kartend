#ifndef KARTPROGRESSDIALOG_H
#define KARTPROGRESSDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLabel;
class QProgressBar;
class QPushButton;
QT_END_NAMESPACE

class KartProgressDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(KartProgressDialog)

public:
  explicit KartProgressDialog(const QString &titleText, QWidget *parent = nullptr);

public slots:
  void setFraction(double fraction);
  void setEntryName(const QString &relPath);
  void markFinished();

signals:
  void cancelRequested();

private:
  QLabel *m_titleLabel = nullptr;
  QLabel *m_entryLabel = nullptr;
  QProgressBar *m_bar = nullptr;
  QPushButton *m_cancelButton = nullptr;
};

#endif
