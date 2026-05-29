#include "scrapercredentialspanel.h"

#include "collectiontypes.h"
#include "settingsmodel.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QVBoxLayout>

namespace {
constexpr int kLabelMinWidth = 200;
constexpr int kLineEditMaxWidth = 320;

// Set a uniform min width on every label in a QFormLayout so the
// label column aligns with the other settings panels. Walks the
// LabelRole items rather than tracking each addRow call.
void uniformLabelColumn(QFormLayout *form) {
  for (int row = 0; row < form->rowCount(); ++row) {
    QLayoutItem *item = form->itemAt(row, QFormLayout::LabelRole);
    if (!item) continue;
    if (auto *label = qobject_cast<QLabel *>(item->widget())) {
      label->setMinimumWidth(kLabelMinWidth);
    }
  }
}
} // namespace

ScraperCredentialsPanel::ScraperCredentialsPanel(QWidget *parent) : QWidget(parent) {
  rebuildLayout();
}

ScraperCredentialsPanel::~ScraperCredentialsPanel() = default;

void ScraperCredentialsPanel::setProvider(const QString &providerId) {
  if (m_providerFilter == providerId) return;
  m_providerFilter = providerId;
  rebuildLayout();
  if (m_model) refresh();
}

void ScraperCredentialsPanel::rebuildLayout() {
  // Wipe the current widget tree so setProvider can re-target without
  // leaving the previous provider's fields behind. QObject::deleteLater
  // would defer disposal past the next field lookup, so use direct
  // delete on the layout + its children here.
  m_fields.clear();
  if (auto *old = layout()) {
    QLayoutItem *child;
    while ((child = old->takeAt(0)) != nullptr) {
      if (auto *w = child->widget()) w->deleteLater();
      delete child;
    }
    delete old;
  }
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);

  const bool showAll = m_providerFilter.isEmpty();
  const bool showTmdb = showAll || m_providerFilter == QLatin1String("tmdb");
  const bool showSs = showAll || m_providerFilter == QLatin1String("screenscraper");

  // ── TMDB ────────────────────────────────────────────────────────
  if (showTmdb) {
    auto *tmdbGroup = new QGroupBox(tr("The Movie Database (TMDB)"), this);
    auto *tmdbForm = new QFormLayout(tmdbGroup);
    tmdbForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    tmdbForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    auto *tmdbHelp =
        new QLabel(tr("Sign up free at themoviedb.org → Settings → API → "
                      "<b>API Read Access Token (v4 auth)</b>. Paste the token below."),
                   tmdbGroup);
    tmdbHelp->setWordWrap(true);
    tmdbHelp->setOpenExternalLinks(true);
    tmdbForm->addRow(tmdbHelp);
    addField(tmdbForm, QStringLiteral("tmdb"), QStringLiteral("api_token"), tr("API token:"),
             /*sensitive=*/true, QStringLiteral("eyJhbGciOiJIUzI1NiJ9..."));
    uniformLabelColumn(tmdbForm);
    root->addWidget(tmdbGroup);
  }

  // ── ScreenScraper.fr ───────────────────────────────────────────
  // Member-only fields — dev credentials are bundled and not user-
  // facing; surfacing them invited mistakes (members pasting their
  // own member creds into the dev slots and silently downgrading
  // their tier).
  if (showSs) {
    auto *ssGroup = new QGroupBox(tr("ScreenScraper.fr"), this);
    auto *ssForm = new QFormLayout(ssGroup);
    ssForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ssForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    auto *ssHelp = new QLabel(tr("Sign in with your own ScreenScraper.fr account at "
                                 "<a href=\"https://www.screenscraper.fr/membreinscription.php\">"
                                 "screenscraper.fr/membreinscription.php</a>. Premium members "
                                 "get more concurrent threads + higher throughput than the "
                                 "shared default. Leave blank to scrape without an account "
                                 "(much lower limits)."),
                              ssGroup);
    ssHelp->setOpenExternalLinks(true);
    ssHelp->setWordWrap(true);
    ssForm->addRow(ssHelp);
    addField(ssForm, QStringLiteral("screenscraper"), QStringLiteral("user_id"), tr("Username:"),
             /*sensitive=*/false);
    addField(ssForm, QStringLiteral("screenscraper"), QStringLiteral("user_password"),
             tr("Password:"),
             /*sensitive=*/true);
    uniformLabelColumn(ssForm);
    root->addWidget(ssGroup);
  }

  root->addStretch();
}

void ScraperCredentialsPanel::addField(QFormLayout *form, const QString &providerId,
                                       const QString &fieldName, const QString &label,
                                       bool sensitive, const QString &placeholder) {
  auto *edit = new QLineEdit(this);
  if (!placeholder.isEmpty()) {
    edit->setPlaceholderText(placeholder);
  }
  if (sensitive) {
    edit->setEchoMode(QLineEdit::Password);
  }
  // Cap value width so usernames and API tokens don't stretch into
  // oversized lozenges across the dialog. Long tokens still scroll
  // within the field.
  edit->setMaximumWidth(kLineEditMaxWidth);
  form->addRow(label, edit);
  m_fields.insert(providerId + QLatin1Char('/') + fieldName, edit);
  // Live-mutate the model + emit changed so the dialog's deferred-save
  // path (saveGeneralSettingsFromUI) commits the new value on Apply.
  connect(edit, &QLineEdit::textEdited, this, [this](const QString &) {
    if (m_loading) return;
    writeModel();
    emit changed();
  });
}

void ScraperCredentialsPanel::setModel(SettingsModel *model) {
  m_model = model;
  refresh();
}

void ScraperCredentialsPanel::refresh() {
  if (!m_model || !m_model->generalSettings) return;
  m_loading = true;
  for (auto it = m_fields.constBegin(); it != m_fields.constEnd(); ++it) {
    const QString fullKey = it.key();
    const int slash = fullKey.indexOf('/');
    if (slash <= 0) continue;
    const QString providerId = fullKey.left(slash);
    const QString fieldName = fullKey.mid(slash + 1);
    it.value()->setText(
        m_model->generalSettings->scraperCredentials.value(providerId).value(fieldName));
  }
  m_loading = false;
}

void ScraperCredentialsPanel::writeModel() {
  if (!m_model || !m_model->generalSettings) return;
  for (auto it = m_fields.constBegin(); it != m_fields.constEnd(); ++it) {
    const QString fullKey = it.key();
    const int slash = fullKey.indexOf('/');
    if (slash <= 0) continue;
    const QString providerId = fullKey.left(slash);
    const QString fieldName = fullKey.mid(slash + 1);
    const QString value = it.value()->text().trimmed();
    if (value.isEmpty()) {
      m_model->generalSettings->scraperCredentials[providerId].remove(fieldName);
      if (m_model->generalSettings->scraperCredentials[providerId].isEmpty()) {
        m_model->generalSettings->scraperCredentials.remove(providerId);
      }
    } else {
      m_model->generalSettings->scraperCredentials[providerId][fieldName] = value;
    }
  }
  // Same legacy-key scrub as the old modal dialog: stale dev_* keys
  // get cleared so any user who once mis-pasted their member creds
  // into the dev slot stops being silently downgraded. Only run when
  // the SS panel is the active one (or when no filter is set).
  if (m_providerFilter.isEmpty() || m_providerFilter == QLatin1String("screenscraper")) {
    auto &ssBlob = m_model->generalSettings->scraperCredentials[QStringLiteral("screenscraper")];
    ssBlob.remove(QStringLiteral("dev_id"));
    ssBlob.remove(QStringLiteral("dev_password"));
    if (ssBlob.isEmpty()) {
      m_model->generalSettings->scraperCredentials.remove(QStringLiteral("screenscraper"));
    }
  }
}
