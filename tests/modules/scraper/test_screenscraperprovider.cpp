// Tests for ScreenScraperProvider's request-URL construction (Kartend-hsboz).
//
// buildJeuInfosUrl turns (credentials, romnom, systemeid, hashes, hasUser)
// into the jeuInfos.php query. It lives in the ScreenScraperUrls namespace
// (screenscraperurls.{h,cpp}) precisely so the SS query shape can be
// regression-tested without the network or a provider instance; the
// provider instance here only backs the entity/hash-cache cases.
//
// What matters and is covered here: dev credentials always ride, user
// credentials only when hasUser; system/rom and the static softname/output
// params map correctly; md5/sha1/crc/romtaille appear only when the hash
// fields are populated; the endpoint stays https + the api host with
// credentials in the query (never the path); and a credential containing
// query delimiters cannot inject or override other params.
#include <atomic>
#include <memory>
#include <optional>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "dbmigrations.h"
#include "filehashcache.h"
#include "romhasher.h"
#include "scrapertypes.h"
#include "screenscrapercatalogmanager.h"
#include "screenscraperprovider.h"
#include "screenscrapersystemcache.h"
#include "screenscraperurls.h"

class TestScreenScraperProvider : public QObject {
  Q_OBJECT

  using Credentials = ScreenScraperCatalogManager::Credentials;

  QUrlQuery build(const Credentials &creds, const QString &romnom, int systemeid,
                  const RomHasher::Result &hashes, bool hasUser) {
    return QUrlQuery(
        ScreenScraperUrls::buildJeuInfosUrl(creds, romnom, systemeid, hashes, hasUser));
  }

private slots:
  void initTestCase();
  void devCredentials_alwaysPresent_userCredentials_gatedByHasUser();
  void systemAndRom_mapToExpectedParams();
  void hashes_appearOnlyWhenSet();
  void endpointIsHttpsApiHost_andCredentialsStayInQueryNotPath();
  void credentialWithReservedChars_doesNotInjectExtraParams();

  // Kartend-3p42r: the FileHashCache hash-reuse path.
  void hashRegularFileCached_cacheHit_returnsCachedNotRehashed();
  void hashRegularFileCached_cacheMiss_hashesAndStores();

  // Kartend-jjyst.6: cancel/skip observed by the hash continuation.
  void lookup_cancelledDuringHash_resolvesCancelledWithoutRequest();

  // Kartend-ckepd.4: platform-entity scraping.
  void supportedEntities_includesPlatform();
  void buildSystemeMediaUrl_mapsParamsAndGatesUser();
  void fetchEntity_userCredsRequireBothIdAndPassword();
  void fetchEntity_platformAssetShapeRolesAndScopeKey();
  void fetchEntity_catalogSkipsTypesTheSystemDoesNotHave();
  void fetchEntity_catalogHonorsPreferredRegion();
  void fetchEntity_absentIdInNonEmptyCatalogIsNotFound();
  void fetchEntity_nonPlatformTypeIsInvalidArgument();
  // Kartend-ckepd.6: empty-identity autodetect via the collection accessor.
  void fetchEntity_emptyIdentityHonorsCollectionSystemIdOverride();
  void fetchEntity_emptyIdentityUnresolvableIsNotFound();

private:
  std::unique_ptr<ScreenScraperProvider> m_provider;
};

void TestScreenScraperProvider::initTestCase() {
  // Empty accessors: the URL builders live in ScreenScraperUrls and take
  // credentials explicitly; the constructor only forwards a null settings
  // pointer to registerHostThrottles.
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

  const QUrl url =
      ScreenScraperUrls::buildJeuInfosUrl(creds, QStringLiteral("rom"), 1, hashes, false);
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

// Kartend-3p42r: a cache HIT must return the stored hashes without re-hashing.
// We plant a DELIBERATELY WRONG hash for an unchanged file, then assert the
// helper hands it back — only possible if it consulted FileHashCache instead
// of running RomHasher (whose real digest would differ).
void TestScreenScraperProvider::hashRegularFileCached_cacheHit_returnsCachedNotRehashed() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString romPath = dir.filePath(QStringLiteral("game.rom"));
  {
    QFile f(romPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArrayLiteral("real-rom-bytes"));
    f.close();
  }
  const QFileInfo fi(romPath);

  // Real main DB at "<dir>/media.db" with the real file_hash_cache schema — no
  // mocking. DatabaseSchema::openConnection (used by the code under test) opens
  // that exact file, so seed it here.
  const QString seedConn = QStringLiteral("test_sshashcache_hit_seed");
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), seedConn);
    db.setDatabaseName(dir.filePath(QStringLiteral("media.db")));
    QVERIFY(db.open());
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("CREATE TABLE collections (id INTEGER PRIMARY KEY, name TEXT)")));
    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, path TEXT, last_modified TEXT)")));
    DbMigrations::applySchemaMigrations(db, seedConn);
    // hashFileCached canonicalises the lookup path, so store under the
    // canonical spelling and the file's real (size, mtime) so it validates.
    const auto stored = FileHashCache::store(
        db, fi.canonicalFilePath(), fi.size(), fi.lastModified().toMSecsSinceEpoch(),
        QStringLiteral("deadbeef"), QStringLiteral("11111111111111111111111111111111"),
        QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    QVERIFY(stored.isOk());
    db.close();
  }
  QSqlDatabase::removeDatabase(seedConn);

  const auto r = ScreenScraperProvider::hashRegularFileCached(dir.path(), romPath, {});
  QVERIFY2(r.isOk(), "cached hash lookup should succeed");
  QCOMPARE(r.value().crc, QStringLiteral("deadbeef"));
  QCOMPARE(r.value().md5, QStringLiteral("11111111111111111111111111111111"));
}

// Kartend-3p42r: a cache MISS must hash the file for real and persist the
// result so the next pass is a hit.
void TestScreenScraperProvider::hashRegularFileCached_cacheMiss_hashesAndStores() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString seedConn = QStringLiteral("test_sshashcache_miss_seed");
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), seedConn);
    db.setDatabaseName(dir.filePath(QStringLiteral("media.db")));
    QVERIFY(db.open());
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("CREATE TABLE collections (id INTEGER PRIMARY KEY, name TEXT)")));
    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, path TEXT, last_modified TEXT)")));
    DbMigrations::applySchemaMigrations(db, seedConn);
    db.close();
  }
  QSqlDatabase::removeDatabase(seedConn);

  const QString romPath = dir.filePath(QStringLiteral("fresh.rom"));
  {
    QFile f(romPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArrayLiteral("fresh-rom-bytes"));
    f.close();
  }
  const auto expected = RomHasher::hashFile(romPath);
  QVERIFY(expected.isOk());

  const auto r = ScreenScraperProvider::hashRegularFileCached(dir.path(), romPath, {});
  QVERIFY2(r.isOk(), "miss should hash the file directly");
  QCOMPARE(r.value().crc, expected.value().crc);
  QCOMPARE(r.value().sha1, expected.value().sha1);

  // The miss must have written the result back for next time.
  const QString verifyConn = QStringLiteral("test_sshashcache_verify");
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), verifyConn);
    db.setDatabaseName(dir.filePath(QStringLiteral("media.db")));
    QVERIFY(db.open());
    const QFileInfo fi(romPath);
    const auto hit = FileHashCache::lookup(db, fi.canonicalFilePath(), fi.size(),
                                           fi.lastModified().toMSecsSinceEpoch());
    QVERIFY2(hit.has_value(), "miss path should have stored the hash");
    QCOMPARE(hit->crc, expected.value().crc);
    db.close();
  }
  QSqlDatabase::removeDatabase(verifyConn);
}

// Kartend-jjyst.6: a skip/cancel token flipped before (or during) the ROM hash
// must resolve the lookup with OperationCancelled — NOT fall back to a
// filename-only jeuInfos request, which burned one quota-counting request per
// skipped item and dispatched it after the runner's clearPending already ran.
void TestScreenScraperProvider::lookup_cancelledDuringHash_resolvesCancelledWithoutRequest() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString romPath = dir.filePath(QStringLiteral("game.rom"));
  {
    QFile f(romPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArrayLiteral("rom-bytes"));
  }

  GeneralSettings settings; // default hashMode == Always → the hash path runs
  auto &blob = settings.scraper.credentials[QStringLiteral("screenscraper")];
  blob.insert(QStringLiteral("dev_id"), QStringLiteral("dev123"));
  blob.insert(QStringLiteral("dev_password"), QStringLiteral("devpw"));
  ScreenScraperProvider provider([&settings]() { return &settings; },
                                 ScreenScraperProvider::CollectionAccessor{});

  MetadataLookupProvider::LookupContext ctx;
  ctx.query = QStringLiteral("game.rom");
  ctx.filePath = romPath;
  ctx.cancelToken = std::make_shared<std::atomic<bool>>(true); // already skipped

  std::optional<ErrorUtils::Result<QList<Scraper::ScrapeCandidate>>> result;
  provider.lookup(ctx, [&result](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> r) {
    result = std::move(r);
  });
  // The hash runs on the QtConcurrent pool and the continuation lands on the
  // main thread — spin the event loop until the callback resolves. On a
  // regression this instead attempts a live systemesListe/jeuInfos round-trip
  // and the cancelled expectation below fails.
  QTRY_VERIFY(result.has_value());
  QVERIFY(result->isError());
  QCOMPARE(result->error().code, ErrorUtils::ErrorCode::OperationCancelled);
}

void TestScreenScraperProvider::supportedEntities_includesPlatform() {
  // Kartend-ckepd.4: ScreenScraper opts into Platform entity scraping (plus Game).
  const auto entities = m_provider->supportedEntities();
  QVERIFY(entities.contains(Scraper::ScrapeEntityType::Game));
  QVERIFY(entities.contains(Scraper::ScrapeEntityType::Platform));
}

void TestScreenScraperProvider::buildSystemeMediaUrl_mapsParamsAndGatesUser() {
  // The mediaSysteme.php URL carries systemeid + media type + dev creds,
  // gates user creds on hasUser, and omits output=json (it returns media
  // bytes, not JSON). Live-API verified: the media parameter must be the
  // region-qualified token — a bare "wheel" answers 200 "NOMEDIA" even for
  // systems that have the art.
  //
  // Kartend-qzk1s moved the qualifier to the CALLER: the token is passed
  // through verbatim so a catalog-supplied variant ("wheel(jp)") survives
  // instead of being overwritten with (wor).
  Credentials creds;
  creds.devId = QStringLiteral("dev123");
  creds.devPassword = QStringLiteral("devpw");
  creds.userId = QStringLiteral("user42");
  creds.userPassword = QStringLiteral("userpw");

  const QUrl url = ScreenScraperUrls::buildSystemeMediaUrl(creds, 7, QStringLiteral("wheel(wor)"),
                                                           /*hasUser=*/false);
  QCOMPARE(url.host(), QStringLiteral("api.screenscraper.fr"));
  QVERIFY(url.path().endsWith(QStringLiteral("/mediaSysteme.php")));
  const QUrlQuery q(url);
  QCOMPARE(q.queryItemValue(QStringLiteral("systemeid")), QStringLiteral("7"));
  QCOMPARE(q.queryItemValue(QStringLiteral("media")), QStringLiteral("wheel(wor)"));
  QCOMPARE(q.queryItemValue(QStringLiteral("devid")), QStringLiteral("dev123"));
  QCOMPARE(q.queryItemValue(QStringLiteral("devpassword")), QStringLiteral("devpw"));
  QVERIFY(!q.hasQueryItem(QStringLiteral("output"))); // media endpoint, not JSON
  QVERIFY(!q.hasQueryItem(QStringLiteral("ssid")));   // user creds gated off

  // hasUser=true adds ssid/sspassword.
  const QUrlQuery q2(ScreenScraperUrls::buildSystemeMediaUrl(
      creds, 7, QStringLiteral("illustration(wor)"), /*hasUser=*/true));
  QCOMPARE(q2.queryItemValue(QStringLiteral("ssid")), QStringLiteral("user42"));
  QCOMPARE(q2.queryItemValue(QStringLiteral("sspassword")), QStringLiteral("userpw"));
  QCOMPARE(q2.queryItemValue(QStringLiteral("media")), QStringLiteral("illustration(wor)"));

  // A non-world catalog token must survive verbatim — the builder no longer
  // rewrites the qualifier, which is what lets the user's region preference
  // reach platform art at all.
  const QUrlQuery q3(ScreenScraperUrls::buildSystemeMediaUrl(creds, 7, QStringLiteral("wheel(jp)"),
                                                             /*hasUser=*/false));
  QCOMPARE(q3.queryItemValue(QStringLiteral("media")), QStringLiteral("wheel(jp)"));
}

void TestScreenScraperProvider::fetchEntity_userCredsRequireBothIdAndPassword() {
  // fetchEntity must gate ssid/sspassword on BOTH fields being set — matching
  // runLookupAfterHash and the account probes. Gating on the id alone appended
  // `ssid=<id>&sspassword=` for a user with a cleared password and made SS
  // reject every platform media URL with a login error.
  //
  // Headless: seed a fresh systems catalog into the sandboxed disk cache so
  // ensureSystemsCatalog resolves synchronously with no network round-trip.
  QStandardPaths::setTestModeEnabled(true);
  const QString cachePath = ScreenScraperSystemCache::defaultCachePath();
  QVERIFY(!cachePath.isEmpty());
  ScreenScraperSystems::System sys;
  sys.id = 42;
  sys.displayName = QStringLiteral("Test Platform");
  QVERIFY(ScreenScraperSystemCache::saveSystems(cachePath, {sys}));

  GeneralSettings settings;
  auto &blob = settings.scraper.credentials[QStringLiteral("screenscraper")];
  blob.insert(QStringLiteral("dev_id"), QStringLiteral("dev123"));
  blob.insert(QStringLiteral("dev_password"), QStringLiteral("devpw"));
  blob.insert(QStringLiteral("user_id"), QStringLiteral("user42")); // password NOT set

  ScreenScraperProvider provider([&settings]() { return &settings; },
                                 ScreenScraperProvider::CollectionAccessor{});
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Platform;
  target.identity = QStringLiteral("42");

  std::optional<ErrorUtils::Result<Scraper::ScrapedItem>> result;
  provider.fetchEntity(
      target, [&result](const ErrorUtils::Result<Scraper::ScrapedItem> &r) { result = r; });
  QVERIFY2(result.has_value(),
           "fetchEntity callback did not fire synchronously from the seeded cache");
  QVERIFY2(result->isOk(), qPrintable(result->isError() ? result->error().message : QString()));
  QVERIFY(!result->value().media.isEmpty());
  for (const auto &asset : result->value().media) {
    const QUrlQuery q(asset.url);
    QCOMPARE(q.queryItemValue(QStringLiteral("devid")), QStringLiteral("dev123"));
    QVERIFY2(!q.hasQueryItem(QStringLiteral("ssid")),
             "ssid emitted for a user with a cleared password");
    QVERIFY2(!q.hasQueryItem(QStringLiteral("sspassword")),
             "empty sspassword emitted for a user with a cleared password");
  }

  // With both fields present the same provider (the accessor re-reads the
  // settings on every call) emits user credentials on every media URL.
  blob.insert(QStringLiteral("user_password"), QStringLiteral("userpw"));
  result.reset();
  provider.fetchEntity(
      target, [&result](const ErrorUtils::Result<Scraper::ScrapedItem> &r) { result = r; });
  QVERIFY(result.has_value());
  QVERIFY(result->isOk());
  QVERIFY(!result->value().media.isEmpty());
  for (const auto &asset : result->value().media) {
    const QUrlQuery q(asset.url);
    QCOMPARE(q.queryItemValue(QStringLiteral("ssid")), QStringLiteral("user42"));
    QCOMPARE(q.queryItemValue(QStringLiteral("sspassword")), QStringLiteral("userpw"));
  }

  // Tidy the sandboxed cache so later runs re-seed deterministically.
  QVERIFY(QFile::remove(cachePath));
}

void TestScreenScraperProvider::fetchEntity_platformAssetShapeRolesAndScopeKey() {
  // Kartend-ckepd.4: a successful Platform fetchEntity emits exactly one asset per
  // kPlatformMediaTypes, each stamped with the EntityArtRole + in-role priority the
  // ckepd.3 config-wiring (applyEntityArtToConfig) consumes, Platform scope, and
  // scopeKey == systemeid. Explicit identity "42" is honored over autodetect, so
  // both the media-URL systemeid and the scopeKey carry it. Headless via the seeded
  // on-disk system cache — ensureSystemsCatalog resolves synchronously, no network.
  QStandardPaths::setTestModeEnabled(true);
  const QString cachePath = ScreenScraperSystemCache::defaultCachePath();
  ScreenScraperSystems::System sys;
  sys.id = 42;
  sys.displayName = QStringLiteral("Test Platform");
  QVERIFY(ScreenScraperSystemCache::saveSystems(cachePath, {sys}));

  GeneralSettings settings;
  auto &blob = settings.scraper.credentials[QStringLiteral("screenscraper")];
  blob.insert(QStringLiteral("dev_id"), QStringLiteral("dev123"));
  blob.insert(QStringLiteral("dev_password"), QStringLiteral("devpw"));
  ScreenScraperProvider provider([&settings]() { return &settings; },
                                 ScreenScraperProvider::CollectionAccessor{});
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Platform;
  target.identity = QStringLiteral("42");

  std::optional<ErrorUtils::Result<Scraper::ScrapedItem>> result;
  provider.fetchEntity(
      target, [&result](const ErrorUtils::Result<Scraper::ScrapedItem> &r) { result = r; });
  QVERIFY2(result.has_value(), "callback did not fire synchronously from the seeded cache");
  QVERIFY2(result->isOk(), qPrintable(result->isError() ? result->error().message : QString()));
  const Scraper::ScrapedItem &item = result->value();
  QCOMPARE(item.title, QStringLiteral("Test Platform"));
  QCOMPARE(item.sourceProviderId, QStringLiteral("screenscraper"));
  // The seeded System carries NO media catalog, which is the pre-Kartend-xny9o
  // cache shape — so this also pins the speculative fallback: one asset per
  // kPlatformMediaTypes entry, each with the world-qualified token the URL
  // builder used to append itself. asset.type is the CANONICAL type (e.g.
  // "logo"), distinct from the api token ("logo-monochrome").
  struct Expect {
    const char *type;
    const char *token;
    Scraper::EntityArtRole role;
    int prio;
  };
  const Expect expected[] = {
      {"wheel", "wheel(wor)", Scraper::EntityArtRole::Logo, 0},
      {"logo", "logo-monochrome(wor)", Scraper::EntityArtRole::Logo, 1},
      {"logo-svg", "logo-svg(wor)", Scraper::EntityArtRole::None, 0},
      {"logo-monochrome-svg", "logo-monochrome-svg(wor)", Scraper::EntityArtRole::None, 0},
      {"illustration", "illustration(wor)", Scraper::EntityArtRole::Background, 0},
      {"photo", "photo(wor)", Scraper::EntityArtRole::Background, 1},
      {"background", "background(wor)", Scraper::EntityArtRole::Background, 2},
  };
  QCOMPARE(item.media.size(), static_cast<qsizetype>(std::size(expected)));
  for (int i = 0; i < static_cast<int>(std::size(expected)); ++i) {
    const Scraper::MediaAsset &a = item.media[i];
    QCOMPARE(a.type, QString::fromLatin1(expected[i].type));
    QCOMPARE(a.entityRole, expected[i].role);
    QCOMPARE(a.entityRolePriority, expected[i].prio);
    QCOMPARE(a.scope, Scraper::MediaScope::Platform);
    QCOMPARE(a.scopeKey, QStringLiteral("42"));
    const QUrlQuery q(a.url);
    QCOMPARE(q.queryItemValue(QStringLiteral("systemeid")), QStringLiteral("42"));
    QCOMPARE(q.queryItemValue(QStringLiteral("media")), QString::fromLatin1(expected[i].token));
  }
  QVERIFY(QFile::remove(cachePath));
}

void TestScreenScraperProvider::fetchEntity_catalogSkipsTypesTheSystemDoesNotHave() {
  // Kartend-qzk1s: with a media catalog present, only the types the system
  // actually carries are requested. The whole point is the ones NOT emitted —
  // each absent type used to cost a media-host request that answered NOMEDIA
  // (measured: 173 of every 1,000 on the live catalog).
  QStandardPaths::setTestModeEnabled(true);
  const QString cachePath = ScreenScraperSystemCache::defaultCachePath();
  ScreenScraperSystems::System sys;
  sys.id = 42;
  sys.displayName = QStringLiteral("Test Platform");
  // Has wheel + photo. Deliberately NO logo-monochrome, illustration, or
  // background — the three that must be skipped.
  sys.media = {
      {QStringLiteral("wheel"),
       QStringLiteral("wheel(wor)"),
       QStringLiteral("wor"),
       {},
       QStringLiteral("png"),
       {},
       {},
       {},
       false},
      {QStringLiteral("photo"),
       QStringLiteral("photo(wor)"),
       QStringLiteral("wor"),
       {},
       QStringLiteral("png"),
       {},
       {},
       {},
       false},
  };
  QVERIFY(ScreenScraperSystemCache::saveSystems(cachePath, {sys}));

  GeneralSettings settings;
  auto &blob = settings.scraper.credentials[QStringLiteral("screenscraper")];
  blob.insert(QStringLiteral("dev_id"), QStringLiteral("dev123"));
  blob.insert(QStringLiteral("dev_password"), QStringLiteral("devpw"));
  ScreenScraperProvider provider([&settings]() { return &settings; },
                                 ScreenScraperProvider::CollectionAccessor{});
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Platform;
  target.identity = QStringLiteral("42");

  std::optional<ErrorUtils::Result<Scraper::ScrapedItem>> result;
  provider.fetchEntity(
      target, [&result](const ErrorUtils::Result<Scraper::ScrapedItem> &r) { result = r; });
  QVERIFY(result.has_value());
  QVERIFY2(result->isOk(), qPrintable(result->isError() ? result->error().message : QString()));
  const auto &media = result->value().media;
  QCOMPARE(media.size(), 2);
  QCOMPARE(media[0].type, QStringLiteral("wheel"));
  QCOMPARE(media[0].entityRole, Scraper::EntityArtRole::Logo);
  QCOMPARE(media[1].type, QStringLiteral("photo"));
  QCOMPARE(media[1].entityRole, Scraper::EntityArtRole::Background);
  // The role/priority stamping is unchanged by the skipping — photo keeps its
  // in-role priority of 1 even though illustration (priority 0) was dropped,
  // so applyEntityArtToConfig still picks the best AVAILABLE candidate rather
  // than being handed a renumbered list.
  QCOMPARE(media[1].entityRolePriority, 1);
  QVERIFY(QFile::remove(cachePath));
}

void TestScreenScraperProvider::fetchEntity_catalogHonorsPreferredRegion() {
  // Kartend-qzk1s: platform art used to be pinned to (wor) because the builder
  // hardcoded the qualifier. The catalog lists every region variant SS holds,
  // so the user's configured region can finally win for platform art too.
  QStandardPaths::setTestModeEnabled(true);
  const QString cachePath = ScreenScraperSystemCache::defaultCachePath();
  ScreenScraperSystems::System sys;
  sys.id = 42;
  sys.displayName = QStringLiteral("Test Platform");
  sys.media = {
      {QStringLiteral("wheel"),
       QStringLiteral("wheel(wor)"),
       QStringLiteral("wor"),
       {},
       QStringLiteral("png"),
       {},
       {},
       {},
       false},
      {QStringLiteral("wheel"),
       QStringLiteral("wheel(jp)"),
       QStringLiteral("jp"),
       {},
       QStringLiteral("png"),
       {},
       {},
       {},
       false},
  };
  QVERIFY(ScreenScraperSystemCache::saveSystems(cachePath, {sys}));

  GeneralSettings settings;
  auto &blob = settings.scraper.credentials[QStringLiteral("screenscraper")];
  blob.insert(QStringLiteral("dev_id"), QStringLiteral("dev123"));
  blob.insert(QStringLiteral("dev_password"), QStringLiteral("devpw"));
  settings.scraper.options.preferredScraperRegion = QStringLiteral("jp");

  ScreenScraperProvider provider([&settings]() { return &settings; },
                                 ScreenScraperProvider::CollectionAccessor{});
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Platform;
  target.identity = QStringLiteral("42");

  std::optional<ErrorUtils::Result<Scraper::ScrapedItem>> result;
  provider.fetchEntity(
      target, [&result](const ErrorUtils::Result<Scraper::ScrapedItem> &r) { result = r; });
  QVERIFY(result.has_value());
  QVERIFY(result->isOk());
  QCOMPARE(result->value().media.size(), 1);
  QCOMPARE(QUrlQuery(result->value().media[0].url).queryItemValue(QStringLiteral("media")),
           QStringLiteral("wheel(jp)"));

  // A region SS has no variant for falls back to the world tag, not to nothing.
  settings.scraper.options.preferredScraperRegion = QStringLiteral("br");
  result.reset();
  provider.fetchEntity(
      target, [&result](const ErrorUtils::Result<Scraper::ScrapedItem> &r) { result = r; });
  QVERIFY(result.has_value());
  QVERIFY(result->isOk());
  QCOMPARE(QUrlQuery(result->value().media[0].url).queryItemValue(QStringLiteral("media")),
           QStringLiteral("wheel(wor)"));
  QVERIFY(QFile::remove(cachePath));
}

void TestScreenScraperProvider::fetchEntity_absentIdInNonEmptyCatalogIsNotFound() {
  // Kartend-ckepd.4 / e8aag: a NON-empty catalog that lacks the requested id is a
  // routine RemoteResourceNotFound (re-queueable), not an error. Identity MUST be
  // explicit: with a null CollectionAccessor an empty identity would fail earlier
  // at resolveSystemId()==0, so the test would pass for the wrong reason.
  QStandardPaths::setTestModeEnabled(true);
  const QString cachePath = ScreenScraperSystemCache::defaultCachePath();
  ScreenScraperSystems::System sys;
  sys.id = 42;
  sys.displayName = QStringLiteral("Test Platform");
  QVERIFY(ScreenScraperSystemCache::saveSystems(cachePath, {sys}));

  GeneralSettings settings;
  auto &blob = settings.scraper.credentials[QStringLiteral("screenscraper")];
  blob.insert(QStringLiteral("dev_id"), QStringLiteral("dev123"));
  blob.insert(QStringLiteral("dev_password"), QStringLiteral("devpw"));
  ScreenScraperProvider provider([&settings]() { return &settings; },
                                 ScreenScraperProvider::CollectionAccessor{});
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Platform;
  target.identity = QStringLiteral("999"); // explicit id, absent from the catalog

  std::optional<ErrorUtils::Result<Scraper::ScrapedItem>> result;
  provider.fetchEntity(
      target, [&result](const ErrorUtils::Result<Scraper::ScrapedItem> &r) { result = r; });
  QVERIFY(result.has_value());
  QVERIFY(result->isError());
  QCOMPARE(result->error().code, ErrorUtils::ErrorCode::RemoteResourceNotFound);
  QVERIFY(QFile::remove(cachePath));
}

// NOTE: the empty-catalog → UnknownError + httpStatus 503 branch
// (screenscraperprovider.cpp:225-235) is deliberately NOT covered here. Reaching
// systems.isEmpty() synchronously would need either no dev creds or a null
// HttpClient, but resolveSsCredentials always falls back to a BUNDLED dev key
// ("cedar") and the catalog manager holds the real HttpClient::instance() — so
// with no stale cache it always attempts a live systemesListe.php round-trip. The
// 503 mapping therefore only triggers on a genuine network/catalog failure and is
// runtime-gated (a stub HttpClient seam would be needed to test it headlessly).

void TestScreenScraperProvider::fetchEntity_nonPlatformTypeIsInvalidArgument() {
  // Kartend-ckepd.4: ScreenScraper advertises only Game + Platform. A non-Platform
  // entity type is rejected synchronously with InvalidArgument, before any catalog
  // access — pinning advertised vs actual entity support.
  GeneralSettings settings;
  ScreenScraperProvider provider([&settings]() { return &settings; },
                                 ScreenScraperProvider::CollectionAccessor{});
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Collection;
  target.identity = QStringLiteral("some-uuid");

  std::optional<ErrorUtils::Result<Scraper::ScrapedItem>> result;
  provider.fetchEntity(
      target, [&result](const ErrorUtils::Result<Scraper::ScrapedItem> &r) { result = r; });
  QVERIFY2(result.has_value(), "non-Platform reject was not synchronous");
  QVERIFY(result->isError());
  QCOMPARE(result->error().code, ErrorUtils::ErrorCode::InvalidArgument);
}

void TestScreenScraperProvider::fetchEntity_emptyIdentityHonorsCollectionSystemIdOverride() {
  // Kartend-ckepd.6: the fresh UI launch leaves identity EMPTY → fetchEntity
  // autodetects via resolveSystemId(), which honors the collection's
  // screenscraperSystemId override. Prove the CollectionAccessor is consulted and
  // the override id flows through to scopeKey (deterministic; the autodetect
  // heuristic itself lives in test_screenscrapersystems).
  QStandardPaths::setTestModeEnabled(true);
  const QString cachePath = ScreenScraperSystemCache::defaultCachePath();
  ScreenScraperSystems::System sys;
  sys.id = 42;
  sys.displayName = QStringLiteral("Test Platform");
  QVERIFY(ScreenScraperSystemCache::saveSystems(cachePath, {sys}));

  GeneralSettings settings;
  auto &blob = settings.scraper.credentials[QStringLiteral("screenscraper")];
  blob.insert(QStringLiteral("dev_id"), QStringLiteral("dev123"));
  blob.insert(QStringLiteral("dev_password"), QStringLiteral("devpw"));
  CollectionConfig cfg;
  cfg.scraperOverrides.screenscraperSystemId = 42;
  ScreenScraperProvider provider([&settings]() { return &settings; },
                                 [&cfg]() -> const CollectionConfig * { return &cfg; });
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Platform; // identity left EMPTY → resolve via override

  std::optional<ErrorUtils::Result<Scraper::ScrapedItem>> result;
  provider.fetchEntity(
      target, [&result](const ErrorUtils::Result<Scraper::ScrapedItem> &r) { result = r; });
  QVERIFY(result.has_value());
  QVERIFY2(result->isOk(), qPrintable(result->isError() ? result->error().message : QString()));
  QVERIFY(!result->value().media.isEmpty());
  QCOMPARE(result->value().media.first().scopeKey, QStringLiteral("42"));
  QVERIFY(QFile::remove(cachePath));
}

void TestScreenScraperProvider::fetchEntity_emptyIdentityUnresolvableIsNotFound() {
  // Kartend-ckepd.6: empty identity + a collection whose system can't be resolved
  // (no override; a name that won't autodetect) → resolveSystemId returns 0 →
  // RemoteResourceNotFound (re-queueable), the "set the System ID override" hint.
  QStandardPaths::setTestModeEnabled(true);
  const QString cachePath = ScreenScraperSystemCache::defaultCachePath();
  ScreenScraperSystems::System sys;
  sys.id = 42;
  sys.displayName = QStringLiteral("Test Platform");
  QVERIFY(ScreenScraperSystemCache::saveSystems(cachePath, {sys}));

  GeneralSettings settings;
  auto &blob = settings.scraper.credentials[QStringLiteral("screenscraper")];
  blob.insert(QStringLiteral("dev_id"), QStringLiteral("dev123"));
  blob.insert(QStringLiteral("dev_password"), QStringLiteral("devpw"));
  CollectionConfig cfg;
  cfg.scraperOverrides.screenscraperSystemId = -1; // no override
  cfg.name = QStringLiteral("zzz-nomatch-zzz");    // won't autodetect vs "Test Platform"
  ScreenScraperProvider provider([&settings]() { return &settings; },
                                 [&cfg]() -> const CollectionConfig * { return &cfg; });
  Scraper::EntityScrapeTarget target;
  target.type = Scraper::ScrapeEntityType::Platform; // identity left EMPTY

  std::optional<ErrorUtils::Result<Scraper::ScrapedItem>> result;
  provider.fetchEntity(
      target, [&result](const ErrorUtils::Result<Scraper::ScrapedItem> &r) { result = r; });
  QVERIFY(result.has_value());
  QVERIFY(result->isError());
  QCOMPARE(result->error().code, ErrorUtils::ErrorCode::RemoteResourceNotFound);
  QVERIFY(QFile::remove(cachePath));
}

QTEST_MAIN(TestScreenScraperProvider)
#include "test_screenscraperprovider.moc"
