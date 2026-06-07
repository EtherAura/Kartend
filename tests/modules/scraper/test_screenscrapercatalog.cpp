// Tests for ScreenScraperCatalogManager::ensureSystemsCatalog — the disk-cache
// hit + stale-cache fallback decision logic. The network-fetch branch needs a
// stubbable HttpClient (the concrete one isn't), so these cover the branches
// that resolve synchronously without touching the network: a fresh cache is
// served directly, and a stale/missing cache with no usable network path
// degrades to the on-disk data rather than erroring.
#include <chrono>
#include <filesystem>

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QList>
#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QTest>

#include "screenscrapercatalogmanager.h"
#include "screenscrapersystemcache.h"
#include "screenscrapersystems.h"

using Catalog = ScreenScraperCatalogManager;

namespace {

const QByteArray SYSTEMS_FIXTURE = R"json({"response":{"systemes":[
  {"id":"10","noms":{"nom_eu":"Display A"},"extensions":"fmt-a"},
  {"id":20,"noms":{"nom_eu":"Display B"},"extensions":"fmt-c"}
]}})json";

QList<ScreenScraperSystems::System> fixtureSystems() {
  auto parsed = ScreenScraperSystemCache::parseSystemsResponse(SYSTEMS_FIXTURE);
  return parsed.isOk() ? parsed.value() : QList<ScreenScraperSystems::System>{};
}

// Backdate a file's mtime well past any freshness window so isCacheStale() == true.
void makeStale(const QString &path) {
  std::filesystem::last_write_time(std::filesystem::path(path.toStdString()),
                                   std::filesystem::file_time_type::clock::now() -
                                       std::chrono::hours(24 * 365));
}

Catalog::Credentials noCreds() {
  return {};
}

Catalog::Credentials devCreds() {
  Catalog::Credentials c;
  c.devId = QStringLiteral("dev");
  c.devPassword = QStringLiteral("pw");
  return c;
}

} // namespace

class TestScreenScraperCatalog : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void cleanup();
  void freshCache_servedWithoutNetwork();
  void missingCacheNoCreds_returnsEmptyFallback();
  void staleCacheNoCreds_fallsBackToStaleData();
  void staleCacheNullClientWithCreds_fallsBackToStaleData();
};

void TestScreenScraperCatalog::initTestCase() {
  // Redirect QStandardPaths (and thus defaultCachePath) to an isolated test
  // location so we never touch a developer's real catalog cache.
  QStandardPaths::setTestModeEnabled(true);
}

void TestScreenScraperCatalog::cleanup() {
  const QString path = ScreenScraperSystemCache::defaultCachePath();
  if (!path.isEmpty()) {
    QFile::remove(path);
  }
}

void TestScreenScraperCatalog::freshCache_servedWithoutNetwork() {
  const QString path = ScreenScraperSystemCache::defaultCachePath();
  QVERIFY(!path.isEmpty());
  QVERIFY(ScreenScraperSystemCache::saveSystems(path, fixtureSystems()));
  QVERIFY(!ScreenScraperSystemCache::isCacheStale(path)); // just written -> fresh

  // No HTTP client at all: a fresh cache must be served directly.
  Catalog catalog(nullptr, QStringLiteral("kartend-test"), noCreds, nullptr);
  QList<ScreenScraperSystems::System> got;
  bool called = false;
  catalog.ensureSystemsCatalog([&](QList<ScreenScraperSystems::System> s) {
    got = std::move(s);
    called = true;
  });
  QVERIFY(called); // the fresh-cache path resolves synchronously
  QCOMPARE(got.size(), 2);
}

void TestScreenScraperCatalog::missingCacheNoCreds_returnsEmptyFallback() {
  // cleanup() removed any cache file; with no creds and no client the only safe
  // result is an empty list (so a scrape can still proceed with systemeid=0).
  Catalog catalog(nullptr, QStringLiteral("kartend-test"), noCreds, nullptr);
  QList<ScreenScraperSystems::System> got;
  bool called = false;
  catalog.ensureSystemsCatalog([&](QList<ScreenScraperSystems::System> s) {
    got = std::move(s);
    called = true;
  });
  QVERIFY(called);
  QVERIFY(got.isEmpty());
}

void TestScreenScraperCatalog::staleCacheNoCreds_fallsBackToStaleData() {
  const QString path = ScreenScraperSystemCache::defaultCachePath();
  QVERIFY(ScreenScraperSystemCache::saveSystems(path, fixtureSystems()));
  makeStale(path);
  QVERIFY(ScreenScraperSystemCache::isCacheStale(path));

  // Stale cache + no creds: must degrade to the on-disk (stale) data rather than
  // erroring or returning empty.
  Catalog catalog(nullptr, QStringLiteral("kartend-test"), noCreds, nullptr);
  QList<ScreenScraperSystems::System> got;
  bool called = false;
  catalog.ensureSystemsCatalog([&](QList<ScreenScraperSystems::System> s) {
    got = std::move(s);
    called = true;
  });
  QVERIFY(called);
  QCOMPARE(got.size(), 2); // served the stale data
}

void TestScreenScraperCatalog::staleCacheNullClientWithCreds_fallsBackToStaleData() {
  const QString path = ScreenScraperSystemCache::defaultCachePath();
  QVERIFY(ScreenScraperSystemCache::saveSystems(path, fixtureSystems()));
  makeStale(path);

  // Creds present but no HTTP client: the network branch is unreachable, so it
  // still degrades to the stale on-disk data.
  Catalog catalog(nullptr, QStringLiteral("kartend-test"), devCreds, nullptr);
  QList<ScreenScraperSystems::System> got;
  bool called = false;
  catalog.ensureSystemsCatalog([&](QList<ScreenScraperSystems::System> s) {
    got = std::move(s);
    called = true;
  });
  QVERIFY(called);
  QCOMPARE(got.size(), 2);
}

QTEST_MAIN(TestScreenScraperCatalog)
#include "test_screenscrapercatalog.moc"
