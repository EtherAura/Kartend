// Kartend-ztc64: ScraperCredentialsPanel gained a non-modal inline banner
// ("credentials stored unencrypted in settings.ini") driven by
// setStorageDemotionNotice. These tests assert the banner's lifecycle
// headlessly (the panel is never shown, mirroring test_configurationpanel):
// hidden by default, shown with the keychain failure reason embedded,
// cleared by the empty "resolved" notice, and rebuilt with its state intact
// when setProvider re-targets the panel (which recreates the widget tree).

#include "scrapercredentialspanel.h"

#include <QApplication>
#include <QLabel>
#include <QTest>

class TestScraperCredentialsPanel : public QObject {
  Q_OBJECT

private slots:
  void banner_hiddenByDefault();
  void setNotice_showsBannerWithReason();
  void emptyNotice_hidesBanner();
  void notice_survivesProviderRebuild();
};

namespace {

QLabel *banner(const ScraperCredentialsPanel &panel) {
  return panel.findChild<QLabel *>(QStringLiteral("credentialStorageWarningLabel"));
}

} // namespace

void TestScraperCredentialsPanel::banner_hiddenByDefault() {
  ScraperCredentialsPanel panel;
  QLabel *label = banner(panel);
  QVERIFY(label);
  QVERIFY(label->isHidden());
  QVERIFY(label->text().isEmpty());
}

void TestScraperCredentialsPanel::setNotice_showsBannerWithReason() {
  ScraperCredentialsPanel panel;
  panel.setStorageDemotionNotice(QStringLiteral("Could not open wallet"));

  QLabel *label = banner(panel);
  QVERIFY(label);
  QVERIFY(!label->isHidden());
  QVERIFY(label->text().contains(QStringLiteral("Could not open wallet")));
  QVERIFY(label->text().contains(QStringLiteral("unencrypted")));
}

void TestScraperCredentialsPanel::emptyNotice_hidesBanner() {
  ScraperCredentialsPanel panel;
  panel.setStorageDemotionNotice(QStringLiteral("Could not open wallet"));
  panel.setStorageDemotionNotice(QString());

  QLabel *label = banner(panel);
  QVERIFY(label);
  QVERIFY(label->isHidden());
  QVERIFY(label->text().isEmpty());
}

void TestScraperCredentialsPanel::notice_survivesProviderRebuild() {
  ScraperCredentialsPanel panel;
  panel.setStorageDemotionNotice(QStringLiteral("Could not open wallet"));

  // setProvider tears down and rebuilds the whole widget tree (including
  // the banner label) — the pending notice must carry over. The old label
  // is disposed via deleteLater, so flush deferred deletes before findChild
  // can race the stale twin.
  panel.setProvider(QStringLiteral("screenscraper"));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  QLabel *label = banner(panel);
  QVERIFY(label);
  QVERIFY(!label->isHidden());
  QVERIFY(label->text().contains(QStringLiteral("Could not open wallet")));
}

QTEST_MAIN(TestScraperCredentialsPanel)
#include "test_scrapercredentialspanel.moc"
