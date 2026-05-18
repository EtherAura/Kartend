#ifndef SCRAPERCREDENTIALSDIALOG_H
#define SCRAPERCREDENTIALSDIALOG_H

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>

QT_BEGIN_NAMESPACE
class QLineEdit;
QT_END_NAMESPACE

struct GeneralSettings;
class ISettingsManager;

/// Modal dialog for per-provider scraper credentials. One section per
/// provider that needs auth; each section is a labeled QLineEdit per
/// credential field (sensitive fields use Echo::Password). Save
/// writes through to GeneralSettings::scraperCredentials and persists
/// via ISettingsManager.
///
/// Legacy modal — superseded by the inline `ScraperCredentialsPanel`
/// instances embedded under each provider's sub-tab in Settings →
/// Scrapers. Kept compiled for one release to avoid a hard removal
/// landing in the same change as the UI relocation; safe to delete
/// once no migration paths reference it.
class ScraperCredentialsDialog : public QDialog {
  Q_OBJECT
public:
  ScraperCredentialsDialog(GeneralSettings *generalSettings, ISettingsManager *settingsManager,
                           QWidget *parent = nullptr);
  ~ScraperCredentialsDialog() override;

private slots:
  void onSave();

private:
  void buildUi();
  /// Add a field row to the form. The field is registered via
  /// `m_fields` keyed on `<providerId>/<fieldName>` so the save
  /// handler can iterate without per-provider boilerplate.
  void addField(class QFormLayout *form, const QString &providerId, const QString &fieldName,
                const QString &label, bool sensitive, const QString &placeholder = {});

  GeneralSettings *m_generalSettings = nullptr;
  ISettingsManager *m_settingsManager = nullptr;

  /// Keyed on `<providerId>/<fieldName>` — same shape as the on-disk
  /// `[Scrapers]` keys.
  QHash<QString, QLineEdit *> m_fields;
};

#endif // SCRAPERCREDENTIALSDIALOG_H
