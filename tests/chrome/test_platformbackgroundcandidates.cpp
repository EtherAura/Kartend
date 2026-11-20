// Platform-background candidate source (Kartend-ji94z foundation slice).
//
// A platform scrape keeps every still image under
// _shared/<type>/platform_<systemeid>.<ext>, but only one is wired into the
// collection background. The helper under test lists the retained
// illustration / photo / background candidates for ONE platform so a later
// picker can show them; these tests pin what it returns (and what it must not
// leak) against a real on-disk shared-art tree of synthetic files. The
// PlatformBackgroundChoice cases then feed that real list to the value-level
// pick operation and check the proposed CollectionBackground.
#include "platformbackgroundcandidates.h"
#include "platformbackgroundchoice.h"

#include "collection/collectionconfig.h"
#include "collectionplatformbackgrounds.h"
#include "screenscrapersystems.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QMap>
#include <QTemporaryDir>
#include <QTest>

using PlatformBackgroundCandidates::Candidate;

namespace {

/// Write a small synthetic file (content is irrelevant — the helper must not
/// decode anything) at `root/relPath`, creating parent dirs.
void touch(const QString &root, const QString &relPath, const QByteArray &content = "x") {
  const QString full = QDir(root).filePath(relPath);
  QVERIFY(QDir().mkpath(QFileInfo(full).absolutePath()));
  QFile f(full);
  QVERIFY(f.open(QIODevice::WriteOnly));
  QCOMPARE(f.write(content), content.size());
}

/// Relative path -> SHA-1 of every file under `root`, to prove enumeration
/// leaves the tree untouched.
QMap<QString, QByteArray> snapshot(const QString &root) {
  QMap<QString, QByteArray> out;
  QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
  const QDir base(root);
  while (it.hasNext()) {
    const QString path = it.next();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) continue;
    out[base.relativeFilePath(path)] =
        QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha1);
  }
  return out;
}

/// A background with every field moved off its default, so a choice that
/// clobbers anything besides the three wallpaper fields shows up in QCOMPARE.
CollectionBackground videoBackgroundWithEverythingSet() {
  CollectionBackground bg;
  bg.backgroundType = BackgroundType::Video;
  bg.backgroundColor = QStringLiteral("#1a1a2e");
  bg.backgroundImage = QStringLiteral("/old/wallpaper.png");
  bg.backgroundImageChosenPath = QString();
  bg.backgroundVideo = QStringLiteral("/old/loop.mp4");
  bg.primaryColor = QStringLiteral("#223344");
  bg.toolbarColorSource = ToolbarColorSource::CollectionPrimary;
  bg.tileColor = QStringLiteral("#556677");
  bg.selectionColor = QStringLiteral("#8899aa");
  bg.headerLogoImage = QStringLiteral("/old/header-logo.png");
  bg.headerLogoPosition = HeaderLogoPosition::TopLeft;
  bg.vignetteEnabled = true;
  bg.vignetteIntensity = 41;
  bg.wallpaperParallax = true;
  bg.parallaxStrength = 77;
  bg.toolbarBackdropBlur = true;
  bg.backdropBlurRadius = 19;
  return bg;
}

/// A two-row synthetic systems catalog: autodetect lands on 1 for a
/// "megadrive" collection scanning .md and on 2 for "snes" / .sfc.
QList<ScreenScraperSystems::System> syntheticSystems() {
  ScreenScraperSystems::System md;
  md.id = 1;
  md.displayName = QStringLiteral("Mega Drive");
  md.extensions = {QStringLiteral("md")};
  md.aliases = {QStringLiteral("megadrive")};
  ScreenScraperSystems::System snes;
  snes.id = 2;
  snes.displayName = QStringLiteral("Super Nintendo");
  snes.extensions = {QStringLiteral("sfc")};
  snes.aliases = {QStringLiteral("snes")};
  return {md, snes};
}

CollectionConfig collectionAt(const QString &name, const QString &artworkDir, int overrideId,
                              const QStringList &extensions = {}) {
  CollectionConfig cfg;
  cfg.name = name;
  cfg.artworkDirectory = artworkDir;
  cfg.extensions = extensions;
  cfg.scraperOverrides.screenscraperSystemId = overrideId;
  return cfg;
}

} // namespace

class TestPlatformBackgroundCandidates : public QObject {
  Q_OBJECT

private slots:
  void backgroundTypesMirrorProviderPriority();
  void findsAllThreeTypesForOnePlatform();
  void keepsSupportedStillImageExtensions();
  void excludesUnrelatedFiles();
  void missingOrEmptyRootYieldsNothing();
  void platformWithoutArtYieldsNothing();
  void rejectsNonNumericIds();
  void repeatedEnumerationIsDeterministicAndReadOnly();
  void choiceSwitchesToImageAndMarksExactPath();
  void choiceOfDisplayedUnmarkedImageRecordsMarker();
  void choiceRejectsEmptyMissingStaleAndOtherPlatformPaths();
  void collectionSourceListsOnlyItsOwnPlatformAndRoot();
  void collectionSourceResolvesTheScrapeSystemId();
  void collectionSourceUnmappedOrRootlessYieldsNothing();
};

void TestPlatformBackgroundCandidates::backgroundTypesMirrorProviderPriority() {
  // kPlatformMediaTypes Background rolePriority: illustration 0, photo 1,
  // background 2.
  QCOMPARE(PlatformBackgroundCandidates::backgroundTypes(),
           (QStringList{QStringLiteral("illustration"), QStringLiteral("photo"),
                        QStringLiteral("background")}));
}

void TestPlatformBackgroundCandidates::findsAllThreeTypesForOnePlatform() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString shared = QDir(tmp.path()).filePath(QStringLiteral("_shared"));
  touch(shared, QStringLiteral("background/platform_57.png"));
  touch(shared, QStringLiteral("photo/platform_57.png"));
  touch(shared, QStringLiteral("illustration/platform_57.png"));

  const QList<Candidate> got =
      PlatformBackgroundCandidates::enumerate(shared, QStringLiteral("57"));
  const QDir s(shared);
  const QList<Candidate> want{
      {QStringLiteral("illustration"), s.filePath(QStringLiteral("illustration/platform_57.png"))},
      {QStringLiteral("photo"), s.filePath(QStringLiteral("photo/platform_57.png"))},
      {QStringLiteral("background"), s.filePath(QStringLiteral("background/platform_57.png"))},
  };
  QCOMPARE(got, want);
  for (const Candidate &c : got) QVERIFY(QFileInfo::exists(c.path));
}

void TestPlatformBackgroundCandidates::keepsSupportedStillImageExtensions() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString shared = QDir(tmp.path()).filePath(QStringLiteral("_shared"));
  touch(shared, QStringLiteral("illustration/platform_3.png"));
  touch(shared, QStringLiteral("illustration/platform_3.jpg"));
  touch(shared, QStringLiteral("photo/platform_3.jpeg"));
  touch(shared, QStringLiteral("photo/platform_3.webp"));
  touch(shared, QStringLiteral("background/platform_3.JPG"));

  const QList<Candidate> got = PlatformBackgroundCandidates::enumerate(shared, QStringLiteral("3"));
  const QDir s(shared);
  const QList<Candidate> want{
      {QStringLiteral("illustration"), s.filePath(QStringLiteral("illustration/platform_3.jpg"))},
      {QStringLiteral("illustration"), s.filePath(QStringLiteral("illustration/platform_3.png"))},
      {QStringLiteral("photo"), s.filePath(QStringLiteral("photo/platform_3.jpeg"))},
      {QStringLiteral("photo"), s.filePath(QStringLiteral("photo/platform_3.webp"))},
      {QStringLiteral("background"), s.filePath(QStringLiteral("background/platform_3.JPG"))},
  };
  QCOMPARE(got, want);
}

void TestPlatformBackgroundCandidates::excludesUnrelatedFiles() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString shared = QDir(tmp.path()).filePath(QStringLiteral("_shared"));
  // The one real candidate.
  touch(shared, QStringLiteral("illustration/platform_1.png"));
  // Other platforms — including id prefixes/suffixes of "1".
  touch(shared, QStringLiteral("illustration/platform_12.png"));
  touch(shared, QStringLiteral("photo/platform_21.png"));
  touch(shared, QStringLiteral("background/platform_10.png"));
  // Collection / company / group scoped art in the same type dirs.
  touch(shared, QStringLiteral("background/collection_1.png"));
  touch(shared, QStringLiteral("background/company_1.png"));
  touch(shared, QStringLiteral("photo/group_1.png"));
  // Non-background platform types.
  touch(shared, QStringLiteral("wheel/platform_1.png"));
  touch(shared, QStringLiteral("logo/platform_1.png"));
  touch(shared, QStringLiteral("logo-svg/platform_1.svg"));
  touch(shared, QStringLiteral("controller/platform_1.png"));
  touch(shared, QStringLiteral("box-2D/platform_1.png"));
  // Non-image / non-still files and odd names for the right platform.
  touch(shared, QStringLiteral("photo/platform_1.txt"));
  touch(shared, QStringLiteral("photo/platform_1.svg"));
  touch(shared, QStringLiteral("photo/platform_1.mp4"));
  touch(shared, QStringLiteral("photo/platform_1.old.png"));
  touch(shared, QStringLiteral("photo/platform_1"));
  // A directory that merely looks like a candidate.
  QVERIFY(QDir().mkpath(QDir(shared).filePath(QStringLiteral("background/platform_1.png.d"))));
  QVERIFY(QDir().mkpath(QDir(shared).filePath(QStringLiteral("background/platform_1.png"))));
  // Per-item art outside _shared.
  touch(tmp.path(), QStringLiteral("illustration/platform_1.png"));
  touch(tmp.path(), QStringLiteral("box-2D/Some Game.png"));

  const QList<Candidate> got = PlatformBackgroundCandidates::enumerate(shared, QStringLiteral("1"));
  const QList<Candidate> want{
      {QStringLiteral("illustration"),
       QDir(shared).filePath(QStringLiteral("illustration/platform_1.png"))},
  };
  QCOMPARE(got, want);
}

void TestPlatformBackgroundCandidates::missingOrEmptyRootYieldsNothing() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString missing = QDir(tmp.path()).filePath(QStringLiteral("nope/_shared"));
  QVERIFY(PlatformBackgroundCandidates::enumerate(missing, QStringLiteral("1")).isEmpty());
  QVERIFY(!QFileInfo::exists(missing)); // and it did not create it

  QVERIFY(PlatformBackgroundCandidates::enumerate(QString(), QStringLiteral("1")).isEmpty());

  const QString empty = QDir(tmp.path()).filePath(QStringLiteral("_shared"));
  QVERIFY(QDir().mkpath(empty));
  QVERIFY(PlatformBackgroundCandidates::enumerate(empty, QStringLiteral("1")).isEmpty());

  // Type dirs present but empty.
  QVERIFY(QDir().mkpath(QDir(empty).filePath(QStringLiteral("illustration"))));
  QVERIFY(QDir().mkpath(QDir(empty).filePath(QStringLiteral("photo"))));
  QVERIFY(PlatformBackgroundCandidates::enumerate(empty, QStringLiteral("1")).isEmpty());
}

void TestPlatformBackgroundCandidates::platformWithoutArtYieldsNothing() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString shared = QDir(tmp.path()).filePath(QStringLiteral("_shared"));
  touch(shared, QStringLiteral("illustration/platform_4.png"));
  touch(shared, QStringLiteral("wheel/platform_9.png")); // logo only for 9
  QVERIFY(PlatformBackgroundCandidates::enumerate(shared, QStringLiteral("9")).isEmpty());
  QVERIFY(PlatformBackgroundCandidates::enumerate(shared, QStringLiteral("5")).isEmpty());
  QVERIFY(PlatformBackgroundCandidates::enumerate(shared, QString()).isEmpty());
}

void TestPlatformBackgroundCandidates::rejectsNonNumericIds() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString shared = QDir(tmp.path()).filePath(QStringLiteral("_shared"));
  touch(shared, QStringLiteral("illustration/platform_4.png"));
  touch(shared, QStringLiteral("illustration/platform_abc.png"));
  // A wildcard must not widen the match to every platform.
  QVERIFY(PlatformBackgroundCandidates::enumerate(shared, QStringLiteral("*")).isEmpty());
  QVERIFY(PlatformBackgroundCandidates::enumerate(shared, QStringLiteral("4*")).isEmpty());
  QVERIFY(PlatformBackgroundCandidates::enumerate(shared, QStringLiteral("abc")).isEmpty());
  QVERIFY(PlatformBackgroundCandidates::enumerate(shared, QStringLiteral("../4")).isEmpty());
}

void TestPlatformBackgroundCandidates::repeatedEnumerationIsDeterministicAndReadOnly() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString shared = QDir(tmp.path()).filePath(QStringLiteral("_shared"));
  touch(shared, QStringLiteral("photo/platform_8.webp"), "photo-bytes");
  touch(shared, QStringLiteral("illustration/platform_8.png"), "illus-bytes");
  touch(shared, QStringLiteral("background/platform_8.jpg"), "bg-bytes");
  touch(shared, QStringLiteral("illustration/platform_8.jpg"), "illus2-bytes");
  touch(shared, QStringLiteral("wheel/platform_8.png"), "wheel-bytes");

  const QMap<QString, QByteArray> before = snapshot(tmp.path());
  QCOMPARE(before.size(), 5);

  const QList<Candidate> first =
      PlatformBackgroundCandidates::enumerate(shared, QStringLiteral("8"));
  const QList<Candidate> second =
      PlatformBackgroundCandidates::enumerate(shared, QStringLiteral("8"));
  QCOMPARE(first.size(), 4);
  QCOMPARE(second, first);
  QCOMPARE(first.at(0).type, QStringLiteral("illustration"));
  QCOMPARE(first.at(0).path, QDir(shared).filePath(QStringLiteral("illustration/platform_8.jpg")));
  QCOMPARE(first.at(3).type, QStringLiteral("background"));
  QCOMPARE(first.at(3).path, QDir(shared).filePath(QStringLiteral("background/platform_8.jpg")));

  QCOMPARE(snapshot(tmp.path()), before);
}

void TestPlatformBackgroundCandidates::choiceSwitchesToImageAndMarksExactPath() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString shared = QDir(tmp.path()).filePath(QStringLiteral("_shared"));
  touch(shared, QStringLiteral("illustration/platform_57.png"));
  touch(shared, QStringLiteral("photo/platform_57.jpg"));
  const QList<Candidate> candidates =
      PlatformBackgroundCandidates::enumerate(shared, QStringLiteral("57"));
  QCOMPARE(candidates.size(), 2);
  const QString picked = candidates.at(1).path;

  const CollectionBackground current = videoBackgroundWithEverythingSet();
  const CollectionBackground original = current;
  const std::optional<CollectionBackground> got =
      PlatformBackgroundChoice::choose(current, candidates, picked);
  QVERIFY(got.has_value());
  QCOMPARE(current, original);

  QCOMPARE(got->backgroundType, BackgroundType::Image);
  QCOMPARE(got->backgroundImage, picked);
  QCOMPARE(got->backgroundImageChosenPath, picked);

  // Everything else — header logo and the prior video path included — kept.
  CollectionBackground want = original;
  want.backgroundType = BackgroundType::Image;
  want.backgroundImage = picked;
  want.backgroundImageChosenPath = picked;
  QCOMPARE(*got, want);
  QCOMPARE(got->headerLogoImage, QStringLiteral("/old/header-logo.png"));
  QCOMPARE(got->headerLogoPosition, HeaderLogoPosition::TopLeft);
  QCOMPARE(got->backgroundVideo, QStringLiteral("/old/loop.mp4"));
}

void TestPlatformBackgroundCandidates::choiceOfDisplayedUnmarkedImageRecordsMarker() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString shared = QDir(tmp.path()).filePath(QStringLiteral("_shared"));
  touch(shared, QStringLiteral("background/platform_3.png"));
  const QList<Candidate> candidates =
      PlatformBackgroundCandidates::enumerate(shared, QStringLiteral("3"));
  QCOMPARE(candidates.size(), 1);
  const QString displayed = candidates.at(0).path;

  // The scrape already wired this file in, but nobody chose it.
  CollectionBackground current = videoBackgroundWithEverythingSet();
  current.backgroundType = BackgroundType::Image;
  current.backgroundImage = displayed;
  current.backgroundImageChosenPath.clear();

  const std::optional<CollectionBackground> got =
      PlatformBackgroundChoice::choose(current, candidates, displayed);
  QVERIFY(got.has_value());
  QVERIFY(*got != current);
  CollectionBackground want = current;
  want.backgroundImageChosenPath = displayed;
  QCOMPARE(*got, want);
}

void TestPlatformBackgroundCandidates::choiceRejectsEmptyMissingStaleAndOtherPlatformPaths() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString shared = QDir(tmp.path()).filePath(QStringLiteral("_shared"));
  touch(shared, QStringLiteral("illustration/platform_1.png"));
  touch(shared, QStringLiteral("photo/platform_1.png"));
  touch(shared, QStringLiteral("illustration/platform_2.png"));
  const QDir s(shared);

  const QList<Candidate> platform1 =
      PlatformBackgroundCandidates::enumerate(shared, QStringLiteral("1"));
  const QList<Candidate> platform2 =
      PlatformBackgroundCandidates::enumerate(shared, QStringLiteral("2"));
  QCOMPARE(platform1.size(), 2);
  QCOMPARE(platform2.size(), 1);

  const CollectionBackground original = videoBackgroundWithEverythingSet();
  const CollectionBackground current = original;
  const auto choose = [&](const QList<Candidate> &list, const QString &path) {
    return PlatformBackgroundChoice::choose(current, list, path);
  };

  // Cancelled pick.
  QVERIFY(!choose(platform1, QString()).has_value());
  // Path that was never a candidate (and does not exist), and look-alikes that
  // share the extension, directory prefix or file name of a real candidate.
  QVERIFY(!choose(platform1, s.filePath(QStringLiteral("background/platform_1.png"))).has_value());
  QVERIFY(!choose(platform1, s.filePath(QStringLiteral("illustration"))).has_value());
  QVERIFY(!choose(platform1, QStringLiteral("platform_1.png")).has_value());
  QVERIFY(!choose(platform1, platform1.at(0).path + QStringLiteral(".png")).has_value());
  QVERIFY(
      !choose(platform1, platform1.at(0).path.left(platform1.at(0).path.size() - 1)).has_value());
  // Another platform's candidate against this platform's list.
  QVERIFY(!choose(platform1, platform2.at(0).path).has_value());
  // Empty list: nothing can match.
  QVERIFY(!choose({}, platform1.at(0).path).has_value());

  // Stale: the file was listed earlier but is gone from a fresh enumeration.
  const QString stalePath = s.filePath(QStringLiteral("photo/platform_1.png"));
  QVERIFY(QFile::remove(stalePath));
  const QList<Candidate> fresh =
      PlatformBackgroundCandidates::enumerate(shared, QStringLiteral("1"));
  QCOMPARE(fresh.size(), 1);
  QVERIFY(!choose(fresh, stalePath).has_value());

  QCOMPARE(current, original);
}

void TestPlatformBackgroundCandidates::collectionSourceListsOnlyItsOwnPlatformAndRoot() {
  // Two collections' art roots. Root A holds platform 1 AND platform 2 art in
  // the same _shared tree; root B holds identically named platform 1 files.
  QTemporaryDir rootA;
  QTemporaryDir rootB;
  QVERIFY(rootA.isValid());
  QVERIFY(rootB.isValid());
  const QString sharedA = QDir(rootA.path()).filePath(QStringLiteral("_shared"));
  const QString sharedB = QDir(rootB.path()).filePath(QStringLiteral("_shared"));
  touch(sharedA, QStringLiteral("background/platform_1.jpg"));
  touch(sharedA, QStringLiteral("illustration/platform_1.png"));
  touch(sharedA, QStringLiteral("wheel/platform_1.png")); // not a background role
  touch(sharedA, QStringLiteral("photo/platform_2.png"));
  touch(sharedA, QStringLiteral("illustration/platform_2.png"));
  touch(sharedB, QStringLiteral("illustration/platform_1.png"));
  touch(sharedB, QStringLiteral("photo/platform_1.png"));
  const QMap<QString, QByteArray> beforeA = snapshot(rootA.path());

  const QList<ScreenScraperSystems::System> systems = syntheticSystems();
  const CollectionConfig cfgA = collectionAt(QStringLiteral("Genesis"), rootA.path(), 1);

  QCOMPARE(CollectionPlatformBackgrounds::sharedRoot(cfgA), sharedA);
  QCOMPARE(CollectionPlatformBackgrounds::systemId(cfgA, systems), 1);
  const QList<Candidate> got = CollectionPlatformBackgrounds::forCollection(cfgA, systems);
  const QDir a(sharedA);
  const QList<Candidate> want{
      {QStringLiteral("illustration"), a.filePath(QStringLiteral("illustration/platform_1.png"))},
      {QStringLiteral("background"), a.filePath(QStringLiteral("background/platform_1.jpg"))},
  };
  QCOMPARE(got, want);
  // Exactly the enumerator's answer for the resolved root/id: same order,
  // same paths, same supported types.
  QCOMPARE(got, PlatformBackgroundCandidates::enumerate(sharedA, QStringLiteral("1")));
  for (const Candidate &c : got) {
    QVERIFY2(c.path.startsWith(sharedA + QLatin1Char('/')), qPrintable(c.path));
    QVERIFY(!c.path.contains(QStringLiteral("platform_2")));
  }
  QCOMPARE(snapshot(rootA.path()), beforeA);

  // The same platform under the other root lists root B's files only.
  const CollectionConfig cfgB = collectionAt(QStringLiteral("Mega Drive B"), rootB.path(), 1);
  const QDir b(sharedB);
  const QList<Candidate> wantB{
      {QStringLiteral("illustration"), b.filePath(QStringLiteral("illustration/platform_1.png"))},
      {QStringLiteral("photo"), b.filePath(QStringLiteral("photo/platform_1.png"))},
  };
  QCOMPARE(CollectionPlatformBackgrounds::forCollection(cfgB, systems), wantB);

  // Platform 2 in root A sees platform 2's art, never platform 1's.
  const CollectionConfig cfgA2 = collectionAt(QStringLiteral("SFC"), rootA.path(), 2);
  const QList<Candidate> wantA2{
      {QStringLiteral("illustration"), a.filePath(QStringLiteral("illustration/platform_2.png"))},
      {QStringLiteral("photo"), a.filePath(QStringLiteral("photo/platform_2.png"))},
  };
  QCOMPARE(CollectionPlatformBackgrounds::forCollection(cfgA2, systems), wantA2);
}

void TestPlatformBackgroundCandidates::collectionSourceResolvesTheScrapeSystemId() {
  // No override: the id comes from the same override → autodetect resolver
  // ScreenScraperProvider::resolveSystemId delegates to.
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString shared = QDir(root.path()).filePath(QStringLiteral("_shared"));
  touch(shared, QStringLiteral("photo/platform_1.png"));
  touch(shared, QStringLiteral("photo/platform_2.png"));
  const QList<ScreenScraperSystems::System> systems = syntheticSystems();

  const CollectionConfig snes =
      collectionAt(QStringLiteral("snes"), root.path(), -1, {QStringLiteral("sfc")});
  QCOMPARE(ScreenScraperSystems::resolveForCollection(-1, snes.name, snes.type, snes.extensions,
                                                      systems),
           2);
  QCOMPARE(CollectionPlatformBackgrounds::systemId(snes, systems), 2);
  const QList<Candidate> want{
      {QStringLiteral("photo"), QDir(shared).filePath(QStringLiteral("photo/platform_2.png"))}};
  QCOMPARE(CollectionPlatformBackgrounds::forCollection(snes, systems), want);

  // An explicit override wins over what the name would autodetect to.
  const CollectionConfig pinned =
      collectionAt(QStringLiteral("snes"), root.path(), 1, {QStringLiteral("sfc")});
  QCOMPARE(CollectionPlatformBackgrounds::systemId(pinned, systems), 1);
  const QList<Candidate> wantPinned{
      {QStringLiteral("photo"), QDir(shared).filePath(QStringLiteral("photo/platform_1.png"))}};
  QCOMPARE(CollectionPlatformBackgrounds::forCollection(pinned, systems), wantPinned);
}

void TestPlatformBackgroundCandidates::collectionSourceUnmappedOrRootlessYieldsNothing() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString shared = QDir(root.path()).filePath(QStringLiteral("_shared"));
  touch(shared, QStringLiteral("illustration/platform_0.png"));
  touch(shared, QStringLiteral("illustration/platform_1.png"));
  touch(shared, QStringLiteral("illustration/platform_2.png"));
  const QList<ScreenScraperSystems::System> systems = syntheticSystems();

  // Unmapped: no override and nothing autodetects.
  const CollectionConfig unmapped =
      collectionAt(QStringLiteral("Something Else"), root.path(), -1, {QStringLiteral("xyz")});
  QCOMPARE(CollectionPlatformBackgrounds::systemId(unmapped, systems), 0);
  QVERIFY(CollectionPlatformBackgrounds::forCollection(unmapped, systems).isEmpty());

  // Autodetectable name, but no catalog loaded: nothing is guessed.
  const CollectionConfig noCatalog =
      collectionAt(QStringLiteral("snes"), root.path(), -1, {QStringLiteral("sfc")});
  QVERIFY(CollectionPlatformBackgrounds::forCollection(noCatalog, {}).isEmpty());

  // Override 0 is the "any system" sentinel, not a platform: platform_0 is
  // never offered.
  QVERIFY(CollectionPlatformBackgrounds::forCollection(
              collectionAt(QStringLiteral("Any"), root.path(), 0), systems)
              .isEmpty());

  // A playlist has no single platform even with an override.
  CollectionConfig playlist = collectionAt(QStringLiteral("Favourites"), root.path(), 1);
  playlist.isPlaylist = true;
  QVERIFY(CollectionPlatformBackgrounds::forCollection(playlist, systems).isEmpty());

  // No artwork directory of its own: no root is borrowed from another
  // collection (root above holds platform_1 art), so nothing is listed.
  const CollectionConfig rootless = collectionAt(QStringLiteral("Genesis"), QString(), 1);
  QVERIFY(CollectionPlatformBackgrounds::sharedRoot(rootless).isEmpty());
  QVERIFY(CollectionPlatformBackgrounds::forCollection(rootless, systems).isEmpty());

  // An artwork directory that does not exist, or one without _shared/.
  const CollectionConfig missing = collectionAt(
      QStringLiteral("Genesis"), QDir(root.path()).filePath(QStringLiteral("gone")), 1);
  QVERIFY(CollectionPlatformBackgrounds::forCollection(missing, systems).isEmpty());
  QTemporaryDir bare;
  QVERIFY(bare.isValid());
  QVERIFY(CollectionPlatformBackgrounds::forCollection(
              collectionAt(QStringLiteral("Genesis"), bare.path(), 1), systems)
              .isEmpty());
}

QTEST_GUILESS_MAIN(TestPlatformBackgroundCandidates)
#include "test_platformbackgroundcandidates.moc"
