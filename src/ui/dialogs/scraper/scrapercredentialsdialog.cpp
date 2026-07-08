// Modal wrapper around the shared ScraperCredentialsPanel. The panel is
// the single source of truth for the credential forms (which providers,
// which fields, help text); this dialog only adds the intro blurb, the
// Save/Cancel buttons, and working-copy isolation so Cancel discards.
#include "scrapercredentialsdialog.h"

#include "uiconstants/color.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

#include "errordialog.h"
#include "isettingsmanager.h"
#include "scrapercredentialspanel.h"

ScraperCredentialsDialog::ScraperCredentialsDialog(GeneralSettings *generalSettings,
                                                   ISettingsManager *settingsManager,
                                                   QWidget *parent)
    : QDialog(parent), m_generalSettings(generalSettings), m_settingsManager(settingsManager) {
  setWindowTitle(tr("Scraper credentials"));
  setModal(true);
  resize(560, 360);
  buildUi();
}

ScraperCredentialsDialog::~ScraperCredentialsDialog() = default;

void ScraperCredentialsDialog::buildUi() {
  auto *root = new QVBoxLayout(this);

  auto *intro = new QLabel(tr("API tokens stored here let Kartend pull metadata + cover art "
                              "from third-party services. Leave fields blank to disable a "
                              "provider — Kartend ships no bundled keys, so each provider is "
                              "off until you paste your own credentials. Values live in your "
                              "config file under the [Scrapers] section."),
                           this);
  intro->setWordWrap(true);
  intro->setStyleSheet(UIConstants::Color::MUTED_TEXT);
  root->addWidget(intro);

  // The panel live-mutates its model on every edit, so it edits a working
  // copy of the caller's settings: Save copies it back, Cancel drops it —
  // preserving the old hand-built dialog's edit-then-Cancel semantics.
  if (m_generalSettings) {
    m_working = *m_generalSettings;
  }
  m_model.generalSettings = &m_working;
  // No setProvider() call — the default (empty) filter renders every
  // provider's fields, which is exactly this dialog's job.
  m_panel = new ScraperCredentialsPanel(this);
  m_panel->setModel(&m_model);
  if (m_settingsManager) {
    // Mirror the settings dialog's storage-demotion banner wiring so the
    // "credentials stored unencrypted" warning also surfaces here.
    m_panel->setStorageDemotionNotice(m_settingsManager->credentialDemotionReason());
    connect(m_settingsManager, &ISettingsManager::credentialStorageDemotionChanged, m_panel,
            &ScraperCredentialsPanel::setStorageDemotionNotice);
  }
  root->addWidget(m_panel);

  root->addStretch();

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &ScraperCredentialsDialog::onSave);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);
}

void ScraperCredentialsDialog::onSave() {
  if (m_generalSettings) {
    // Flush the panel into the working copy (trimmed write-through, empty-
    // value key cleanup, legacy dev_* scrub), then commit to the caller.
    m_panel->save();
    *m_generalSettings = m_working;
    if (m_settingsManager) {
      // Logout silently leaving credentials on disk would defeat the purpose
      // — surface the disk-write failure so the user knows their password is
      // still persisted.
      if (auto result = m_settingsManager->saveGeneralSettings(*m_generalSettings);
          result.isError()) {
        ErrorDialog::showError(this, result.error());
      }
    }
  }
  accept();
}
