// Kartend-ztc64: ScraperCredentialsPanel gained a non-modal inline banner
// ("credentials stored unencrypted in settings.ini") driven by
// setStorageDemotionNotice. These tests assert the banner's lifecycle
// headlessly (the panel is never shown, mirroring test_configurationpanel):
// hidden by default, shown with the keychain failure reason embedded,
// cleared by the empty "resolved" notice, and rebuilt with its state intact
// when setProvider re-targets the panel (which recreates the widget tree).

#include "scrapercredentialspanel.h"
#include "settingskeys.h"

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
  void noKeychainBuildSentinelGetsItsOwnWording();
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

void TestScraperCredentialsPanel::noKeychainBuildSentinelGetsItsOwnWording() {
  // Kartend-4ahok. Two demotion causes that must NOT share wording:
  //   - a runtime keychain failure is recoverable, so promising the
  //     credentials move back is true;
  //   - a build compiled without keychain support has nothing to move back
  //     to, so the same promise would be a lie.
  // The sentinel is how the panel tells them apart.
  ScraperCredentialsPanel panel;
  QLabel *label = banner(panel);
  QVERIFY(label);

  panel.setStorageDemotionNotice(
      QLatin1String(kartend::settings::keys::kCredentialDemotionNoKeychainBuild));
  QVERIFY(label->isVisibleTo(&panel));
  const QString buildText = label->text();
  QVERIFY2(!buildText.contains(QStringLiteral("move back")),
           "A build without keychain support must not promise recovery");
  QVERIFY(buildText.contains(QStringLiteral("without")));
  // The raw sentinel must never reach the user.
  QVERIFY2(!buildText.contains(QStringLiteral("@no-keychain-build")),
           "The sentinel is an internal marker, not display text");

  // A runtime reason keeps the recoverable wording AND still shows the reason.
  panel.setStorageDemotionNotice(QStringLiteral("Could not open wallet"));
  const QString runtimeText = label->text();
  QVERIFY(runtimeText.contains(QStringLiteral("Could not open wallet")));
  QVERIFY(runtimeText.contains(QStringLiteral("move back")));

  // Both name the real file. This string used to say "settings.ini", which is
  // a file Kartend does not write — a user following it found nothing.
  for (const QString &text : {buildText, runtimeText}) {
    QVERIFY2(text.contains(QStringLiteral("kartend.cfg")), "Banner must name the real config file");
    QVERIFY(!text.contains(QStringLiteral("settings.ini")));
  }
}

QTEST_MAIN(TestScraperCredentialsPanel)
#include "test_scrapercredentialspanel.moc"
