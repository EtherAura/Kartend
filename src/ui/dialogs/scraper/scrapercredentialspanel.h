#ifndef SCRAPERCREDENTIALSPANEL_H
#define SCRAPERCREDENTIALSPANEL_H

#include <QHash>
#include <QString>
#include <QWidget>

class QLineEdit;
class QFormLayout;
struct SettingsModel;

/// Embeddable per-provider credentials editor — lives inline inside
/// each scraper's sub-tab under Settings → Scrapers. Default-constructed
/// for Qt-designer use, then specialised at setup time via
/// `setProvider(providerId)` to render exactly that provider's fields.
/// Uses the deferred-save shape (panel mutates the model on edit + emits
/// `changed()`; the dialog persists on Apply).
class ScraperCredentialsPanel : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ScraperCredentialsPanel)
public:
  explicit ScraperCredentialsPanel(QWidget *parent = nullptr);
  ~ScraperCredentialsPanel() override;

  /// Restrict the panel to a single provider's fields. Pass "tmdb" or
  /// "screenscraper". Empty string keeps the all-providers layout
  /// (used as the legacy fallback during transition).
  void setProvider(const QString &providerId);

  void setModel(SettingsModel *model);
  void refresh();

signals:
  void changed();

private:
  void rebuildLayout();
  void addField(QFormLayout *form, const QString &providerId, const QString &fieldName,
                const QString &label, bool sensitive, const QString &placeholder = QString());
  void writeModel();

  SettingsModel *m_model = nullptr;
  QString m_providerFilter; // empty = all providers
  /// Field-key → QLineEdit. Key shape `<providerId>/<fieldName>` so
  /// onSave can recover the GeneralSettings::scraperCredentials path.
  QHash<QString, QLineEdit *> m_fields;
  bool m_loading = false;
};

#endif // SCRAPERCREDENTIALSPANEL_H
