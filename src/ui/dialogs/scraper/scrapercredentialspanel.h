#ifndef SCRAPERCREDENTIALSPANEL_H
#define SCRAPERCREDENTIALSPANEL_H

#include "isettingspanel.h"
#include <QHash>
#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QFormLayout;
struct SettingsModel;

/// Embeddable per-provider credentials editor — lives inline inside
/// each scraper's sub-tab under Settings → Scrapers. Default-constructed
/// for Qt-designer use, then specialised at setup time via
/// `setProvider(providerId)` to render exactly that provider's fields.
/// Uses the deferred-save shape (panel mutates the model on edit + emits
/// `changed()`; the dialog persists on Apply).
class ScraperCredentialsPanel : public QWidget, public ISettingsPanel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ScraperCredentialsPanel)
public:
  explicit ScraperCredentialsPanel(QWidget *parent = nullptr);
  ~ScraperCredentialsPanel() override;

  /// Restrict the panel to a single provider's fields. Pass "tmdb" or
  /// "screenscraper". Empty string keeps the all-providers layout
  /// (used by the standalone ScraperCredentialsDialog).
  void setProvider(const QString &providerId);

  void setModel(SettingsModel *model);

  /// Kartend-ztc64: show/hide the non-modal "credentials stored unencrypted"
  /// banner. Non-empty @p reason (the keychain failure description) shows the
  /// banner; empty hides it. The host dialog seeds this from
  /// ISettingsManager::credentialDemotionReason() and tracks live changes via
  /// the credentialStorageDemotionChanged signal, so the banner clears as
  /// soon as a later save lands the secret back in the keychain.
  void setStorageDemotionNotice(const QString &reason);
  // ISettingsPanel (Kartend-ny2ki). load() was refresh(); save() flushes via
  // the existing private writeModel() (the panel's live-edit flush); clear()
  // is a no-op — this global panel is always backed by the single live model.
  void load() override;
  void save() override;
  void clear() override {}

signals:
  void changed();

private:
  void rebuildLayout();
  void addField(QFormLayout *form, const QString &providerId, const QString &fieldName,
                const QString &label, bool sensitive, const QString &placeholder = QString());
  void writeModel();

  void updateStorageDemotionBanner();

  SettingsModel *m_model = nullptr;
  QString m_providerFilter; // empty = all providers
  /// Kartend-ztc64: last reason passed to setStorageDemotionNotice. Kept as a
  /// member (not just label state) because rebuildLayout() recreates the
  /// banner widget on every setProvider call.
  QString m_storageDemotionReason;
  /// Warning banner row above the provider groups. Built by rebuildLayout,
  /// hidden unless m_storageDemotionReason is non-empty.
  QLabel *m_storageDemotionBanner = nullptr;
  /// Field-key → QLineEdit. Key shape `<providerId>/<fieldName>` so
  /// onSave can recover the GeneralSettings::scraperCredentials path.
  QHash<QString, QLineEdit *> m_fields;
  bool m_loading = false;
};

#endif // SCRAPERCREDENTIALSPANEL_H
