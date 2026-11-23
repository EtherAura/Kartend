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
#include <QUuid>

#include <memory>

#include "../../support/testsandbox.h"
#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "databasemanager.h"
#include "entitymetadata.h"
#include "itemmetadata.h"
#include "scraperservice.h"
#include "sessionmanager.h"
#include "wikidatalogoprovider.h"

using Scraper::ScraperService;

namespace {

const QString kUuid = QStringLiteral("uuid-console");
const QByteArray kPhotoBytes = QByteArray("\xFF\xD8\xFF\xE0", 4) + QByteArray("console-photo");
const QByteArray kLogoBytes = QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\"/>");

/// Answers every Wikidata/Commons request offline. @p claims is the claims
/// object of the resolved entity; @p description its one-line description.
/// Any request the fixture does not know fails the test. The labels hop for
/// the referenced CPU entity Q900100 answers only when @p labels is given.
void installFetch(const QByteArray &claims, const QByteArray &description, int *mediaFetches,
                  const QByteArray &labels = QByteArray()) {
  ProviderBase::setFetchFunctionForTesting([claims, description, mediaFetches,
                                            labels](const QUrl &url,
                                                    const Scraper::HttpClient::RawHeaders &,
                                                    Scraper::HttpClient::ResponseCallback cb,
                                                    const QStringList &allowedHostSuffixes) {
    const QString s = url.toString();
    if (!labels.isEmpty() && s.contains(QStringLiteral("ids=Q900100"))) {
      cb(labels);
    } else if (s.contains(QStringLiteral("wbsearchentities"))) {
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

/// A platform provider that knows no such system: every Platform entity is
/// not-found, which sends the coordinator to its Wikidata fallback.
class NotFoundPlatformProvider : public MetadataLookupProvider {
public:
  QString id() const override { return QStringLiteral("platformstub"); }
  QString displayName() const override { return QStringLiteral("PlatformStub"); }
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
  void fetchMediaBytes(const QUrl &url, MediaCallback) override {
    QFAIL(qPrintable(QStringLiteral("unexpected media fetch: %1").arg(url.toString())));
  }
  void fetchEntity(const Scraper::EntityScrapeTarget &, DetailCallback cb) override {
    cb(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::RemoteResourceNotFound,
                                       QStringLiteral("no such platform"),
                                       QStringLiteral("NotFoundPlatformProvider")));
  }
};

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
  void cpuOnlyRescrapeKeepsSavedMetadata_data();
  void cpuOnlyRescrapeKeepsSavedMetadata();
  void cpuOnlyCollectionJobKeepsPlatformSummary();
  void cpuOnlyRescrapeLeavesUnreadableRowAlone();
  void populatedCollectionRowKeepsSummaryPrecedence();
  void cpuOnlyFirstScrapeSavesCpu();

private:
  /// Runs one CPU-only entity job for collections[0] against @p appCtx's
  /// real DatabaseManager: a Collection job straight through Wikidata, or a
  /// Platform job (identity "4") whose platform provider finds nothing so the
  /// coordinator falls back to Wikidata. Waits until it is booked as scraped.
  void runCpuOnlyJob(QList<CollectionConfig> &collections, ApplicationContext &appCtx,
                     bool collectionJob, const QString &uuid);
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

void TestWikidataPlatformPhoto::runCpuOnlyJob(QList<CollectionConfig> &collections,
                                              ApplicationContext &appCtx, bool collectionJob,
                                              const QString &uuid) {
  ScraperService service;
  ScraperService::Context ctx;
  ctx.ctx = &appCtx;
  ctx.providerBuilder = [&collections,
                         collectionJob](int) -> std::shared_ptr<MetadataLookupProvider> {
    if (!collectionJob) return std::make_shared<NotFoundPlatformProvider>();
    return std::make_shared<WikidataLogoProvider>(
        [&collections]() -> const CollectionConfig * { return &collections[0]; });
  };
  ctx.collections = &collections;
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionIndex = 0;
  job.collectionName = collections[0].name;
  job.collectionUuid = uuid;
  job.artworkDir = m_tmp->path();
  job.entity.type =
      collectionJob ? Scraper::ScrapeEntityType::Collection : Scraper::ScrapeEntityType::Platform;
  job.entity.identity = collectionJob ? uuid : QStringLiteral("4");
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

// Kartend-5b5r1 (CPU-only slice): a rescrape whose only usable Wikidata data
// is the P880 CPU is a metadata-only success the coordinator persists with
// an empty art path. Against a collection whose entity_metadata row is
// already populated it must update the CPU and keep the description, art
// link and other specs that this sparse answer does not supply. Real
// DatabaseManager on the sandboxed on-disk SQLite file, reached through the
// application context exactly as production does.
//
// The same path runs for both original job keys: a Collection job (row keyed
// collection/uuid/uuid) and a Platform job whose platform provider finds
// nothing, so the coordinator falls back to Wikidata and writes the row of
// the original platform key (platform/4/uuid).
void TestWikidataPlatformPhoto::cpuOnlyRescrapeKeepsSavedMetadata_data() {
  QTest::addColumn<bool>("collectionJob");
  QTest::newRow("collection job") << true;
  QTest::newRow("platform job via Wikidata fallback") << false;
}

void TestWikidataPlatformPhoto::cpuOnlyRescrapeKeepsSavedMetadata() {
  QFETCH(bool, collectionJob);
  int mediaFetches = 0;
  installFetch(QByteArrayLiteral(R"({
      "P880":[{"mainsnak":{"datavalue":{"value":{"id":"Q900100"}}}}]})"),
               QByteArray(), &mediaFetches, QByteArrayLiteral(R"({"entities":{
        "Q900100":{"labels":{"en":{"value":"Example CPU 32-bit"}}}}})"));

  const QString collectionType = QLatin1String(EntityMetadataStore::kTypeCollection);
  const QString cpuKey = QLatin1String(EntityMetadataStore::kFieldCpu);
  const QString unitsKey = QLatin1String(EntityMetadataStore::kFieldUnitsSold);
  const QString savedArt =
      QDir(m_tmp->path()).filePath(QStringLiteral("_shared/logo-svg/collection_uuid-console.svg"));
  const QString savedDescription = QStringLiteral("home video game console");

  auto session = std::make_unique<SessionManager>();
  ApplicationContext appCtx;
  appCtx.managers.sessionManager = session.get();
  auto db = std::make_unique<DatabaseManager>(&appCtx);
  appCtx.managers.databaseManager = db.get();

  // The row the scrape writes: the original job key (type, job identity,
  // job collection).
  const QString rowType =
      collectionJob ? collectionType : QLatin1String(EntityMetadataStore::kTypePlatform);
  const QString rowIdentity = collectionJob ? kUuid : QStringLiteral("4");
  const QString savedTitle = QStringLiteral("Example Console 32 (catalog title)");
  EntityMetadataStore::EntityMetadata seed;
  seed.entityType = rowType;
  seed.entityIdentity = rowIdentity;
  seed.collectionUuid = kUuid;
  seed.title = savedTitle;
  seed.description = savedDescription;
  seed.artPath = savedArt;
  seed.source = QStringLiteral("wikidata");
  seed.customFields = ItemMetadataStore::serializeCustomFields(
      {{cpuKey, QStringLiteral("Old CPU")}, {unitsKey, QStringLiteral("1000000")}});
  QVERIFY(db->saveEntityMetadata(seed));
  const auto seeded = db->loadEntityMetadata(rowType, rowIdentity, kUuid);
  QVERIFY(seeded.isOk());
  QCOMPARE(seeded.value().title, savedTitle);
  QCOMPARE(seeded.value().description, savedDescription);
  QCOMPARE(seeded.value().artPath, savedArt);

  QList<CollectionConfig> collections = platformCollection(m_platformLogo, m_platformBackground);
  // The catalog title differs from the local collection name the provider
  // synthesizes as item.title; only the CPU is new, so the title must stay.
  QVERIFY(collections[0].name != savedTitle);
  runCpuOnlyJob(collections, appCtx, collectionJob, kUuid);
  if (QTest::currentTestFailed()) return;
  QCOMPARE(mediaFetches, 0);

  // Reload the exact persisted row from SQLite.
  const auto reloaded = db->loadEntityMetadata(rowType, rowIdentity, kUuid);
  QVERIFY(reloaded.isOk());
  const EntityMetadataStore::EntityMetadata &row = reloaded.value();
  const ItemMetadataStore::CustomFieldList fields =
      ItemMetadataStore::parseCustomFields(row.customFields);
  const auto field = [&fields](const QString &key) {
    for (const auto &pair : fields) {
      if (pair.first == key) return pair.second;
    }
    return QString();
  };
  QCOMPARE(field(cpuKey), QStringLiteral("Example CPU 32-bit"));
  QCOMPARE(row.description, savedDescription);
  QCOMPARE(row.artPath, savedArt);
  QCOMPARE(field(unitsKey), QStringLiteral("1000000"));
  QCOMPARE(fields.size(), 2);
  QCOMPARE(row.title, savedTitle);
  QCOMPARE(row.source, QStringLiteral("wikidata"));
  db.reset();
}

// A platform row is the only saved metadata for the collection, and the
// CPU-only Wikidata answer lands on a new collection-keyed row. The summary
// data path (DetailsPaneManager::buildCollectionSummary reads
// loadEntityMetadataForCollection) must still show the platform row's
// manufacturer and release date, and the platform row must stay untouched.
void TestWikidataPlatformPhoto::cpuOnlyCollectionJobKeepsPlatformSummary() {
  int mediaFetches = 0;
  installFetch(QByteArrayLiteral(R"({
      "P880":[{"mainsnak":{"datavalue":{"value":{"id":"Q900100"}}}}]})"),
               QByteArray(), &mediaFetches, QByteArrayLiteral(R"({"entities":{
        "Q900100":{"labels":{"en":{"value":"Example CPU 32-bit"}}}}})"));
  const QString collectionType = QLatin1String(EntityMetadataStore::kTypeCollection);
  const QString platformType = QLatin1String(EntityMetadataStore::kTypePlatform);
  const QString cpuKey = QLatin1String(EntityMetadataStore::kFieldCpu);
  const QString manufacturerKey = QLatin1String(EntityMetadataStore::kFieldManufacturer);
  const QString releaseKey = QLatin1String(EntityMetadataStore::kFieldReleaseDate);
  // The sandboxed media.db outlives one run: a per-run collection uuid keeps
  // rows of earlier runs and cases out of this collection's selection.
  const QString uuid = QStringLiteral("uuid-console-summary-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

  auto session = std::make_unique<SessionManager>();
  ApplicationContext appCtx;
  appCtx.managers.sessionManager = session.get();
  auto db = std::make_unique<DatabaseManager>(&appCtx);
  appCtx.managers.databaseManager = db.get();

  EntityMetadataStore::EntityMetadata platformSeed;
  platformSeed.entityType = platformType;
  platformSeed.entityIdentity = QStringLiteral("4");
  platformSeed.collectionUuid = uuid;
  platformSeed.title = QStringLiteral("Example Console (catalog)");
  platformSeed.description = QStringLiteral("A home console.");
  platformSeed.source = QStringLiteral("screenscraper");
  platformSeed.customFields = ItemMetadataStore::serializeCustomFields(
      {{manufacturerKey, QStringLiteral("Example Corp")}, {releaseKey, QStringLiteral("1994")}});
  QVERIFY(db->saveEntityMetadata(platformSeed));
  const auto platformBefore = db->loadEntityMetadata(platformType, QStringLiteral("4"), uuid);
  QVERIFY(platformBefore.isOk());
  QVERIFY(!platformBefore.value().isEmpty());
  const auto noCollectionRow = db->loadEntityMetadata(collectionType, uuid, uuid);
  QVERIFY(noCollectionRow.isOk());
  QVERIFY(noCollectionRow.value().isEmpty());

  const auto summaryField = [&db, &uuid](const QString &key) {
    const auto entity = db->loadEntityMetadataForCollection(uuid);
    for (const auto &pair : ItemMetadataStore::parseCustomFields(entity.customFields)) {
      if (pair.first == key) return pair.second;
    }
    return QString();
  };
  QCOMPARE(summaryField(manufacturerKey), QStringLiteral("Example Corp"));
  QCOMPARE(summaryField(releaseKey), QStringLiteral("1994"));

  QList<CollectionConfig> collections = platformCollection(m_platformLogo, m_platformBackground);
  runCpuOnlyJob(collections, appCtx, /*collectionJob=*/true, uuid);
  if (QTest::currentTestFailed()) return;
  QCOMPARE(mediaFetches, 0);

  // The CPU is persisted under the collection key...
  const auto collectionRow = db->loadEntityMetadata(collectionType, uuid, uuid);
  QVERIFY(collectionRow.isOk());
  const auto cpuFields = ItemMetadataStore::parseCustomFields(collectionRow.value().customFields);
  QCOMPARE(cpuFields.size(), 1);
  QCOMPARE(cpuFields.first().first, cpuKey);
  QCOMPARE(cpuFields.first().second, QStringLiteral("Example CPU 32-bit"));
  // ...the platform row is exactly as seeded...
  const auto platformAfter = db->loadEntityMetadata(platformType, QStringLiteral("4"), uuid);
  QVERIFY(platformAfter.isOk());
  QCOMPARE(platformAfter.value().title, platformBefore.value().title);
  QCOMPARE(platformAfter.value().description, platformBefore.value().description);
  QCOMPARE(platformAfter.value().artPath, platformBefore.value().artPath);
  QCOMPARE(platformAfter.value().customFields, platformBefore.value().customFields);
  QCOMPARE(platformAfter.value().source, platformBefore.value().source);
  QCOMPARE(platformAfter.value().updatedAt, platformBefore.value().updatedAt);
  // ...and the summary still sees the platform row's details.
  QCOMPARE(summaryField(manufacturerKey), QStringLiteral("Example Corp"));
  QCOMPARE(summaryField(releaseKey), QStringLiteral("1994"));
  QCOMPARE(db->loadEntityMetadataForCollection(uuid).description,
           QStringLiteral("A home console."));
  db.reset();
}

// Saved custom fields the store cannot parse are not replaced by a CPU-only
// write: the row stays exactly as it was.
void TestWikidataPlatformPhoto::cpuOnlyRescrapeLeavesUnreadableRowAlone() {
  int mediaFetches = 0;
  installFetch(QByteArrayLiteral(R"({
      "P880":[{"mainsnak":{"datavalue":{"value":{"id":"Q900100"}}}}]})"),
               QByteArray(), &mediaFetches, QByteArrayLiteral(R"({"entities":{
        "Q900100":{"labels":{"en":{"value":"Example CPU 32-bit"}}}}})"));
  const QString collectionType = QLatin1String(EntityMetadataStore::kTypeCollection);
  const QString uuid = QStringLiteral("uuid-console-unreadable-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  auto session = std::make_unique<SessionManager>();
  ApplicationContext appCtx;
  appCtx.managers.sessionManager = session.get();
  auto db = std::make_unique<DatabaseManager>(&appCtx);
  appCtx.managers.databaseManager = db.get();

  EntityMetadataStore::EntityMetadata seed;
  seed.entityType = collectionType;
  seed.entityIdentity = uuid;
  seed.collectionUuid = uuid;
  seed.title = QStringLiteral("Example Console 32 (catalog title)");
  seed.description = QStringLiteral("home video game console");
  seed.customFields = QStringLiteral("not json {");
  QVERIFY(db->saveEntityMetadata(seed));
  const auto before = db->loadEntityMetadata(collectionType, uuid, uuid);
  QVERIFY(before.isOk());
  QCOMPARE(before.value().customFields, seed.customFields);

  QList<CollectionConfig> collections = platformCollection(m_platformLogo, m_platformBackground);
  runCpuOnlyJob(collections, appCtx, /*collectionJob=*/true, uuid);
  if (QTest::currentTestFailed()) return;

  const auto after = db->loadEntityMetadata(collectionType, uuid, uuid);
  QVERIFY(after.isOk());
  QCOMPARE(after.value().customFields, seed.customFields);
  QCOMPARE(after.value().title, seed.title);
  QCOMPARE(after.value().description, seed.description);
  QCOMPARE(after.value().updatedAt, before.value().updatedAt);
  db.reset();
}

// An ordinary collection row (more than a CPU) keeps its precedence over the
// platform row in the summary selection.
void TestWikidataPlatformPhoto::populatedCollectionRowKeepsSummaryPrecedence() {
  const QString collectionType = QLatin1String(EntityMetadataStore::kTypeCollection);
  const QString platformType = QLatin1String(EntityMetadataStore::kTypePlatform);
  const QString cpuKey = QLatin1String(EntityMetadataStore::kFieldCpu);
  const QString uuid = QStringLiteral("uuid-console-precedence-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  auto session = std::make_unique<SessionManager>();
  ApplicationContext appCtx;
  appCtx.managers.sessionManager = session.get();
  auto db = std::make_unique<DatabaseManager>(&appCtx);
  appCtx.managers.databaseManager = db.get();

  EntityMetadataStore::EntityMetadata platformRow;
  platformRow.entityType = platformType;
  platformRow.entityIdentity = QStringLiteral("4");
  platformRow.collectionUuid = uuid;
  platformRow.description = QStringLiteral("Console boilerplate.");
  QVERIFY(db->saveEntityMetadata(platformRow));
  EntityMetadataStore::EntityMetadata collectionRow;
  collectionRow.entityType = collectionType;
  collectionRow.entityIdentity = uuid;
  collectionRow.collectionUuid = uuid;
  collectionRow.description = QStringLiteral("Collection description.");
  collectionRow.customFields =
      ItemMetadataStore::serializeCustomFields({{cpuKey, QStringLiteral("Example CPU")}});
  QVERIFY(db->saveEntityMetadata(collectionRow));
  const auto selected = db->loadEntityMetadataForCollection(uuid);
  QCOMPARE(selected.entityType, collectionType);
  QCOMPARE(selected.description, QStringLiteral("Collection description."));
  db.reset();
}

// The sparse write needs no prior row: the first CPU-only scrape of a
// collection (an identity nothing was saved for) still saves its CPU.
void TestWikidataPlatformPhoto::cpuOnlyFirstScrapeSavesCpu() {
  int mediaFetches = 0;
  installFetch(QByteArrayLiteral(R"({
      "P880":[{"mainsnak":{"datavalue":{"value":{"id":"Q900100"}}}}]})"),
               QByteArray(), &mediaFetches, QByteArrayLiteral(R"({"entities":{
        "Q900100":{"labels":{"en":{"value":"Example CPU 32-bit"}}}}})"));
  const QString collectionType = QLatin1String(EntityMetadataStore::kTypeCollection);
  // The sandboxed media.db outlives one run, so the never-saved identity is
  // made unique per run rather than assumed absent.
  const QString fresh = QStringLiteral("uuid-console-fresh-%1")
                            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

  auto session = std::make_unique<SessionManager>();
  ApplicationContext appCtx;
  appCtx.managers.sessionManager = session.get();
  auto db = std::make_unique<DatabaseManager>(&appCtx);
  appCtx.managers.databaseManager = db.get();
  const auto before = db->loadEntityMetadata(collectionType, fresh, fresh);
  QVERIFY(before.isOk());
  QVERIFY(before.value().isEmpty());

  QList<CollectionConfig> collections = platformCollection(m_platformLogo, m_platformBackground);
  {
    ScraperService service;
    ScraperService::Context ctx;
    ctx.ctx = &appCtx;
    ctx.providerBuilder = [&collections](int) -> std::shared_ptr<MetadataLookupProvider> {
      return std::make_shared<WikidataLogoProvider>(
          [&collections]() -> const CollectionConfig * { return &collections[0]; });
    };
    ctx.collections = &collections;
    service.setContext(ctx);
    ScraperService::CollectionJob job;
    job.collectionIndex = 0;
    job.collectionName = collections[0].name;
    job.collectionUuid = fresh;
    job.artworkDir = m_tmp->path();
    job.entity.type = Scraper::ScrapeEntityType::Collection;
    job.entity.identity = fresh;
    service.startScrape({job}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                        /*writeMetadata=*/true);
    QTRY_COMPARE(service.summary().scraped, 1);
    QCOMPARE(service.summary().notFound, 0);
    QCOMPARE(service.summary().errors, 0);
  }

  const auto reloaded = db->loadEntityMetadata(collectionType, fresh, fresh);
  QVERIFY(reloaded.isOk());
  const auto fields = ItemMetadataStore::parseCustomFields(reloaded.value().customFields);
  QCOMPARE(fields.size(), 1);
  QCOMPARE(fields.first().first, QLatin1String(EntityMetadataStore::kFieldCpu));
  QCOMPARE(fields.first().second, QStringLiteral("Example CPU 32-bit"));
  QVERIFY(reloaded.value().description.isEmpty());
  QVERIFY(reloaded.value().artPath.isEmpty());
  db.reset();
}

QTEST_MAIN(TestWikidataPlatformPhoto)
#include "test_wikidataplatformphoto.moc"
