// Kartend-0eeuk: ScraperCredentialsDialog dialog-level save mapping (the
// inline ScraperCredentialsPanel internals are covered by
// test_scrapercredentialspanel.cpp; this legacy modal composes its own
// field set). Pins the provider/field composition (tmdb token + the two
// ScreenScraper member fields, sensitive ones in Password echo),
// pre-population from the live GeneralSettings, Save's trimmed write-through
// into scraper.credentials, the empty-value remove-key/remove-provider
// cleanup, and the legacy dev_id / dev_password scrub. The ISettingsManager
// is null throughout — persistence is the manager's job, the mapping is the
// dialog's. Driven headlessly — never shown or exec()'d.

#include "scrapercredentialsdialog.h"

#include "collection/generalsettings.h"

#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

namespace {

/// The TMDB token edit is the only one with the sample-token placeholder.
QLineEdit *tmdbTokenEdit(const QWidget &dlg) {
  const auto edits = dlg.findChildren<QLineEdit *>();
  for (QLineEdit *e : edits) {
    if (e->placeholderText().startsWith(QStringLiteral("eyJ"))) {
      return e;
    }
  }
  return nullptr;
}

/// The ScreenScraper username is the only Normal-echo edit.
QLineEdit *ssUserEdit(const QWidget &dlg) {
  const auto edits = dlg.findChildren<QLineEdit *>();
  for (QLineEdit *e : edits) {
    if (e->echoMode() == QLineEdit::Normal) {
      return e;
    }
  }
  return nullptr;
}

/// The ScreenScraper password is the Password-echo edit without the TMDB
/// sample-token placeholder.
QLineEdit *ssPasswordEdit(const QWidget &dlg) {
  const auto edits = dlg.findChildren<QLineEdit *>();
  for (QLineEdit *e : edits) {
    if (e->echoMode() == QLineEdit::Password && e->placeholderText().isEmpty()) {
      return e;
    }
  }
  return nullptr;
}

void clickSave(const QWidget &dlg) {
  auto *box = dlg.findChild<QDialogButtonBox *>();
  QVERIFY(box);
  QPushButton *save = box->button(QDialogButtonBox::Save);
  QVERIFY(save);
  save->click();
}

} // namespace

class TestScraperCredentialsDialog : public QObject {
  Q_OBJECT

private slots:
  void composesThreeFieldsWithSensitiveEchoGating();
  void prepopulatesFromLiveSettings();
  void saveWritesTrimmedValuesAndAccepts();
  void emptyValueRemovesFieldAndEmptyProviderBlob();
  void saveScrubsLegacyDevCredentials();
};

void TestScraperCredentialsDialog::composesThreeFieldsWithSensitiveEchoGating() {
  GeneralSettings settings;
  ScraperCredentialsDialog dlg(&settings, /*settingsManager=*/nullptr);

  QLineEdit *token = tmdbTokenEdit(dlg);
  QLineEdit *user = ssUserEdit(dlg);
  QLineEdit *password = ssPasswordEdit(dlg);
  QVERIFY(token);
  QVERIFY(user);
  QVERIFY(password);
  QVERIFY(token != password);
  QCOMPARE(dlg.findChildren<QLineEdit *>().size(), 3);

  // Sensitive fields hide the value at rest; the username does not.
  QCOMPARE(token->echoMode(), QLineEdit::Password);
  QCOMPARE(password->echoMode(), QLineEdit::Password);
  QCOMPARE(user->echoMode(), QLineEdit::Normal);
}

void TestScraperCredentialsDialog::prepopulatesFromLiveSettings() {
  GeneralSettings settings;
  auto &creds = settings.scraper.credentials;
  creds[QStringLiteral("tmdb")][QStringLiteral("api_token")] = QStringLiteral("tok-1");
  creds[QStringLiteral("screenscraper")][QStringLiteral("user_id")] = QStringLiteral("alice");
  creds[QStringLiteral("screenscraper")][QStringLiteral("user_password")] = QStringLiteral("pw");

  ScraperCredentialsDialog dlg(&settings, nullptr);
  QCOMPARE(tmdbTokenEdit(dlg)->text(), QStringLiteral("tok-1"));
  QCOMPARE(ssUserEdit(dlg)->text(), QStringLiteral("alice"));
  QCOMPARE(ssPasswordEdit(dlg)->text(), QStringLiteral("pw"));
}

void TestScraperCredentialsDialog::saveWritesTrimmedValuesAndAccepts() {
  GeneralSettings settings;
  ScraperCredentialsDialog dlg(&settings, nullptr);
  QSignalSpy accepted(&dlg, &QDialog::accepted);

  tmdbTokenEdit(dlg)->setText(QStringLiteral("  tok-2  "));
  ssUserEdit(dlg)->setText(QStringLiteral(" bob "));
  ssPasswordEdit(dlg)->setText(QStringLiteral("secret"));
  clickSave(dlg);

  const auto &creds = settings.scraper.credentials;
  QCOMPARE(creds.value(QStringLiteral("tmdb")).value(QStringLiteral("api_token")),
           QStringLiteral("tok-2"));
  QCOMPARE(creds.value(QStringLiteral("screenscraper")).value(QStringLiteral("user_id")),
           QStringLiteral("bob"));
  QCOMPARE(creds.value(QStringLiteral("screenscraper")).value(QStringLiteral("user_password")),
           QStringLiteral("secret"));
  QCOMPARE(accepted.count(), 1);
}

void TestScraperCredentialsDialog::emptyValueRemovesFieldAndEmptyProviderBlob() {
  GeneralSettings settings;
  auto &creds = settings.scraper.credentials;
  creds[QStringLiteral("tmdb")][QStringLiteral("api_token")] = QStringLiteral("old-tok");
  creds[QStringLiteral("screenscraper")][QStringLiteral("user_id")] = QStringLiteral("alice");
  creds[QStringLiteral("screenscraper")][QStringLiteral("user_password")] = QStringLiteral("pw");

  ScraperCredentialsDialog dlg(&settings, nullptr);
  // Blank out TMDB entirely and the SS password; keep the SS username.
  tmdbTokenEdit(dlg)->setText(QString());
  ssPasswordEdit(dlg)->setText(QStringLiteral("   ")); // whitespace == empty
  clickSave(dlg);

  // The empty provider blob disappears, keeping [Scrapers] honest.
  QVERIFY(!creds.contains(QStringLiteral("tmdb")));
  // The partially-cleared provider keeps only its remaining field.
  QVERIFY(creds.contains(QStringLiteral("screenscraper")));
  QVERIFY(!creds.value(QStringLiteral("screenscraper")).contains(QStringLiteral("user_password")));
  QCOMPARE(creds.value(QStringLiteral("screenscraper")).value(QStringLiteral("user_id")),
           QStringLiteral("alice"));
}

void TestScraperCredentialsDialog::saveScrubsLegacyDevCredentials() {
  // dev_* keys are no longer surfaced; a save must clear stale overrides
  // so they stop silently shadowing the bundled dev key.
  GeneralSettings settings;
  auto &creds = settings.scraper.credentials;
  creds[QStringLiteral("screenscraper")][QStringLiteral("dev_id")] = QStringLiteral("stale-dev");
  creds[QStringLiteral("screenscraper")][QStringLiteral("dev_password")] = QStringLiteral("x");

  {
    // With member fields filled the blob keeps only the member keys.
    ScraperCredentialsDialog dlg(&settings, nullptr);
    ssUserEdit(dlg)->setText(QStringLiteral("carol"));
    clickSave(dlg);
    const auto blob = creds.value(QStringLiteral("screenscraper"));
    QVERIFY(!blob.contains(QStringLiteral("dev_id")));
    QVERIFY(!blob.contains(QStringLiteral("dev_password")));
    QCOMPARE(blob.value(QStringLiteral("user_id")), QStringLiteral("carol"));
  }

  // Second save with the member fields blanked: scrubbing the dev keys
  // empties the blob, which must then be removed outright.
  {
    ScraperCredentialsDialog dlg(&settings, nullptr);
    ssUserEdit(dlg)->setText(QString());
    clickSave(dlg);
    QVERIFY(!creds.contains(QStringLiteral("screenscraper")));
  }
}

QTEST_MAIN(TestScraperCredentialsDialog)
#include "test_scrapercredentialsdialog.moc"
