#ifndef KARTEND_UI_DIALOGS_KART_KARTPREFLIGHTDIALOG_H
#define KARTEND_UI_DIALOGS_KART_KARTPREFLIGHTDIALOG_H

#include <QDialog>

#include "kartpreflight.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QTreeWidget;
QT_END_NAMESPACE

/// Modal preflight summary shown after the user picks a .kart file but
/// before extraction begins. Surfaces the bundle's metadata plus any
/// validation concerns (missing launchers, suspicious paths, name
/// conflicts). Accepting the dialog tells KartManager to proceed; rejecting
/// cancels the import without touching any files.
class KartPreflightDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(KartPreflightDialog)

public:
  explicit KartPreflightDialog(const KartPreflight::PreflightReport &report,
                               QWidget *parent = nullptr);
  ~KartPreflightDialog() override = default;

private:
  void setupUi(const KartPreflight::PreflightReport &report);
  void populateSummary(const KartPreflight::PreflightReport &report);
  void populateIssues(const KartPreflight::PreflightReport &report);

  QLabel *m_summaryLabel = nullptr;
  QTreeWidget *m_issuesTree = nullptr;
  QLabel *m_statusBanner = nullptr;
};

#endif // KARTEND_UI_DIALOGS_KART_KARTPREFLIGHTDIALOG_H
