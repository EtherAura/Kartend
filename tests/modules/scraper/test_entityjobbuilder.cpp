// Kartend-ud6q2: the entity job queue is now built by a free function shared
// between the scraper dialog and the silent creation-time fetch. These cases
// pin the rules that used to live inside a private dialog method, where the
// only way to observe them was to run a scrape.

#include "collection/collectionconfig.h"
#include "entityjobbuilder.h"
#include "metadatalookupprovider.h"
#include "scrapertypes.h"
#include "stubmetadataprovider.h"

#include <memory>
#include <utility>

#include <QList>
#include <QObject>
#include <QString>
#include <QTest>

namespace {

/// Declares whatever entity types the case under test needs. Only
/// supportedEntities() is consulted by the builder, so everything else comes
/// from the shared stub — no lookup or fetch is ever issued here.
class FakeEntityProvider : public KartendTest::StubMetadataProvider {
public:
  explicit FakeEntityProvider(QList<Scraper::ScrapeEntityType> entities)
      : m_entities(std::move(entities)) {}

  [[nodiscard]] QList<Scraper::ScrapeEntityType> supportedEntities() const override {
    return m_entities;
  }

private:
  QList<Scraper::ScrapeEntityType> m_entities;
};

Scraper::EntityProviderBuilder builderYielding(QList<Scraper::ScrapeEntityType> entities) {
  return [entities](int) -> std::shared_ptr<MetadataLookupProvider> {
    return std::make_shared<FakeEntityProvider>(entities);
  };
}

Scraper::EntityProviderBuilder builderYieldingNothing() {
  return [](int) -> std::shared_ptr<MetadataLookupProvider> { return nullptr; };
}

bool hasJobOfType(const QList<Scraper::ScraperService::CollectionJob> &jobs,
                  Scraper::ScrapeEntityType type) {
  for (const auto &job : jobs) {
    if (job.entity.type == type) return true;
  }
  return false;
}

CollectionConfig namedCollection(const QString &name) {
  CollectionConfig cfg;
  cfg.name = name;
  cfg.type = QStringLiteral("video");
  return cfg;
}

} // namespace

class TestEntityJobBuilder : public QObject {
  Q_OBJECT

private slots:
  void platformProviderAlsoGetsACollectionJob();
  void gameEntityIsNeverQueued();
  void platformIdentityIsEmptyWhileCollectionCarriesTheUuid();
  void playlistProducesNoJobs();
  void collectionWithoutAProviderProducesNoJobs();
  void outOfRangeIndexProducesNoJobs();
  void jobsCarryTheCallerResolvedUuidAndArtworkDir();
};

void TestEntityJobBuilder::platformProviderAlsoGetsACollectionJob() {
  // Kartend-445su: a Platform-only provider (ScreenScraper) must still yield a
  // Collection job so the coordinator's capability routing can dispatch it to
  // the Wikidata fallback. This is the rule the background path most depends
  // on — a launcher-imported collection has no platform to resolve.
  QList<CollectionConfig> collections{namedCollection(QStringLiteral("SNES"))};
  const auto jobs =
      Scraper::buildEntityJobs(collections, 0, QStringLiteral("uuid-snes"), QStringLiteral("/art"),
                               builderYielding({Scraper::ScrapeEntityType::Platform}));

  QCOMPARE(jobs.size(), 2);
  QVERIFY(hasJobOfType(jobs, Scraper::ScrapeEntityType::Platform));
  QVERIFY(hasJobOfType(jobs, Scraper::ScrapeEntityType::Collection));
}

void TestEntityJobBuilder::gameEntityIsNeverQueued() {
  // Game is the per-item path; an entity queue that included it would scrape
  // the whole library behind the user's back on every collection creation.
  QList<CollectionConfig> collections{namedCollection(QStringLiteral("Films"))};
  const auto jobs = Scraper::buildEntityJobs(
      collections, 0, QStringLiteral("uuid-films"), QStringLiteral("/art"),
      builderYielding({Scraper::ScrapeEntityType::Game, Scraper::ScrapeEntityType::Collection}));

  QCOMPARE(jobs.size(), 1);
  QCOMPARE(jobs.first().entity.type, Scraper::ScrapeEntityType::Collection);
  QVERIFY(!hasJobOfType(jobs, Scraper::ScrapeEntityType::Game));
}

void TestEntityJobBuilder::platformIdentityIsEmptyWhileCollectionCarriesTheUuid() {
  // Kartend-ckepd.1: Platform resolves its systemeid from an empty identity
  // (override → autodetect); Collection/Category are addressed by uuid.
  QList<CollectionConfig> collections{namedCollection(QStringLiteral("SNES"))};
  const auto jobs =
      Scraper::buildEntityJobs(collections, 0, QStringLiteral("uuid-snes"), QStringLiteral("/art"),
                               builderYielding({Scraper::ScrapeEntityType::Platform}));

  for (const auto &job : jobs) {
    if (job.entity.type == Scraper::ScrapeEntityType::Platform) {
      QVERIFY2(job.entity.identity.isEmpty(), "Platform identity must stay empty for autodetect");
    } else {
      QCOMPARE(job.entity.identity, QStringLiteral("uuid-snes"));
    }
  }
}

void TestEntityJobBuilder::playlistProducesNoJobs() {
  // A playlist spans whatever its rules match, so it has no single platform —
  // an entity job could only ever land in the not-found bucket. It matters for
  // the creation-time path specifically: resyncPlaylistCollections re-appends
  // every playlist row on each resync.
  CollectionConfig playlist = namedCollection(QStringLiteral("Favorites"));
  playlist.isPlaylist = true;
  QList<CollectionConfig> collections{playlist};

  const auto jobs =
      Scraper::buildEntityJobs(collections, 0, QStringLiteral("uuid-fav"), QStringLiteral("/art"),
                               builderYielding({Scraper::ScrapeEntityType::Platform}));
  QVERIFY(jobs.isEmpty());
}

void TestEntityJobBuilder::collectionWithoutAProviderProducesNoJobs() {
  QList<CollectionConfig> collections{namedCollection(QStringLiteral("Unscraped"))};
  QVERIFY(Scraper::buildEntityJobs(collections, 0, QStringLiteral("uuid-x"), QStringLiteral("/art"),
                                   builderYieldingNothing())
              .isEmpty());
  // An absent builder is inert too — the closures are wired before the app's
  // state is guaranteed to exist.
  QVERIFY(
      Scraper::buildEntityJobs(collections, 0, QStringLiteral("uuid-x"), QStringLiteral("/art"), {})
          .isEmpty());
}

void TestEntityJobBuilder::outOfRangeIndexProducesNoJobs() {
  QList<CollectionConfig> collections{namedCollection(QStringLiteral("Films"))};
  const auto provider = builderYielding({Scraper::ScrapeEntityType::Collection});
  QVERIFY(Scraper::buildEntityJobs(collections, -1, QStringLiteral("u"), QStringLiteral("/art"),
                                   provider)
              .isEmpty());
  QVERIFY(Scraper::buildEntityJobs(collections, 1, QStringLiteral("u"), QStringLiteral("/art"),
                                   provider)
              .isEmpty());
  QVERIFY(Scraper::buildEntityJobs({}, 0, QStringLiteral("u"), QStringLiteral("/art"), provider)
              .isEmpty());
}

void TestEntityJobBuilder::jobsCarryTheCallerResolvedUuidAndArtworkDir() {
  // The service's persistence layer keys jobs by uuid + artwork dir so a
  // resume survives a config reorder — every job must carry both, plus the
  // index and name the progress label reads.
  QList<CollectionConfig> collections{namedCollection(QStringLiteral("Films")),
                                      namedCollection(QStringLiteral("Docs"))};
  const auto jobs = Scraper::buildEntityJobs(
      collections, 1, QStringLiteral("uuid-docs"), QStringLiteral("/art/docs"),
      builderYielding({Scraper::ScrapeEntityType::Platform}));

  QVERIFY(!jobs.isEmpty());
  for (const auto &job : jobs) {
    QCOMPARE(job.collectionIndex, 1);
    QCOMPARE(job.collectionUuid, QStringLiteral("uuid-docs"));
    QCOMPARE(job.collectionName, QStringLiteral("Docs"));
    QCOMPARE(job.artworkDir, QStringLiteral("/art/docs"));
    QCOMPARE(job.entity.collectionIndex, 1);
  }
}

QTEST_MAIN(TestEntityJobBuilder)
#include "test_entityjobbuilder.moc"
