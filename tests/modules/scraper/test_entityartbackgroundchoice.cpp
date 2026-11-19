// Kartend-ji94z: a platform wallpaper the user explicitly picked must survive
// later entity scrapes. The real ScraperService entity coordinator runs
// offline against a synthetic provider whose Background-role platform asset
// lands under `_shared/` and would otherwise replace the configured wallpaper
// (same platform scope); only a marker naming the exact configured path keeps
// it. Unmarked and stale markers stay on the normal scrape-owned rules.
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
#include "metadatalookupprovider.h"
#include "scraperservice.h"

using Scraper::ScraperService;

namespace {

const QByteArray kIllustrationBytes = QByteArray("\x89PNG\x0d\x0a", 6) + "illustration";
const QUrl kIllustrationUrl(QStringLiteral("https://example.test/illustration.png"));

/// Answers fetchEntity() with one platform-scoped Background-role asset and
/// serves its bytes from memory — no network, no account.
class BackgroundArtProvider : public MetadataLookupProvider {
public:
  QString id() const override { return QStringLiteral("backgroundstub"); }
  QString displayName() const override { return QStringLiteral("BackgroundStub"); }
  QStringList categories() const override { return {}; }
  Capabilities capabilities() const override { return Capability::MetadataLookup; }
  QList<Scraper::ScrapeEntityType> supportedEntities() const override {
    return {Scraper::ScrapeEntityType::Game, Scraper::ScrapeEntityType::Platform};
  }
  QUrl searchUrl(const QString &) const override { return {}; }
  void lookup(const QString &, LookupCallback cb) override {
    cb(QList<Scraper::ScrapeCandidate>{});
  }
  void fetchDetail(const Scraper::ScrapeCandidate &, DetailCallback cb) override {
    cb(Scraper::ScrapedItem{});
  }
  void fetchMediaBytes(const QUrl &url, MediaCallback cb) override {
    if (url != kIllustrationUrl) {
      QFAIL(qPrintable(QStringLiteral("unexpected media fetch: %1").arg(url.toString())));
    }
    cb(kIllustrationBytes);
  }
  void fetchEntity(const Scraper::EntityScrapeTarget &, DetailCallback cb) override {
    Scraper::MediaAsset a;
    a.type = QStringLiteral("illustration");
    a.scope = Scraper::MediaScope::Platform;
    a.scopeKey = QStringLiteral("4");
    a.url = kIllustrationUrl;
    a.entityRole = Scraper::EntityArtRole::Background;
    Scraper::ScrapedItem item;
    item.title = QStringLiteral("Example Console");
    item.media = {a};
    cb(item);
  }
};

QByteArray readFile(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {};
  return f.readAll();
}

} // namespace

class TestEntityArtBackgroundChoice : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void init();
  void cleanup();

  void matchingMarkerKeepsChosenWallpaper();
  void unmarkedWallpaperIsReplaced();
  void nonmatchingMarkerDoesNotProtect();
  void staleMarkerNamingIncomingPathIsNotAdopted();

private:
  /// Runs one Platform entity job for @p collections[0] and waits until it is
  /// booked as scraped; the incoming illustration must have landed.
  void runEntityJob(QList<CollectionConfig> &collections);
  QList<CollectionConfig> collectionWith(const QString &chosenPath) const;

  std::unique_ptr<QTemporaryDir> m_tmp;
  QString m_currentWallpaper; // a platform image under _shared, as if picked
  QString m_incoming;         // where the scraped Background asset lands
};

void TestEntityArtBackgroundChoice::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-entityartbackgroundchoice"));
}

void TestEntityArtBackgroundChoice::init() {
  m_tmp = std::make_unique<QTemporaryDir>();
  QVERIFY(m_tmp->isValid());
  m_currentWallpaper =
      QDir(m_tmp->path()).filePath(QStringLiteral("_shared/fanart/platform_4.png"));
  m_incoming = QDir(m_tmp->path()).filePath(QStringLiteral("_shared/illustration/platform_4.png"));
}

void TestEntityArtBackgroundChoice::cleanup() {
  m_tmp.reset();
}

QList<CollectionConfig>
TestEntityArtBackgroundChoice::collectionWith(const QString &chosenPath) const {
  CollectionConfig cfg;
  cfg.name = QStringLiteral("Example Console");
  cfg.type = QStringLiteral("Games");
  cfg.background.backgroundImage = m_currentWallpaper;
  cfg.background.backgroundImageChosenPath = chosenPath;
  return {cfg};
}

void TestEntityArtBackgroundChoice::runEntityJob(QList<CollectionConfig> &collections) {
  auto provider = std::make_shared<BackgroundArtProvider>();
  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  ctx.collections = &collections; // ctx.ctx left null → in-memory mutate, no disk save
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionIndex = 0;
  job.collectionName = collections[0].name;
  job.artworkDir = m_tmp->path();
  job.entity.type = Scraper::ScrapeEntityType::Platform;
  job.entity.identity = QStringLiteral("4");
  service.startScrape({job}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);
  QTRY_COMPARE(service.summary().scraped, 1);
  QCOMPARE(service.summary().errors, 0);
  QCOMPARE(service.summary().mediaWritten, 1);
  // The scraped file is retained either way.
  QCOMPARE(readFile(m_incoming), kIllustrationBytes);
}

void TestEntityArtBackgroundChoice::matchingMarkerKeepsChosenWallpaper() {
  QList<CollectionConfig> collections = collectionWith(m_currentWallpaper);
  runEntityJob(collections);
  if (QTest::currentTestFailed()) return;
  QCOMPARE(collections[0].background.backgroundImage, m_currentWallpaper);
  QCOMPARE(collections[0].background.backgroundImageChosenPath, m_currentWallpaper);
}

void TestEntityArtBackgroundChoice::unmarkedWallpaperIsReplaced() {
  QList<CollectionConfig> collections = collectionWith(QString());
  runEntityJob(collections);
  if (QTest::currentTestFailed()) return;
  QCOMPARE(collections[0].background.backgroundImage, m_incoming);
  QVERIFY(collections[0].background.backgroundImageChosenPath.isEmpty());
}

void TestEntityArtBackgroundChoice::nonmatchingMarkerDoesNotProtect() {
  const QString other =
      QDir(m_tmp->path()).filePath(QStringLiteral("_shared/photo/platform_4.png"));
  QList<CollectionConfig> collections = collectionWith(other);
  runEntityJob(collections);
  if (QTest::currentTestFailed()) return;
  QCOMPARE(collections[0].background.backgroundImage, m_incoming);
  QVERIFY(collections[0].background.backgroundImageChosenPath !=
          collections[0].background.backgroundImage);
}

void TestEntityArtBackgroundChoice::staleMarkerNamingIncomingPathIsNotAdopted() {
  // The marker names the very path the scrape is about to wire in; after the
  // replacement it must not read as a user choice of that path.
  QList<CollectionConfig> collections = collectionWith(m_incoming);
  runEntityJob(collections);
  if (QTest::currentTestFailed()) return;
  QCOMPARE(collections[0].background.backgroundImage, m_incoming);
  QVERIFY(collections[0].background.backgroundImageChosenPath !=
          collections[0].background.backgroundImage);
}

QTEST_MAIN(TestEntityArtBackgroundChoice)
#include "test_entityartbackgroundchoice.moc"
