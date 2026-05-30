// Per-provider scraper credentials dialog. Currently configures TMDB
// (one field). When ScreenScraper.fr support lands, add a second
// section in buildUi() with the SS dev/user fields — the save flow
// is provider-agnostic via the m_fields map.
#include "scrapercredentialsdialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "collection/generalsettings.h"
#include "errordialog.h"
#include "isettingsmanager.h"

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
  intro->setStyleSheet("color: palette(mid);");
  root->addWidget(intro);

  // ── TMDB ────────────────────────────────────────────────────────
  auto *tmdbGroup = new QGroupBox(tr("The Movie Database (TMDB)"), this);
  auto *tmdbForm = new QFormLayout(tmdbGroup);
  auto *tmdbHelp = new QLabel(tr("Sign up free at themoviedb.org → Settings → API → "
                                 "<b>API Read Access Token (v4 auth)</b>. Paste the token below."),
                              tmdbGroup);
  tmdbHelp->setWordWrap(true);
  tmdbHelp->setOpenExternalLinks(true);
  tmdbForm->addRow(tmdbHelp);
  addField(tmdbForm, QStringLiteral("tmdb"), QStringLiteral("api_token"), tr("API token:"),
           /*sensitive=*/true, QStringLiteral("eyJhbGciOiJIUzI1NiJ9..."));
  root->addWidget(tmdbGroup);

  // ── ScreenScraper.fr ───────────────────────────────────────────
  // Member-only UI: the developer credentials are bundled into Kartend
  // and shared across every install. Surfacing them as overrideable
  // fields encouraged users to paste their *member* credentials into
  // the dev slot by mistake, which silently downgraded their effective
  // tier. The bundled dev_id stays in source (BundledCredentials::
  // screenscraper()); only the member account fields are user-facing.
  auto *ssGroup = new QGroupBox(tr("ScreenScraper.fr"), this);
  auto *ssForm = new QFormLayout(ssGroup);
  auto *ssHelp = new QLabel(tr("Sign in with your own ScreenScraper.fr account at "
                               "<a href=\"https://www.screenscraper.fr/membreinscription.php\">"
                               "screenscraper.fr/membreinscription.php</a>. Member credentials "
                               "raise your per-account request quota; premium members get more "
                               "concurrent threads + higher throughput than the shared default. "
                               "Leave blank to scrape without an account (much lower limits). "
                               "The per-collection ScreenScraper system is auto-detected from "
                               "the collection's name / type / extensions; override it from "
                               "the collection's <b>Configuration</b> tab if autodetect picks "
                               "the wrong one."),
                            ssGroup);
  ssHelp->setOpenExternalLinks(true);
  ssHelp->setWordWrap(true);
  ssForm->addRow(ssHelp);
  addField(ssForm, QStringLiteral("screenscraper"), QStringLiteral("user_id"), tr("Username:"),
           /*sensitive=*/false);
  addField(ssForm, QStringLiteral("screenscraper"), QStringLiteral("user_password"),
           tr("Password:"), /*sensitive=*/true);
  root->addWidget(ssGroup);

  root->addStretch();

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &ScraperCredentialsDialog::onSave);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);
}

void ScraperCredentialsDialog::addField(QFormLayout *form, const QString &providerId,
                                        const QString &fieldName, const QString &label,
                                        bool sensitive, const QString &placeholder) {
  auto *edit = new QLineEdit(this);
  if (!placeholder.isEmpty()) {
    edit->setPlaceholderText(placeholder);
  }
  if (sensitive) {
    // Password mode hides the token at rest. Users who want to verify
    // the value can copy it (Ctrl+C still works on a password field
    // when text is selected) — we don't add a "show password" toggle
    // because the token is visible in the INI on disk anyway.
    edit->setEchoMode(QLineEdit::Password);
  }
  // Pre-populate from the live settings so editing-then-Cancel
  // doesn't lose the existing value.
  if (m_generalSettings) {
    edit->setText(m_generalSettings->scraper.credentials.value(providerId).value(fieldName));
  }
  form->addRow(label, edit);
  m_fields.insert(providerId + QLatin1Char('/') + fieldName, edit);
}

void ScraperCredentialsDialog::onSave() {
  if (m_generalSettings) {
    for (auto it = m_fields.constBegin(); it != m_fields.constEnd(); ++it) {
      const QString fullKey = it.key();
      const int slash = fullKey.indexOf('/');
      if (slash <= 0) continue;
      const QString providerId = fullKey.left(slash);
      const QString fieldName = fullKey.mid(slash + 1);
      const QString value = it.value()->text().trimmed();
      if (value.isEmpty()) {
        // Empty = "remove this credential". Cleaning up keeps the
        // [Scrapers] section honest about what's configured.
        m_generalSettings->scraper.credentials[providerId].remove(fieldName);
        if (m_generalSettings->scraper.credentials[providerId].isEmpty()) {
          m_generalSettings->scraper.credentials.remove(providerId);
        }
      } else {
        m_generalSettings->scraper.credentials[providerId][fieldName] = value;
      }
    }
    // Scrub legacy dev_* keys. They used to be editable from this
    // dialog; since they're not surfaced any more, anyone who set
    // them by mistake would otherwise keep silently overriding the
    // bundled dev key. Saving the dialog now clears them. Power users
    // who genuinely registered their own dev_id can drop the value
    // back into `[Scrapers] screenscraper/dev_id=` directly in the
    // INI; that path still works at runtime.
    auto &ssBlob = m_generalSettings->scraper.credentials[QStringLiteral("screenscraper")];
    ssBlob.remove(QStringLiteral("dev_id"));
    ssBlob.remove(QStringLiteral("dev_password"));
    if (ssBlob.isEmpty()) {
      m_generalSettings->scraper.credentials.remove(QStringLiteral("screenscraper"));
    }
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
