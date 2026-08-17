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
  void parseSystemsResponse_retainsCatalogFields();
  void parseSystemsResponse_absentCatalogFieldsReadEmpty();
  void parseSystemsResponse_mediaKeepsTokenAndHashesButNoUrl();
  void parseSystemsResponse_mediaFlagsVideoEndpoint();
  void parseSystemsResponse_mediaWithoutTypeIsSkipped();
  void roundTrip_writeThenReadProducesSameLogicalContent();
  void roundTrip_preservesCatalogFieldsAndMedia();
  void saveSystems_neverWritesCredentialsToDisk();
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

// Kartend-xny9o: the retained catalog fields plus a medias[] array shaped
// like the live response — including the credential-bearing urls SS hands
// back, which must not survive parsing. Entry 30 carries everything; entry
// 31 carries none of it (SS omits `compagnie` for roughly a third of the
// real catalog, so absence has to be routine).
const QByteArray FIXTURE_CATALOG = R"json({
  "response": {
    "systemes": [
      {
        "id": 30,
        "noms": {"nom_eu": "Display C"},
        "extensions": "fmt-d",
        "compagnie": "Placeholder Manufacturer",
        "type": "Category One",
        "datedebut": "1988",
        "datefin": "1998",
        "romtype": "rom",
        "supporttype": "media-kind",
        "medias": [
          {
            "type": "wheel",
            "parent": "systeme",
            "url": "https://neoclone.example.invalid/api2/mediaSysteme.php?devid=devuser&devpassword=s3cr3t&softname=kartend&ssid=&sspassword=alsosecret&systemeid=30&media=wheel(wor)",
            "region": "WOR",
            "support": "0",
            "crc": "85832e9c",
            "md5": "668b1201f280946b24cad28593da321a",
            "sha1": "b42b877f8805efedb5c685570285dd2fe1c84d68",
            "format": "PNG"
          },
          {
            "type": "video",
            "parent": "systeme",
            "url": "https://neoclone.example.invalid/api2/mediaVideoSysteme.php?devid=devuser&devpassword=s3cr3t&systemeid=30&media=video(wor)",
            "region": "wor",
            "format": "mp4"
          },
          {
            "parent": "systeme",
            "url": "https://neoclone.example.invalid/api2/mediaSysteme.php?systemeid=30&media=orphan"
          }
        ]
      },
      {
        "id": 31,
        "noms": {"nom_eu": "Display D"},
        "extensions": "fmt-e"
      }
    ]
  }
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
  QCOMPARE(result.value()[0].extensions, QStringList({"fmt-a", "fmt-b", "7z"}));
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
  auto result = ScreenScraperSystemCache::parseSystemsResponse(FIXTURE_ARRAY_EXTENSIONS);
  QVERIFY(result.isOk());
  // Leading dot stripped; lowercased; deduped.
  QCOMPARE(result.value()[0].extensions, QStringList({"fmt-p", "fmt-q", "fmt-r"}));
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

void TestScreenScraperSystemCache::parseSystemsResponse_retainsCatalogFields() {
  auto result = ScreenScraperSystemCache::parseSystemsResponse(FIXTURE_CATALOG);
  QVERIFY(result.isOk());
  const auto &s = result.value()[0];
  QCOMPARE(s.company, QStringLiteral("Placeholder Manufacturer"));
  QCOMPARE(s.systemType, QStringLiteral("Category One"));
  QCOMPARE(s.startDate, QStringLiteral("1988"));
  QCOMPARE(s.endDate, QStringLiteral("1998"));
  QCOMPARE(s.romType, QStringLiteral("rom"));
  QCOMPARE(s.supportType, QStringLiteral("media-kind"));
}

void TestScreenScraperSystemCache::parseSystemsResponse_absentCatalogFieldsReadEmpty() {
  // SS omits `compagnie` on ~1/3 of the live catalog, and the whole media
  // block on some entries. An entry carrying none of the new fields must
  // still parse — absence is routine, never an error.
  auto result = ScreenScraperSystemCache::parseSystemsResponse(FIXTURE_CATALOG);
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 2);
  const auto &s = result.value()[1];
  QCOMPARE(s.id, 31);
  QVERIFY(s.company.isEmpty());
  QVERIFY(s.systemType.isEmpty());
  QVERIFY(s.media.isEmpty());
  // The fields that always worked are untouched by the addition.
  QCOMPARE(s.displayName, QStringLiteral("Display D"));
  QCOMPARE(s.extensions, QStringList({"fmt-e"}));
}

void TestScreenScraperSystemCache::parseSystemsResponse_mediaKeepsTokenAndHashesButNoUrl() {
  auto result = ScreenScraperSystemCache::parseSystemsResponse(FIXTURE_CATALOG);
  QVERIFY(result.isOk());
  const auto &media = result.value()[0].media;
  // Three entries in the fixture; the untyped one is dropped.
  QCOMPARE(media.size(), 2);
  const auto &wheel = media[0];
  QCOMPARE(wheel.type, QStringLiteral("wheel"));
  // The region-qualified token is kept verbatim — SS resolves region
  // fallback itself, so reassembling type + region would not round-trip.
  QCOMPARE(wheel.token, QStringLiteral("wheel(wor)"));
  QCOMPARE(wheel.region, QStringLiteral("wor")); // lowercased from "WOR"
  QCOMPARE(wheel.support, QStringLiteral("0"));
  QCOMPARE(wheel.format, QStringLiteral("png")); // lowercased from "PNG"
  QCOMPARE(wheel.crc, QStringLiteral("85832e9c"));
  QCOMPARE(wheel.md5, QStringLiteral("668b1201f280946b24cad28593da321a"));
  QCOMPARE(wheel.sha1, QStringLiteral("b42b877f8805efedb5c685570285dd2fe1c84d68"));
  QVERIFY(!wheel.video);
}

void TestScreenScraperSystemCache::parseSystemsResponse_mediaFlagsVideoEndpoint() {
  // mediaSysteme.php and mediaVideoSysteme.php take the same params but
  // are not interchangeable, so which one served the entry has to survive.
  auto result = ScreenScraperSystemCache::parseSystemsResponse(FIXTURE_CATALOG);
  QVERIFY(result.isOk());
  const auto &video = result.value()[0].media[1];
  QCOMPARE(video.type, QStringLiteral("video"));
  QCOMPARE(video.token, QStringLiteral("video(wor)"));
  QVERIFY(video.video);
}

void TestScreenScraperSystemCache::parseSystemsResponse_mediaWithoutTypeIsSkipped() {
  // An entry with no `type` is unaddressable — it cannot be bucketed into
  // an art role or named on disk — so it is dropped rather than stored as
  // a blank-typed row that a consumer would have to filter again.
  auto result = ScreenScraperSystemCache::parseSystemsResponse(FIXTURE_CATALOG);
  QVERIFY(result.isOk());
  for (const auto &m : result.value()[0].media) {
    QVERIFY(!m.type.isEmpty());
    QVERIFY(m.token != QStringLiteral("orphan"));
  }
}

void TestScreenScraperSystemCache::roundTrip_preservesCatalogFieldsAndMedia() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.path() + "/catalog.json";

  auto parsed = ScreenScraperSystemCache::parseSystemsResponse(FIXTURE_CATALOG);
  QVERIFY(parsed.isOk());
  QVERIFY(ScreenScraperSystemCache::saveSystems(path, parsed.value()));

  auto reloaded = ScreenScraperSystemCache::loadCachedSystems(path);
  QVERIFY(reloaded.isOk());
  QCOMPARE(reloaded.value().size(), parsed.value().size());
  for (int i = 0; i < parsed.value().size(); ++i) {
    const auto &a = parsed.value()[i];
    const auto &b = reloaded.value()[i];
    QCOMPARE(b.company, a.company);
    QCOMPARE(b.systemType, a.systemType);
    QCOMPARE(b.startDate, a.startDate);
    QCOMPARE(b.endDate, a.endDate);
    QCOMPARE(b.romType, a.romType);
    QCOMPARE(b.supportType, a.supportType);
    // The saver writes `media` (the token) instead of the discarded `url`,
    // so the reader has to accept that shape too — Media compares equal
    // only if that second parse path is wired up.
    QCOMPARE(b.media, a.media);
  }
}

void TestScreenScraperSystemCache::saveSystems_neverWritesCredentialsToDisk() {
  // SS interpolates devid/devpassword/ssid/sspassword into every medias[]
  // url it returns. Persisting one verbatim would put the dev password in
  // cleartext under CacheLocation — a regression against the credential
  // handling the HTTP layer already enforces (redactedUrlForLog). Assert
  // on the actual bytes, not on the struct, so a future saver that starts
  // writing urls again trips this immediately.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.path() + "/nocreds.json";

  auto parsed = ScreenScraperSystemCache::parseSystemsResponse(FIXTURE_CATALOG);
  QVERIFY(parsed.isOk());
  QVERIFY(ScreenScraperSystemCache::saveSystems(path, parsed.value()));

  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QByteArray written = f.readAll();
  f.close();
  QVERIFY(!written.isEmpty());
  for (const char *needle : {"devpassword", "sspassword", "devid", "ssid", "s3cr3t", "alsosecret",
                             "mediaSysteme.php", "mediaVideoSysteme.php"}) {
    QVERIFY2(!written.contains(needle),
             qPrintable(QString("Cache file leaked %1").arg(QLatin1String(needle))));
  }
  // Sanity: the file is not empty-by-accident — the token we DO keep is there.
  QVERIFY(written.contains("wheel(wor)"));
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
  auto result =
      ScreenScraperSystemCache::loadCachedSystems(QStringLiteral("/nonexistent/path/foo.json"));
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 0);
}

void TestScreenScraperSystemCache::isCacheStale_missingFileIsStale() {
  QVERIFY(ScreenScraperSystemCache::isCacheStale(QStringLiteral("/nonexistent/path/cache.json")));
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
