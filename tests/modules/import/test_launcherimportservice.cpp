// LauncherImportService::syncEntries diff semantics against temp dirs: stub
// write/update/removal, the only-delete-what-we-own contract, deterministic
// collision numbering, fill-missing artwork, and the stub-name sanitizer.
// Provider I/O (Steam/Flatpak/Lutris readers) is covered by the utils tests;
// here the entry list is synthetic so every branch is reachable.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QSet>
#include <QTest>

#include "../../support/appinfofixture.h"
#include "../../support/migrateddb.h"
#include "itemmetadata.h"
#include <QImage>
#include <QImageReader>

#include "kartlink.h"
#include "launcherimportservice.h"

using LauncherImportService::GameEntry;
using LauncherImportService::SyncedStub;
using LauncherImportService::SyncResult;

class TestLauncherImportService : public QObject {
  Q_OBJECT

private slots:
  void watchPathsAreDirectoriesThatExist();
  void watchPathsIgnoreUnknownSources();
  void sanitizeStubBaseName_data();
  void sanitizeStubBaseName();
  void initialSyncWritesStubs();
  void resyncIsIdempotent();
  void removesOnlyOwnedStaleStubs();
  void collisionNumberingIsStable();
  void artworkFillMissingNeverOverwrites();
  void makeCollectionConfigShape();
  void multiCollectionSourceKeepsSlicesApart();
  void syncCarriesLaunchArgsOntoStubs();
  void scalableIconIsRasterizedIntoArtwork();
  void remoteCoverIsPendingOnlyWhenTheSlotIsEmpty();
  void importScopeRoundTrips();
  void importScopeUnknownNeverWidens();
  void makeCollectionConfigRecordsScope();
  void stubsMissingDescriptionSelectsUnenrichedOnly();
  void stubsMissingDescriptionFailsOpen();
  void steamMetadataPreservesScrapedDescription();
  void steamMetadataFillsEmptyRows();
  void steamMetadataNeverOverwritesExistingFields();
  void steamMetadataMissingAppInfoReportsError();

  // removeManagedImportDirs (Kartend-i366w)
  void cleanupDeletesManagedDirsAndEmptyBase();
  void cleanupRefusesUnmanagedAndTraversalDirs();
  void cleanupIsNoOpForNonImportCollections();
  void cleanupKeepsBaseWhenUserContentRemains();

private:
  QTemporaryDir m_dir;
  int m_caseCounter = 0;

  /// Fresh stub/artwork dir pair per case so slots can't see each other's
  /// files.
  void freshDirs(QString &stubDir, QString &artworkDir) {
    const QString base = m_dir.filePath(QStringLiteral("case%1").arg(m_caseCounter++));
    stubDir = base + QStringLiteral("/games");
    artworkDir = base + QStringLiteral("/artwork");
  }

  static GameEntry entry(const QString &title, const QString &target) {
    GameEntry e;
    e.title = title;
    e.target = target;
    return e;
  }

  /// Writes a one-game (appid 620) V29 appinfo fixture and returns its path.
  QString stageAppInfo() {
    KartendTest::AppInfoFixture fixture(/*v29=*/true);
    fixture.addGame(620, QStringLiteral("Portal 2"), QStringLiteral("Valve"),
                    QStringLiteral("Valve Publishing"), 1303171200);
    const QString path = m_dir.filePath(QStringLiteral("appinfo%1.vdf").arg(m_caseCounter++));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
      return {};
    }
    file.write(fixture.build());
    return path;
  }
};

void TestLauncherImportService::sanitizeStubBaseName_data() {
  QTest::addColumn<QString>("title");
  QTest::addColumn<QString>("expected");
  QTest::newRow("plain") << "Half-Life 2" << "Half-Life 2";
  QTest::newRow("colon kept") << "Half-Life 2: Episode Two" << "Half-Life 2: Episode Two";
  QTest::newRow("path separators") << "A/B\\C" << "A B C";
  QTest::newRow("security chars") << "X;|`$<>\"*?Y" << "X Y";
  QTest::newRow("collapse whitespace") << "  Too   Many\tSpaces  " << "Too Many Spaces";
  QTest::newRow("trailing dots") << "S.T.A.L.K.E.R." << "S.T.A.L.K.E.R";
  QTest::newRow("leading dash") << "--force" << "force";
  QTest::newRow("empty") << "" << "Untitled";
  QTest::newRow("only forbidden") << "///" << "Untitled";
}

void TestLauncherImportService::sanitizeStubBaseName() {
  QFETCH(QString, title);
  QFETCH(QString, expected);
  QCOMPARE(LauncherImportService::sanitizeStubBaseName(title), expected);
}

void TestLauncherImportService::initialSyncWritesStubs() {
  QString stubDir;
  QString artworkDir;
  freshDirs(stubDir, artworkDir);

  const QList<GameEntry> entries = {
      entry(QStringLiteral("Half-Life 2"), QStringLiteral("steam://rungameid/220")),
      entry(QStringLiteral("Portal"), QStringLiteral("steam://rungameid/400")),
  };
  const SyncResult result =
      LauncherImportService::syncEntries(entries, QStringLiteral("steam"), stubDir, artworkDir);
  QCOMPARE(result.written, 2);
  QCOMPARE(result.removed, 0);
  QCOMPARE(result.unchanged, 0);
  QCOMPARE(result.totalPresent(), 2);
  QVERIFY(result.errors.isEmpty());

  const auto loaded = KartLink::read(stubDir + QStringLiteral("/Half-Life 2.kartlink"));
  QVERIFY(!loaded.isError());
  QCOMPARE(loaded.value().target, QStringLiteral("steam://rungameid/220"));
  QCOMPARE(loaded.value().source, QStringLiteral("steam"));
  QCOMPARE(loaded.value().title, QStringLiteral("Half-Life 2"));
}

void TestLauncherImportService::resyncIsIdempotent() {
  QString stubDir;
  QString artworkDir;
  freshDirs(stubDir, artworkDir);

  const QList<GameEntry> entries = {
      entry(QStringLiteral("Celeste"), QStringLiteral("lutris:rungame/celeste"))};
  SyncResult first =
      LauncherImportService::syncEntries(entries, QStringLiteral("lutris"), stubDir, artworkDir);
  QCOMPARE(first.written, 1);

  SyncResult second =
      LauncherImportService::syncEntries(entries, QStringLiteral("lutris"), stubDir, artworkDir);
  QCOMPARE(second.written, 0);
  QCOMPARE(second.unchanged, 1);
  QCOMPARE(second.removed, 0);
  QVERIFY(!second.changed());
  // The user-facing count: nothing was (re)written, yet the game IS there —
  // an import summary built from this result must say 1, not 0.
  QCOMPARE(second.totalPresent(), 1);
}

void TestLauncherImportService::removesOnlyOwnedStaleStubs() {
  QString stubDir;
  QString artworkDir;
  freshDirs(stubDir, artworkDir);

  // Seed: one game that will disappear, one foreign-source stub, one
  // hand-made (unparseable) file with the stub extension.
  const QList<GameEntry> seed = {
      entry(QStringLiteral("Uninstalled Soon"), QStringLiteral("steam://rungameid/1"))};
  QVERIFY(LauncherImportService::syncEntries(seed, QStringLiteral("steam"), stubDir, artworkDir)
              .written == 1);
  KartLink::LinkData foreign;
  foreign.source = QStringLiteral("flatpak");
  foreign.target = QStringLiteral("org.example.Game");
  QVERIFY(KartLink::write(stubDir + QStringLiteral("/Foreign.kartlink"), foreign));
  {
    QFile handMade(stubDir + QStringLiteral("/handmade.kartlink"));
    QVERIFY(handMade.open(QIODevice::WriteOnly));
    handMade.write("not json");
  }

  // Next steam sync: the steam library is now empty.
  const SyncResult result =
      LauncherImportService::syncEntries({}, QStringLiteral("steam"), stubDir, artworkDir);
  QCOMPARE(result.removed, 1);
  QVERIFY(!QFileInfo::exists(stubDir + QStringLiteral("/Uninstalled Soon.kartlink")));
  // The ownership contract: foreign and unparseable files survive.
  QVERIFY(QFileInfo::exists(stubDir + QStringLiteral("/Foreign.kartlink")));
  QVERIFY(QFileInfo::exists(stubDir + QStringLiteral("/handmade.kartlink")));
}

void TestLauncherImportService::collisionNumberingIsStable() {
  QString stubDir;
  QString artworkDir;
  freshDirs(stubDir, artworkDir);

  // Two distinct games whose titles sanitize to the same basename. Input
  // order deliberately differs from the sorted (title, target) order the
  // numbering promises.
  const QList<GameEntry> entries = {
      entry(QStringLiteral("Same/Name"), QStringLiteral("steam://rungameid/2")),
      entry(QStringLiteral("Same Name"), QStringLiteral("steam://rungameid/1")),
  };
  const SyncResult first =
      LauncherImportService::syncEntries(entries, QStringLiteral("steam"), stubDir, artworkDir);
  QCOMPARE(first.written, 2);
  QVERIFY(QFileInfo::exists(stubDir + QStringLiteral("/Same Name.kartlink")));
  QVERIFY(QFileInfo::exists(stubDir + QStringLiteral("/Same Name (2).kartlink")));

  // Same input again (any order) — same assignment, nothing rewritten.
  const QList<GameEntry> reversed = {entries.at(1), entries.at(0)};
  const SyncResult second =
      LauncherImportService::syncEntries(reversed, QStringLiteral("steam"), stubDir, artworkDir);
  QCOMPARE(second.written, 0);
  QCOMPARE(second.unchanged, 2);
}

void TestLauncherImportService::artworkFillMissingNeverOverwrites() {
  QString stubDir;
  QString artworkDir;
  freshDirs(stubDir, artworkDir);

  // Launcher-side artwork files to copy.
  const QString sourceArt = m_dir.filePath(QStringLiteral("srcart"));
  QDir().mkpath(sourceArt);
  const auto stage = [](const QString &path, const QByteArray &content) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
  };
  stage(sourceArt + QStringLiteral("/cover.jpg"), QByteArrayLiteral("launcher-cover"));
  stage(sourceArt + QStringLiteral("/logo.png"), QByteArrayLiteral("launcher-logo"));

  GameEntry game = entry(QStringLiteral("Covered"), QStringLiteral("steam://rungameid/7"));
  game.coverPath = sourceArt + QStringLiteral("/cover.jpg");
  game.logoPath = sourceArt + QStringLiteral("/logo.png");

  // Pre-existing scraped cover (different extension on purpose — any raster
  // file with the basename claims the slot).
  QDir().mkpath(artworkDir + QStringLiteral("/front"));
  stage(artworkDir + QStringLiteral("/front/Covered.png"), QByteArrayLiteral("scraped-cover"));

  const SyncResult result =
      LauncherImportService::syncEntries({game}, QStringLiteral("steam"), stubDir, artworkDir);
  // Only the logo slot was empty.
  QCOMPARE(result.artworkCopied, 1);
  QVERIFY(QFileInfo::exists(artworkDir + QStringLiteral("/logo/Covered.png")));
  QVERIFY(!QFileInfo::exists(artworkDir + QStringLiteral("/front/Covered.jpg")));
  QFile scraped(artworkDir + QStringLiteral("/front/Covered.png"));
  QVERIFY(scraped.open(QIODevice::ReadOnly));
  QCOMPARE(scraped.readAll(), QByteArrayLiteral("scraped-cover"));

  // A second sync copies nothing further.
  const SyncResult second =
      LauncherImportService::syncEntries({game}, QStringLiteral("steam"), stubDir, artworkDir);
  QCOMPARE(second.artworkCopied, 0);
}

void TestLauncherImportService::makeCollectionConfigShape() {
  const CollectionConfig steam =
      LauncherImportService::makeCollectionConfig(QStringLiteral("steam"));
  QCOMPARE(steam.name, QStringLiteral("Steam"));
  QCOMPARE(steam.importSource, QStringLiteral("steam"));
  // Steam collections pin the store scraper so a scrape resolves exact
  // appids instead of name-matching (Kartend-ksjx0).
  QCOMPARE(steam.scraperOverrides.scraperProviderId, QStringLiteral("steam"));
  QCOMPARE(steam.extensions, QStringList{QStringLiteral("kartlink")});
  QCOMPARE(steam.launcher.launcherPath, QStringLiteral("xdg-open"));
  QCOMPARE(steam.launcher.launchParameters, QStringLiteral("%1"));
  QVERIFY(steam.mediaDirectory.endsWith(QStringLiteral("launcher-imports/steam/games")));
  QVERIFY(steam.artworkDirectory.endsWith(QStringLiteral("launcher-imports/steam/artwork")));

  const CollectionConfig flatpak =
      LauncherImportService::makeCollectionConfig(QStringLiteral("flatpak"));
  QCOMPARE(flatpak.launcher.launcherPath, QStringLiteral("flatpak"));
  QCOMPARE(flatpak.launcher.launchParameters, QStringLiteral("run %1"));
  QCOMPARE(flatpak.importSource, QStringLiteral("flatpak"));

  // Kartend-4cff2 sources. Each template has to agree with the target shape
  // its reader produces, which is the one thing a wrong entry here would break
  // silently — the import would look perfect and nothing would launch.
  const CollectionConfig heroic =
      LauncherImportService::makeCollectionConfig(QStringLiteral("heroic"));
  QCOMPARE(heroic.name, QStringLiteral("Heroic"));
  QCOMPARE(heroic.launcher.launcherPath, QStringLiteral("xdg-open")); // heroic:// URI
  QCOMPARE(heroic.launcher.launchParameters, QStringLiteral("%1"));

  const CollectionConfig itch = LauncherImportService::makeCollectionConfig(QStringLiteral("itch"));
  QCOMPARE(itch.name, QStringLiteral("itch.io"));
  QCOMPARE(itch.launcher.launcherPath, QStringLiteral("xdg-open")); // itch:// URI

  const CollectionConfig bottles =
      LauncherImportService::makeCollectionConfig(QStringLiteral("bottles"));
  QCOMPARE(bottles.launcher.launcherPath, QStringLiteral("bottles-cli"));
  // The bottle itself rides on each stub's args; the template places only the
  // program name.
  QCOMPARE(bottles.launcher.launchParameters, QStringLiteral("run -p %1"));

  const CollectionConfig xdg = LauncherImportService::makeCollectionConfig(QStringLiteral("xdg"));
  QCOMPARE(xdg.launcher.launcherPath, QStringLiteral("gio"));
  QCOMPARE(xdg.launcher.launchParameters, QStringLiteral("launch %1"));
  QVERIFY(xdg.mediaDirectory.endsWith(QStringLiteral("launcher-imports/xdg/games")));
}

// Kartend-0tddh: SVG-only applications are the norm now, and an SVG cannot be
// copied into the artwork tree as-is — the artwork lookup is raster-only. The
// importer renders it instead, and the file it writes is a real PNG under a
// .png name (a name that lied about its content is the Kartend-aiws7 defect).
void TestLauncherImportService::scalableIconIsRasterizedIntoArtwork() {
  if (!QImageReader::supportedImageFormats().contains("svg")) {
    QSKIP("Qt has no SVG reader here (qsvg imageformats plugin absent) — the "
          "importer degrades to no cover by design, so there is nothing to assert");
  }
  QString stubDir;
  QString artworkDir;
  freshDirs(stubDir, artworkDir);

  const QString svgPath = m_dir.filePath(QStringLiteral("icon.svg"));
  QFile svg(svgPath);
  QVERIFY(svg.open(QIODevice::WriteOnly));
  svg.write("<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64'>"
            "<rect width='64' height='64' fill='#3366cc'/></svg>");
  svg.close();

  GameEntry game = entry(QStringLiteral("Vector Game"), QStringLiteral("/x/vector.desktop"));
  game.coverPath = svgPath;
  const auto result =
      LauncherImportService::syncEntries({game}, QStringLiteral("xdg"), stubDir, artworkDir);
  QCOMPARE(result.written, 1);
  QCOMPARE(result.artworkCopied, 1);

  // Written as .png regardless of the source suffix…
  const QString cover = artworkDir + QStringLiteral("/front/Vector Game.png");
  QVERIFY(QFileInfo::exists(cover));
  // …and it is genuinely a PNG that loads, not an SVG wearing the extension.
  QImage loaded(cover);
  QVERIFY(!loaded.isNull());
  QCOMPARE(QImageReader(cover).format(), QByteArray("png"));
  // Rendered from the vector rather than upscaled from its 64px intrinsic
  // size, so the cover is usable on a 4K grid.
  QVERIFY(loaded.width() > 64);
}

// Kartend-g1g30: the sync stays OFFLINE — it never fetches a remote cover, it
// only decides which ones are worth fetching. Getting that decision here (not
// in the controller) keeps the fill-missing rule in one place, so a scraped or
// hand-placed cover is never downloaded over.
void TestLauncherImportService::remoteCoverIsPendingOnlyWhenTheSlotIsEmpty() {
  QString stubDir;
  QString artworkDir;
  freshDirs(stubDir, artworkDir);

  GameEntry remote = entry(QStringLiteral("Remote Art"), QStringLiteral("itch://caves/a/launch"));
  remote.coverUrl = QStringLiteral("https://img.itch.zone/abc/original/cover.png");
  GameEntry local = entry(QStringLiteral("Local Art"), QStringLiteral("itch://caves/b/launch"));
  local.coverUrl = QStringLiteral("https://img.itch.zone/def/original/cover.png");

  // Pre-place a cover for the second one, as a scrape or the user would.
  QVERIFY(QDir().mkpath(artworkDir + QStringLiteral("/front")));
  QFile existing(artworkDir + QStringLiteral("/front/Local Art.jpg"));
  QVERIFY(existing.open(QIODevice::WriteOnly));
  existing.write("x");
  existing.close();

  const auto result = LauncherImportService::syncEntries({remote, local}, QStringLiteral("itch"),
                                                         stubDir, artworkDir);
  QCOMPARE(result.syncedStubs.size(), 2);
  // Nothing was fetched — this call does no network at all.
  QCOMPARE(result.artworkCopied, 0);

  QString pendingFor;
  QString coveredFor;
  for (const SyncedStub &stub : result.syncedStubs) {
    const QString base = QFileInfo(stub.path).completeBaseName();
    if (!stub.pendingCoverUrl.isEmpty()) {
      pendingFor = base;
    } else {
      coveredFor = base;
    }
  }
  QCOMPARE(pendingFor, QStringLiteral("Remote Art"));
  QCOMPARE(coveredFor, QStringLiteral("Local Art"));

  // A source with no remote covers leaves every stub unflagged, so the
  // controller's pass is a no-op for Steam/Flatpak/Lutris/Bottles/XDG.
  QString otherStubs;
  QString otherArtwork;
  freshDirs(otherStubs, otherArtwork);
  const auto steam = LauncherImportService::syncEntries(
      {entry(QStringLiteral("Portal"), QStringLiteral("steam://rungameid/400"))},
      QStringLiteral("steam"), otherStubs, otherArtwork);
  QCOMPARE(steam.syncedStubs.size(), 1);
  QVERIFY(steam.syncedStubs.at(0).pendingCoverUrl.isEmpty());
}

// Kartend-ilkne: ES-DE yields one collection PER SYSTEM, so the pieces that
// were "one per source" have to become "one per slice". The stub directory is
// the one that bites hardest: a sync deletes the stubs whose source matches,
// so sharing a folder between systems would make each system's sync delete the
// others' games.
void TestLauncherImportService::multiCollectionSourceKeepsSlicesApart() {
  const CollectionConfig nes = LauncherImportService::makeCollectionConfig(
      QStringLiteral("esde"), LauncherImportService::ImportScope::Installed, QStringLiteral("nes"));
  const CollectionConfig snes = LauncherImportService::makeCollectionConfig(
      QStringLiteral("esde"), LauncherImportService::ImportScope::Installed,
      QStringLiteral("snes"));
  QCOMPARE(nes.importSource, QStringLiteral("esde"));
  QCOMPARE(nes.importSourceKey, QStringLiteral("nes"));
  QCOMPARE(nes.name, QStringLiteral("ES-DE: nes"));
  QCOMPARE(snes.importSourceKey, QStringLiteral("snes"));

  // Distinct managed folders — the whole point.
  QVERIFY(nes.mediaDirectory != snes.mediaDirectory);
  QVERIFY(nes.mediaDirectory.endsWith(QStringLiteral("launcher-imports/esde/_nes/games")));
  QVERIFY(snes.artworkDirectory.endsWith(QStringLiteral("launcher-imports/esde/_snes/artwork")));

  // No launcher is guessed: ES-DE's emulator command lives in files only ES-DE
  // can read, so a fabricated one would silently fail to launch.
  QVERIFY(nes.launcher.launcherPath.isEmpty());

  // A key comes from the user's ROM tree, so it must not be able to climb out
  // of the managed root. The property that matters is CONTAINMENT after
  // normalisation, not the absence of dots: the sanitizer strips path
  // separators, so "../../etc" becomes one oddly-named directory that cannot
  // traverse anywhere. cleanPath is what would collapse a real climb.
  const QString root =
      QDir::cleanPath(LauncherImportService::defaultBaseDir(QStringLiteral("esde")));
  const QString escaping = QDir::cleanPath(
      LauncherImportService::defaultBaseDir(QStringLiteral("esde"), QStringLiteral("../../etc")));
  QVERIFY(escaping.startsWith(root + QLatin1Char('/')));
  QVERIFY(!escaping.contains(QStringLiteral("/../")));

  // An empty key is the single-collection case every other source uses, and
  // must keep the pre-Kartend-ilkne layout exactly.
  const CollectionConfig steam =
      LauncherImportService::makeCollectionConfig(QStringLiteral("steam"));
  QVERIFY(steam.importSourceKey.isEmpty());
  QVERIFY(steam.mediaDirectory.endsWith(QStringLiteral("launcher-imports/steam/games")));

  // Sources that are one collection report no slices — "no slices" means "I am
  // one collection", not "I have nothing".
  QVERIFY(LauncherImportService::sourceSlices(QStringLiteral("steam")).isEmpty());
  QVERIFY(LauncherImportService::sourceSlices(QStringLiteral("xdg")).isEmpty());
}

void TestLauncherImportService::syncCarriesLaunchArgsOntoStubs() {
  QString stubDir;
  QString artworkDir;
  freshDirs(stubDir, artworkDir);
  GameEntry game = entry(QStringLiteral("The Game"), QStringLiteral("The Game"));
  game.launchArgs = {QStringLiteral("-b"), QStringLiteral("My Bottle"), QStringLiteral("--")};

  const auto result =
      LauncherImportService::syncEntries({game}, QStringLiteral("bottles"), stubDir, artworkDir);
  QCOMPARE(result.written, 1);
  const auto stub = KartLink::read(stubDir + QStringLiteral("/The Game.kartlink"));
  QVERIFY(!stub.isError());
  QCOMPARE(stub.value().args, game.launchArgs);

  // A re-sync compares the parsed stub against what it would write, so the
  // args have to take part in that comparison — otherwise a bottle rename
  // would leave every stub pointing at the old one forever.
  const auto again =
      LauncherImportService::syncEntries({game}, QStringLiteral("bottles"), stubDir, artworkDir);
  QCOMPARE(again.written, 0);
  QCOMPARE(again.unchanged, 1);

  game.launchArgs = {QStringLiteral("-b"), QStringLiteral("Renamed Bottle"), QStringLiteral("--")};
  const auto renamed =
      LauncherImportService::syncEntries({game}, QStringLiteral("bottles"), stubDir, artworkDir);
  QCOMPARE(renamed.written, 1);
}

void TestLauncherImportService::importScopeRoundTrips() {
  using LauncherImportService::ImportScope;
  for (const ImportScope scope :
       {ImportScope::Installed, ImportScope::Owned, ImportScope::AllRecognized}) {
    QCOMPARE(LauncherImportService::scopeFromString(LauncherImportService::scopeToString(scope)),
             scope);
  }
  // The persisted spelling is a file format — pin it so a rename can't
  // silently re-tier every existing collection on the next sync.
  QCOMPARE(LauncherImportService::scopeToString(ImportScope::Installed),
           QStringLiteral("installed"));
  QCOMPARE(LauncherImportService::scopeToString(ImportScope::Owned), QStringLiteral("owned"));
  QCOMPARE(LauncherImportService::scopeToString(ImportScope::AllRecognized),
           QStringLiteral("allRecognized"));
}

void TestLauncherImportService::importScopeUnknownNeverWidens() {
  using LauncherImportService::ImportScope;
  // Empty is what every collection imported before Kartend-el5st persists,
  // and a garbled value must not be treated as "wider" either: a sync at too
  // wide a tier silently adds games, and at too narrow a tier deletes them.
  QCOMPARE(LauncherImportService::scopeFromString(QString()), ImportScope::Installed);
  QCOMPARE(LauncherImportService::scopeFromString(QStringLiteral("   ")), ImportScope::Installed);
  QCOMPARE(LauncherImportService::scopeFromString(QStringLiteral("everything")),
           ImportScope::Installed);
  // Casing drift in a hand-edited INI still resolves to the intended tier.
  QCOMPARE(LauncherImportService::scopeFromString(QStringLiteral(" OWNED ")), ImportScope::Owned);
  QCOMPARE(LauncherImportService::scopeFromString(QStringLiteral("allrecognized")),
           ImportScope::AllRecognized);
}

void TestLauncherImportService::makeCollectionConfigRecordsScope() {
  // The scope has to land on the config, because the startup re-sync reads it
  // back from there; an unstamped collection would re-list at Installed and
  // prune every not-installed stub it holds.
  const CollectionConfig owned = LauncherImportService::makeCollectionConfig(
      QStringLiteral("steam"), LauncherImportService::ImportScope::Owned);
  QCOMPARE(owned.importScope, QStringLiteral("owned"));

  const CollectionConfig wide = LauncherImportService::makeCollectionConfig(
      QStringLiteral("steam"), LauncherImportService::ImportScope::AllRecognized);
  QCOMPARE(wide.importScope, QStringLiteral("allRecognized"));

  // Default argument keeps the pre-existing callers on the narrow tier.
  const CollectionConfig defaulted =
      LauncherImportService::makeCollectionConfig(QStringLiteral("steam"));
  QCOMPARE(defaulted.importScope, QStringLiteral("installed"));
}

// The store pass is a fire-and-forget network batch, so a sync has to be able
// to tell which games it never reached and resume just those.
void TestLauncherImportService::stubsMissingDescriptionSelectsUnenrichedOnly() {
  KartendTest::MigratedDb db;
  const QString dbPath = db.database().databaseName();
  const QString uuid = QStringLiteral("test-uuid-enrich");

  const QString enriched = QStringLiteral("/imports/steam/games/Enriched.kartlink");
  const QString blank = QStringLiteral("/imports/steam/games/Blank.kartlink");
  const QString whitespace = QStringLiteral("/imports/steam/games/Whitespace.kartlink");
  const QString absent = QStringLiteral("/imports/steam/games/NoRowYet.kartlink");

  QSqlDatabase handle = db.database();
  const auto writeRow = [&](const QString &path, const QString &description) {
    ItemMetadataStore::ItemMetadata row;
    row.collectionUuid = uuid;
    row.path = path;
    row.description = description;
    QVERIFY(!ItemMetadataStore::save(handle, row).isError());
  };
  writeRow(enriched, QStringLiteral("A real store description."));
  writeRow(blank, QString());
  // A description of only whitespace is as useless as none — the store pass
  // never landed for it either.
  writeRow(whitespace, QStringLiteral("   \n  "));

  const QList<SyncedStub> stubs = {
      {enriched, QStringLiteral("steam://rungameid/1"), QStringLiteral("Enriched")},
      {blank, QStringLiteral("steam://rungameid/2"), QStringLiteral("Blank")},
      {whitespace, QStringLiteral("steam://rungameid/3"), QStringLiteral("Whitespace")},
      // Never had a metadata row written at all.
      {absent, QStringLiteral("steam://rungameid/4"), QStringLiteral("NoRowYet")},
  };

  QStringList missing = LauncherImportService::stubsMissingDescription(dbPath, uuid, stubs);
  missing.sort();
  QStringList expected{blank, whitespace, absent};
  expected.sort();
  QCOMPARE(missing, expected);
  QVERIFY2(!missing.contains(enriched), "re-requested a page whose text was already stored");
}

void TestLauncherImportService::stubsMissingDescriptionFailsOpen() {
  const QList<SyncedStub> stubs = {{QStringLiteral("/imports/steam/games/A.kartlink"),
                                    QStringLiteral("steam://rungameid/1"), QStringLiteral("A")}};
  // Unusable inputs must return everything, not nothing: an extra request is
  // cheap, a permanently missing description is not.
  QCOMPARE(LauncherImportService::stubsMissingDescription(QString(), QStringLiteral("uuid"), stubs),
           QStringList{stubs.first().path});
  QCOMPARE(LauncherImportService::stubsMissingDescription(QStringLiteral("/nonexistent/x.db"),
                                                          QStringLiteral("uuid"), stubs),
           QStringList{stubs.first().path});
  QVERIFY(LauncherImportService::stubsMissingDescription(QStringLiteral("/tmp/x.db"),
                                                         QStringLiteral("uuid"), {})
              .isEmpty());
}

// The appinfo pass runs on every sync, long after the store pass has written
// descriptions. appinfo carries no descriptions at all, so a load/fill/save
// round-trip that dropped the field would silently wipe every one of them on
// the next launch — invisible until a user restarts. Pin it.
void TestLauncherImportService::steamMetadataPreservesScrapedDescription() {
  KartendTest::MigratedDb db;
  const QString dbPath = db.database().databaseName();
  const QString appInfo = stageAppInfo();
  QVERIFY(!appInfo.isEmpty());
  const QString uuid = QStringLiteral("test-uuid-preserve-desc");
  const QString stubPath = QStringLiteral("/imports/steam/games/Portal 2.kartlink");

  QSqlDatabase handle = db.database();
  ItemMetadataStore::ItemMetadata seeded;
  seeded.collectionUuid = uuid;
  seeded.path = stubPath;
  seeded.description = QStringLiteral("Store text the appinfo pass must not touch.");
  QVERIFY(!ItemMetadataStore::save(handle, seeded).isError());

  const QList<SyncedStub> stubs = {
      {stubPath, QStringLiteral("steam://rungameid/620"), QStringLiteral("Portal 2")}};
  const auto result = LauncherImportService::applySteamMetadata(dbPath, uuid, stubs, appInfo);
  QVERIFY(result.errors.isEmpty());

  const auto after = ItemMetadataStore::load(handle, uuid, stubPath);
  QVERIFY(!after.isError());
  QCOMPARE(after.value().description, seeded.description);
  // …and the pass still did its own job on the fields appinfo owns.
  QCOMPARE(after.value().developer, QStringLiteral("Valve"));
}

void TestLauncherImportService::steamMetadataFillsEmptyRows() {
  KartendTest::MigratedDb db;
  const QString dbPath = db.database().databaseName();
  const QString appInfo = stageAppInfo();
  QVERIFY(!appInfo.isEmpty());

  const QString uuid = QStringLiteral("test-uuid-fill");
  const QString stubPath = QStringLiteral("/imports/steam/games/Portal 2.kartlink");
  const QList<SyncedStub> stubs = {
      {stubPath, QStringLiteral("steam://rungameid/620"), QStringLiteral("Portal 2")},
      // Non-Steam target: ignored without error.
      {QStringLiteral("/imports/flatpak/games/Game.kartlink"), QStringLiteral("org.example.Game"),
       QStringLiteral("Game")},
  };
  const auto result = LauncherImportService::applySteamMetadata(dbPath, uuid, stubs, appInfo);
  QVERIFY(result.errors.isEmpty());
  QCOMPARE(result.rowsWritten, 1);
  QCOMPARE(result.writtenPaths, QStringList{stubPath});

  QSqlDatabase handle = db.database();
  auto loaded = ItemMetadataStore::load(handle, uuid, stubPath);
  QVERIFY(!loaded.isError());
  const ItemMetadataStore::ItemMetadata row = loaded.value();
  QCOMPARE(row.title, QStringLiteral("Portal 2"));
  QCOMPARE(row.developer, QStringLiteral("Valve"));
  QCOMPARE(row.publisher, QStringLiteral("Valve Publishing"));
  QCOMPARE(row.releaseDate, QStringLiteral("2011-04-19"));
  QCOMPARE(row.genre, QStringLiteral("Action, Adventure"));
  QCOMPARE(row.players, QStringLiteral("Single-player, Online Co-op"));
  QCOMPARE(row.source, QStringLiteral("steam"));
  const auto fields = ItemMetadataStore::parseCustomFields(row.customFields);
  bool sawMetacritic = false;
  for (const auto &field : fields) {
    if (field.first == QStringLiteral("Metacritic")) {
      sawMetacritic = true;
      QCOMPARE(field.second, QStringLiteral("95"));
    }
  }
  QVERIFY(sawMetacritic);

  // Idempotent: nothing left to fill on a second pass.
  const auto again = LauncherImportService::applySteamMetadata(dbPath, uuid, stubs, appInfo);
  QCOMPARE(again.rowsWritten, 0);
}

void TestLauncherImportService::steamMetadataNeverOverwritesExistingFields() {
  KartendTest::MigratedDb db;
  const QString dbPath = db.database().databaseName();
  const QString appInfo = stageAppInfo();
  QVERIFY(!appInfo.isEmpty());

  const QString uuid = QStringLiteral("test-uuid-merge");
  const QString stubPath = QStringLiteral("/imports/steam/games/Portal 2.kartlink");
  // Pre-existing user edits: custom title + developer + user attribution.
  ItemMetadataStore::ItemMetadata existing;
  existing.collectionUuid = uuid;
  existing.path = stubPath;
  existing.title = QStringLiteral("My Custom Title");
  existing.developer = QStringLiteral("Hand-edited Dev");
  existing.source = QStringLiteral("user");
  QSqlDatabase handle = db.database();
  QVERIFY(!ItemMetadataStore::save(handle, existing).isError());

  const QList<SyncedStub> stubs = {
      {stubPath, QStringLiteral("steam://rungameid/620"), QStringLiteral("Portal 2")}};
  const auto result = LauncherImportService::applySteamMetadata(dbPath, uuid, stubs, appInfo);
  QCOMPARE(result.rowsWritten, 1); // publisher/genre/... were still empty

  auto loaded = ItemMetadataStore::load(handle, uuid, stubPath);
  QVERIFY(!loaded.isError());
  const ItemMetadataStore::ItemMetadata row = loaded.value();
  // User values and attribution survive; only gaps were filled.
  QCOMPARE(row.title, QStringLiteral("My Custom Title"));
  QCOMPARE(row.developer, QStringLiteral("Hand-edited Dev"));
  QCOMPARE(row.source, QStringLiteral("user"));
  QCOMPARE(row.publisher, QStringLiteral("Valve Publishing"));
  QCOMPARE(row.genre, QStringLiteral("Action, Adventure"));
}

void TestLauncherImportService::steamMetadataMissingAppInfoReportsError() {
  KartendTest::MigratedDb db;
  const QList<SyncedStub> stubs = {{QStringLiteral("/x/Game.kartlink"),
                                    QStringLiteral("steam://rungameid/620"),
                                    QStringLiteral("Game")}};
  const auto result = LauncherImportService::applySteamMetadata(
      db.database().databaseName(), QStringLiteral("u"), stubs,
      m_dir.filePath(QStringLiteral("missing-appinfo.vdf")));
  QCOMPARE(result.rowsWritten, 0);
  QVERIFY(!result.errors.isEmpty());
}

// ─────────────────────────────────────────────────────────────────────────────
// removeManagedImportDirs (Kartend-i366w) — the containment rule IS the
// feature: only dirs provably inside the managed launcher-imports root may
// be deleted by the removal checkbox.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

CollectionConfig importCollection(const QString &base, const QString &source = "steam") {
  CollectionConfig cfg;
  cfg.name = QStringLiteral("Steam");
  cfg.importSource = source;
  cfg.mediaDirectory = base + QStringLiteral("/games");
  cfg.artworkDirectory = base + QStringLiteral("/artwork");
  return cfg;
}

void touch(const QString &path) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x");
}

} // namespace

void TestLauncherImportService::cleanupDeletesManagedDirsAndEmptyBase() {
  const QString base = m_dir.filePath(QStringLiteral("cleanup%1").arg(m_caseCounter++));
  touch(base + QStringLiteral("/games/Portal 2.kartlink"));
  touch(base + QStringLiteral("/artwork/front/Portal 2.jpg"));

  const auto result = LauncherImportService::removeManagedImportDirs(importCollection(base), base);
  QCOMPARE(result.removedDirs.size(), 2);
  QVERIFY(result.errors.isEmpty());
  QVERIFY(result.skippedUnmanaged.isEmpty());
  // Both children gone, and the emptied base itself was dropped too.
  QVERIFY(!QDir(base).exists());
}

void TestLauncherImportService::cleanupRefusesUnmanagedAndTraversalDirs() {
  const QString base = m_dir.filePath(QStringLiteral("cleanup%1").arg(m_caseCounter++));
  const QString victim = m_dir.filePath(QStringLiteral("victim%1").arg(m_caseCounter++));
  touch(victim + QStringLiteral("/precious.jpg"));
  touch(base + QStringLiteral("/games/stub.kartlink"));

  // The artwork dir was re-pointed at the user's own folder; the media dir
  // dresses a traversal up in a managed prefix. Neither may be deleted.
  CollectionConfig cfg = importCollection(base);
  cfg.artworkDirectory = victim;
  cfg.mediaDirectory = base + QStringLiteral("/games/../../victim%1").arg(m_caseCounter - 1);

  const auto result = LauncherImportService::removeManagedImportDirs(cfg, base);
  QVERIFY(result.removedDirs.isEmpty());
  QCOMPARE(result.skippedUnmanaged.size(), 2);
  QVERIFY(QFileInfo::exists(victim + QStringLiteral("/precious.jpg")));
  QVERIFY(QFileInfo::exists(base + QStringLiteral("/games/stub.kartlink")));
}

void TestLauncherImportService::cleanupIsNoOpForNonImportCollections() {
  const QString base = m_dir.filePath(QStringLiteral("cleanup%1").arg(m_caseCounter++));
  touch(base + QStringLiteral("/games/file.mp4"));

  CollectionConfig cfg = importCollection(base, /*source=*/QString());
  const auto result = LauncherImportService::removeManagedImportDirs(cfg, base);
  QVERIFY(result.removedDirs.isEmpty());
  QVERIFY(result.skippedUnmanaged.isEmpty());
  QVERIFY(QFileInfo::exists(base + QStringLiteral("/games/file.mp4")));
}

void TestLauncherImportService::cleanupKeepsBaseWhenUserContentRemains() {
  const QString base = m_dir.filePath(QStringLiteral("cleanup%1").arg(m_caseCounter++));
  touch(base + QStringLiteral("/games/stub.kartlink"));
  touch(base + QStringLiteral("/artwork/front/a.jpg"));
  touch(base + QStringLiteral("/notes.txt")); // the user's own file

  const auto result = LauncherImportService::removeManagedImportDirs(importCollection(base), base);
  QCOMPARE(result.removedDirs.size(), 2);
  // The plain (non-recursive) base rmdir must fail closed: the user's file
  // — and with it the base dir — survives the cleanup.
  QVERIFY(QFileInfo::exists(base + QStringLiteral("/notes.txt")));
}

void TestLauncherImportService::watchPathsAreDirectoriesThatExist() {
  // Machine-independent invariants: this runs wherever the suite runs, and
  // which launchers are installed is none of its business. What it pins is
  // the contract that keeps the watch alive.
  const QStringList sources = {QStringLiteral("steam"),   QStringLiteral("flatpak"),
                               QStringLiteral("lutris"),  QStringLiteral("heroic"),
                               QStringLiteral("itch"),    QStringLiteral("bottles"),
                               QStringLiteral("xdg"),     QStringLiteral("esde")};
  for (const QString &sourceId : sources) {
    const QStringList paths = LauncherImportService::watchPaths(sourceId);
    QSet<QString> seen;
    for (const QString &path : paths) {
      // DIRECTORIES ONLY. Steam replaces appmanifest_*.acf during a download
      // and QFileSystemWatcher drops a replaced file permanently, so a file
      // entry here would make the watch die exactly when it mattered.
      QVERIFY2(QFileInfo(path).isDir(),
               qPrintable(QStringLiteral("%1: %2 is not a directory").arg(sourceId, path)));
      QVERIFY2(!seen.contains(path),
               qPrintable(QStringLiteral("%1: %2 listed twice").arg(sourceId, path)));
      seen.insert(path);
    }
  }
}

void TestLauncherImportService::watchPathsIgnoreUnknownSources() {
  // Callers hand over whatever importSource a collection carries, including
  // one written by a newer build; an unknown id must be silent, not a crash
  // and not a stray watch.
  QVERIFY(LauncherImportService::watchPaths(QStringLiteral("not-a-launcher")).isEmpty());
  QVERIFY(LauncherImportService::watchPaths(QString()).isEmpty());
}

QTEST_MAIN(TestLauncherImportService)
#include "test_launcherimportservice.moc"
