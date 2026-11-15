// Kartend-5b5r1: the Wikidata console photograph (P18) of a platform
// collection must survive the whole entity pipeline — the real
// WikidataLogoProvider answered offline through ProviderBase's fetch seam,
// ScraperService's entity coordinator, and the shared-art writer — and land
// as its own `_shared/photo/collection_<uuid>.<ext>` file without displacing
// the config-wired Logo/Background picks an earlier platform scrape made.
// Fixture names and entity ids are generic placeholders.
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QList>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <memory>

#include "../../support/testsandbox.h"
#include "collection/collectionconfig.h"
#include "scraperservice.h"
#include "wikidatalogoprovider.h"

using Scraper::ScraperService;

namespace {

const QString kUuid = QStringLiteral("uuid-console");
const QByteArray kPhotoBytes = QByteArray("\xFF\xD8\xFF\xE0", 4) + QByteArray("console-photo");
const QByteArray kLogoBytes = QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\"/>");

/// Answers every Wikidata/Commons request offline. @p claims is the claims
/// object of the resolved entity; @p description its one-line description.
/// Any request the fixture does not know fails the test.
void installFetch(const QByteArray &claims, const QByteArray &description, int *mediaFetches) {
  ProviderBase::setFetchFunctionForTesting([claims, description,
                                            mediaFetches](const QUrl &url,
                                                          const Scraper::HttpClient::RawHeaders &,
                                                          Scraper::HttpClient::ResponseCallback cb,
                                                          const QStringList &allowedHostSuffixes) {
    const QString s = url.toString();
    if (s.contains(QStringLiteral("wbsearchentities"))) {
      cb(QByteArrayLiteral(R"({"search":[{"id":"Q900001","label":"Example Console",)"
                           R"("description":"home video game console",)"
                           R"("match":{"text":"Example Console"}}]})"));
    } else if (s.contains(QStringLiteral("ids=Q900001"))) {
      QByteArray body = R"({"entities":{"Q900001":{)";
      if (!description.isEmpty()) {
        body += R"("descriptions":{"en":{"value":")" + description + R"("}},)";
      }
      body += R"("claims":)" + claims + "}}}";
      cb(body);
    } else if (url.host() == QStringLiteral("commons.wikimedia.org")) {
      // The media fetch keeps its Wikimedia host pin.
      if (!allowedHostSuffixes.contains(QStringLiteral("wikimedia.org"))) {
        QFAIL(qPrintable(QStringLiteral("media fetch without Wikimedia host pin: %1").arg(s)));
      }
      ++*mediaFetches;
      if (url.path().endsWith(QStringLiteral("Special:FilePath/Example_console_photo.jpg"))) {
        cb(kPhotoBytes);
      } else if (url.path().endsWith(QStringLiteral("Special:FilePath/Example_console_logo.svg"))) {
        cb(kLogoBytes);
      } else {
        QFAIL(qPrintable(QStringLiteral("unexpected media fetch: %1").arg(s)));
      }
    } else {
      QFAIL(qPrintable(QStringLiteral("unexpected fetch: %1").arg(s)));
    }
  });
}

QByteArray readFile(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {};
  return f.readAll();
}

} // namespace

class TestWikidataPlatformPhoto : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void init();
  void cleanup();

  void photoLandsInSharedArtWithoutRewiringPlatformPicks();
  void entityWithoutPhotoLeavesSharedArtAndConfigAsBefore();
  void photoOnlyEntityStillRetainsPhoto();

private:
  /// Runs one Collection entity job for collections[0] through the real
  /// Wikidata provider and waits until it is booked as scraped.
  void runEntityJob(QList<CollectionConfig> &collections, ScraperService &service);

  std::unique_ptr<QTemporaryDir> m_tmp;
  QString m_platformLogo;
  QString m_platformBackground;
};

void TestWikidataPlatformPhoto::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-wikidataplatformphoto"));
}

void TestWikidataPlatformPhoto::init() {
  m_tmp = std::make_unique<QTemporaryDir>();
  QVERIFY(m_tmp->isValid());
  // What an earlier ScreenScraper platform scrape wired into the config.
  m_platformLogo = QDir(m_tmp->path()).filePath(QStringLiteral("_shared/wheel/platform_4.png"));
  m_platformBackground =
      QDir(m_tmp->path()).filePath(QStringLiteral("_shared/illustration/platform_4.png"));
}

void TestWikidataPlatformPhoto::cleanup() {
  ProviderBase::setFetchFunctionForTesting({});
  m_tmp.reset();
}

void TestWikidataPlatformPhoto::runEntityJob(QList<CollectionConfig> &collections,
                                             ScraperService &service) {
  ScraperService::Context ctx;
  ctx.providerBuilder = [&collections](int) -> std::shared_ptr<MetadataLookupProvider> {
    return std::make_shared<WikidataLogoProvider>(
        [&collections]() -> const CollectionConfig * { return &collections[0]; });
  };
  ctx.collections = &collections; // ctx.ctx left null → in-memory mutate, no disk save
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionIndex = 0;
  job.collectionName = collections[0].name;
  job.artworkDir = m_tmp->path();
  job.entity.type = Scraper::ScrapeEntityType::Collection;
  job.entity.identity = kUuid;
  service.startScrape({job}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);
  QTRY_COMPARE(service.summary().scraped, 1);
  QCOMPARE(service.summary().notFound, 0);
  QCOMPARE(service.summary().errors, 0);
}

static QList<CollectionConfig> platformCollection(const QString &logo, const QString &background) {
  CollectionConfig cfg;
  cfg.name = QStringLiteral("Example Console");
  cfg.type = QStringLiteral("Games");
  cfg.collectionIcon = logo;
  cfg.background.headerLogoImage = logo;
  cfg.background.backgroundImage = background;
  return {cfg};
}

void TestWikidataPlatformPhoto::photoLandsInSharedArtWithoutRewiringPlatformPicks() {
  int mediaFetches = 0;
  installFetch(QByteArrayLiteral(R"({
      "P154":[{"mainsnak":{"datavalue":{"value":"Example console logo.svg"}}}],
      "P18":[{"mainsnak":{"datavalue":{"value":"Example console photo.jpg"}}}]})"),
               QByteArrayLiteral("home video game console"), &mediaFetches);

  QList<CollectionConfig> collections = platformCollection(m_platformLogo, m_platformBackground);
  ScraperService service;
  runEntityJob(collections, service);
  if (QTest::currentTestFailed()) return;

  QCOMPARE(mediaFetches, 2);
  QCOMPARE(service.summary().mediaWritten, 2);
  const QString photo =
      QDir(m_tmp->path()).filePath(QStringLiteral("_shared/photo/collection_uuid-console.jpg"));
  QVERIFY2(QFile::exists(photo), qPrintable(photo));
  QCOMPARE(readFile(photo), kPhotoBytes);
  // The Wikidata logo lands beside it (role Logo, collection scope)...
  const QString logo =
      QDir(m_tmp->path()).filePath(QStringLiteral("_shared/logo-svg/collection_uuid-console.svg"));
  QCOMPARE(readFile(logo), kLogoBytes);
  // ...but neither it nor the photo displaces the platform-scoped picks.
  QCOMPARE(collections[0].collectionIcon, m_platformLogo);
  QCOMPARE(collections[0].background.headerLogoImage, m_platformLogo);
  QCOMPARE(collections[0].background.backgroundImage, m_platformBackground);
}

void TestWikidataPlatformPhoto::entityWithoutPhotoLeavesSharedArtAndConfigAsBefore() {
  int mediaFetches = 0;
  installFetch(QByteArrayLiteral(R"({
      "P154":[{"mainsnak":{"datavalue":{"value":"Example console logo.svg"}}}]})"),
               QByteArrayLiteral("home video game console"), &mediaFetches);

  QList<CollectionConfig> collections = platformCollection(m_platformLogo, m_platformBackground);
  ScraperService service;
  runEntityJob(collections, service);
  if (QTest::currentTestFailed()) return;

  QCOMPARE(mediaFetches, 1);
  QCOMPARE(service.summary().mediaWritten, 1);
  QVERIFY(!QDir(QDir(m_tmp->path()).filePath(QStringLiteral("_shared/photo"))).exists());
  const QString logo =
      QDir(m_tmp->path()).filePath(QStringLiteral("_shared/logo-svg/collection_uuid-console.svg"));
  QCOMPARE(readFile(logo), kLogoBytes);
  QCOMPARE(collections[0].collectionIcon, m_platformLogo);
  QCOMPARE(collections[0].background.headerLogoImage, m_platformLogo);
  QCOMPARE(collections[0].background.backgroundImage, m_platformBackground);
}

void TestWikidataPlatformPhoto::photoOnlyEntityStillRetainsPhoto() {
  // An entity whose only usable claim is the P18 photo — no logo, no
  // description, no manufacturer, no sitelink — still has art to retain; it
  // must not be booked as not-found with the photo thrown away.
  int mediaFetches = 0;
  installFetch(QByteArrayLiteral(R"({
      "P18":[{"mainsnak":{"datavalue":{"value":"Example console photo.jpg"}}}]})"),
               QByteArray(), &mediaFetches);

  QList<CollectionConfig> collections = platformCollection(m_platformLogo, m_platformBackground);
  ScraperService service;
  runEntityJob(collections, service);
  if (QTest::currentTestFailed()) return;

  QCOMPARE(mediaFetches, 1);
  QCOMPARE(service.summary().mediaWritten, 1);
  const QString photo =
      QDir(m_tmp->path()).filePath(QStringLiteral("_shared/photo/collection_uuid-console.jpg"));
  QCOMPARE(readFile(photo), kPhotoBytes);
  QCOMPARE(collections[0].collectionIcon, m_platformLogo);
  QCOMPARE(collections[0].background.headerLogoImage, m_platformLogo);
  QCOMPARE(collections[0].background.backgroundImage, m_platformBackground);
}

QTEST_MAIN(TestWikidataPlatformPhoto)
#include "test_wikidataplatformphoto.moc"
