// Tests for ScreenScraperSystemCache. Pure JSON parsing + on-disk
// round-trip + age check. Fixture JSON shaped to match SS's
// systemesListe.php response (deeply nested noms_* fields per
// platform), but the test fixtures use generic placeholder names so
// the test binary itself stays free of hardcoded platform identifiers.
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include "screenscrapersystemcache.h"
#include "screenscrapersystems.h"

class TestScreenScraperSystemCache : public QObject {
  Q_OBJECT
private slots:
  void parseSystemsResponse_extractsIdAndNames();
  void parseSystemsResponse_collectsEveryNomFieldAsAlias();
  void parseSystemsResponse_splitsCommaSeparatedExtensions();
  void parseSystemsResponse_acceptsArrayValuedNoms();
  void parseSystemsResponse_acceptsArrayValuedExtensions();
  void parseSystemsResponse_skipsEntriesWithoutId();
  void parseSystemsResponse_acceptsBareSystemesArrayShape();
  void parseSystemsResponse_emptySystemesArrayIsSuccess();
  void parseSystemsResponse_malformedJsonReturnsError();
  void roundTrip_writeThenReadProducesSameLogicalContent();
  void loadCachedSystems_missingFileReturnsEmptySuccess();
  void isCacheStale_missingFileIsStale();
  void isCacheStale_recentFileIsFresh();
};

namespace {

const QByteArray FIXTURE_WRAPPED = R"json({
  "header": {"APIversion": "2"},
  "response": {
    "systemes": [
      {
        "id": "10",
        "noms": {
          "nom_eu": "Display A",
          "nom_us": "Display A US",
          "nom_recalbox": "alpha",
          "nom_retropie": "alpha"
        },
        "extensions": "fmt-a,fmt-b,7z"
      },
      {
        "id": 20,
        "noms": {
          "nom_eu": "Display B",
          "nom_recalbox": "beta",
          "nom_synonyms": ["beta-shorthand", "BETA-CAPS"]
        },
        "extensions": "fmt-c"
      },
      {
        "id": "",
        "noms": {"nom_eu": "Skip me"}
      }
    ]
  }
})json";

const QByteArray FIXTURE_BARE = R"json({
  "systemes": [
    {"id": 99, "noms": {"nom_eu": "Bare"}, "extensions": "fmt-x"}
  ]
})json";

const QByteArray FIXTURE_ARRAY_EXTENSIONS = R"json({
  "systemes": [
    {"id": 5, "noms": {"nom_eu": "Arr"}, "extensions": ["fmt-p", "fmt-q", ".fmt-r"]}
  ]
})json";

} // namespace

void TestScreenScraperSystemCache::parseSystemsResponse_extractsIdAndNames() {
  auto result = ScreenScraperSystemCache::parseSystemsResponse(FIXTURE_WRAPPED);
  QVERIFY(result.isOk());
  // Three entries in fixture; one has empty id and is skipped.
  QCOMPARE(result.value().size(), 2);
  QCOMPARE(result.value()[0].id, 10);
  QCOMPARE(result.value()[0].displayName, QStringLiteral("Display A"));
  QCOMPARE(result.value()[1].id, 20);
}

void TestScreenScraperSystemCache::parseSystemsResponse_collectsEveryNomFieldAsAlias() {
  auto result = ScreenScraperSystemCache::parseSystemsResponse(FIXTURE_WRAPPED);
  QVERIFY(result.isOk());
  // Entry 0: nom_eu + nom_us + nom_recalbox + nom_retropie are all
  // string-valued — every distinct lowercase value should appear.
  // recalbox + retropie share "alpha" so dedup keeps it once.
  const auto aliases0 = result.value()[0].aliases;
  QVERIFY(aliases0.contains(QStringLiteral("display a")));
  QVERIFY(aliases0.contains(QStringLiteral("display a us")));
  QVERIFY(aliases0.contains(QStringLiteral("alpha")));
  // Dedup check.
  QCOMPARE(aliases0.count(QStringLiteral("alpha")), 1);
}

void TestScreenScraperSystemCache::parseSystemsResponse_splitsCommaSeparatedExtensions() {
  auto result = ScreenScraperSystemCache::parseSystemsResponse(FIXTURE_WRAPPED);
  QVERIFY(result.isOk());
  QCOMPARE(result.value()[0].extensions,
           QStringList({"fmt-a", "fmt-b", "7z"}));
  QCOMPARE(result.value()[1].extensions, QStringList({"fmt-c"}));
}

void TestScreenScraperSystemCache::parseSystemsResponse_acceptsArrayValuedNoms() {
  auto result = ScreenScraperSystemCache::parseSystemsResponse(FIXTURE_WRAPPED);
  QVERIFY(result.isOk());
  // Entry 1 has nom_synonyms as a JSON array. Each element should
  // become an alias (lowercased + deduped).
  const auto aliases1 = result.value()[1].aliases;
  QVERIFY(aliases1.contains(QStringLiteral("beta-shorthand")));
  QVERIFY(aliases1.contains(QStringLiteral("beta-caps")));
}

void TestScreenScraperSystemCache::parseSystemsResponse_acceptsArrayValuedExtensions() {
  auto result =
      ScreenScraperSystemCache::parseSystemsResponse(FIXTURE_ARRAY_EXTENSIONS);
  QVERIFY(result.isOk());
  // Leading dot stripped; lowercased; deduped.
  QCOMPARE(result.value()[0].extensions,
           QStringList({"fmt-p", "fmt-q", "fmt-r"}));
}

void TestScreenScraperSystemCache::parseSystemsResponse_skipsEntriesWithoutId() {
  auto result = ScreenScraperSystemCache::parseSystemsResponse(FIXTURE_WRAPPED);
  QVERIFY(result.isOk());
  // The empty-id third entry must be silently dropped (count of 2,
  // not 3).
  QCOMPARE(result.value().size(), 2);
}

void TestScreenScraperSystemCache::parseSystemsResponse_acceptsBareSystemesArrayShape() {
  // Some test fixtures (and possibly future SS variants) skip the
  // wrapping `response` object. Parser must handle both shapes.
  auto result = ScreenScraperSystemCache::parseSystemsResponse(FIXTURE_BARE);
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 1);
  QCOMPARE(result.value()[0].id, 99);
}

void TestScreenScraperSystemCache::parseSystemsResponse_emptySystemesArrayIsSuccess() {
  auto result = ScreenScraperSystemCache::parseSystemsResponse(
      QByteArray(R"json({"response": {"systemes": []}})json"));
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 0);
}

void TestScreenScraperSystemCache::parseSystemsResponse_malformedJsonReturnsError() {
  auto result = ScreenScraperSystemCache::parseSystemsResponse(QByteArray("{not json"));
  QVERIFY(result.isError());
}

void TestScreenScraperSystemCache::roundTrip_writeThenReadProducesSameLogicalContent() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.path() + "/cache.json";

  // Start from parsed-fixture content so the round-trip exercises
  // the saver against real System structs (not hand-built ones the
  // saver might serialise differently).
  auto parsed = ScreenScraperSystemCache::parseSystemsResponse(FIXTURE_WRAPPED);
  QVERIFY(parsed.isOk());
  QVERIFY(ScreenScraperSystemCache::saveSystems(path, parsed.value()));

  auto reloaded = ScreenScraperSystemCache::loadCachedSystems(path);
  QVERIFY(reloaded.isOk());
  QCOMPARE(reloaded.value().size(), parsed.value().size());
  for (int i = 0; i < parsed.value().size(); ++i) {
    QCOMPARE(reloaded.value()[i].id, parsed.value()[i].id);
    QCOMPARE(reloaded.value()[i].extensions, parsed.value()[i].extensions);
    // Aliases set may be reordered or augmented (the saver writes
    // displayName under nom_kartend which the reader picks up too)
    // but the originals must all survive.
    for (const QString &alias : parsed.value()[i].aliases) {
      QVERIFY2(reloaded.value()[i].aliases.contains(alias),
               qPrintable(QString("Lost alias %1").arg(alias)));
    }
  }
}

void TestScreenScraperSystemCache::loadCachedSystems_missingFileReturnsEmptySuccess() {
  // "Not cached yet" is a successful empty list, not an error — so
  // the provider's ensureSystemsCatalog can distinguish a fresh
  // install from a corrupted cache.
  auto result = ScreenScraperSystemCache::loadCachedSystems(
      QStringLiteral("/nonexistent/path/foo.json"));
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 0);
}

void TestScreenScraperSystemCache::isCacheStale_missingFileIsStale() {
  QVERIFY(ScreenScraperSystemCache::isCacheStale(
      QStringLiteral("/nonexistent/path/cache.json")));
}

void TestScreenScraperSystemCache::isCacheStale_recentFileIsFresh() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.path() + "/fresh.json";
  // Create the file with current mtime — should be considered fresh.
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("{}");
  f.close();
  QVERIFY(!ScreenScraperSystemCache::isCacheStale(path));
}

QTEST_MAIN(TestScreenScraperSystemCache)
#include "test_screenscrapersystemcache.moc"
