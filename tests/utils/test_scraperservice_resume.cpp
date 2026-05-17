// Regression test for ScraperService resume: the persisted run
// summary must round-trip the media-written count. Dropping it made a
// resumed scrape come back showing "0 media" even though earlier media
// had already been downloaded.

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QTest>

#include <memory>

#include "metadatalookupprovider.h"
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
  void cancelledInteractiveScrapeIgnoresLateLookupResult();

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
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("Kartend"));
  QCoreApplication::setApplicationName(QStringLiteral("kartend-test-scraperservice-resume"));
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
      "media_written": 27,
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

QTEST_MAIN(TestScraperServiceResume)
#include "test_scraperservice_resume.moc"
