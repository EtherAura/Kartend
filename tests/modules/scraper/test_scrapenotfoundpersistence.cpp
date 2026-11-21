/**
 * @file test_scrapenotfoundpersistence.cpp
 * @brief ScreenScraper not-found stamps recorded by the production batch path
 *        (Kartend-bb8df, recording slice).
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
 * connection after the runner's `finished` and destruction, so a stamp that
 * was only queued (not stored) fails the test.
 *
 * QTEST_GUILESS_MAIN creates a plain QCoreApplication: every timer context
 * here is the typed QCoreApplication::instance(), never qApp (which a
 * transitively included QApplication header turns into a bad downcast).
 */

#include <functional>
#include <memory>
#include <optional>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
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
const QString kSeedKey = QStringLiteral("systemeid=4&romnom=Seeded%20Key&crc=DEADBEEF");

using Reply = ErrorUtils::Result<QByteArray>;

/// A fixed UTC instant with a non-zero millisecond part, so any loss of
/// fractional seconds in the store would show up as a mismatch.
QDateTime seededAt() {
  return QDateTime::fromMSecsSinceEpoch(1700000000123LL, Qt::UTC);
}

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

ApplicationContext makeCtx(SessionManager *session) {
  ApplicationContext ctx;
  ctx.managers.sessionManager = session;
  return ctx;
}

/// Delivers @p callback asynchronously on the live QCoreApplication, like
/// HttpClient does. The context is the typed instance(), not qApp.
void deliverLater(Scraper::HttpClient::ResponseCallback callback, Reply answer) {
  QCoreApplication *const app = QCoreApplication::instance();
  Q_ASSERT(app);
  QTimer::singleShot(
      0, app, [callback = std::move(callback), answer]() mutable { callback(std::move(answer)); });
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
  void validEmptyNoMatchRecordsStamp_data();
  void validEmptyNoMatchRecordsStamp();
  void platformIdentityControlsPersistence_data();
  void platformIdentityControlsPersistence();
  void repeatedMissUpdatesSameIdentityAndKeysFollowInputs();
  void transientAndQuotaFailuresNeverTouchStamps_data();
  void transientAndQuotaFailuresNeverTouchStamps();
  void malformedRepliesRecordNothing_data();
  void malformedRepliesRecordNothing();
  void cancellationRecordsNothing_data();
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
  /// Raw stored text of the two persisted stamp columns, read straight from
  /// SQL (independent of ScrapeNotFoundStore::load's parsing).
  QPair<QString, QString> rawStamp(const QString &path, const QString &provider);
  void seedStamp(const QString &path, const QString &provider, const QString &key,
                 const QDateTime &at);
  /// Seeded stamp still byte-for-byte the seeded one: same row, same key,
  /// same UTC instant down to the millisecond.
  void verifySeedUntouched(const QString &path, qint64 rowId, const QPair<QString, QString> &raw);
  /// Only the ROMs this test wrote exist in the ROM/artwork dir: no sidecar.
  void verifyNoSidecar(int romCount);

  std::unique_ptr<SessionManager> m_session;
  ApplicationContext m_ctx;
  std::unique_ptr<DatabaseManager> m_db;
  QSqlDatabase m_reader;
  std::unique_ptr<QTemporaryDir> m_roms;
  GeneralSettings m_settings;
  CollectionConfig m_cfg;
};

void TestScrapeNotFoundPersistence::initTestCase() {
  // QTEST_GUILESS_MAIN: the live application is a plain QCoreApplication.
  QCoreApplication *const app = QCoreApplication::instance();
  QVERIFY(app != nullptr);
  QCOMPARE(app->metaObject()->className(), "QCoreApplication");

  KartendTest::initSandboxedTestCase(QStringLiteral("kartend-test-scrapenotfound-persistence"));
  // Offline catalogs: a fresh on-disk cache short-circuits both fetches.
  ScreenScraperSystems::System snes;
  snes.id = 4;
  snes.displayName = QStringLiteral("Super Nintendo");
  snes.aliases = {QStringLiteral("snes")};
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
  m_settings.scraper.options.hashMode = ScraperOptions::ScraperHashMode::Always;
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
        deliverLater(std::move(callback), reply(url));
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
  // Drain any reply still queued on the application so none outlives `run`.
  QCoreApplication::processEvents();
  ProviderBase::setFetchFunctionForTesting({});
  // No key may linger in the provider for any item of the batch.
  for (const QString &p : paths) {
    if (provider->takeNoEntryLookupKey(p).has_value()) run.finished = false;
  }
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

QPair<QString, QString> TestScrapeNotFoundPersistence::rawStamp(const QString &path,
                                                                const QString &provider) {
  QSqlQuery q(m_reader);
  q.prepare(QStringLiteral("SELECT lookup_key, missed_at FROM scrape_not_found "
                           "WHERE collection_uuid = ? AND path = ? AND provider = ?"));
  q.addBindValue(kUuid);
  q.addBindValue(path);
  q.addBindValue(provider);
  if (!q.exec() || !q.next()) return {};
  return {q.value(0).toString(), q.value(1).toString()};
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

void TestScrapeNotFoundPersistence::verifySeedUntouched(const QString &path, qint64 rowId,
                                                        const QPair<QString, QString> &raw) {
  QCOMPARE(stampRowId(path, kSs), rowId);
  QCOMPARE(rawStamp(path, kSs), raw);
  const auto kept = stampFor(path);
  QVERIFY(kept.has_value());
  QCOMPARE(kept->lookupKey, kSeedKey);
  QCOMPARE(kept->missedAt.toMSecsSinceEpoch(), seededAt().toMSecsSinceEpoch());
  QCOMPARE(kept->missedAt.timeSpec(), Qt::UTC);
  QCOMPARE(metadataRows(path), 0);
}

void TestScrapeNotFoundPersistence::verifyNoSidecar(int romCount) {
  const QStringList entries =
      QDir(m_roms->path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
  QVERIFY2(entries.size() == romCount, qPrintable(entries.join(QLatin1Char(','))));
}

void TestScrapeNotFoundPersistence::genuineNotFoundRecordsOneScopedStamp() {
  const QByteArray bytes("unknown homebrew rom image");
  const QString path = writeRom(QStringLiteral("Unknown Hack (J).sfc"), bytes);
  // A stamp of another provider for the same item stays untouched.
  seedStamp(path, kOtherProvider, kSeedKey, seededAt());
  const QPair<QString, QString> otherRaw = rawStamp(path, kOtherProvider);
  const QDateTime before = QDateTime::currentDateTimeUtc();
  const Run run = runBatch({path}, [](const QUrl &) { return httpError(404); });
  const QDateTime after = QDateTime::currentDateTimeUtc();
  QVERIFY(run.finished);
  QCOMPARE(run.summary.notFound, 1);
  QCOMPARE(run.summary.errors, 0);
  QCOMPARE(run.urls.size(), 1);

  QCOMPARE(stampCount(), 2);
  QCOMPARE(rawStamp(path, kOtherProvider), otherRaw);
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
  QVERIFY2(key.endsWith(QStringLiteral("&romtaille=") + QString::number(bytes.size())),
           qPrintable(key));
  // Never credentials or the authenticated URL.
  QVERIFY(!key.contains(kDevId));
  QVERIFY(!key.contains(kDevPw));
  QVERIFY(!key.contains(QStringLiteral("devpassword")));
  QVERIFY(!key.contains(QStringLiteral("devid")));
  QVERIFY(!key.contains(QStringLiteral("http")));
  // UTC timestamp taken when the answer arrived.
  QCOMPARE(stamp->missedAt.timeSpec(), Qt::UTC);
  QVERIFY(stamp->missedAt >= before.addMSecs(-1));
  QVERIFY(stamp->missedAt <= after.addMSecs(1));
  // A miss is remembered without creating metadata or a sidecar.
  QCOMPARE(metadataRows(path), 0);
  verifyNoSidecar(1);
}

void TestScrapeNotFoundPersistence::validEmptyNoMatchRecordsStamp_data() {
  QTest::addColumn<QByteArray>("body");
  QTest::newRow("response without jeu")
      << QByteArray(R"({"header":{"success":"true"},"response":{"ssuser":{"id":"x"}}})");
  QTest::newRow("jeu null") << QByteArray(R"({"response":{"jeu":null}})");
  QTest::newRow("jeu empty object") << QByteArray(R"({"response":{"jeu":{}}})");
  QTest::newRow("jeu empty array") << QByteArray(R"({"response":{"jeu":[]}})");
}

void TestScrapeNotFoundPersistence::validEmptyNoMatchRecordsStamp() {
  QFETCH(QByteArray, body);
  const QString path = writeRom(QStringLiteral("Translation (T+Eng).sfc"), "translated");
  const Run run = runBatch({path}, [body](const QUrl &) { return Reply(body); });
  QVERIFY(run.finished);
  QCOMPARE(run.summary.notFound, 1);
  QCOMPARE(run.summary.errors, 0);
  const auto stamp = stampFor(path);
  QVERIFY(stamp.has_value());
  QCOMPARE(stamp->lookupKey, ScreenScraperUrls::jeuInfosLookupKey(run.urls.first()));
  QCOMPARE(stampCount(), 1);
  QCOMPARE(metadataRows(path), 0);
  verifyNoSidecar(1);
}

void TestScrapeNotFoundPersistence::platformIdentityControlsPersistence_data() {
  QTest::addColumn<int>("requestSystem");
  QTest::addColumn<QByteArray>("system");
  QTest::addColumn<QByteArray>("rom");
  QTest::addColumn<bool>("sendHash");
  QTest::addColumn<bool>("accept");

  QTest::newRow("same numeric system despite conflicting hash")
      << 4 << QByteArray(R"({"id":4})") << QByteArray(R"({"romsha1":"bad"})") << true << true;
  QTest::newRow("same system through collection alias")
      << -1 << QByteArray(R"({"id":"004"})") << QByteArray("{}") << true << true;
  QTest::newRow("declared arcade child")
      << 75 << QByteArray(R"({"id":"56","parentid":75})") << QByteArray("{}") << true << true;
  QTest::newRow("unrelated arcade system")
      << 75 << QByteArray(R"({"id":"56","parentid":0})") << QByteArray("{}") << true << false;
  QTest::newRow("console parent is not an alias for add-on")
      << 1 << QByteArray(R"({"id":20,"parentid":1})") << QByteArray("{}") << true << false;
  QTest::newRow("any system hash lookup")
      << 0 << QByteArray(R"({"id":3})") << QByteArray("{}") << true << true;
  QTest::newRow("missing system") << 4 << QByteArray() << QByteArray("{}") << true << true;
  QTest::newRow("malformed system id is unknown")
      << 4 << QByteArray(R"({"id":3.5})") << QByteArray("{}") << true << true;
  QTest::newRow("wrong platform with identical name only")
      << 106 << QByteArray(R"({"id":3,"parentid":0})")
      << QByteArray(R"({"romfilename":"Synthetic.sfc"})") << false << false;
  QTest::newRow("wrong platform with matching SHA-1")
      << 106 << QByteArray(R"({"id":3})") << QByteArray(R"({"romsha1":"$sha1","rommd5":"bad"})")
      << true << true;
  QTest::newRow("weak hash cannot overrule a stronger mismatch")
      << 106 << QByteArray(R"({"id":3})")
      << QByteArray(R"({"romsha1":"bad","rommd5":"$md5","romcrc":"$crc"})") << true << false;
  QTest::newRow("wrong platform with matching MD5")
      << 106 << QByteArray(R"({"id":3})") << QByteArray(R"({"rommd5":"$md5"})") << true << true;
  QTest::newRow("wrong platform with matching CRC")
      << 106 << QByteArray(R"({"id":3})") << QByteArray(R"({"romcrc":"$crc"})") << true << true;
  QTest::newRow("wrong platform with different CRC")
      << 106 << QByteArray(R"({"id":3})") << QByteArray(R"({"romcrc":"DEADBEEF"})") << true
      << false;
}

void TestScrapeNotFoundPersistence::platformIdentityControlsPersistence() {
  QFETCH(int, requestSystem);
  QFETCH(QByteArray, system);
  QFETCH(QByteArray, rom);
  QFETCH(bool, sendHash);
  QFETCH(bool, accept);
  m_cfg.scraperOverrides.screenscraperSystemId = requestSystem;
  if (!sendHash) m_settings.scraper.options.hashMode = ScraperOptions::ScraperHashMode::Never;
  const QString path = writeRom(QStringLiteral("Synthetic.sfc"), "synthetic ROM contents");
  QVERIFY(!path.isEmpty());

  const Run run = runBatch({path}, [system, rom](const QUrl &url) mutable {
    const QUrlQuery query(url);
    rom.replace("$sha1", query.queryItemValue(QStringLiteral("sha1")).toLatin1().toUpper());
    rom.replace("$md5", query.queryItemValue(QStringLiteral("md5")).toLatin1().toUpper());
    rom.replace("$crc", query.queryItemValue(QStringLiteral("crc")).toLatin1().toUpper());
    QByteArray payload =
        R"({"response":{"jeu":{"id":"42","noms":[{"region":"wor","text":"Synthetic"}])";
    if (!system.isEmpty()) payload += R"(,"systeme":)" + system;
    return Reply(payload + R"(,"rom":)" + rom + "}}}");
  });
  QVERIFY(run.finished);
  QCOMPARE(run.urls.size(), 1);
  QCOMPARE(QUrlQuery(run.urls.first()).queryItemValue(QStringLiteral("systemeid")),
           QString::number(requestSystem == -1 ? 4 : requestSystem));
  QCOMPARE(QUrlQuery(run.urls.first()).hasQueryItem(QStringLiteral("sha1")), sendHash);
  QCOMPARE(run.summary.errors, 0);
  QCOMPARE(metadataRows(path), accept ? 1 : 0);
  QCOMPARE(run.summary.notFound, accept ? 0 : 1);
  QCOMPARE(run.summary.scraped, accept ? 1 : 0);
  QCOMPARE(stampCount(), accept ? 0 : 1);
  if (!accept) {
    const auto stamp = stampFor(path);
    QVERIFY(stamp.has_value());
    QCOMPARE(stamp->lookupKey, ScreenScraperUrls::jeuInfosLookupKey(run.urls.first()));
    verifyNoSidecar(1);
  }
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
  const QPair<QString, QString> otherRaw = rawStamp(otherPath, kSs);

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

  // Changed name (romnom) for the same contents → distinct key.
  QVERIFY(QFile::rename(path, m_roms->filePath(QStringLiteral("Renamed Dump.sfc"))));
  const QString renamed = m_roms->filePath(QStringLiteral("Renamed Dump.sfc"));
  QVERIFY(runBatch({renamed}, notFound).finished);
  const auto byName = stampFor(renamed);
  QVERIFY(byName.has_value());
  QVERIFY(byName->lookupKey.startsWith(QStringLiteral("systemeid=1&romnom=Renamed%20Dump&")));
  QVERIFY(byName->lookupKey != rehashed->lookupKey);
  QCOMPARE(stampCount(), 3);
  // The other item's stamp was never touched by these runs.
  QCOMPARE(rawStamp(otherPath, kSs), otherRaw);
  verifyNoSidecar(2);
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
  seedStamp(stamped, kSs, kSeedKey, seededAt());
  const qint64 rowId = stampRowId(stamped, kSs);
  const QPair<QString, QString> raw = rawStamp(stamped, kSs);
  QVERIFY(rowId > 0);

  const Run run = runBatch({fresh, stamped}, [status](const QUrl &) {
    return status == 0 ? timeoutError() : httpError(status);
  });
  QVERIFY(run.finished);
  QCOMPARE(run.summary.notFound, 0);
  QCOMPARE(run.summary.scraped, 0);
  QVERIFY(!run.urls.isEmpty());

  QVERIFY(!stampFor(fresh).has_value());
  QCOMPARE(metadataRows(fresh), 0);
  verifySeedUntouched(stamped, rowId, raw);
  QCOMPARE(stampCount(), 1);
  verifyNoSidecar(2);
}

void TestScrapeNotFoundPersistence::malformedRepliesRecordNothing_data() {
  QTest::addColumn<QByteArray>("body");
  QTest::addColumn<bool>("countsAsNotFound"); // reporting stays as before
  QTest::newRow("not json") << QByteArray("<html>gateway</html>") << false;
  QTest::newRow("truncated json") << QByteArray(R"({"response":{"jeu":)") << false;
  QTest::newRow("Erreur body") << QByteArray("Erreur de login : Verifier vos identifiants !")
                               << false;
  // Parsed by the legacy parser as "no match" (benign empty list, counted as
  // not found), but not a structurally valid no-entry answer → no stamp.
  QTest::newRow("object without response") << QByteArray("{}") << true;
  QTest::newRow("response not an object") << QByteArray(R"({"response":"oops"})") << true;
  QTest::newRow("jeu number") << QByteArray(R"({"response":{"jeu":42}})") << true;
  QTest::newRow("jeu string") << QByteArray(R"({"response":{"jeu":"5555"}})") << true;
  QTest::newRow("jeu boolean") << QByteArray(R"({"response":{"jeu":true}})") << true;
  QTest::newRow("jeu nonempty array")
      << QByteArray(R"({"response":{"jeu":[{"id":"5555"}]}})") << true;
}

void TestScrapeNotFoundPersistence::malformedRepliesRecordNothing() {
  QFETCH(QByteArray, body);
  QFETCH(bool, countsAsNotFound);
  const QString fresh = writeRom(QStringLiteral("Malformed Fresh.sfc"), "fresh");
  const QString stamped = writeRom(QStringLiteral("Malformed Stamped.sfc"), "stamped");
  seedStamp(stamped, kSs, kSeedKey, seededAt());
  const qint64 rowId = stampRowId(stamped, kSs);
  const QPair<QString, QString> raw = rawStamp(stamped, kSs);
  QVERIFY(rowId > 0);

  const Run run = runBatch({fresh, stamped}, [body](const QUrl &) { return Reply(body); });
  QVERIFY(run.finished);
  QCOMPARE(run.summary.scraped, 0);
  QCOMPARE(run.summary.notFound, countsAsNotFound ? 2 : 0);

  QVERIFY(!stampFor(fresh).has_value());
  QCOMPARE(metadataRows(fresh), 0);
  verifySeedUntouched(stamped, rowId, raw);
  QCOMPARE(stampCount(), 1);
  verifyNoSidecar(2);
}

void TestScrapeNotFoundPersistence::cancellationRecordsNothing_data() {
  QTest::addColumn<bool>("seeded");
  QTest::newRow("fresh item") << false;
  QTest::newRow("seeded item") << true;
}

void TestScrapeNotFoundPersistence::cancellationRecordsNothing() {
  // The 404 arrives only after the user cancelled: the provider vouches for
  // the miss, but the runner discards the answer, so nothing is stored.
  QFETCH(bool, seeded);
  const QString path = writeRom(QStringLiteral("Cancelled.sfc"), "cancelled");
  qint64 rowId = -1;
  QPair<QString, QString> raw;
  if (seeded) {
    seedStamp(path, kSs, kSeedKey, seededAt());
    rowId = stampRowId(path, kSs);
    raw = rawStamp(path, kSs);
    QVERIFY(rowId > 0);
  }
  const int rowsBefore = stampCount();
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
    // Delayed 404, delivered after cancel through the live application.
    deliverLater(std::move(held), httpError(404));
    held = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
    QCoreApplication::processEvents();
  }
  ProviderBase::setFetchFunctionForTesting({});
  QCOMPARE(summary.notFound, 0);
  QCOMPARE(stampCount(), rowsBefore);
  if (seeded) {
    verifySeedUntouched(path, rowId, raw);
  } else {
    QVERIFY(!stampFor(path).has_value());
    QCOMPARE(stampCount(), 0);
  }
  // The discarded key does not linger in the provider.
  QVERIFY(!provider->takeNoEntryLookupKey(path).has_value());
  verifyNoSidecar(1);
}

QTEST_GUILESS_MAIN(TestScrapeNotFoundPersistence)
#include "test_scrapenotfoundpersistence.moc"
