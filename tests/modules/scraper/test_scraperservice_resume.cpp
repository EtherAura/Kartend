// Regression test for ScraperService resume: the persisted run
// summary must round-trip the media-written count. Dropping it made a
// resumed scrape come back showing "0 media" even though earlier media
// had already been downloaded.

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>
#include <optional>

#include "../../support/parkedcallbacksink.h"
#include "../../support/testsandbox.h"
#include "collection/generalsettings.h"
#include "collection/typehelpers.h"
#include "metadatalookupprovider.h"
#include "scrapependingstate.h"
#include "scraperservice.h"

using Scraper::ScraperService;

/// A lookup provider that captures its lookup() callback but never
/// invokes it on its own — so an interactive scrape parks at its very
/// first item. That is the startup window the persisted-snapshot fix
/// has to cover (a crash here used to leave no pending-scrape.json).
/// A test can also fire `lastLookupCallback` by hand to simulate a
/// network result arriving late (e.g. after the scrape was cancelled).
class ParkedProvider : public MetadataLookupProvider {
public:
  mutable LookupCallback lastLookupCallback;
  QString id() const override { return QStringLiteral("parked"); }
  QString displayName() const override { return QStringLiteral("Parked"); }
  QStringList categories() const override { return {QStringLiteral("games")}; }
  Capabilities capabilities() const override { return Capability::MetadataLookup; }
  QUrl searchUrl(const QString &) const override { return {}; }
  void lookup(const QString &, LookupCallback cb) override { lastLookupCallback = std::move(cb); }
  void fetchDetail(const Scraper::ScrapeCandidate &, DetailCallback) override {}
  void fetchMediaBytes(const QUrl &, MediaCallback) override {}
};

/// Provider that answers fetchEntity() synchronously with a canned result —
/// drives the entity-scrape queue path end to end (Kartend-ckepd.2).
class EntityStubProvider : public MetadataLookupProvider {
public:
  ErrorUtils::Result<Scraper::ScrapedItem> entityResult{Scraper::ScrapedItem{}}; // default success
  Scraper::EntityScrapeTarget lastTarget;
  int fetchEntityCalls = 0;

  QString id() const override { return QStringLiteral("entitystub"); }
  QString displayName() const override { return QStringLiteral("EntityStub"); }
  QStringList categories() const override { return {}; }
  Capabilities capabilities() const override { return Capability::MetadataLookup; }
  // Collection included since Kartend-445su: the coordinator's capability
  // routing swaps in the real Wikidata data provider for Collection jobs a
  // provider does not advertise — a stub that under-advertises would have
  // its stubbed fetchEntity bypassed in favour of live network code.
  QList<Scraper::ScrapeEntityType> supportedEntities() const override {
    return {Scraper::ScrapeEntityType::Game, Scraper::ScrapeEntityType::Platform,
            Scraper::ScrapeEntityType::Collection};
  }
  /// Canned media bytes per URL (Kartend-ckepd.3 download pipeline test).
  QHash<QUrl, QByteArray> mediaBytes;
  /// When set, every fetchMediaBytes call fails with this error instead of
  /// answering from mediaBytes (Kartend-xzqel failure-path tests).
  std::optional<ErrorUtils::ErrorContext> mediaError;
  /// When set, fetchMediaBytes parks its callback into this fixture-owned sink
  /// instead of firing — lets a test fire it by hand to drive the in-flight /
  /// pause window without forming a provider<->callback cycle (see
  /// ParkedCallbackSink). Enable via parkMediaInto(); the two are coupled so a
  /// park-mode test cannot forget to provide the sink.
  KartendTest::ParkedCallbackSink *mediaSink = nullptr;
  void parkMediaInto(KartendTest::ParkedCallbackSink &sink) { mediaSink = &sink; }
  void lookup(const QString &, LookupCallback cb) override {
    cb(QList<Scraper::ScrapeCandidate>{});
  }
  void fetchDetail(const Scraper::ScrapeCandidate &, DetailCallback cb) override {
    cb(Scraper::ScrapedItem{});
  }
  void fetchMediaBytes(const QUrl &url, MediaCallback cb) override {
    if (mediaSink) {
      mediaSink->media = std::move(cb);
      return;
    }
    if (mediaError.has_value()) {
      cb(*mediaError);
      return;
    }
    cb(mediaBytes.value(url));
  }
  void fetchEntity(const Scraper::EntityScrapeTarget &target, DetailCallback cb) override {
    ++fetchEntityCalls;
    lastTarget = target;
    cb(entityResult);
  }
};

/// Parks its fetchEntity() callback (never fires it itself) so a test can drive
/// the in-flight / pause / cancel windows of the entity path by hand — the
/// realistic async-provider case (Kartend-ckepd.2 review regressions).
class ParkedEntityProvider : public MetadataLookupProvider {
public:
  /// Always parks fetchEntity() into this fixture-owned sink; the sink is a
  /// required ctor argument so the provider<->callback cycle cannot form (see
  /// ParkedCallbackSink) and cannot be forgotten (it is a compile error to omit).
  explicit ParkedEntityProvider(KartendTest::ParkedCallbackSink &sink) : entitySink(sink) {}
  KartendTest::ParkedCallbackSink &entitySink;
  int fetchEntityCalls = 0;
  QString id() const override { return QStringLiteral("parkedentity"); }
  QString displayName() const override { return QStringLiteral("ParkedEntity"); }
  QStringList categories() const override { return {}; }
  Capabilities capabilities() const override { return Capability::MetadataLookup; }
  // Collection included since Kartend-445su: the coordinator's capability
  // routing swaps in the real Wikidata data provider for Collection jobs a
  // provider does not advertise — a stub that under-advertises would have
  // its stubbed fetchEntity bypassed in favour of live network code.
  QList<Scraper::ScrapeEntityType> supportedEntities() const override {
    return {Scraper::ScrapeEntityType::Game, Scraper::ScrapeEntityType::Platform,
            Scraper::ScrapeEntityType::Collection};
  }
  void lookup(const QString &, LookupCallback) override {}
  void fetchDetail(const Scraper::ScrapeCandidate &, DetailCallback) override {}
  void fetchMediaBytes(const QUrl &, MediaCallback) override {}
  void fetchEntity(const Scraper::EntityScrapeTarget &, DetailCallback cb) override {
    ++fetchEntityCalls;
    entitySink.entity = std::move(cb); // park it; a test fires it by hand
  }
};

class TestScraperServiceResume : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void init();
  void cleanup();
  void loadRestoresMediaWrittenCount();
  void loadDefaultsMediaWrittenToZeroForLegacyFile();
  void loadSkipsResumeWhenOwnedByLiveInstance();
  void startScrapePersistsInitialSnapshot();
  void startScrapeWhileActiveRefusesAndKeepsRunState();
  void interactivePreFilterDropsCoveredItems();
  void interactivePreFilterRespectsModeGate();
  void cancelledInteractiveScrapeIgnoresLateLookupResult();
  void entityScrapeProcessesViaQueue();
  void entityScrapeNotFoundCountedSeparately();
  void entityScrapeDownloadsArtToSharedAndConfig();
  void entityScrapeCollectionArtToSharedAndConfig();
  void entityScrapeCollectionLogoDoesNotStealPlatformWiredIcon();
  void entityScrapeFillMissingWiresExistingArtIntoConfig();
  void entityJobLoadsFromPendingState();
  void interactiveEntityJobResumedNotSkipped();
  void staleEntityCallbackAfterRestartIgnored();
  void pausedEntityMediaCallbackAfterResumeIgnored();
  void entityFetchQuotaStopsQueueWithResumePoint();
  void interactiveQuotaErrorStopsQueueWithResumePoint();
  void interactiveConsecutive429sEscalateToQuotaStop();
  void interactive429StreakResetByDeliveredResult();
  void entityConsecutive429sEscalateToQuotaStop();
  void entityMedia429sEscalateToQuotaStop();
  void entityMediaQuotaKeepsJobQueuedForResume();
  void entityAllMediaFetchesFailedCountsError();
  void resumeReresolvesCollectionIndexByUuid();
  void resumeDropsJobWhoseUuidNoLongerResolves();
  void failedItemsRoundTripThroughPendingState();
  void failedItemsAggregateAcrossCollections();
  void persistWritesFailedItemsMidRun();
  void entityFailureRecordsEntityDiscriminator();
  void entityFailedItemRoundTripsDiscriminator();
  void entityCatalogUnavailableBucketedAsError();
  void entityNotFoundBucketedNotError();

private:
  static void writePendingFile(const QByteArray &json);
  // Mirrors ScraperService::pendingStateFilePath() (which is private).
  // If the production path ever moves, loadPendingState() will read
  // nothing and the isValid() assertions below fail loudly.
  static QString pendingFilePath();
  // Mirrors ScraperService::pendingStateLockFilePath().
  static QString pendingLockFilePath();
  // A small valid pending-scrape snapshot reused by several tests.
  static QByteArray validPendingJson();

  // Fixture-owned home for any callback a stub parks this test (see
  // ParkedCallbackSink). Drained in cleanup() so the parked closure's captured
  // provider shared_ptr is released — no per-test clear, no LSan cycle.
  KartendTest::ParkedCallbackSink m_parkedCallbacks;
};

QString TestScraperServiceResume::pendingFilePath() {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  return QDir(dir).filePath(QStringLiteral("pending-scrape.json"));
}

QString TestScraperServiceResume::pendingLockFilePath() {
  return pendingFilePath() + QStringLiteral(".lock");
}

QByteArray TestScraperServiceResume::validPendingJson() {
  return R"json({
    "version": 1,
    "started_at_unix_ms": 1715000000000,
    "mode": "auto",
    "write_metadata": true,
    "summary_so_far": { "scraped": 1, "skipped": 0, "errors": 0 },
    "queue": [
      {
        "collection_index": 0,
        "collection_uuid": "uuid-1",
        "collection_name": "Coll",
        "artwork_dir": "/art",
        "remaining": ["/m/a.bin"]
      }
    ]
  })json";
}

void TestScraperServiceResume::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-scraperservice-resume"));
}

void TestScraperServiceResume::writePendingFile(const QByteArray &json) {
  const QString path = pendingFilePath();
  QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
  f.write(json);
  f.close();
}

void TestScraperServiceResume::init() {
  // Each test starts from a clean slate so a stale file can't leak in.
  QFile::remove(pendingFilePath());
  QFile::remove(pendingLockFilePath());
}

void TestScraperServiceResume::cleanup() {
  m_parkedCallbacks.clear();
  QFile::remove(pendingFilePath());
  QFile::remove(pendingLockFilePath());
}

void TestScraperServiceResume::loadRestoresMediaWrittenCount() {
  // A pending-scrape snapshot with progress already made: 12 items
  // scraped and 27 media files written before the interruption.
  const QByteArray json = R"json({
    "version": 1,
    "started_at_unix_ms": 1715000000000,
    "mode": "auto",
    "write_metadata": true,
    "media_filter": ["front", "screenshot"],
    "summary_so_far": {
      "scraped": 12,
      "skipped": 3,
      "errors": 1,
      "not_found": 4,
      "media_written": 27,
      "media_fetch_failures": 6,
      "media_write_failures": 2,
      "first_failures": ["foo.bin: timeout"]
    },
    "queue": [
      {
        "collection_index": 0,
        "collection_uuid": "uuid-1",
        "collection_name": "Coll",
        "artwork_dir": "/art",
        "remaining": ["/m/a.bin", "/m/b.bin"]
      }
    ]
  })json";
  writePendingFile(json);

  ScraperService service;
  const ScraperService::PendingState state = service.loadPendingState(/*consumeOnLoad=*/false);

  QVERIFY(state.isValid());
  // The media-written count must survive the round-trip — this is the
  // value the dialog shows as "N media" when the run resumes.
  QCOMPARE(state.summarySoFar.mediaWritten, 27);
  // Sibling counters were already persisted; assert them too so a
  // future change can't quietly regress the whole summary block.
  QCOMPARE(state.summarySoFar.scraped, 12);
  QCOMPARE(state.summarySoFar.skipped, 3);
  QCOMPARE(state.summarySoFar.errors, 1);
  // notFound must round-trip too (Kartend-e8aag).
  QCOMPARE(state.summarySoFar.notFound, 4);
  // Media failure counters must round-trip too — a resumed run used to come
  // back reporting zero media failures (Kartend-jjyst.16).
  QCOMPARE(state.summarySoFar.mediaFetchFailures, 6);
  QCOMPARE(state.summarySoFar.mediaWriteFailures, 2);
}

void TestScraperServiceResume::loadDefaultsMediaWrittenToZeroForLegacyFile() {
  // A snapshot written before media_written was persisted (older
  // builds) must still load — the missing field defaults to 0 rather
  // than rejecting the whole resume.
  const QByteArray legacyJson = R"json({
    "version": 1,
    "started_at_unix_ms": 1715000000000,
    "mode": "auto",
    "write_metadata": true,
    "summary_so_far": {
      "scraped": 5,
      "skipped": 0,
      "errors": 0
    },
    "queue": [
      {
        "collection_index": 0,
        "collection_uuid": "uuid-1",
        "collection_name": "Coll",
        "artwork_dir": "/art",
        "remaining": ["/m/a.bin"]
      }
    ]
  })json";
  writePendingFile(legacyJson);

  ScraperService service;
  const ScraperService::PendingState state = service.loadPendingState(/*consumeOnLoad=*/false);

  QVERIFY(state.isValid());
  QCOMPARE(state.summarySoFar.mediaWritten, 0);
  QCOMPARE(state.summarySoFar.scraped, 5);
  // Legacy snapshots without media failure counters default to 0 too.
  QCOMPARE(state.summarySoFar.mediaFetchFailures, 0);
  QCOMPARE(state.summarySoFar.mediaWriteFailures, 0);
}

void TestScraperServiceResume::loadSkipsResumeWhenOwnedByLiveInstance() {
  // A second Kartend instance must not resume a scrape that another,
  // still-running instance owns. The running instance is simulated by
  // a QLockFile held at the sibling `.lock` path — its owning PID (us)
  // is alive, so loadPendingState() must treat the snapshot as live.
  writePendingFile(validPendingJson());

  QLockFile liveOwner(pendingLockFilePath());
  liveOwner.setStaleLockTime(0);
  QVERIFY(liveOwner.tryLock(0));

  ScraperService service;
  const ScraperService::PendingState owned = service.loadPendingState(/*consumeOnLoad=*/false);
  QVERIFY(!owned.isValid()); // resume must NOT be offered
  // The owning instance's state file must be left untouched, even
  // though a consume was not requested.
  QVERIFY(QFile::exists(pendingFilePath()));

  // Once the owning instance exits, the lock is released and the
  // snapshot becomes a genuine interrupted scrape again.
  liveOwner.unlock();
  const ScraperService::PendingState orphaned = service.loadPendingState(/*consumeOnLoad=*/false);
  QVERIFY(orphaned.isValid());
}

void TestScraperServiceResume::startScrapePersistsInitialSnapshot() {
  // A scrape that has begun but not yet completed (or even started) a
  // single item must already have pending-scrape.json on disk —
  // otherwise a crash in the startup window leaves nothing to resume
  // and the next launch shows no resume prompt. Regression: startScrape
  // called persistState() while m_state was still Idle, so persistState
  // hit its Idle guard and cleared the file instead of writing it.
  QVERIFY(!QFile::exists(pendingFilePath())); // clean slate from init()

  ScraperService service;
  ScraperService::Context ctx;
  // Interactive mode parks on the first item's lookup (ParkedProvider
  // never calls back), so the scrape stays RunningInteractive with the
  // full queue intact — the exact "started, nothing done yet" state.
  ctx.providerBuilder = [](int) -> std::shared_ptr<MetadataLookupProvider> {
    return std::make_shared<ParkedProvider>();
  };
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionIndex = 0;
  job.collectionUuid = QStringLiteral("uuid-1");
  job.collectionName = QStringLiteral("Coll");
  job.artworkDir = QStringLiteral("/art");
  job.items = QStringList{QStringLiteral("/m/a.bin"), QStringLiteral("/m/b.bin")};

  service.startScrape({job}, ScraperService::Mode::Interactive, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  // The snapshot must exist immediately — before any item completes and
  // before the debounced persist timer could ever have fired.
  QVERIFY(QFile::exists(pendingFilePath()));

  // ...and it must be a complete, parseable snapshot carrying the whole
  // queue, not a truncated stub. (loadPendingState() can't be used here
  // — `service` still holds the ownership lock, so it would correctly
  // report the scrape as live and refuse to surface it.)
  QFile f(pendingFilePath());
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
  f.close();
  QVERIFY(doc.isObject());
  QCOMPARE(doc.object().value(QStringLiteral("mode")).toString(), QStringLiteral("interactive"));
  const QJsonArray queue = doc.object().value(QStringLiteral("queue")).toArray();
  QCOMPARE(queue.size(), 1);
  QCOMPARE(queue.first().toObject().value(QStringLiteral("remaining")).toArray().size(), 2);
}

void TestScraperServiceResume::startScrapeWhileActiveRefusesAndKeepsRunState() {
  // Kartend-jjyst.8: a busy service must REPORT the refusal, not just log it.
  // A second startScrape while a run is live returns false and leaves the
  // live run untouched — no scrapeStarted, no queue/counter reset — so a
  // caller (e.g. the entity-scrape launcher) can tell nothing was started
  // instead of presenting a dead dialog.
  ScraperService service;
  ScraperService::Context ctx;
  // Interactive mode parks on the first item's lookup (ParkedProvider never
  // calls back), so the run stays RunningInteractive for the whole test.
  ctx.providerBuilder = [](int) -> std::shared_ptr<MetadataLookupProvider> {
    return std::make_shared<ParkedProvider>();
  };
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionIndex = 0;
  job.collectionUuid = QStringLiteral("uuid-1");
  job.collectionName = QStringLiteral("Coll");
  job.artworkDir = QStringLiteral("/art");
  job.items = QStringList{QStringLiteral("/m/a.bin"), QStringLiteral("/m/b.bin")};

  QSignalSpy startedSpy(&service, &ScraperService::scrapeStarted);
  QVERIFY(service.startScrape({job}, ScraperService::Mode::Interactive, /*mediaFilter=*/{},
                              /*writeMetadata=*/true));
  QCOMPARE(service.state(), ScraperService::State::RunningInteractive);
  QCOMPARE(startedSpy.count(), 1);
  QCOMPARE(service.totalItems(), 2);

  // Second launch while live: refused, and every observable piece of the
  // first run survives — state, mode, total, and no second scrapeStarted.
  ScraperService::CollectionJob other = job;
  other.collectionUuid = QStringLiteral("uuid-2");
  other.collectionName = QStringLiteral("Other");
  other.items = QStringList{QStringLiteral("/m/c.bin")};
  QVERIFY(!service.startScrape({other}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                               /*writeMetadata=*/false));
  QCOMPARE(service.state(), ScraperService::State::RunningInteractive);
  QCOMPARE(service.currentMode(), ScraperService::Mode::Interactive);
  QCOMPARE(service.totalItems(), 2);
  QCOMPARE(startedSpy.count(), 1);
}

void TestScraperServiceResume::interactivePreFilterDropsCoveredItems() {
  // Kartend-resxp: an interactive Skip/FillMissing scrape must drop
  // already-covered items from its queue up front — the Auto runner has
  // this pre-filter inside BatchScrapeRunner::start(), but the interactive
  // picker walked every queued item and prompted for items that needed
  // nothing.
  QTemporaryDir artDir;
  QVERIFY(artDir.isValid());
  // A metadata sidecar marks a.bin as already scraped — the DB-free
  // presence marker shouldSkipScrapedItem probes under Skip mode.
  QVERIFY(QDir(artDir.path()).mkpath(QStringLiteral("metadata")));
  QFile sidecar(QDir(artDir.path()).filePath(QStringLiteral("metadata/a.json")));
  QVERIFY(sidecar.open(QIODevice::WriteOnly));
  sidecar.write("{}");
  sidecar.close();

  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [](int) -> std::shared_ptr<MetadataLookupProvider> {
    return std::make_shared<ParkedProvider>();
  };
  GeneralSettings settings;
  settings.scraper.options.rescrapeMode = ScraperRescrapeMode::Skip;
  ctx.generalSettings = &settings;
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionIndex = 0;
  job.collectionUuid = QStringLiteral("uuid-1");
  job.collectionName = QStringLiteral("Coll");
  job.artworkDir = artDir.path();
  job.items = QStringList{QStringLiteral("/m/a.bin"), QStringLiteral("/m/b.bin")};

  QSignalSpy startedSpy(&service, &ScraperService::scrapeStarted);
  QSignalSpy beganSpy(&service, &ScraperService::itemBegan);
  QVERIFY(service.startScrape({job}, ScraperService::Mode::Interactive, /*mediaFilter=*/{},
                              /*writeMetadata=*/true));

  // a.bin was dropped up front: it counts as skipped-and-settled (total
  // stays 2, progress starts at 1), and the only picker prompt is b.bin.
  QCOMPARE(startedSpy.count(), 1);
  QCOMPARE(startedSpy.first().at(0).toInt(), 2);
  QTRY_COMPARE(beganSpy.count(), 1);
  QCOMPARE(beganSpy.first().at(0).toInt(), 1);
  QCOMPARE(beganSpy.first().at(3).toString(), QStringLiteral("b.bin"));
  QCOMPARE(service.summary().skipped, 1);
}

void TestScraperServiceResume::interactivePreFilterRespectsModeGate() {
  // Overwrite / UpdateChanged intentionally visit every item (Overwrite
  // always re-fetches; UpdateChanged needs the bytes back to compare), so
  // the interactive pre-filter must not drop covered items under them —
  // same gate as BatchScrapeRunner::start().
  QTemporaryDir artDir;
  QVERIFY(artDir.isValid());
  QVERIFY(QDir(artDir.path()).mkpath(QStringLiteral("metadata")));
  QFile sidecar(QDir(artDir.path()).filePath(QStringLiteral("metadata/a.json")));
  QVERIFY(sidecar.open(QIODevice::WriteOnly));
  sidecar.write("{}");
  sidecar.close();

  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [](int) -> std::shared_ptr<MetadataLookupProvider> {
    return std::make_shared<ParkedProvider>();
  };
  GeneralSettings settings;
  settings.scraper.options.rescrapeMode = ScraperRescrapeMode::Overwrite;
  ctx.generalSettings = &settings;
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionIndex = 0;
  job.collectionUuid = QStringLiteral("uuid-1");
  job.collectionName = QStringLiteral("Coll");
  job.artworkDir = artDir.path();
  job.items = QStringList{QStringLiteral("/m/a.bin"), QStringLiteral("/m/b.bin")};

  QSignalSpy beganSpy(&service, &ScraperService::itemBegan);
  QVERIFY(service.startScrape({job}, ScraperService::Mode::Interactive, /*mediaFilter=*/{},
                              /*writeMetadata=*/true));

  // Nothing dropped: the covered a.bin is still the first picker prompt
  // and the skipped tally stays clean.
  QTRY_COMPARE(beganSpy.count(), 1);
  QCOMPARE(beganSpy.first().at(0).toInt(), 0);
  QCOMPARE(beganSpy.first().at(3).toString(), QStringLiteral("a.bin"));
  QCOMPARE(service.summary().skipped, 0);
  QCOMPARE(service.totalItems(), 2);
}

void TestScraperServiceResume::cancelledInteractiveScrapeIgnoresLateLookupResult() {
  // After an interactive scrape is cancelled, a lookup callback that
  // was already in flight must not advance — let alone restart — the
  // queue. Regression: interactiveLookupComplete only bailed for the
  // Paused state, so a result landing after cancel re-entered pump()
  // and resurrected the cancelled run.
  auto provider = std::make_shared<ParkedProvider>();
  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionIndex = 0;
  job.collectionUuid = QStringLiteral("uuid-1");
  job.collectionName = QStringLiteral("Coll");
  job.artworkDir = QStringLiteral("/art");
  job.items = QStringList{QStringLiteral("/m/a.bin"), QStringLiteral("/m/b.bin")};
  service.startScrape({job}, ScraperService::Mode::Interactive, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  // The first item's lookup is in flight — its callback was captured.
  QVERIFY(static_cast<bool>(provider->lastLookupCallback));
  QCOMPARE(service.state(), ScraperService::State::RunningInteractive);

  service.cancel();
  QCOMPARE(service.state(), ScraperService::State::Idle);

  // Fire the now-stale lookup result. The cancelled service must
  // ignore it and stay idle — not pick up the next queued item.
  provider->lastLookupCallback(QList<Scraper::ScrapeCandidate>{});
  QCOMPARE(service.state(), ScraperService::State::Idle);
}

void TestScraperServiceResume::entityScrapeProcessesViaQueue() {
  // Kartend-ckepd.2: an entity job (no item list) is dispatched via one
  // fetchEntity() and counted in the shared summary — proving it isn't skipped
  // by the empty-items guard and flows through the same queue/finish path.
  auto provider = std::make_shared<EntityStubProvider>();
  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionIndex = 2;
  job.collectionName = QStringLiteral("SNES");
  job.entity.type = Scraper::ScrapeEntityType::Platform;
  job.entity.identity = QStringLiteral("4"); // a systemeid
  job.entity.collectionIndex = 2;
  QVERIFY(job.isEntityJob());

  QSignalSpy finishedSpy(&service, &ScraperService::scrapeFinished);
  service.startScrape({job}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  // The synchronous stub processes the entity during startScrape.
  QCOMPARE(provider->fetchEntityCalls, 1);
  QCOMPARE(provider->lastTarget.type, Scraper::ScrapeEntityType::Platform);
  QCOMPARE(provider->lastTarget.identity, QStringLiteral("4"));
  QCOMPARE(service.state(), ScraperService::State::Idle);
  QCOMPARE(service.summary().scraped, 1);
  QCOMPARE(service.summary().errors, 0);
  QCOMPARE(finishedSpy.count(), 1);
}

void TestScraperServiceResume::entityScrapeNotFoundCountedSeparately() {
  // A not-found entity (niche platform absent from the catalog) lands in
  // notFound, not errors (Kartend-e8aag carried into the entity path).
  auto provider = std::make_shared<EntityStubProvider>();
  provider->entityResult = ErrorUtils::ErrorContext::error(
      ErrorUtils::ErrorCode::RemoteResourceNotFound, QStringLiteral("no such platform"));
  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionName = QStringLiteral("Obscure");
  job.entity.type = Scraper::ScrapeEntityType::Platform;
  job.entity.identity = QStringLiteral("999");
  service.startScrape({job}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  QCOMPARE(service.summary().notFound, 1);
  QCOMPARE(service.summary().errors, 0);
  QCOMPARE(service.summary().scraped, 0);
}

void TestScraperServiceResume::entityScrapeDownloadsArtToSharedAndConfig() {
  // Kartend-ckepd.3: a platform scrape downloads its art, writes it to the
  // collection's _shared/<type>/platform_<id>.<ext> path, and wires the path
  // into the collection's config art fields.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());

  auto provider = std::make_shared<EntityStubProvider>();
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("SNES");
  // Providers declare each asset's config slot via EntityArtRole — the
  // coordinator wires art from the roles, not from hardcoded type strings.
  Scraper::MediaAsset wheel;
  wheel.type = QStringLiteral("wheel");
  wheel.scope = Scraper::MediaScope::Platform;
  wheel.scopeKey = QStringLiteral("4");
  wheel.url = QUrl(QStringLiteral("https://example.test/wheel.png"));
  wheel.entityRole = Scraper::EntityArtRole::Logo;
  item.media.append(wheel);
  Scraper::MediaAsset illustration;
  illustration.type = QStringLiteral("illustration");
  illustration.scope = Scraper::MediaScope::Platform;
  illustration.scopeKey = QStringLiteral("4");
  illustration.url = QUrl(QStringLiteral("https://example.test/illustration.png"));
  illustration.entityRole = Scraper::EntityArtRole::Background;
  item.media.append(illustration);
  provider->entityResult = item;
  // Adjacent-literal split terminates the \x0a hex escape before the following
  // text — otherwise "\x0afake" parses as the single escape \x0afa (out of
  // range for char, a hard error under clang).
  provider->mediaBytes.insert(wheel.url, QByteArray("\x89PNG\x0d\x0a"
                                                    "fake-bytes"));
  provider->mediaBytes.insert(illustration.url, QByteArray("\x89PNG\x0d\x0a"
                                                           "other-bytes"));

  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  QList<CollectionConfig> collections;
  collections.append(CollectionConfig{});
  ctx.collections = &collections; // ctx.ctx left null → in-memory mutate, no disk save
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionIndex = 0;
  job.collectionName = QStringLiteral("SNES");
  job.artworkDir = tmp.path();
  job.entity.type = Scraper::ScrapeEntityType::Platform;
  job.entity.identity = QStringLiteral("4");
  service.startScrape({job}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  // The stub answers fetch + media download synchronously, but the file write
  // runs on the global QThreadPool (mirrors the batch runner) — spin the event
  // loop until the write's watcher lands back on the main thread.
  QTRY_COMPARE(service.summary().scraped, 1);
  QVERIFY(service.summary().mediaWritten >= 1);
  // Art landed under _shared with the platform_ prefix.
  const QString expected =
      QDir(tmp.path()).filePath(QStringLiteral("_shared/wheel/platform_4.png"));
  QVERIFY2(QFile::exists(expected), qPrintable(expected));
  // Role-driven config wiring: the Logo-role asset feeds the header logo +
  // icon, the Background-role asset feeds the background image.
  QCOMPARE(collections[0].background.headerLogoImage, expected);
  QCOMPARE(collections[0].collectionIcon, expected);
  const QString expectedBg =
      QDir(tmp.path()).filePath(QStringLiteral("_shared/illustration/platform_4.png"));
  QVERIFY2(QFile::exists(expectedBg), qPrintable(expectedBg));
  QCOMPARE(collections[0].background.backgroundImage, expectedBg);
}

void TestScraperServiceResume::entityScrapeCollectionArtToSharedAndConfig() {
  // Kartend-ckepd.5: a COLLECTION entity scrape (TMDB-style) downloads its art and
  // writes it to _shared/<type>/collection_<uuid>.<ext> — exercising the
  // MediaScope::Collection branch of sharedScopePrefix() that every platform test
  // leaves unexercised (they hardcode platform_) — then wires the Logo/Background
  // roles into the collection config exactly as the platform path does.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString uuid = QStringLiteral("a1b2c3d4");

  auto provider = std::make_shared<EntityStubProvider>();
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Star Wars Collection");
  Scraper::MediaAsset logo;
  logo.type = QStringLiteral("logo");
  logo.scope = Scraper::MediaScope::Collection;
  logo.scopeKey = uuid;
  logo.url = QUrl(QStringLiteral("https://example.test/poster.png"));
  logo.entityRole = Scraper::EntityArtRole::Logo;
  item.media.append(logo);
  Scraper::MediaAsset background;
  background.type = QStringLiteral("background");
  background.scope = Scraper::MediaScope::Collection;
  background.scopeKey = uuid;
  background.url = QUrl(QStringLiteral("https://example.test/backdrop.png"));
  background.entityRole = Scraper::EntityArtRole::Background;
  item.media.append(background);
  provider->entityResult = item;
  provider->mediaBytes.insert(logo.url, QByteArray("\x89PNG\x0d\x0a"
                                                   "logo-bytes"));
  provider->mediaBytes.insert(background.url, QByteArray("\x89PNG\x0d\x0a"
                                                         "bg-bytes"));

  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  QList<CollectionConfig> collections;
  collections.append(CollectionConfig{});
  ctx.collections = &collections; // ctx.ctx left null → in-memory mutate, no disk save
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionIndex = 0;
  job.collectionName = QStringLiteral("Star Wars");
  job.artworkDir = tmp.path();
  job.entity.type = Scraper::ScrapeEntityType::Collection;
  job.entity.identity = uuid;
  service.startScrape({job}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  QTRY_COMPARE(service.summary().scraped, 1);
  QVERIFY(service.summary().mediaWritten >= 1);
  // Art landed under _shared with the collection_ prefix (NOT platform_).
  const QString expectedLogo =
      QDir(tmp.path()).filePath(QStringLiteral("_shared/logo/collection_a1b2c3d4.png"));
  QVERIFY2(QFile::exists(expectedLogo), qPrintable(expectedLogo));
  // Same role-driven config wiring as the platform path: Logo → header logo +
  // icon, Background → background image.
  QCOMPARE(collections[0].background.headerLogoImage, expectedLogo);
  QCOMPARE(collections[0].collectionIcon, expectedLogo);
  const QString expectedBg =
      QDir(tmp.path()).filePath(QStringLiteral("_shared/background/collection_a1b2c3d4.png"));
  QVERIFY2(QFile::exists(expectedBg), qPrintable(expectedBg));
  QCOMPARE(collections[0].background.backgroundImage, expectedBg);
}

void TestScraperServiceResume::entityScrapeCollectionLogoDoesNotStealPlatformWiredIcon() {
  // Kartend-5b5r1 field report: the collection-data job runs AFTER the
  // platform job in every entity queue, and its Wikidata logo overwrote the
  // ScreenScraper wheel as the SNES icon (a degenerate SVG, at that). A
  // collection_-scoped asset must NOT replace platform_-scoped art; it may
  // only fill empty or same-scope slots.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString uuid = QStringLiteral("e5f6a7b8");

  auto provider = std::make_shared<EntityStubProvider>();
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("Fallback Logo");
  Scraper::MediaAsset logo;
  logo.type = QStringLiteral("logo-svg");
  logo.scope = Scraper::MediaScope::Collection;
  logo.scopeKey = uuid;
  logo.url = QUrl(QStringLiteral("https://example.test/logo.svg"));
  logo.entityRole = Scraper::EntityArtRole::Logo;
  item.media.append(logo);
  provider->entityResult = item;
  provider->mediaBytes.insert(logo.url, QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\"/>"));

  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  QList<CollectionConfig> collections;
  CollectionConfig cfg;
  // The platform job already wired the SS wheel on this run (or an earlier
  // one) — scrape-owned, platform_-scoped.
  const QString wheel = QDir(tmp.path()).filePath(QStringLiteral("_shared/wheel/platform_4.png"));
  cfg.collectionIcon = wheel;
  cfg.background.headerLogoImage = wheel;
  collections.append(cfg);
  ctx.collections = &collections;
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionIndex = 0;
  job.collectionName = QStringLiteral("Super Famicom");
  job.artworkDir = tmp.path();
  job.entity.type = Scraper::ScrapeEntityType::Collection;
  job.entity.identity = uuid;
  service.startScrape({job}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  QTRY_COMPARE(service.summary().scraped, 1);
  // The Wikidata file still lands on disk (the gallery shows it)…
  const QString landed =
      QDir(tmp.path()).filePath(QStringLiteral("_shared/logo-svg/collection_e5f6a7b8.svg"));
  QVERIFY2(QFile::exists(landed), qPrintable(landed));
  // …but the platform-wired slots are untouched.
  QCOMPARE(collections[0].collectionIcon, wheel);
  QCOMPARE(collections[0].background.headerLogoImage, wheel);
}

void TestScraperServiceResume::entityScrapeFillMissingWiresExistingArtIntoConfig() {
  // Kartend-jjyst.5 established that a FillMissing re-run wires kept files
  // into the config. Kartend-5b5r1 sharpened the entity-art contract: the
  // files are region-less (platform_<id>.png), so FillMissing upgrades to
  // UpdateChanged here — DIFFERENT bytes (a changed region preference: the
  // Dreamcast stuck on the blue PAL wheel after switching to us) replace
  // the stale file, and the refreshed paths are wired into the config.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString existingLogo =
      QDir(tmp.path()).filePath(QStringLiteral("_shared/wheel/platform_4.png"));
  const QString existingBg =
      QDir(tmp.path()).filePath(QStringLiteral("_shared/illustration/platform_4.png"));
  QVERIFY(QDir().mkpath(QFileInfo(existingLogo).absolutePath()));
  QVERIFY(QDir().mkpath(QFileInfo(existingBg).absolutePath()));
  {
    QFile f(existingLogo);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QVERIFY(f.write(QByteArray("OLD_LOGO")) > 0);
  }
  {
    QFile f(existingBg);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QVERIFY(f.write(QByteArray("OLD_BG")) > 0);
  }

  auto provider = std::make_shared<EntityStubProvider>();
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("SNES");
  Scraper::MediaAsset wheel;
  wheel.type = QStringLiteral("wheel");
  wheel.scope = Scraper::MediaScope::Platform;
  wheel.scopeKey = QStringLiteral("4");
  wheel.url = QUrl(QStringLiteral("https://example.test/wheel.png"));
  wheel.entityRole = Scraper::EntityArtRole::Logo;
  item.media.append(wheel);
  Scraper::MediaAsset illustration;
  illustration.type = QStringLiteral("illustration");
  illustration.scope = Scraper::MediaScope::Platform;
  illustration.scopeKey = QStringLiteral("4");
  illustration.url = QUrl(QStringLiteral("https://example.test/illustration.png"));
  illustration.entityRole = Scraper::EntityArtRole::Background;
  item.media.append(illustration);
  provider->entityResult = item;
  provider->mediaBytes.insert(wheel.url, QByteArray("NEW_LOGO_BYTES"));
  provider->mediaBytes.insert(illustration.url, QByteArray("NEW_BG_BYTES"));

  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  QList<CollectionConfig> collections;
  collections.append(CollectionConfig{});
  ctx.collections = &collections; // ctx.ctx left null → in-memory mutate, no disk save
  GeneralSettings settings;
  settings.scraper.options.rescrapeMode = ScraperRescrapeMode::FillMissing;
  ctx.generalSettings = &settings;
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionIndex = 0;
  job.collectionName = QStringLiteral("SNES");
  job.artworkDir = tmp.path();
  job.entity.type = Scraper::ScrapeEntityType::Platform;
  job.entity.identity = QStringLiteral("4");
  service.startScrape({job}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  QTRY_COMPARE(service.summary().scraped, 1);
  // Both destinations held STALE bytes — the UpdateChanged upgrade replaces
  // them so a changed region preference actually lands (Kartend-5b5r1).
  QCOMPARE(service.summary().mediaWritten, 2);
  {
    QFile f(existingLogo);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("NEW_LOGO_BYTES"));
  }
  // The refreshed files are wired into the config (Kartend-jjyst.5).
  QCOMPARE(collections[0].background.headerLogoImage, existingLogo);
  QCOMPARE(collections[0].collectionIcon, existingLogo);
  QCOMPARE(collections[0].background.backgroundImage, existingBg);
}

void TestScraperServiceResume::entityJobLoadsFromPendingState() {
  // An entity job round-trips through pending-scrape.json — the descriptor is
  // restored so an interrupted entity scrape resumes (Kartend-ckepd.2).
  const QByteArray json = R"json({
    "version": 2,
    "mode": "auto",
    "write_metadata": true,
    "media_filter": [],
    "summary_so_far": { "scraped": 0, "skipped": 0, "errors": 0 },
    "queue": [
      {
        "collection_index": 2,
        "collection_uuid": "uuid-snes",
        "collection_name": "SNES",
        "artwork_dir": "/art",
        "remaining": [],
        "entity": { "type": 1, "identity": "4", "collection_index": 2 }
      }
    ]
  })json";
  writePendingFile(json);

  ScraperService service;
  const ScraperService::PendingState state = service.loadPendingState(/*consumeOnLoad=*/false);
  QVERIFY(state.isValid());
  QCOMPARE(state.queue.size(), 1);
  QVERIFY(state.queue.first().isEntityJob());
  QCOMPARE(state.queue.first().entity.type, Scraper::ScrapeEntityType::Platform);
  QCOMPARE(state.queue.first().entity.identity, QStringLiteral("4"));
  QCOMPARE(state.queue.first().entity.collectionIndex, 2);
  // An entity job counts as one unit of remaining work in the resume prompt.
  QCOMPARE(state.totalRemaining(), 1);
}

void TestScraperServiceResume::interactiveEntityJobResumedNotSkipped() {
  // Kartend-ckepd.2 review: an entity job paused mid-fetch in Interactive mode
  // must be re-dispatched on resume, NOT silently skipped by startInteractiveItem
  // (whose empty-items "done" branch would otherwise eat it).
  auto provider = std::make_shared<ParkedEntityProvider>(m_parkedCallbacks);
  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionName = QStringLiteral("SNES");
  job.entity.type = Scraper::ScrapeEntityType::Platform;
  job.entity.identity = QStringLiteral("4");
  service.startScrape({job}, ScraperService::Mode::Interactive, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  // The entity was dispatched and its fetch parked (in flight).
  QCOMPARE(provider->fetchEntityCalls, 1);
  QCOMPARE(service.state(), ScraperService::State::RunningInteractive);

  // Pause (dialog closed) mid-fetch, then resume (dialog reopened).
  service.pauseInteractive();
  QCOMPARE(service.state(), ScraperService::State::PausedInteractive);
  service.resumePaused();

  // Resume must re-dispatch the entity, not skip past it: still running, fetched
  // again — never finishing the run with the entity unscraped.
  QCOMPARE(service.state(), ScraperService::State::RunningInteractive);
  QCOMPARE(provider->fetchEntityCalls, 2);
}

void TestScraperServiceResume::staleEntityCallbackAfterRestartIgnored() {
  // Kartend-ckepd.2 review: an entity-fetch callback that resolves after its run
  // was cancelled and a NEW run started must NOT mutate the new run (the run
  // generation rejects it; the entity path has no runner to detach).
  auto provider = std::make_shared<ParkedEntityProvider>(m_parkedCallbacks);
  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob jobA;
  jobA.collectionName = QStringLiteral("RunA");
  jobA.entity.type = Scraper::ScrapeEntityType::Platform;
  jobA.entity.identity = QStringLiteral("1");
  service.startScrape({jobA}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);
  QCOMPARE(provider->fetchEntityCalls, 1);
  const auto staleCallback = m_parkedCallbacks.entity; // run A's parked callback
  QVERIFY(static_cast<bool>(staleCallback));

  // Cancel run A, then start a fresh run B (also a parked entity).
  service.cancel();
  QCOMPARE(service.state(), ScraperService::State::Idle);
  ScraperService::CollectionJob jobB;
  jobB.collectionName = QStringLiteral("RunB");
  jobB.entity.type = Scraper::ScrapeEntityType::Platform;
  jobB.entity.identity = QStringLiteral("2");
  service.startScrape({jobB}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);
  QCOMPARE(service.state(), ScraperService::State::RunningAuto); // B in flight, parked

  // Fire run A's stale callback as a "success". The run-generation guard must
  // make it a no-op — run B's summary and cursor untouched.
  staleCallback(Scraper::ScrapedItem{});
  QCOMPARE(service.summary().scraped, 0);
  QCOMPARE(service.state(), ScraperService::State::RunningAuto);
}

void TestScraperServiceResume::pausedEntityMediaCallbackAfterResumeIgnored() {
  // Kartend-ckepd.3 review: a media-download callback issued before a pause must
  // be rejected once it resolves AFTER resume (which re-dispatches the item) —
  // otherwise it double-advances the cursor and double-counts. pauseInteractive
  // bumps the run generation so the stale callback's generation no longer
  // matches.
  auto provider = std::make_shared<EntityStubProvider>();
  Scraper::ScrapedItem item;
  item.title = QStringLiteral("SNES");
  Scraper::MediaAsset wheel;
  wheel.type = QStringLiteral("wheel");
  wheel.scope = Scraper::MediaScope::Platform;
  wheel.scopeKey = QStringLiteral("4");
  wheel.url = QUrl(QStringLiteral("https://example.test/wheel.png"));
  item.media.append(wheel);
  provider->entityResult = item;
  provider->parkMediaInto(m_parkedCallbacks); // park the media download so the test controls timing

  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionName = QStringLiteral("SNES");
  job.entity.type = Scraper::ScrapeEntityType::Platform;
  job.entity.identity = QStringLiteral("4");
  service.startScrape({job}, ScraperService::Mode::Interactive, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  // Entity fetched (sync); media download dispatched and parked.
  QVERIFY(static_cast<bool>(m_parkedCallbacks.media));
  const auto staleCallback = m_parkedCallbacks.media; // the pre-pause callback
  QCOMPARE(service.summary().scraped, 0);

  // Pause (Close) then resume (reopen) — resume re-dispatches the entity.
  service.pauseInteractive();
  QCOMPARE(service.state(), ScraperService::State::PausedInteractive);
  service.resumePaused();
  QCOMPARE(service.state(), ScraperService::State::RunningInteractive);

  // Fire the STALE pre-pause media callback. The generation bump must make it a
  // no-op: nothing counted, no advance, run still in flight.
  staleCallback(QByteArray("\x89PNG-bytes"));
  QCOMPARE(service.summary().scraped, 0);
  QCOMPARE(service.state(), ScraperService::State::RunningInteractive);
}

void TestScraperServiceResume::entityFetchQuotaStopsQueueWithResumePoint() {
  // Kartend-xzqel: a 429/430/431 on the entity fetch itself must stop the
  // queue and leave the job persisted as the resume point — not land in the
  // errors bucket while a mixed queue keeps dispatching against a dead quota.
  auto provider = std::make_shared<EntityStubProvider>();
  provider->entityResult = ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                                           QStringLiteral("quota exhausted"))
                               .withHttpStatus(430);
  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionUuid = QStringLiteral("uuid-quota");
  job.collectionName = QStringLiteral("SNES");
  job.entity.type = Scraper::ScrapeEntityType::Platform;
  job.entity.identity = QStringLiteral("4");
  QSignalSpy finishedSpy(&service, &ScraperService::scrapeFinished);
  service.startScrape({job}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  QCOMPARE(service.state(), ScraperService::State::Idle);
  QVERIFY(service.summary().quotaExhausted);
  QCOMPARE(service.summary().errors, 0);
  QCOMPARE(service.summary().scraped, 0);
  QCOMPARE(finishedSpy.count(), 1);
  // The resume point survived: the pending file is still on disk with the
  // entity job queued. (Read it raw — the service still holds the ownership
  // lock, so loadPendingState would rightly report the scrape as live.)
  QVERIFY(QFile::exists(pendingFilePath()));
  QFile f(pendingFilePath());
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
  const QJsonArray queue = root.value(QStringLiteral("queue")).toArray();
  QCOMPARE(queue.size(), 1);
  QVERIFY(queue.first().toObject().contains(QStringLiteral("entity")));
  QVERIFY(root.value(QStringLiteral("summary_so_far"))
              .toObject()
              .value(QStringLiteral("quota_exhausted"))
              .toBool());
}

void TestScraperServiceResume::interactiveQuotaErrorStopsQueueWithResumePoint() {
  // A 429/430/431 on an interactive lookup must stop the queue with a resume
  // point — the old per-item auto-advance fired the next lookup immediately,
  // machine-gunning the remaining items into the exhausted quota.
  auto provider = std::make_shared<ParkedProvider>();
  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionUuid = QStringLiteral("uuid-int-quota");
  job.collectionName = QStringLiteral("Coll");
  job.items = QStringList{QStringLiteral("/m/a.bin"), QStringLiteral("/m/b.bin")};
  QSignalSpy finishedSpy(&service, &ScraperService::scrapeFinished);
  service.startScrape({job}, ScraperService::Mode::Interactive, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);
  QVERIFY(static_cast<bool>(provider->lastLookupCallback));

  provider->lastLookupCallback(
      ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                      QStringLiteral("quota exhausted"))
          .withHttpStatus(430));

  QCOMPARE(service.state(), ScraperService::State::Idle);
  QVERIFY(service.summary().quotaExhausted);
  QCOMPARE(service.summary().errors, 0);
  QCOMPARE(finishedSpy.count(), 1);
  // Resume point kept, with BOTH items still queued (the erroring lookup
  // consumed nothing scrapable).
  QVERIFY(QFile::exists(pendingFilePath()));
  QFile f(pendingFilePath());
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
  const QJsonArray queue = root.value(QStringLiteral("queue")).toArray();
  QCOMPARE(queue.size(), 1);
  QCOMPARE(queue.first().toObject().value(QStringLiteral("remaining")).toArray().size(), 2);
}

void TestScraperServiceResume::interactiveConsecutive429sEscalateToQuotaStop() {
  // Kartend-jjyst.15: the interactive path mirrors the runner's consecutive-429
  // escalation (threshold 3). A lone 429 fails just its item, but a streak
  // means the limiter isn't letting up — the queue stops through the same
  // graceful quota-stop, with the tripping item (and everything behind it)
  // left queued as the resume point.
  auto provider = std::make_shared<ParkedProvider>();
  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionUuid = QStringLiteral("uuid-429");
  job.collectionName = QStringLiteral("Coll");
  job.items = QStringList{QStringLiteral("/m/a.bin"), QStringLiteral("/m/b.bin"),
                          QStringLiteral("/m/c.bin"), QStringLiteral("/m/d.bin")};
  QSignalSpy finishedSpy(&service, &ScraperService::scrapeFinished);
  service.startScrape({job}, ScraperService::Mode::Interactive, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  const auto rateLimited = ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                                           QStringLiteral("rate limited"))
                               .withHttpStatus(429);
  const auto fire429 = [provider, &rateLimited]() {
    auto cb = provider->lastLookupCallback; // copy — firing parks the NEXT item's callback
    QVERIFY(static_cast<bool>(cb));
    cb(rateLimited);
  };

  // Two 429s in a row: both items fail as ordinary errors, run keeps going.
  fire429();
  fire429();
  QCOMPARE(service.state(), ScraperService::State::RunningInteractive);
  QCOMPARE(service.summary().errors, 2);
  QVERIFY(!service.summary().quotaExhausted);

  // The third consecutive 429 trips the escalation: graceful stop, resume
  // point kept, the tripping item NOT consumed as a terminal error.
  fire429();
  QCOMPARE(service.state(), ScraperService::State::Idle);
  QVERIFY(service.summary().quotaExhausted);
  QCOMPARE(service.summary().errors, 2);
  QCOMPARE(finishedSpy.count(), 1);
  QVERIFY(service.summary()
              .firstFailures.join(QLatin1Char('\n'))
              .contains(QStringLiteral("consecutive HTTP 429")));
  // The tripping item c and the never-dispatched d stay queued for resume.
  QVERIFY(QFile::exists(pendingFilePath()));
  QFile f(pendingFilePath());
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
  const QJsonArray queue = root.value(QStringLiteral("queue")).toArray();
  QCOMPARE(queue.size(), 1);
  const QJsonArray remaining =
      queue.first().toObject().value(QStringLiteral("remaining")).toArray();
  QCOMPARE(remaining.size(), 2);
  QCOMPARE(remaining.first().toString(), QStringLiteral("/m/c.bin"));
}

void TestScraperServiceResume::interactive429StreakResetByDeliveredResult() {
  // Kartend-jjyst.15: any delivered lookup result (here a clean "no match")
  // proves the limiter let a request through — it must reset the 429 streak,
  // so alternating 429s and results never trip the escalation.
  auto provider = std::make_shared<ParkedProvider>();
  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionName = QStringLiteral("Coll");
  for (int i = 0; i < 6; ++i) {
    job.items.append(QStringLiteral("/m/item%1.bin").arg(i));
  }
  service.startScrape({job}, ScraperService::Mode::Interactive, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  const auto rateLimited = ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                                           QStringLiteral("rate limited"))
                               .withHttpStatus(429);
  const auto fire = [provider](const ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> &r) {
    auto cb = provider->lastLookupCallback;
    QVERIFY(static_cast<bool>(cb));
    cb(r);
  };

  fire(rateLimited);
  fire(rateLimited);
  fire(QList<Scraper::ScrapeCandidate>{}); // delivered "no match" — resets the streak
  fire(rateLimited);
  fire(rateLimited);

  // Never three CONSECUTIVE 429s, so no escalation: the run is still live on
  // its last item.
  QCOMPARE(service.state(), ScraperService::State::RunningInteractive);
  QVERIFY(!service.summary().quotaExhausted);
  QCOMPARE(service.summary().errors, 4);
  QCOMPARE(service.summary().notFound, 1);
}

void TestScraperServiceResume::entityConsecutive429sEscalateToQuotaStop() {
  // Kartend-jjyst.15, entity path: three consecutive 429'd entity fetches
  // escalate to the graceful quota-stop — the tripping job stays queued as
  // the resume point instead of the whole queue machine-gunning into a
  // limiter that isn't letting up.
  auto provider = std::make_shared<EntityStubProvider>();
  provider->entityResult = ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                                           QStringLiteral("rate limited"))
                               .withHttpStatus(429);
  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  QList<ScraperService::CollectionJob> jobs;
  for (int i = 0; i < 4; ++i) {
    ScraperService::CollectionJob job;
    job.collectionIndex = i;
    job.collectionUuid = QStringLiteral("uuid-%1").arg(i);
    job.collectionName = QStringLiteral("Coll%1").arg(i);
    job.entity.type = Scraper::ScrapeEntityType::Platform;
    job.entity.identity = QString::number(i);
    job.entity.collectionIndex = i;
    jobs.append(job);
  }
  QSignalSpy finishedSpy(&service, &ScraperService::scrapeFinished);
  service.startScrape(jobs, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  // Jobs 0 and 1 fail as ordinary errors; job 2's fetch trips the streak and
  // stops the queue; job 3 is never dispatched.
  QCOMPARE(provider->fetchEntityCalls, 3);
  QCOMPARE(service.state(), ScraperService::State::Idle);
  QVERIFY(service.summary().quotaExhausted);
  QCOMPARE(service.summary().errors, 2);
  QCOMPARE(service.summary().scraped, 0);
  QCOMPARE(finishedSpy.count(), 1);
  QVERIFY(service.summary()
              .firstFailures.join(QLatin1Char('\n'))
              .contains(QStringLiteral("consecutive HTTP 429")));
  // The tripping job (and the never-dispatched one) persist as the resume point.
  QVERIFY(QFile::exists(pendingFilePath()));
  QFile f(pendingFilePath());
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
  const QJsonArray queue = root.value(QStringLiteral("queue")).toArray();
  QCOMPARE(queue.size(), 2);
  QCOMPARE(queue.first().toObject().value(QStringLiteral("collection_uuid")).toString(),
           QStringLiteral("uuid-2"));
}

void TestScraperServiceResume::entityMedia429sEscalateToQuotaStop() {
  // Kartend-jjyst.15, entity media path: image CDNs are the realistic 429
  // source. Three parallel platform-art fetches all answering 429 trip the
  // shared escalation — the job stays queued for resume (atomic, one fetch to
  // retry) rather than landing in errors.
  auto provider = std::make_shared<EntityStubProvider>();
  Scraper::ScrapedItem item;
  for (const QString &type :
       {QStringLiteral("wheel"), QStringLiteral("illustration"), QStringLiteral("steel")}) {
    Scraper::MediaAsset asset;
    asset.type = type;
    asset.scope = Scraper::MediaScope::Platform;
    asset.scopeKey = QStringLiteral("4");
    asset.url = QUrl(QStringLiteral("https://example.test/%1.png").arg(type));
    item.media.append(asset);
  }
  provider->entityResult = item;
  provider->mediaError = ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                                         QStringLiteral("rate limited"))
                             .withHttpStatus(429);

  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionUuid = QStringLiteral("uuid-media-429");
  job.collectionName = QStringLiteral("SNES");
  job.entity.type = Scraper::ScrapeEntityType::Platform;
  job.entity.identity = QStringLiteral("4");
  service.startScrape({job}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  QCOMPARE(service.state(), ScraperService::State::Idle);
  QVERIFY(service.summary().quotaExhausted);
  QCOMPARE(service.summary().scraped, 0);
  QCOMPARE(service.summary().errors, 0); // queued for resume, not a terminal error
  QVERIFY(QFile::exists(pendingFilePath()));
}

void TestScraperServiceResume::entityMediaQuotaKeepsJobQueuedForResume() {
  // Kartend-xzqel: quota death on a platform-art media fetch must also stop
  // the queue and keep the entity job queued — unlike a game item, the art IS
  // the point, and a retry after quota reset costs one fetch.
  auto provider = std::make_shared<EntityStubProvider>();
  Scraper::ScrapedItem item;
  Scraper::MediaAsset wheel;
  wheel.type = QStringLiteral("wheel");
  wheel.scope = Scraper::MediaScope::Platform;
  wheel.scopeKey = QStringLiteral("4");
  wheel.url = QUrl(QStringLiteral("https://example.test/wheel.png"));
  item.media.append(wheel);
  provider->entityResult = item;
  provider->mediaError = ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                                         QStringLiteral("quota exhausted"))
                             .withHttpStatus(430);

  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionUuid = QStringLiteral("uuid-quota-media");
  job.collectionName = QStringLiteral("SNES");
  job.entity.type = Scraper::ScrapeEntityType::Platform;
  job.entity.identity = QStringLiteral("4");
  service.startScrape({job}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  QCOMPARE(service.state(), ScraperService::State::Idle);
  QVERIFY(service.summary().quotaExhausted);
  QCOMPARE(service.summary().scraped, 0);
  QVERIFY(QFile::exists(pendingFilePath())); // resume point kept
}

void TestScraperServiceResume::entityAllMediaFetchesFailedCountsError() {
  // Kartend-xzqel: an entity whose media fetches ALL fail (auth error, wrong
  // endpoint params) used to be counted scraped with zero art and no failure
  // record — it must land in errors and be re-queueable via failedItems.
  auto provider = std::make_shared<EntityStubProvider>();
  Scraper::ScrapedItem item;
  Scraper::MediaAsset wheel;
  wheel.type = QStringLiteral("wheel");
  wheel.scope = Scraper::MediaScope::Platform;
  wheel.scopeKey = QStringLiteral("4");
  wheel.url = QUrl(QStringLiteral("https://example.test/wheel.png"));
  item.media.append(wheel);
  provider->entityResult = item;
  provider->mediaError = ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                                         QStringLiteral("HTTP failed"))
                             .withHttpStatus(500);

  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionIndex = 3;
  job.collectionUuid = QStringLiteral("uuid-allfail");
  job.collectionName = QStringLiteral("SNES");
  job.entity.type = Scraper::ScrapeEntityType::Platform;
  job.entity.identity = QStringLiteral("4");
  service.startScrape({job}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  QCOMPARE(service.summary().errors, 1);
  QCOMPARE(service.summary().scraped, 0);
  QCOMPARE(service.summary().notFound, 0);
  QCOMPARE(service.summary().failedItems.size(), 1);
  QCOMPARE(service.summary().failedItems.first().path, QStringLiteral("4"));
  QCOMPARE(service.summary().failedItems.first().collectionUuid, QStringLiteral("uuid-allfail"));
  QVERIFY(!service.summary().quotaExhausted);
  QCOMPARE(service.state(), ScraperService::State::Idle);
}

void TestScraperServiceResume::resumeReresolvesCollectionIndexByUuid() {
  // Kartend-8zd3q: collection indices are not stable across sessions. A
  // resumed job must re-resolve its live index from the persisted UUID —
  // otherwise the provider is built with (and entity art written into)
  // another collection's config.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir().mkpath(tmp.path() + QStringLiteral("/alpha")));
  QVERIFY(QDir().mkpath(tmp.path() + QStringLiteral("/beta")));
  CollectionConfig alpha;
  alpha.name = QStringLiteral("Alpha");
  alpha.mediaDirectory = tmp.path() + QStringLiteral("/alpha");
  CollectionConfig beta;
  beta.name = QStringLiteral("Beta");
  beta.mediaDirectory = tmp.path() + QStringLiteral("/beta");
  QList<CollectionConfig> collections{alpha, beta};
  const QString betaUuid = CollectionUtils::computeCollectionUuid(collections[1]);

  auto provider = std::make_shared<ParkedEntityProvider>(m_parkedCallbacks);
  QList<int> requestedIndices;
  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider,
                         &requestedIndices](int idx) -> std::shared_ptr<MetadataLookupProvider> {
    requestedIndices.append(idx);
    return provider;
  };
  ctx.collections = &collections;
  service.setContext(ctx);

  // The persisted snapshot was written when Beta sat at index 0 — since then
  // a collection was inserted ahead of it, so the stale index now names Alpha.
  ScraperService::PendingState state;
  state.hasState = true;
  ScraperService::CollectionJob job;
  job.collectionIndex = 0; // stale
  job.collectionUuid = betaUuid;
  job.collectionName = QStringLiteral("Beta");
  job.entity.type = Scraper::ScrapeEntityType::Platform;
  job.entity.identity = QStringLiteral("7");
  job.entity.collectionIndex = 0; // stale
  state.queue.append(job);

  service.resumeFromState(state);

  // The provider must have been built for Beta's LIVE index, not the stale 0.
  QCOMPARE(requestedIndices.size(), 1);
  QCOMPARE(requestedIndices.first(), 1);
}

void TestScraperServiceResume::resumeDropsJobWhoseUuidNoLongerResolves() {
  // Kartend-8zd3q: a resumed job whose collection no longer exists must be
  // dropped (with a warning), never guessed by stale index.
  CollectionConfig only;
  only.name = QStringLiteral("Solo");
  QList<CollectionConfig> collections{only};

  auto provider = std::make_shared<ParkedEntityProvider>(m_parkedCallbacks);
  int builderCalls = 0;
  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider, &builderCalls](int) -> std::shared_ptr<MetadataLookupProvider> {
    ++builderCalls;
    return provider;
  };
  ctx.collections = &collections;
  service.setContext(ctx);

  ScraperService::PendingState state;
  state.hasState = true;
  ScraperService::CollectionJob job;
  job.collectionIndex = 0; // in range — would hit "Solo" if trusted
  job.collectionUuid = QStringLiteral("deadbeef-no-such-collection");
  job.collectionName = QStringLiteral("Removed");
  job.entity.type = Scraper::ScrapeEntityType::Platform;
  job.entity.identity = QStringLiteral("7");
  state.queue.append(job);

  QSignalSpy finishedSpy(&service, &ScraperService::scrapeFinished);
  service.resumeFromState(state);

  // Job dropped: no provider built, queue drained immediately.
  QCOMPARE(builderCalls, 0);
  QCOMPARE(service.state(), ScraperService::State::Idle);
  QCOMPARE(finishedSpy.count(), 1);
}

void TestScraperServiceResume::failedItemsRoundTripThroughPendingState() {
  // Kartend-ufmok: failedItems / sidecarFailures / quotaExhausted must load
  // from the snapshot — a resumed run used to come back with an empty failure
  // list, so "Re-scrape failed items" had nothing to re-queue.
  const QByteArray json = R"json({
    "version": 2,
    "mode": "auto",
    "write_metadata": true,
    "media_filter": [],
    "summary_so_far": {
      "scraped": 2,
      "skipped": 0,
      "errors": 2,
      "sidecar_failures": 3,
      "quota_exhausted": true,
      "failed_items": [
        { "collection_index": 1, "collection_uuid": "uuid-a", "path": "/m/broken.bin" },
        { "collection_index": 2, "collection_uuid": "uuid-b", "path": "/m/worse.bin" }
      ]
    },
    "queue": [
      {
        "collection_index": 1,
        "collection_uuid": "uuid-a",
        "collection_name": "Coll",
        "artwork_dir": "/art",
        "remaining": ["/m/a.bin"]
      }
    ]
  })json";
  writePendingFile(json);

  ScraperService service;
  const ScraperService::PendingState state = service.loadPendingState(/*consumeOnLoad=*/false);
  QVERIFY(state.isValid());
  QCOMPARE(state.summarySoFar.sidecarFailures, 3);
  QVERIFY(state.summarySoFar.quotaExhausted);
  QCOMPARE(state.summarySoFar.failedItems.size(), 2);
  QCOMPARE(state.summarySoFar.failedItems.first().collectionIndex, 1);
  QCOMPARE(state.summarySoFar.failedItems.first().collectionUuid, QStringLiteral("uuid-a"));
  QCOMPARE(state.summarySoFar.failedItems.first().path, QStringLiteral("/m/broken.bin"));
  QCOMPARE(state.summarySoFar.failedItems.last().path, QStringLiteral("/m/worse.bin"));
}

void TestScraperServiceResume::failedItemsAggregateAcrossCollections() {
  // Kartend-jjjo5, criterion 2: two collections, each contributing exactly one
  // errored item, must BOTH appear in summary().failedItems — proving the
  // aggregate list accumulates across collections rather than the second
  // collection's processing dropping the first's failure. Entity jobs are used
  // because they need no DB (a game batch requires ctx.ctx per the no-DB-mocking
  // rule); both feed the SAME m_summary.failedItems sink. Each item keeps its
  // OWN collection's index/uuid so the re-scrape path rebuilds one job per owner.
  auto provider = std::make_shared<EntityStubProvider>();
  provider->entityResult = ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                                           QStringLiteral("boom"));

  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob a;
  a.collectionIndex = 0;
  a.collectionUuid = QStringLiteral("uuid-a");
  a.collectionName = QStringLiteral("Alpha");
  a.entity.type = Scraper::ScrapeEntityType::Platform;
  a.entity.identity = QStringLiteral("11");
  a.entity.collectionIndex = 0;
  ScraperService::CollectionJob b;
  b.collectionIndex = 1;
  b.collectionUuid = QStringLiteral("uuid-b");
  b.collectionName = QStringLiteral("Beta");
  b.entity.type = Scraper::ScrapeEntityType::Platform;
  b.entity.identity = QStringLiteral("22");
  b.entity.collectionIndex = 1;

  service.startScrape({a, b}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  QTRY_COMPARE(service.summary().errors, 2);
  const auto failed = service.summary().failedItems; // by value — copy outlives temporary
  QCOMPARE(failed.size(), 2);

  const auto find = [&failed](const QString &identity) {
    return std::find_if(
        failed.begin(), failed.end(),
        [&identity](const ScraperService::Summary::FailedItem &fi) { return fi.path == identity; });
  };
  const auto itA = find(QStringLiteral("11"));
  const auto itB = find(QStringLiteral("22"));
  QVERIFY(itA != failed.end());
  QVERIFY(itB != failed.end());
  // Each failure carries its OWN collection's identity (not the last one's).
  QVERIFY(itA->isEntity);
  QCOMPARE(itA->collectionIndex, 0);
  QCOMPARE(itA->collectionUuid, QStringLiteral("uuid-a"));
  QVERIFY(itB->isEntity);
  QCOMPARE(itB->collectionIndex, 1);
  QCOMPARE(itB->collectionUuid, QStringLiteral("uuid-b"));
}

void TestScraperServiceResume::persistWritesFailedItemsMidRun() {
  // Kartend-ufmok, persist direction: after an item fails mid-run the next
  // debounced snapshot must carry it in summary_so_far.failed_items.
  auto errorProvider = std::make_shared<EntityStubProvider>();
  errorProvider->entityResult = ErrorUtils::ErrorContext::error(
      ErrorUtils::ErrorCode::InvalidArgument, QStringLiteral("boom"));
  auto parkedProvider = std::make_shared<ParkedEntityProvider>(m_parkedCallbacks);

  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [errorProvider,
                         parkedProvider](int idx) -> std::shared_ptr<MetadataLookupProvider> {
    if (idx == 0) return errorProvider;
    return parkedProvider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob failing;
  failing.collectionIndex = 0;
  failing.collectionUuid = QStringLiteral("uuid-failing");
  failing.collectionName = QStringLiteral("Failing");
  failing.entity.type = Scraper::ScrapeEntityType::Platform;
  failing.entity.identity = QStringLiteral("9");
  ScraperService::CollectionJob parked;
  parked.collectionIndex = 1;
  parked.collectionUuid = QStringLiteral("uuid-parked");
  parked.collectionName = QStringLiteral("Parked");
  parked.entity.type = Scraper::ScrapeEntityType::Platform;
  parked.entity.identity = QStringLiteral("8");

  service.startScrape({failing, parked}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  // Job 1 errored synchronously; job 2 is parked in flight, so the run is
  // still live and the debounced persist (250ms) will rewrite the snapshot.
  QCOMPARE(service.summary().errors, 1);
  const auto readFailedItems = []() {
    QFile f(pendingFilePath());
    if (!f.open(QIODevice::ReadOnly)) return QJsonArray{};
    return QJsonDocument::fromJson(f.readAll())
        .object()
        .value(QStringLiteral("summary_so_far"))
        .toObject()
        .value(QStringLiteral("failed_items"))
        .toArray();
  };
  QTRY_VERIFY(!readFailedItems().isEmpty());
  const QJsonObject failed = readFailedItems().first().toObject();
  QCOMPARE(failed.value(QStringLiteral("path")).toString(), QStringLiteral("9"));
  QCOMPARE(failed.value(QStringLiteral("collection_uuid")).toString(),
           QStringLiteral("uuid-failing"));
  QCOMPARE(failed.value(QStringLiteral("collection_index")).toInt(), 0);
}

void TestScraperServiceResume::entityFailureRecordsEntityDiscriminator() {
  // Fix A: an entity job that fails must record its failedItem tagged as an
  // entity (isEntity=true) carrying the full EntityScrapeTarget — so the
  // re-scrape path rebuilds it AS an entity job, not a bogus game lookup of the
  // systemeid. Regression: the failedItem used to look identical to a game item
  // (just the systemeid stuffed into `path`).
  auto provider = std::make_shared<EntityStubProvider>();
  provider->entityResult = ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::UnknownError,
                                                           QStringLiteral("catalog unavailable"))
                               .withHttpStatus(503);
  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionIndex = 5;
  job.collectionUuid = QStringLiteral("uuid-plat");
  job.collectionName = QStringLiteral("SNES");
  job.entity.type = Scraper::ScrapeEntityType::Platform;
  job.entity.identity = QStringLiteral("4");
  job.entity.collectionIndex = 5;
  service.startScrape({job}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  // Bucketed as an error (503 is neither 404 nor RemoteResourceNotFound), and
  // recorded as a re-queueable entity failure.
  QCOMPARE(service.summary().errors, 1);
  QCOMPARE(service.summary().notFound, 0);
  QCOMPARE(service.summary().failedItems.size(), 1);
  // summary() returns BY VALUE, so bind a copy — a reference would dangle the
  // instant the temporary Summary is destroyed at the end of this statement
  // (a use-after-free TSan flags and non-TSan builds pass by luck).
  const auto fi = service.summary().failedItems.first();
  QVERIFY(fi.isEntity);
  QCOMPARE(fi.entity.type, Scraper::ScrapeEntityType::Platform);
  QCOMPARE(fi.entity.identity, QStringLiteral("4"));
  QCOMPARE(fi.entity.collectionIndex, 5);
  QCOMPARE(fi.collectionUuid, QStringLiteral("uuid-plat"));
}

void TestScraperServiceResume::entityFailedItemRoundTripsDiscriminator() {
  // Fix A persistence: an entity failedItem's discriminator + target must
  // survive a serialize→deserialize so a resumed run keeps it re-queueable AS
  // an entity. A game failedItem (no is_entity) must deserialize as isEntity
  // false (backward-compat).
  ScraperService::PendingState snap;
  snap.hasState = true;
  snap.startedAtUnixMs = 123;
  ScraperService::Summary::FailedItem entityFail;
  entityFail.collectionIndex = 2;
  entityFail.collectionUuid = QStringLiteral("uuid-ent");
  entityFail.path = QStringLiteral("4"); // display carries the systemeid
  entityFail.isEntity = true;
  entityFail.entity.type = Scraper::ScrapeEntityType::Platform;
  entityFail.entity.identity = QStringLiteral("4");
  entityFail.entity.collectionIndex = 2;
  ScraperService::Summary::FailedItem gameFail;
  gameFail.collectionIndex = 1;
  gameFail.collectionUuid = QStringLiteral("uuid-game");
  gameFail.path = QStringLiteral("/m/broken.bin"); // an ordinary game item
  snap.summarySoFar.failedItems = {entityFail, gameFail};
  // Media failure counters ride the same serialize→deserialize round trip
  // (Kartend-jjyst.16).
  snap.summarySoFar.mediaFetchFailures = 7;
  snap.summarySoFar.mediaWriteFailures = 3;
  // A queue entry is required for isValid()/deserialize to keep the state.
  ScraperService::CollectionJob queued;
  queued.collectionIndex = 1;
  queued.collectionName = QStringLiteral("Coll");
  queued.items = {QStringLiteral("/m/a.bin")};
  snap.queue = {queued};

  const QByteArray bytes = Scraper::ScrapePendingState::serialize(snap);
  const ScraperService::PendingState back = Scraper::ScrapePendingState::deserialize(bytes);
  QCOMPARE(back.summarySoFar.mediaFetchFailures, 7);
  QCOMPARE(back.summarySoFar.mediaWriteFailures, 3);
  QCOMPARE(back.summarySoFar.failedItems.size(), 2);
  const auto &e = back.summarySoFar.failedItems.at(0);
  QVERIFY(e.isEntity);
  QCOMPARE(e.entity.type, Scraper::ScrapeEntityType::Platform);
  QCOMPARE(e.entity.identity, QStringLiteral("4"));
  QCOMPARE(e.entity.collectionIndex, 2);
  QCOMPARE(e.collectionUuid, QStringLiteral("uuid-ent"));
  const auto &g = back.summarySoFar.failedItems.at(1);
  QVERIFY(!g.isEntity);
  QCOMPARE(g.entity.type, Scraper::ScrapeEntityType::Game);
  QCOMPARE(g.path, QStringLiteral("/m/broken.bin"));
}

void TestScraperServiceResume::entityCatalogUnavailableBucketedAsError() {
  // Fix B (coordinator-level): the catalog-manager seam isn't injectable
  // headlessly, so we simulate its observable effect — fetchEntity returning a
  // transient, non-notFound error (as the provider now does when the systems
  // catalog is EMPTY, e.g. offline / no-creds / cold-cache). It must land in
  // errors + failedItems (re-queueable), NOT be silently consumed as notFound.
  auto provider = std::make_shared<EntityStubProvider>();
  provider->entityResult =
      ErrorUtils::ErrorContext::error(
          ErrorUtils::ErrorCode::UnknownError,
          QStringLiteral("ScreenScraper systems catalog unavailable; cannot resolve system 4"))
          .withHttpStatus(503);
  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionIndex = 0;
  job.collectionName = QStringLiteral("SNES");
  job.entity.type = Scraper::ScrapeEntityType::Platform;
  job.entity.identity = QStringLiteral("4");
  service.startScrape({job}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  QCOMPARE(service.summary().errors, 1);
  QCOMPARE(service.summary().notFound, 0);
  QCOMPARE(service.summary().failedItems.size(), 1);
  QVERIFY(service.summary().failedItems.first().isEntity);
}

void TestScraperServiceResume::entityNotFoundBucketedNotError() {
  // Fix B contrast: a GENUINE not-found (catalog loaded, id absent) still maps
  // to RemoteResourceNotFound → the notFound bucket, and is NOT recorded as a
  // re-queueable failure. Proves the two paths stay distinguished.
  auto provider = std::make_shared<EntityStubProvider>();
  provider->entityResult = ErrorUtils::ErrorContext::error(
      ErrorUtils::ErrorCode::RemoteResourceNotFound, QStringLiteral("no such system 999"));
  ScraperService service;
  ScraperService::Context ctx;
  ctx.providerBuilder = [provider](int) -> std::shared_ptr<MetadataLookupProvider> {
    return provider;
  };
  service.setContext(ctx);

  ScraperService::CollectionJob job;
  job.collectionName = QStringLiteral("Obscure");
  job.entity.type = Scraper::ScrapeEntityType::Platform;
  job.entity.identity = QStringLiteral("999");
  service.startScrape({job}, ScraperService::Mode::Auto, /*mediaFilter=*/{},
                      /*writeMetadata=*/true);

  QCOMPARE(service.summary().notFound, 1);
  QCOMPARE(service.summary().errors, 0);
  QVERIFY(service.summary().failedItems.isEmpty());
}

QTEST_MAIN(TestScraperServiceResume)
#include "test_scraperservice_resume.moc"
