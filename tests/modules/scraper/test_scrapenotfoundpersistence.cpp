/**
 * @file test_scrapenotfoundpersistence.cpp
 * @brief ScreenScraper not-found stamps written by the production batch path
 *        (Kartend-bb8df).
 *
 * Drives the real chain end to end: BatchScrapeRunner → ScreenScraperProvider
 * (real hashing, URL building, reply classification) → ScrapeWriteWorker on
 * its own QThread + connection → ScrapeNotFoundStore → the real, migrated,
 * on-disk media.db that a real DatabaseManager created in the sandboxed
 * AppDataLocation. Only the HTTP transport is replaced, through the
 * ProviderBase fetch seam, with deterministic local replies; the systems and
 * media-type catalogs are pre-seeded on disk so nothing reaches the network.
 *
 * Every assertion reads the database back through a separate main-thread
 * connection after the runner's `finished`, so a stamp that was only queued
 * (not stored) fails the test.
 */

#include <functional>
#include <memory>
#include <optional>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include "../../support/testsandbox.h"
#include "applicationcontext.h"
#include "batchscraperunner.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "databasemanager.h"
#include "databaseschema.h"
#include "providerbase.h"
#include "scrapenotfound.h"
#include "screenscrapermediatypecache.h"
#include "screenscraperprovider.h"
#include "screenscrapersystemcache.h"
#include "screenscraperurls.h"
#include "sessionmanager.h"

using ScrapeNotFoundStore::Stamp;

namespace {

const QString kUuid = QStringLiteral("u-notfound");
const QString kSs = QStringLiteral("screenscraper");
const QString kOtherProvider = QStringLiteral("thegamesdb");
const QString kDevId = QStringLiteral("dev123");
const QString kDevPw = QStringLiteral("devpw-secret");

using Reply = ErrorUtils::Result<QByteArray>;

Reply httpError(int status) {
  return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::NetworkRequestFailed,
                                         QStringLiteral("HTTP request failed"), "test")
      .withHttpStatus(status);
}

Reply timeoutError() {
  // What HttpClient reports for a QNAM transfer timeout (no HTTP status).
  return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::OperationCancelled,
                                         QStringLiteral("Network request timed out"), "test");
}

Reply emptyNoMatch() {
  return QByteArray(R"({"header":{"success":"true"},"response":{"ssuser":{"id":"x"}}})");
}

Reply validMatch() {
  return QByteArray(R"({"response":{"jeu":{"id":"5555","romid":"1",)"
                    R"("noms":[{"region":"wor","text":"Matched Game"}],)"
                    R"("systeme":{"id":"4","text":"Super Nintendo"}}}})");
}

ApplicationContext makeCtx(SessionManager *session) {
  ApplicationContext ctx;
  ctx.managers.sessionManager = session;
  return ctx;
}

} // namespace

class TestScrapeNotFoundPersistence : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  void genuineNotFoundRecordsOneScopedStamp();
  void emptyNoMatchRecordsStamp();
  void repeatedMissUpdatesSameIdentityAndKeysFollowInputs();
  void successClearsOnlyThatProvidersStamp();
  void transientAndQuotaFailuresNeverTouchStamps_data();
  void transientAndQuotaFailuresNeverTouchStamps();
  void malformedRepliesRecordNothing_data();
  void malformedRepliesRecordNothing();
  void cancellationRecordsNothing();

private:
  struct Run {
    Scraper::BatchScrapeRunner::Summary summary;
    bool finished = false;
    QList<QUrl> urls;
  };
  Run runBatch(const QStringList &paths, const std::function<Reply(const QUrl &)> &reply);
  QString writeRom(const QString &name, const QByteArray &bytes);
  std::optional<Stamp> stampFor(const QString &path, const QString &provider = kSs);
  int stampCount();
  int metadataRows(const QString &path);
  qint64 stampRowId(const QString &path, const QString &provider);
  void seedStamp(const QString &path, const QString &provider, const QString &key,
                 const QDateTime &at);

  std::unique_ptr<SessionManager> m_session;
  ApplicationContext m_ctx;
  std::unique_ptr<DatabaseManager> m_db;
  QSqlDatabase m_reader;
  std::unique_ptr<QTemporaryDir> m_roms;
  GeneralSettings m_settings;
  CollectionConfig m_cfg;
};

void TestScrapeNotFoundPersistence::initTestCase() {
  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-scrapenotfound-persistence"));
  // Offline catalogs: a fresh on-disk cache short-circuits both fetches.
  ScreenScraperSystems::System snes;
  snes.id = 4;
  snes.displayName = QStringLiteral("Super Nintendo");
  ScreenScraperSystems::System md;
  md.id = 1;
  md.displayName = QStringLiteral("Mega Drive");
  QVERIFY(ScreenScraperSystemCache::saveSystems(ScreenScraperSystemCache::defaultCachePath(),
                                                {snes, md}));
  ScreenScraperMediaTypeCache::MediaType box;
  box.type = QStringLiteral("box-2d");
  box.displayName = QStringLiteral("Box 2D");
  box.category = QStringLiteral("jeu");
  box.fileFormat = QStringLiteral("png");
  QVERIFY(ScreenScraperMediaTypeCache::saveMediaTypes(
      ScreenScraperMediaTypeCache::defaultCachePath(), {box}));

  m_session = std::make_unique<SessionManager>();
  m_ctx = makeCtx(m_session.get());
  m_db = std::make_unique<DatabaseManager>(&m_ctx);
  m_ctx.managers.databaseManager = m_db.get();

  // Independent main-thread reader on the same media.db, opened with the
  // production opener (the runner's worker uses the same location).
  m_reader = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                       QStringLiteral("scrapenotfound_persistence_reader"));
  QVERIFY(DatabaseSchema::openConnection(
      m_reader, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)));

  auto &blob = m_settings.scraper.credentials[kSs];
  blob.insert(QStringLiteral("dev_id"), kDevId);
  blob.insert(QStringLiteral("dev_password"), kDevPw);
}

void TestScrapeNotFoundPersistence::cleanupTestCase() {
  ProviderBase::setFetchFunctionForTesting({});
  m_reader.close();
  m_reader = QSqlDatabase();
  QSqlDatabase::removeDatabase(QStringLiteral("scrapenotfound_persistence_reader"));
  m_db.reset();
  m_session.reset();
}

void TestScrapeNotFoundPersistence::init() {
  m_roms = std::make_unique<QTemporaryDir>();
  QVERIFY(m_roms->isValid());
  m_cfg = CollectionConfig{};
  m_cfg.name = QStringLiteral("SNES");
  m_cfg.scraperOverrides.screenscraperSystemId = 4;
  QSqlQuery q(m_reader);
  QVERIFY(q.exec(QStringLiteral("DELETE FROM scrape_not_found")));
  QVERIFY(q.exec(QStringLiteral("DELETE FROM item_metadata")));
}

void TestScrapeNotFoundPersistence::cleanup() {
  ProviderBase::setFetchFunctionForTesting({});
  m_roms.reset();
}

QString TestScrapeNotFoundPersistence::writeRom(const QString &name, const QByteArray &bytes) {
  const QString path = m_roms->filePath(name);
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
  f.write(bytes);
  f.close();
  return path;
}

TestScrapeNotFoundPersistence::Run
TestScrapeNotFoundPersistence::runBatch(const QStringList &paths,
                                        const std::function<Reply(const QUrl &)> &reply) {
  Run run;
  ProviderBase::setFetchFunctionForTesting(
      [&run, reply](const QUrl &url, const Scraper::HttpClient::RawHeaders &,
                    Scraper::HttpClient::ResponseCallback callback, const QStringList &) {
        run.urls.append(url);
        Reply answer = reply(url);
        // Deliver asynchronously, like HttpClient does.
        QTimer::singleShot(0, qApp, [callback = std::move(callback), answer]() mutable {
          callback(std::move(answer));
        });
      });
  auto provider = std::make_shared<ScreenScraperProvider>([this]() { return &m_settings; },
                                                          [this]() { return &m_cfg; });
  {
    Scraper::BatchScrapeRunner runner(&m_ctx, provider, kUuid, paths, m_roms->path(),
                                      /*fetchPrimaryCover=*/false);
    QObject::connect(&runner, &Scraper::BatchScrapeRunner::finished,
                     [&run](const Scraper::BatchScrapeRunner::Summary &s) {
                       run.summary = s;
                       run.finished = true;
                     });
    runner.start();
    // Bounded: retried transient failures back off for a few seconds.
    QElapsedTimer timer;
    timer.start();
    while (!run.finished && timer.elapsed() < 60000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    // Runner destruction joins its write worker (closeConnection queued
    // after every stamp write it dispatched).
  }
  ProviderBase::setFetchFunctionForTesting({});
  return run;
}

std::optional<Stamp> TestScrapeNotFoundPersistence::stampFor(const QString &path,
                                                             const QString &provider) {
  auto loaded = ScrapeNotFoundStore::load(m_reader, kUuid, path, provider);
  if (loaded.isError()) return std::nullopt;
  return loaded.value();
}

int TestScrapeNotFoundPersistence::stampCount() {
  QSqlQuery q(m_reader);
  return (q.exec(QStringLiteral("SELECT COUNT(*) FROM scrape_not_found")) && q.next())
             ? q.value(0).toInt()
             : -1;
}

int TestScrapeNotFoundPersistence::metadataRows(const QString &path) {
  QSqlQuery q(m_reader);
  q.prepare(QStringLiteral("SELECT COUNT(*) FROM item_metadata WHERE path = ?"));
  q.addBindValue(path);
  return (q.exec() && q.next()) ? q.value(0).toInt() : -1;
}

qint64 TestScrapeNotFoundPersistence::stampRowId(const QString &path, const QString &provider) {
  QSqlQuery q(m_reader);
  q.prepare(QStringLiteral("SELECT id FROM scrape_not_found "
                           "WHERE collection_uuid = ? AND path = ? AND provider = ?"));
  q.addBindValue(kUuid);
  q.addBindValue(path);
  q.addBindValue(provider);
  return (q.exec() && q.next()) ? q.value(0).toLongLong() : -1;
}

void TestScrapeNotFoundPersistence::seedStamp(const QString &path, const QString &provider,
                                              const QString &key, const QDateTime &at) {
  Stamp s;
  s.collectionUuid = kUuid;
  s.path = path;
  s.provider = provider;
  s.lookupKey = key;
  s.missedAt = at;
  QVERIFY(ScrapeNotFoundStore::record(m_reader, s).isOk());
}

void TestScrapeNotFoundPersistence::genuineNotFoundRecordsOneScopedStamp() {
  const QByteArray bytes("unknown homebrew rom image");
  const QString path = writeRom(QStringLiteral("Unknown Hack (J).sfc"), bytes);
  const QDateTime before = QDateTime::currentDateTimeUtc();
  const Run run = runBatch({path}, [](const QUrl &) { return httpError(404); });
  const QDateTime after = QDateTime::currentDateTimeUtc();
  QVERIFY(run.finished);
  QCOMPARE(run.summary.notFound, 1);
  QCOMPARE(run.summary.errors, 0);
  QCOMPARE(run.urls.size(), 1);

  QCOMPARE(stampCount(), 1);
  const auto stamp = stampFor(path);
  QVERIFY(stamp.has_value());
  QCOMPARE(stamp->collectionUuid, kUuid);
  QCOMPARE(stamp->path, path);
  QCOMPARE(stamp->provider, kSs);
  // The key is exactly what was sent: systemeid, romnom and the real hashes.
  QCOMPARE(stamp->lookupKey, ScreenScraperUrls::jeuInfosLookupKey(run.urls.first()));
  const QString key = stamp->lookupKey;
  // romnom is the runner's query (the file's complete base name), encoded.
  QVERIFY2(key.startsWith(QStringLiteral("systemeid=4&romnom=Unknown%20Hack%20%28J%29&")),
           qPrintable(key));
  const QString md5 =
      QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex());
  const QString sha1 =
      QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
  QVERIFY2(key.contains(QStringLiteral("md5=") + md5, Qt::CaseInsensitive), qPrintable(key));
  QVERIFY2(key.contains(QStringLiteral("sha1=") + sha1, Qt::CaseInsensitive), qPrintable(key));
  QVERIFY2(key.contains(QStringLiteral("crc=")), qPrintable(key));
  // Never credentials or the authenticated URL.
  QVERIFY(!key.contains(kDevId));
  QVERIFY(!key.contains(kDevPw));
  QVERIFY(!key.contains(QStringLiteral("devpassword")));
  QVERIFY(!key.contains(QStringLiteral("http")));
  // UTC timestamp taken when the answer arrived.
  QCOMPARE(stamp->missedAt.timeSpec(), Qt::UTC);
  QVERIFY(stamp->missedAt >= before.addMSecs(-1));
  QVERIFY(stamp->missedAt <= after.addMSecs(1));
  // A miss is remembered without creating metadata.
  QCOMPARE(metadataRows(path), 0);
}

void TestScrapeNotFoundPersistence::emptyNoMatchRecordsStamp() {
  const QString path = writeRom(QStringLiteral("Translation (T+Eng).sfc"), "translated");
  const Run run = runBatch({path}, [](const QUrl &) { return emptyNoMatch(); });
  QVERIFY(run.finished);
  QCOMPARE(run.summary.notFound, 1);
  QCOMPARE(run.summary.errors, 0);
  const auto stamp = stampFor(path);
  QVERIFY(stamp.has_value());
  QCOMPARE(stamp->lookupKey, ScreenScraperUrls::jeuInfosLookupKey(run.urls.first()));
  QCOMPARE(stampCount(), 1);
  QCOMPARE(metadataRows(path), 0);
}

void TestScrapeNotFoundPersistence::repeatedMissUpdatesSameIdentityAndKeysFollowInputs() {
  const QString path = writeRom(QStringLiteral("Bad Dump.sfc"), "first dump contents");
  const QString otherPath = writeRom(QStringLiteral("Other Hack.sfc"), "first dump contents");
  auto notFound = [](const QUrl &) { return httpError(404); };

  QVERIFY(runBatch({path, otherPath}, notFound).finished);
  QCOMPARE(stampCount(), 2);
  const auto first = stampFor(path);
  const auto other = stampFor(otherPath);
  QVERIFY(first.has_value() && other.has_value());
  // Same bytes, different romnom → distinct keys.
  QVERIFY(first->lookupKey != other->lookupKey);
  const qint64 rowId = stampRowId(path, kSs);
  QVERIFY(rowId > 0);

  // Same inputs again: same row, same key, refreshed time.
  QTest::qWait(5);
  QVERIFY(runBatch({path}, notFound).finished);
  const auto second = stampFor(path);
  QVERIFY(second.has_value());
  QCOMPARE(stampCount(), 2);
  QCOMPARE(stampRowId(path, kSs), rowId);
  QCOMPARE(second->lookupKey, first->lookupKey);
  QVERIFY(second->missedAt > first->missedAt);

  // Changed system → distinct key, still one row for the identity.
  m_cfg.scraperOverrides.screenscraperSystemId = 1;
  QVERIFY(runBatch({path}, notFound).finished);
  const auto system = stampFor(path);
  QVERIFY(system.has_value());
  QVERIFY(system->lookupKey.startsWith(QStringLiteral("systemeid=1&")));
  QVERIFY(system->lookupKey != first->lookupKey);
  QCOMPARE(stampRowId(path, kSs), rowId);

  // Changed contents (different hashes and size) → distinct key again.
  QVERIFY(!writeRom(QStringLiteral("Bad Dump.sfc"), "a re-dumped, longer image").isEmpty());
  QVERIFY(runBatch({path}, notFound).finished);
  const auto rehashed = stampFor(path);
  QVERIFY(rehashed.has_value());
  QVERIFY(rehashed->lookupKey != system->lookupKey);
  QCOMPARE(stampRowId(path, kSs), rowId);
  QCOMPARE(stampCount(), 2);
  // The other item's stamp was never touched by these runs.
  QCOMPARE(stampFor(otherPath)->lookupKey, other->lookupKey);
  QCOMPARE(stampFor(otherPath)->missedAt, other->missedAt);
}

void TestScrapeNotFoundPersistence::successClearsOnlyThatProvidersStamp() {
  const QString path = writeRom(QStringLiteral("Real Game (U).sfc"), "real game");
  const QString otherPath = writeRom(QStringLiteral("Still Unknown.sfc"), "unknown");
  const QDateTime at = QDateTime::currentDateTimeUtc().addDays(-3);
  seedStamp(path, kSs, QStringLiteral("old-ss-key"), at);
  seedStamp(path, kOtherProvider, QStringLiteral("other-provider-key"), at);
  seedStamp(otherPath, kSs, QStringLiteral("other-item-key"), at);

  const Run run = runBatch({path}, [](const QUrl &) { return validMatch(); });
  QVERIFY(run.finished);
  QCOMPARE(run.summary.scraped, 1);
  QCOMPARE(run.summary.errors, 0);

  QVERIFY(!stampFor(path).has_value());
  const auto otherProvider = stampFor(path, kOtherProvider);
  QVERIFY(otherProvider.has_value());
  QCOMPARE(otherProvider->lookupKey, QStringLiteral("other-provider-key"));
  QCOMPARE(otherProvider->missedAt, at);
  const auto otherItem = stampFor(otherPath);
  QVERIFY(otherItem.has_value());
  QCOMPARE(otherItem->lookupKey, QStringLiteral("other-item-key"));
  QCOMPARE(stampCount(), 2);
  QCOMPARE(metadataRows(path), 1);
}

void TestScrapeNotFoundPersistence::transientAndQuotaFailuresNeverTouchStamps_data() {
  QTest::addColumn<int>("status"); // 0 = transfer timeout
  QTest::newRow("430 daily quota") << 430;
  QTest::newRow("431 failed-lookup quota") << 431;
  QTest::newRow("429 rate limited") << 429;
  QTest::newRow("500 server error") << 500;
  QTest::newRow("503 unavailable") << 503;
  QTest::newRow("timeout") << 0;
}

void TestScrapeNotFoundPersistence::transientAndQuotaFailuresNeverTouchStamps() {
  QFETCH(int, status);
  const QString fresh = writeRom(QStringLiteral("Never Stamped.sfc"), "fresh");
  const QString stamped = writeRom(QStringLiteral("Already Stamped.sfc"), "stamped");
  const QDateTime at = QDateTime::currentDateTimeUtc().addDays(-7);
  seedStamp(stamped, kSs, QStringLiteral("pre-existing-key"), at);

  const Run run = runBatch({fresh, stamped}, [status](const QUrl &) {
    return status == 0 ? timeoutError() : httpError(status);
  });
  QVERIFY(run.finished);
  QCOMPARE(run.summary.notFound, 0);
  QVERIFY(!run.urls.isEmpty());

  QVERIFY(!stampFor(fresh).has_value());
  const auto kept = stampFor(stamped);
  QVERIFY(kept.has_value());
  QCOMPARE(kept->lookupKey, QStringLiteral("pre-existing-key"));
  QCOMPARE(kept->missedAt, at);
  QCOMPARE(stampCount(), 1);
  QCOMPARE(metadataRows(fresh), 0);
}

void TestScrapeNotFoundPersistence::malformedRepliesRecordNothing_data() {
  QTest::addColumn<QByteArray>("body");
  QTest::newRow("not json") << QByteArray("<html>gateway</html>");
  QTest::newRow("truncated json") << QByteArray(R"({"response":{"jeu":)");
  QTest::newRow("object without response") << QByteArray("{}");
  QTest::newRow("Erreur body") << QByteArray("Erreur de login : Verifier vos identifiants !");
}

void TestScrapeNotFoundPersistence::malformedRepliesRecordNothing() {
  QFETCH(QByteArray, body);
  const QString fresh = writeRom(QStringLiteral("Malformed Fresh.sfc"), "fresh");
  const QString stamped = writeRom(QStringLiteral("Malformed Stamped.sfc"), "stamped");
  const QDateTime at = QDateTime::currentDateTimeUtc().addDays(-2);
  seedStamp(stamped, kSs, QStringLiteral("pre-existing-key"), at);

  const Run run = runBatch({fresh, stamped}, [body](const QUrl &) { return Reply(body); });
  QVERIFY(run.finished);
  QCOMPARE(run.summary.scraped, 0);

  QVERIFY(!stampFor(fresh).has_value());
  const auto kept = stampFor(stamped);
  QVERIFY(kept.has_value());
  QCOMPARE(kept->lookupKey, QStringLiteral("pre-existing-key"));
  QCOMPARE(kept->missedAt, at);
  QCOMPARE(stampCount(), 1);
}

void TestScrapeNotFoundPersistence::cancellationRecordsNothing() {
  // The 404 arrives only after the user cancelled: the provider vouches for
  // the miss, but the runner discards the answer, so nothing is stored.
  const QString path = writeRom(QStringLiteral("Cancelled.sfc"), "cancelled");
  Scraper::HttpClient::ResponseCallback held;
  ProviderBase::setFetchFunctionForTesting(
      [&held](const QUrl &, const Scraper::HttpClient::RawHeaders &,
              Scraper::HttpClient::ResponseCallback callback,
              const QStringList &) { held = std::move(callback); });
  auto provider = std::make_shared<ScreenScraperProvider>([this]() { return &m_settings; },
                                                          [this]() { return &m_cfg; });
  bool finished = false;
  Scraper::BatchScrapeRunner::Summary summary;
  {
    Scraper::BatchScrapeRunner runner(&m_ctx, provider, kUuid, {path}, m_roms->path(),
                                      /*fetchPrimaryCover=*/false);
    QObject::connect(&runner, &Scraper::BatchScrapeRunner::finished,
                     [&](const Scraper::BatchScrapeRunner::Summary &s) {
                       summary = s;
                       finished = true;
                     });
    runner.start();
    QTRY_VERIFY_WITH_TIMEOUT(static_cast<bool>(held), 10000);
    runner.cancel();
    held(httpError(404));
    QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
  }
  ProviderBase::setFetchFunctionForTesting({});
  QCOMPARE(summary.notFound, 0);
  QVERIFY(!stampFor(path).has_value());
  QCOMPARE(stampCount(), 0);
  // The discarded key does not leak into the next lookup of the same item.
  QVERIFY(!provider->takeNoEntryLookupKey(path).has_value());
}

QTEST_GUILESS_MAIN(TestScrapeNotFoundPersistence)
#include "test_scrapenotfoundpersistence.moc"
