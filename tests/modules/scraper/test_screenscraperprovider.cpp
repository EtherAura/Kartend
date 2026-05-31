// Tests for ScreenScraperProvider's request-URL construction (Kartend-hsboz).
//
// buildJeuInfosUrl turns (credentials, romnom, systemeid, hashes, hasUser)
// into the jeuInfos.php query. It is a private member but was extracted
// precisely so the SS query shape can be regression-tested without the
// network; TestScreenScraperProvider is a friend (see the class declaration)
// so it can call the builder directly. The builder touches no member state,
// so one shared provider with empty accessors is sufficient.
//
// What matters and is covered here: dev credentials always ride, user
// credentials only when hasUser; system/rom and the static softname/output
// params map correctly; md5/sha1/crc/romtaille appear only when the hash
// fields are populated; the endpoint stays https + the api host with
// credentials in the query (never the path); and a credential containing
// query delimiters cannot inject or override other params.
#include <memory>

#include <QString>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

#include "romhasher.h"
#include "screenscrapercatalogmanager.h"
#include "screenscraperprovider.h"

class TestScreenScraperProvider : public QObject {
  Q_OBJECT

  using Credentials = ScreenScraperCatalogManager::Credentials;

  QUrlQuery build(const Credentials &creds, const QString &romnom, int systemeid,
                  const RomHasher::Result &hashes, bool hasUser) {
    return QUrlQuery(m_provider->buildJeuInfosUrl(creds, romnom, systemeid, hashes, hasUser));
  }

private slots:
  void initTestCase();
  void devCredentials_alwaysPresent_userCredentials_gatedByHasUser();
  void systemAndRom_mapToExpectedParams();
  void hashes_appearOnlyWhenSet();
  void endpointIsHttpsApiHost_andCredentialsStayInQueryNotPath();
  void credentialWithReservedChars_doesNotInjectExtraParams();

private:
  std::unique_ptr<ScreenScraperProvider> m_provider;
};

void TestScreenScraperProvider::initTestCase() {
  // Empty accessors: buildJeuInfosUrl never reads them, and the constructor
  // only forwards a null settings pointer to registerHostThrottles.
  m_provider =
      std::make_unique<ScreenScraperProvider>(ScreenScraperProvider::GeneralSettingsAccessor{},
                                              ScreenScraperProvider::CollectionAccessor{});
}

void TestScreenScraperProvider::devCredentials_alwaysPresent_userCredentials_gatedByHasUser() {
  Credentials creds;
  creds.devId = QStringLiteral("dev123");
  creds.devPassword = QStringLiteral("devpw");
  creds.userId = QStringLiteral("user42");
  creds.userPassword = QStringLiteral("userpw");
  const RomHasher::Result hashes;

  // hasUser=false: dev credentials present, user credentials omitted entirely
  // (not emitted as empty params).
  {
    const QUrlQuery q = build(creds, QStringLiteral("rom"), 1, hashes, /*hasUser=*/false);
    QCOMPARE(q.queryItemValue(QStringLiteral("devid")), QStringLiteral("dev123"));
    QCOMPARE(q.queryItemValue(QStringLiteral("devpassword")), QStringLiteral("devpw"));
    QVERIFY(!q.hasQueryItem(QStringLiteral("ssid")));
    QVERIFY(!q.hasQueryItem(QStringLiteral("sspassword")));
  }
  // hasUser=true: user credentials added under ssid/sspassword.
  {
    const QUrlQuery q = build(creds, QStringLiteral("rom"), 1, hashes, /*hasUser=*/true);
    QCOMPARE(q.queryItemValue(QStringLiteral("devid")), QStringLiteral("dev123"));
    QCOMPARE(q.queryItemValue(QStringLiteral("devpassword")), QStringLiteral("devpw"));
    QCOMPARE(q.queryItemValue(QStringLiteral("ssid")), QStringLiteral("user42"));
    QCOMPARE(q.queryItemValue(QStringLiteral("sspassword")), QStringLiteral("userpw"));
  }
}

void TestScreenScraperProvider::systemAndRom_mapToExpectedParams() {
  Credentials creds;
  creds.devId = QStringLiteral("d");
  creds.devPassword = QStringLiteral("p");
  const RomHasher::Result hashes;

  const QUrlQuery q = build(creds, QStringLiteral("Sonic The Hedgehog.bin"), 1, hashes, false);
  QCOMPARE(q.queryItemValue(QStringLiteral("systemeid")), QStringLiteral("1"));
  QCOMPARE(q.queryItemValue(QStringLiteral("romnom")), QStringLiteral("Sonic The Hedgehog.bin"));
  QCOMPARE(q.queryItemValue(QStringLiteral("softname")), QStringLiteral("kartend"));
  QCOMPARE(q.queryItemValue(QStringLiteral("output")), QStringLiteral("json"));
}

void TestScreenScraperProvider::hashes_appearOnlyWhenSet() {
  Credentials creds;
  creds.devId = QStringLiteral("d");
  creds.devPassword = QStringLiteral("p");

  // Default Result (empty hashes, size=-1): no hash params at all.
  {
    const RomHasher::Result hashes;
    const QUrlQuery q = build(creds, QStringLiteral("rom"), 1, hashes, false);
    QVERIFY(!q.hasQueryItem(QStringLiteral("md5")));
    QVERIFY(!q.hasQueryItem(QStringLiteral("sha1")));
    QVERIFY(!q.hasQueryItem(QStringLiteral("crc")));
    QVERIFY(!q.hasQueryItem(QStringLiteral("romtaille")));
  }
  // Fully populated: every hash param present with its value.
  {
    RomHasher::Result hashes;
    hashes.md5 = QStringLiteral("d41d8cd98f00b204e9800998ecf8427e");
    hashes.sha1 = QStringLiteral("da39a3ee5e6b4b0d3255bfef95601890afd80709");
    hashes.crc = QStringLiteral("00000000");
    hashes.size = 1048576;
    const QUrlQuery q = build(creds, QStringLiteral("rom"), 1, hashes, false);
    QCOMPARE(q.queryItemValue(QStringLiteral("md5")), hashes.md5);
    QCOMPARE(q.queryItemValue(QStringLiteral("sha1")), hashes.sha1);
    QCOMPARE(q.queryItemValue(QStringLiteral("crc")), hashes.crc);
    QCOMPARE(q.queryItemValue(QStringLiteral("romtaille")), QStringLiteral("1048576"));
  }
  // Partial: only md5 set (size still -1) -> only md5 emitted.
  {
    RomHasher::Result hashes;
    hashes.md5 = QStringLiteral("abc");
    const QUrlQuery q = build(creds, QStringLiteral("rom"), 1, hashes, false);
    QVERIFY(q.hasQueryItem(QStringLiteral("md5")));
    QVERIFY(!q.hasQueryItem(QStringLiteral("sha1")));
    QVERIFY(!q.hasQueryItem(QStringLiteral("crc")));
    QVERIFY(!q.hasQueryItem(QStringLiteral("romtaille")));
  }
}

void TestScreenScraperProvider::endpointIsHttpsApiHost_andCredentialsStayInQueryNotPath() {
  Credentials creds;
  creds.devId = QStringLiteral("dev123");
  creds.devPassword = QStringLiteral("secretpw");
  const RomHasher::Result hashes;

  const QUrl url = m_provider->buildJeuInfosUrl(creds, QStringLiteral("rom"), 1, hashes, false);
  QCOMPARE(url.scheme(), QStringLiteral("https"));
  QCOMPARE(url.host(), QStringLiteral("api.screenscraper.fr"));
  QCOMPARE(url.path(), QStringLiteral("/api2/jeuInfos.php"));
  // Credentials must ride in the query string, never the path (Kartend-0gp7).
  QVERIFY2(!url.path().contains(QStringLiteral("secretpw")),
           "dev password leaked into the URL path");
}

void TestScreenScraperProvider::credentialWithReservedChars_doesNotInjectExtraParams() {
  Credentials creds;
  creds.devId = QStringLiteral("dev");
  // A password carrying query delimiters must not smuggle in extra params:
  // if '&'/'=' are not encoded, this would inject systemeid=999 ahead of the
  // real systemeid pair and override the requested system.
  creds.devPassword = QStringLiteral("p&systemeid=999");
  const RomHasher::Result hashes;

  const QUrlQuery q = build(creds, QStringLiteral("rom"), 42, hashes, false);
  QCOMPARE(q.queryItemValue(QStringLiteral("systemeid")), QStringLiteral("42"));
  QCOMPARE(q.queryItemValue(QStringLiteral("devpassword")), QStringLiteral("p&systemeid=999"));
}

QTEST_MAIN(TestScreenScraperProvider)
#include "test_screenscraperprovider.moc"
