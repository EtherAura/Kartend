// Tests for ScreenScraperCompanyRegistry (Kartend-cnti4): the accumulated
// company id → name registry plus the name → on-disk-art resolution that
// gives a hand-made manufacturer parent collection its logo. Fixture names
// are generic placeholders per the no-real-identifiers convention.
#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "screenscrapercompanyregistry.h"

using ScreenScraperCompanyRegistry::CompanyMap;

class TestScreenScraperCompanyRegistry : public QObject {
  Q_OBJECT
private slots:
  void parse_readsIdNamePairs();
  void parse_rejectsHostileIds();
  void load_missingFileIsEmptySuccess();
  void roundTrip_saveThenLoadPreservesPairs();
  void merge_newIdChangesMap_existingDoesNot();
  void merge_upgradesEmptyNameOnly();
  void idsForName_caseInsensitiveSortedMultiMatch();
  void findCompanyArt_prefersPictocouleurThenMonochromeThenAny();
  void findCompanyArt_walksRootsInOrder();
  void logoForCollectionName_matchesNameToArtOnDisk();
  /// The whole-list pass (startup + scrape-completion shared core): wires
  /// matching shells, never displaces platform art or user icons.
  void applyToCollections_wiresShellsAndRespectsPrecedence();
};

namespace {

/// Create `<root>/_shared/<type>/company_<id>.png` and return its path.
QString seedArt(const QString &root, const QString &type, const QString &id) {
  const QString dir = root + QStringLiteral("/_shared/") + type;
  QDir().mkpath(dir);
  const QString path = dir + QStringLiteral("/company_") + id + QStringLiteral(".png");
  QFile f(path);
  f.open(QIODevice::WriteOnly);
  f.write("png-bytes");
  f.close();
  return path;
}

} // namespace

void TestScreenScraperCompanyRegistry::parse_readsIdNamePairs() {
  auto result = ScreenScraperCompanyRegistry::parse(QByteArrayLiteral(
      R"json({"companies":[{"id":"3","name":"Maker A"},{"id":"77","name":"Maker B"}]})json"));
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 2);
  QCOMPARE(result.value().value(QStringLiteral("3")), QStringLiteral("Maker A"));
  QCOMPARE(result.value().value(QStringLiteral("77")), QStringLiteral("Maker B"));
}

void TestScreenScraperCompanyRegistry::parse_rejectsHostileIds() {
  // The id becomes a `company_<id>` filename component in findCompanyArt, so
  // load() must distrust a hand-edited file the same way the parser distrusts
  // the API (Kartend-xhbt posture).
  auto result = ScreenScraperCompanyRegistry::parse(QByteArrayLiteral(
      R"json({"companies":[{"id":"../evil","name":"Bad"},{"id":"9","name":"Good"}]})json"));
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 1);
  QVERIFY(result.value().contains(QStringLiteral("9")));
}

void TestScreenScraperCompanyRegistry::load_missingFileIsEmptySuccess() {
  auto result = ScreenScraperCompanyRegistry::load(QStringLiteral("/nonexistent/companies.json"));
  QVERIFY(result.isOk());
  QVERIFY(result.value().isEmpty());
}

void TestScreenScraperCompanyRegistry::roundTrip_saveThenLoadPreservesPairs() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.path() + QStringLiteral("/companies.json");
  CompanyMap map;
  map.insert(QStringLiteral("3"), QStringLiteral("Maker A"));
  map.insert(QStringLiteral("77"), QStringLiteral("Maker B"));
  QVERIFY(ScreenScraperCompanyRegistry::save(path, map));
  auto reloaded = ScreenScraperCompanyRegistry::load(path);
  QVERIFY(reloaded.isOk());
  QCOMPARE(reloaded.value(), map);
}

void TestScreenScraperCompanyRegistry::merge_newIdChangesMap_existingDoesNot() {
  CompanyMap map;
  // The bool return gates the provider's file write — a false for an
  // already-known pair is what keeps a 20k-item batch at zero steady-state I/O.
  QVERIFY(ScreenScraperCompanyRegistry::merge(map, QStringLiteral("3"), QStringLiteral("Maker")));
  QVERIFY(!ScreenScraperCompanyRegistry::merge(map, QStringLiteral("3"), QStringLiteral("Maker")));
  QVERIFY(!ScreenScraperCompanyRegistry::merge(map, QStringLiteral("3"),
                                               QStringLiteral("Different Text")));
  QCOMPARE(map.value(QStringLiteral("3")), QStringLiteral("Maker")); // first-seen kept
  QVERIFY(
      !ScreenScraperCompanyRegistry::merge(map, QStringLiteral("../evil"), QStringLiteral("Bad")));
  QCOMPARE(map.size(), 1);
}

void TestScreenScraperCompanyRegistry::merge_upgradesEmptyNameOnly() {
  CompanyMap map;
  QVERIFY(ScreenScraperCompanyRegistry::merge(map, QStringLiteral("5"), QString()));
  QVERIFY(ScreenScraperCompanyRegistry::merge(map, QStringLiteral("5"), QStringLiteral("Named")));
  QCOMPARE(map.value(QStringLiteral("5")), QStringLiteral("Named"));
}

void TestScreenScraperCompanyRegistry::idsForName_caseInsensitiveSortedMultiMatch() {
  CompanyMap map;
  // SS keeps distinct ids for regional arms sharing one display name.
  map.insert(QStringLiteral("20"), QStringLiteral("Maker A"));
  map.insert(QStringLiteral("3"), QStringLiteral("MAKER A"));
  map.insert(QStringLiteral("77"), QStringLiteral("Maker B"));
  QCOMPARE(ScreenScraperCompanyRegistry::idsForName(map, QStringLiteral("maker a")),
           (QStringList{QStringLiteral("20"), QStringLiteral("3")}));
  QVERIFY(ScreenScraperCompanyRegistry::idsForName(map, QString()).isEmpty());
  QVERIFY(ScreenScraperCompanyRegistry::idsForName(map, QStringLiteral("Unknown")).isEmpty());

  // Word-boundary prefix fallback: "Maker" claims "Maker A"/"Maker B" (the
  // corporate-long-form case — SS publisher entities rarely equal the
  // colloquial manufacturer name) but a bare substring must NOT match:
  // "Mak" hits nothing, and exact matches always sort ahead of prefix ones.
  map.insert(QStringLiteral("9"), QStringLiteral("Maker"));
  QCOMPARE(ScreenScraperCompanyRegistry::idsForName(map, QStringLiteral("maker")),
           (QStringList{QStringLiteral("9"), QStringLiteral("20"), QStringLiteral("3"),
                        QStringLiteral("77")}));
  QVERIFY(ScreenScraperCompanyRegistry::idsForName(map, QStringLiteral("Mak")).isEmpty());
}

void TestScreenScraperCompanyRegistry::findCompanyArt_prefersPictocouleurThenMonochromeThenAny() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString root = tmp.path();
  const QString other = seedArt(root, QStringLiteral("customtype"), QStringLiteral("3"));
  QCOMPARE(ScreenScraperCompanyRegistry::findCompanyArt({root}, QStringLiteral("3")), other);
  const QString mono = seedArt(root, QStringLiteral("pictomonochrome"), QStringLiteral("3"));
  QCOMPARE(ScreenScraperCompanyRegistry::findCompanyArt({root}, QStringLiteral("3")), mono);
  const QString colour = seedArt(root, QStringLiteral("pictocouleur"), QStringLiteral("3"));
  QCOMPARE(ScreenScraperCompanyRegistry::findCompanyArt({root}, QStringLiteral("3")), colour);
  // Hostile id never becomes a glob.
  QVERIFY(ScreenScraperCompanyRegistry::findCompanyArt({root}, QStringLiteral("../3")).isEmpty());
}

void TestScreenScraperCompanyRegistry::findCompanyArt_walksRootsInOrder() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString rootA = tmp.path() + QStringLiteral("/a");
  const QString rootB = tmp.path() + QStringLiteral("/b");
  const QString inB = seedArt(rootB, QStringLiteral("pictocouleur"), QStringLiteral("3"));
  QCOMPARE(ScreenScraperCompanyRegistry::findCompanyArt({rootA, rootB}, QStringLiteral("3")), inB);
  const QString inA = seedArt(rootA, QStringLiteral("pictocouleur"), QStringLiteral("3"));
  QCOMPARE(ScreenScraperCompanyRegistry::findCompanyArt({rootA, rootB}, QStringLiteral("3")), inA);
}

void TestScreenScraperCompanyRegistry::logoForCollectionName_matchesNameToArtOnDisk() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString root = tmp.path();
  CompanyMap map;
  map.insert(QStringLiteral("3"), QStringLiteral("Maker A"));
  map.insert(QStringLiteral("20"), QStringLiteral("Maker A")); // no art for this arm
  // Name matches but nothing scraped yet — a normal early state, not an error.
  QVERIFY(
      ScreenScraperCompanyRegistry::logoForCollectionName(map, QStringLiteral("Maker A"), {root})
          .isEmpty());
  const QString art = seedArt(root, QStringLiteral("pictocouleur"), QStringLiteral("3"));
  // Sorted id order tries 20 (no art) then 3 (hit): partial art still resolves.
  QCOMPARE(
      ScreenScraperCompanyRegistry::logoForCollectionName(map, QStringLiteral("maker a"), {root}),
      art);
  QVERIFY(ScreenScraperCompanyRegistry::logoForCollectionName(map, QStringLiteral("Nobody"), {root})
              .isEmpty());
}

void TestScreenScraperCompanyRegistry::applyToCollections_wiresShellsAndRespectsPrecedence() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString artworkRoot = tmp.path() + QStringLiteral("/art");
  const QString art = seedArt(artworkRoot, QStringLiteral("pictocouleur"), QStringLiteral("3"));
  const QString regPath = tmp.path() + QStringLiteral("/companies.json");
  CompanyMap reg;
  reg.insert(QStringLiteral("3"), QStringLiteral("Maker Corporation Ltd"));
  QVERIFY(ScreenScraperCompanyRegistry::save(regPath, reg));

  const auto makeCfg = [&](const QString &name) {
    CollectionConfig c;
    c.name = name;
    c.artworkDirectory = artworkRoot;
    return c;
  };
  QList<CollectionConfig> collections;
  collections.append(makeCfg(QStringLiteral("Maker"))); // shell — prefix match, empty icon
  CollectionConfig platform = makeCfg(QStringLiteral("Maker Corporation Ltd"));
  platform.collectionIcon = QStringLiteral("/x/_shared/wheel/platform_7.png"); // platform art
  collections.append(platform);
  CollectionConfig manual = makeCfg(QStringLiteral("Maker Corporation Ltd"));
  manual.collectionIcon = QStringLiteral("/home/user/my-icon.png"); // user choice
  collections.append(manual);
  CollectionConfig playlist = makeCfg(QStringLiteral("Maker"));
  playlist.isPlaylist = true;
  collections.append(playlist);

  QVERIFY(ScreenScraperCompanyRegistry::applyToCollections(collections, regPath));
  QCOMPARE(collections[0].collectionIcon, art); // shell wired
  QCOMPARE(collections[0].background.headerLogoImage, art);
  QCOMPARE(collections[1].collectionIcon,
           QStringLiteral("/x/_shared/wheel/platform_7.png")); // platform art kept
  QCOMPARE(collections[2].collectionIcon,
           QStringLiteral("/home/user/my-icon.png")); // user icon kept
  QVERIFY(collections[3].collectionIcon.isEmpty());   // playlists untouched

  // Idempotent: a second pass changes nothing.
  QVERIFY(!ScreenScraperCompanyRegistry::applyToCollections(collections, regPath));

  // Missing registry file: quiet no-op, never an error path.
  QVERIFY(!ScreenScraperCompanyRegistry::applyToCollections(
      collections, tmp.path() + QStringLiteral("/absent.json")));

  // Second probe (Kartend-czna3): a collection the company match cannot
  // serve still wires from COLLECTION-scoped art keyed by its uuid — the
  // shape the Wikidata fallback / TMDB collection art writes. This is what
  // lets a logo fetched while the app was closed get wired at next boot.
  CollectionConfig wikiShell = makeCfg(QStringLiteral("Unmatched Shell"));
  collections.append(wikiShell);
  const QString uuid = CollectionUtils::computeCollectionUuid(collections.last());
  const QString dir = artworkRoot + QStringLiteral("/_shared/logo-svg");
  QDir().mkpath(dir);
  const QString wikiArt = dir + QStringLiteral("/collection_") + uuid + QStringLiteral(".svg");
  {
    QFile f(wikiArt);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("<svg/>");
  }
  QVERIFY(ScreenScraperCompanyRegistry::applyToCollections(collections, regPath));
  QCOMPARE(collections.last().collectionIcon, wikiArt);
}

QTEST_MAIN(TestScreenScraperCompanyRegistry)
#include "test_screenscrapercompanyregistry.moc"
