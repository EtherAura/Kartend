// Kartend-0eeuk: ScraperProvidersDialog registry-view mapping. The dialog is
// a read-only diagnostic surface over MetadataProviderRegistry::builtIn();
// these tests pin the row population (one row per provider, id stashed under
// UserRole), the credentials-column derivation from GeneralSettings::
// scraper.credentials (any non-blank value = configured; whitespace-only or
// absent = not configured; null settings tolerated), and the Test-query
// activation path that renders the provider's searchUrl into the status
// label. Driven headlessly — the dialog is never shown or exec()'d.

#include "scraperprovidersdialog.h"

#include "collection/generalsettings.h"
#include "metadataprovider.h"
#include "metadataproviderregistry.h"

#include <QLabel>
#include <QLineEdit>
#include <QTest>
#include <QTreeWidget>

namespace {

QTreeWidgetItem *rowForProviderId(QTreeWidget *tree, const QString &id) {
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    QTreeWidgetItem *item = tree->topLevelItem(i);
    if (item->data(0, Qt::UserRole).toString() == id) {
      return item;
    }
  }
  return nullptr;
}

/// The status label is the only selectable-by-mouse QLabel in the dialog.
QLabel *statusLabel(const QWidget &dlg) {
  const auto labels = dlg.findChildren<QLabel *>();
  for (QLabel *l : labels) {
    if (l->textInteractionFlags().testFlag(Qt::TextSelectableByMouse)) {
      return l;
    }
  }
  return nullptr;
}

} // namespace

class TestScraperProvidersDialog : public QObject {
  Q_OBJECT

private slots:
  void populatesOneRowPerBuiltInProvider();
  void credentialsColumnReflectsConfiguredBlob();
  void whitespaceOnlyCredentialCountsAsNotConfigured();
  void nullSettingsShowsEveryProviderUnconfigured();
  void activatingARowRendersItsSearchUrl();
};

void TestScraperProvidersDialog::populatesOneRowPerBuiltInProvider() {
  GeneralSettings settings;
  ScraperProvidersDialog dlg(&settings);
  auto *tree = dlg.findChild<QTreeWidget *>();
  QVERIFY(tree);

  const auto providers = MetadataProviderRegistry::builtIn();
  QCOMPARE(tree->topLevelItemCount(), static_cast<int>(providers.size()));

  // Every registry provider has exactly one row carrying its id under
  // UserRole, with the display name in column 0 — the lookup key the
  // activation handler uses to re-resolve the provider.
  for (const auto &provider : providers) {
    QTreeWidgetItem *row = rowForProviderId(tree, provider->id());
    QVERIFY2(row, qPrintable(QStringLiteral("no row for %1").arg(provider->id())));
    QCOMPARE(row->text(0), provider->displayName());
  }
}

void TestScraperProvidersDialog::credentialsColumnReflectsConfiguredBlob() {
  GeneralSettings settings;
  settings.scraper.credentials[QStringLiteral("tmdb")][QStringLiteral("api_token")] =
      QStringLiteral("tok-123");

  ScraperProvidersDialog dlg(&settings);
  auto *tree = dlg.findChild<QTreeWidget *>();
  QVERIFY(tree);

  QTreeWidgetItem *tmdb = rowForProviderId(tree, QStringLiteral("tmdb"));
  QVERIFY(tmdb);
  QCOMPARE(tmdb->text(3), QStringLiteral("configured"));

  // A provider with no credential blob stays unconfigured.
  QTreeWidgetItem *moby = rowForProviderId(tree, QStringLiteral("mobygames"));
  QVERIFY(moby);
  QCOMPARE(moby->text(3), QStringLiteral("not configured"));
}

void TestScraperProvidersDialog::whitespaceOnlyCredentialCountsAsNotConfigured() {
  // The check trims values: a blob holding only blanks must not read as
  // configured (it would mislead a user diagnosing auth failures).
  GeneralSettings settings;
  settings.scraper.credentials[QStringLiteral("tmdb")][QStringLiteral("api_token")] =
      QStringLiteral("   ");

  ScraperProvidersDialog dlg(&settings);
  auto *tree = dlg.findChild<QTreeWidget *>();
  QVERIFY(tree);
  QTreeWidgetItem *tmdb = rowForProviderId(tree, QStringLiteral("tmdb"));
  QVERIFY(tmdb);
  QCOMPARE(tmdb->text(3), QStringLiteral("not configured"));
}

void TestScraperProvidersDialog::nullSettingsShowsEveryProviderUnconfigured() {
  ScraperProvidersDialog dlg(nullptr);
  auto *tree = dlg.findChild<QTreeWidget *>();
  QVERIFY(tree);
  QVERIFY(tree->topLevelItemCount() > 0);
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    QCOMPARE(tree->topLevelItem(i)->text(3), QStringLiteral("not configured"));
  }
}

void TestScraperProvidersDialog::activatingARowRendersItsSearchUrl() {
  GeneralSettings settings;
  ScraperProvidersDialog dlg(&settings);
  auto *tree = dlg.findChild<QTreeWidget *>();
  auto *query = dlg.findChild<QLineEdit *>();
  QLabel *status = statusLabel(dlg);
  QVERIFY(tree);
  QVERIFY(query);
  QVERIFY(status);
  QVERIFY(status->text().isEmpty()); // idle until a row is activated

  query->setText(QStringLiteral("Live concert"));
  QTreeWidgetItem *moby = rowForProviderId(tree, QStringLiteral("mobygames"));
  QVERIFY(moby);
  emit tree->itemActivated(moby, 0);

  // The handler re-resolves the provider by id and prints its test URL —
  // host from the registry's URL template, query carried through.
  QVERIFY2(status->text().contains(QStringLiteral("mobygames.com")), qPrintable(status->text()));
  QVERIFY(status->text().contains(QStringLiteral("Test URL for MobyGames")));
}

QTEST_MAIN(TestScraperProvidersDialog)
#include "test_scraperprovidersdialog.moc"
