#ifndef COLLECTIONHEALTHDIALOG_H
#define COLLECTIONHEALTHDIALOG_H

#include <QDialog>

#include "collectionhealth.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QTextEdit;
class QVBoxLayout;
QT_END_NAMESPACE

/// Read-only viewer for a `CollectionHealth::Report`. Renders the
/// per-category counts plus the (capped) example paths in a scrolling
/// monospace text area so users can paste the list elsewhere or scan it
/// at a glance. No remediation actions surface here — the dialog is
/// strictly diagnostic.
class CollectionHealthDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CollectionHealthDialog)
public:
  explicit CollectionHealthDialog(QWidget *parent = nullptr);

  void setReport(const QString &collectionName, const CollectionHealth::Report &report);

private:
  void setupUi();

  QLabel *m_headerLabel = nullptr;
  QLabel *m_summaryLabel = nullptr;
  QTextEdit *m_detailView = nullptr;
};

#endif // COLLECTIONHEALTHDIALOG_H
