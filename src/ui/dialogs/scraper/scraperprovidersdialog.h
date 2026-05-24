#ifndef SCRAPERPROVIDERSDIALOG_H
#define SCRAPERPROVIDERSDIALOG_H

#include <QDialog>

struct GeneralSettings;

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

/// Read-only registry view for the built-in metadata providers.
///
/// Shows one row per provider with its categories, capability flags,
/// credentials-configured status (derived from
/// `GeneralSettings::scraperCredentials`), and a per-row "Test query"
/// button that invokes `searchUrl()` so the user can verify the URL
/// template against a sample query. Credentials are edited via the
/// existing ScraperCredentialsDialog — this dialog stays a diagnostic
/// surface only.
class ScraperProvidersDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ScraperProvidersDialog)
public:
  explicit ScraperProvidersDialog(const GeneralSettings *settings, QWidget *parent = nullptr);

private:
  void setupUi();
  void populate();
  void onRowActivated(QTreeWidgetItem *item);

  const GeneralSettings *m_settings = nullptr;
  QLineEdit *m_testQueryEdit = nullptr;
  QTreeWidget *m_tree = nullptr;
  QLabel *m_statusLabel = nullptr;
};

#endif // SCRAPERPROVIDERSDIALOG_H
