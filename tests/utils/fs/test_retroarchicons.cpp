// Tests for RetroArchIcons (Kartend-1kkk2) — pack discovery, the
// content-sibling rule that separates a system icon from RetroArch's own menu
// chrome, path resolution (including the component validation that keeps a
// config value from walking out of the assets tree), and name autodetect.
//
// Every test builds its own miniature assets tree in a QTemporaryDir rather
// than reading whatever RetroArch the developer happens to have installed —
// the results have to be the same on a machine with no RetroArch at all,
// which is also every CI runner.

#include "retroarchicons.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

class TestRetroArchIcons : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();

  void discoverPacks_findsPacksAndCountsSystems();
  void discoverPacks_skipsDirectoriesWithNoSystemIcons();
  void discoverPacks_classifiesKnownPacksAndFlagsUnknownOnes();
  void discoverSystems_requiresAContentSibling();
  void defaultPackFor_prefersTheCuratedPackForTheSubject();
  void defaultPackFor_emptyWhenNoPreferredPackIsInstalled();
  void resolvePack_subjectWinsOverAContradictingSet();
  void resolvePack_honoursASetThatAgreesOrIsUnclassified();
  void iconPath_resolvesPlainAndContentVariants();
  void iconPath_emptyForSystemThePackDoesNotCover();
  void iconPath_rejectsTraversalInPackAndSystem();
  void autodetect_exactAndManufacturerQualifiedNames();
  void autodetect_prefersTheShorterNameOnATie();
  void autodetect_usesBuiltinShorthand();
  void autodetect_usesCallerSuppliedAliases();
  void autodetect_emptyWhenNothingMatches();
  void autodetect_ignoresWordsInsideDatQualifiers();
  void autodetect_prefersTheLeastQualifiedVariant();
  void autodetect_longestAliasWinsAndConsumesItsWords();

private:
  QTemporaryDir m_dir;
  QString m_assets;
  /// Write a 1x1 PNG at `<assets>/xmb/<pack>/png/<base>.png`. Content is
  /// irrelevant to every function under test — only the name and the
  /// existence of the pair matter.
  void writeIcon(const QString &pack, const QString &base);
  /// Write both `<base>.png` and `<base>-content.png`, i.e. a real system.
  void writeSystem(const QString &pack, const QString &base);
};

void TestRetroArchIcons::writeIcon(const QString &pack, const QString &base) {
  const QString dir = m_assets + QStringLiteral("/xmb/") + pack + QStringLiteral("/png");
  QVERIFY(QDir().mkpath(dir));
  QImage image(1, 1, QImage::Format_ARGB32);
  image.fill(Qt::white);
  QVERIFY(image.save(dir + QLatin1Char('/') + base + QStringLiteral(".png")));
}

void TestRetroArchIcons::writeSystem(const QString &pack, const QString &base) {
  writeIcon(pack, base);
  writeIcon(pack, base + QStringLiteral("-content"));
}

void TestRetroArchIcons::initTestCase() {
  QVERIFY(m_dir.isValid());
  m_assets = m_dir.filePath(QStringLiteral("assets"));

  // `monochrome` is a curated CONTROLLER pack, `systematic` a curated CONSOLE
  // one, and `housebrand` stands for a pack no release of Kartend has heard
  // of — the case the classification table has to degrade gracefully for.
  writeSystem(QStringLiteral("monochrome"), QStringLiteral("Nintendo - Game Boy"));
  writeSystem(QStringLiteral("monochrome"), QStringLiteral("Nintendo - Game Boy Advance"));
  writeSystem(QStringLiteral("monochrome"),
              QStringLiteral("Nintendo - Super Nintendo Entertainment System"));
  writeSystem(QStringLiteral("monochrome"), QStringLiteral("Sega - Mega Drive - Genesis"));
  // RetroArch's own menu chrome lives in the same directory and must never be
  // offered as a system: no `-content` sibling, so the rule excludes it.
  writeIcon(QStringLiteral("monochrome"), QStringLiteral("add-favorite"));
  writeIcon(QStringLiteral("monochrome"), QStringLiteral("battery-full"));

  writeSystem(QStringLiteral("systematic"), QStringLiteral("Nintendo - Game Boy"));

  writeSystem(QStringLiteral("housebrand"), QStringLiteral("Nintendo - Game Boy"));

  // A directory under xmb/ that is not an icon pack at all — a fonts folder,
  // say. Nothing in it pairs up, so it must not reach the picker.
  writeIcon(QStringLiteral("notapack"), QStringLiteral("some-glyph"));
}

void TestRetroArchIcons::discoverPacks_findsPacksAndCountsSystems() {
  const QList<RetroArchIcons::Pack> packs = RetroArchIcons::discoverPacks(m_assets);
  QStringList ids;
  for (const RetroArchIcons::Pack &pack : packs) ids << pack.id;
  std::sort(ids.begin(), ids.end());
  QCOMPARE(ids, (QStringList{QStringLiteral("housebrand"), QStringLiteral("monochrome"),
                             QStringLiteral("systematic")}));

  const auto monochrome =
      std::find_if(packs.cbegin(), packs.cend(), [](const RetroArchIcons::Pack &p) {
        return p.id == QLatin1String("monochrome");
      });
  QVERIFY(monochrome != packs.cend());
  // Four systems, and neither the two menu glyphs nor the four -content
  // siblings inflating the count.
  QCOMPARE(monochrome->systemCount, 4);
}

void TestRetroArchIcons::discoverPacks_skipsDirectoriesWithNoSystemIcons() {
  for (const RetroArchIcons::Pack &pack : RetroArchIcons::discoverPacks(m_assets)) {
    QVERIFY2(pack.id != QLatin1String("notapack"),
             "a directory holding no paired system icons is not a pack");
  }
}

void TestRetroArchIcons::discoverPacks_classifiesKnownPacksAndFlagsUnknownOnes() {
  const QList<RetroArchIcons::Pack> packs = RetroArchIcons::discoverPacks(m_assets);
  const auto find = [&packs](const QString &id) {
    return std::find_if(packs.cbegin(), packs.cend(),
                        [&id](const RetroArchIcons::Pack &p) { return p.id == id; });
  };
  QCOMPARE(find(QStringLiteral("monochrome"))->subject, SystemIconSubject::Controller);
  QVERIFY(find(QStringLiteral("monochrome"))->subjectKnown);
  QCOMPARE(find(QStringLiteral("systematic"))->subject, SystemIconSubject::Console);
  QVERIFY(find(QStringLiteral("systematic"))->subjectKnown);
  // Still listed — enumeration is from disk — but not classified, so it is
  // never auto-selected for a subject.
  QVERIFY(!find(QStringLiteral("housebrand"))->subjectKnown);
}

void TestRetroArchIcons::discoverSystems_requiresAContentSibling() {
  const QStringList systems =
      RetroArchIcons::discoverSystems(m_assets, QStringLiteral("monochrome"));
  QCOMPARE(systems, (QStringList{QStringLiteral("Nintendo - Game Boy"),
                                 QStringLiteral("Nintendo - Game Boy Advance"),
                                 QStringLiteral("Nintendo - Super Nintendo Entertainment System"),
                                 QStringLiteral("Sega - Mega Drive - Genesis")}));
  QVERIFY(!systems.contains(QStringLiteral("add-favorite")));
  QVERIFY(!systems.contains(QStringLiteral("battery-full")));
  // The -content files are the marker, never entries in their own right.
  QVERIFY(!systems.contains(QStringLiteral("Nintendo - Game Boy-content")));
}

void TestRetroArchIcons::defaultPackFor_prefersTheCuratedPackForTheSubject() {
  const QList<RetroArchIcons::Pack> packs = RetroArchIcons::discoverPacks(m_assets);
  QCOMPARE(RetroArchIcons::defaultPackFor(SystemIconSubject::Controller, packs),
           QStringLiteral("monochrome"));
  QCOMPARE(RetroArchIcons::defaultPackFor(SystemIconSubject::Console, packs),
           QStringLiteral("systematic"));
  // Content is a file variant rather than a pack family, so it rides on the
  // controller order.
  QCOMPARE(RetroArchIcons::defaultPackFor(SystemIconSubject::Content, packs),
           QStringLiteral("monochrome"));
}

void TestRetroArchIcons::defaultPackFor_emptyWhenNoPreferredPackIsInstalled() {
  // Only the unclassified pack is present. It renders perfectly well when
  // chosen by hand, but nothing knows what it DRAWS, so it must not be picked
  // to satisfy a request for a console or a controller.
  QList<RetroArchIcons::Pack> packs;
  RetroArchIcons::Pack unknown;
  unknown.id = QStringLiteral("housebrand");
  packs.append(unknown);
  QVERIFY(RetroArchIcons::defaultPackFor(SystemIconSubject::Controller, packs).isEmpty());
  QVERIFY(RetroArchIcons::defaultPackFor(SystemIconSubject::Console, packs).isEmpty());
}

void TestRetroArchIcons::resolvePack_subjectWinsOverAContradictingSet() {
  // Field report 2026-08-22, reported twice: "still showing controller icons,
  // but i selected console/monochrome". A set holds exactly ONE icon per
  // system, so a controller set has no console to hand back — honouring the
  // set there means silently refusing what was asked for.
  const QList<RetroArchIcons::Pack> packs = RetroArchIcons::discoverPacks(m_assets);
  QCOMPARE(
      RetroArchIcons::resolvePack(SystemIconSubject::Console, QStringLiteral("monochrome"), packs),
      QStringLiteral("systematic"));
  QCOMPARE(RetroArchIcons::resolvePack(SystemIconSubject::Controller, QStringLiteral("systematic"),
                                       packs),
           QStringLiteral("monochrome"));
  // No override at all is the same answer by a different route.
  QCOMPARE(RetroArchIcons::resolvePack(SystemIconSubject::Console, QString(), packs),
           QStringLiteral("systematic"));

  // A wrong-subject icon still beats a blank row: with no set of the requested
  // kind installed, the override stands rather than resolving to nothing.
  QList<RetroArchIcons::Pack> controllersOnly;
  for (const RetroArchIcons::Pack &p : packs) {
    if (p.id != QLatin1String("systematic")) controllersOnly.append(p);
  }
  QCOMPARE(RetroArchIcons::resolvePack(SystemIconSubject::Console, QStringLiteral("monochrome"),
                                       controllersOnly),
           QStringLiteral("monochrome"));
}

void TestRetroArchIcons::resolvePack_honoursASetThatAgreesOrIsUnclassified() {
  const QList<RetroArchIcons::Pack> packs = RetroArchIcons::discoverPacks(m_assets);
  // Agrees — the pick stands.
  QCOMPARE(RetroArchIcons::resolvePack(SystemIconSubject::Controller, QStringLiteral("monochrome"),
                                       packs),
           QStringLiteral("monochrome"));
  // Unclassified — nothing knows what it draws, so there is no basis to
  // override the user.
  QCOMPARE(
      RetroArchIcons::resolvePack(SystemIconSubject::Console, QStringLiteral("housebrand"), packs),
      QStringLiteral("housebrand"));
  // Content rides in every set, so an override can always satisfy it.
  QCOMPARE(
      RetroArchIcons::resolvePack(SystemIconSubject::Content, QStringLiteral("monochrome"), packs),
      QStringLiteral("monochrome"));
}

void TestRetroArchIcons::iconPath_resolvesPlainAndContentVariants() {
  const QString plain = RetroArchIcons::iconPath(m_assets, QStringLiteral("monochrome"),
                                                 QStringLiteral("Nintendo - Game Boy"),
                                                 SystemIconSubject::Controller);
  QVERIFY(plain.endsWith(QStringLiteral("monochrome/png/Nintendo - Game Boy.png")));
  // Console resolves the same FILE — the subject differs in which pack is
  // chosen, not in which file within it.
  QCOMPARE(RetroArchIcons::iconPath(m_assets, QStringLiteral("monochrome"),
                                    QStringLiteral("Nintendo - Game Boy"),
                                    SystemIconSubject::Console),
           plain);
  const QString content =
      RetroArchIcons::iconPath(m_assets, QStringLiteral("monochrome"),
                               QStringLiteral("Nintendo - Game Boy"), SystemIconSubject::Content);
  QVERIFY(content.endsWith(QStringLiteral("monochrome/png/Nintendo - Game Boy-content.png")));
}

void TestRetroArchIcons::iconPath_emptyForSystemThePackDoesNotCover() {
  // `systematic` only carries the Game Boy here. An explicit pack is honoured
  // strictly — no silent fallback to a pack that does cover it — so this is a
  // blank, which is what the settings page warns about.
  QVERIFY(RetroArchIcons::iconPath(m_assets, QStringLiteral("systematic"),
                                   QStringLiteral("Sega - Mega Drive - Genesis"),
                                   SystemIconSubject::Console)
              .isEmpty());
}

void TestRetroArchIcons::iconPath_rejectsTraversalInPackAndSystem() {
  // Both values come from the collection config, which a hand-edited INI or an
  // imported .kart can set, and both become path components.
  QVERIFY(RetroArchIcons::iconPath(m_assets, QStringLiteral("../../../etc"),
                                   QStringLiteral("Nintendo - Game Boy"),
                                   SystemIconSubject::Controller)
              .isEmpty());
  QVERIFY(RetroArchIcons::iconPath(m_assets, QStringLiteral("monochrome"),
                                   QStringLiteral("../../../../etc/passwd"),
                                   SystemIconSubject::Controller)
              .isEmpty());
  QVERIFY(RetroArchIcons::iconPath(m_assets, QStringLiteral("monochrome"),
                                   QStringLiteral("sub/dir/Nintendo - Game Boy"),
                                   SystemIconSubject::Controller)
              .isEmpty());
  // And the same validation guards enumeration, which builds a directory path
  // from the pack id.
  QVERIFY(RetroArchIcons::discoverSystems(m_assets, QStringLiteral("../xmb/monochrome")).isEmpty());
}

void TestRetroArchIcons::autodetect_exactAndManufacturerQualifiedNames() {
  const QStringList systems =
      RetroArchIcons::discoverSystems(m_assets, QStringLiteral("monochrome"));
  // The full libretro name, exactly.
  QCOMPARE(RetroArchIcons::autodetectSystem(QStringLiteral("Nintendo - Game Boy Advance"), systems),
           QStringLiteral("Nintendo - Game Boy Advance"));
  // The part users actually name a collection after — no manufacturer.
  QCOMPARE(RetroArchIcons::autodetectSystem(QStringLiteral("Game Boy Advance"), systems),
           QStringLiteral("Nintendo - Game Boy Advance"));
  // Punctuation and case are normalised away on both sides.
  QCOMPARE(RetroArchIcons::autodetectSystem(QStringLiteral("game-boy advance!"), systems),
           QStringLiteral("Nintendo - Game Boy Advance"));
}

void TestRetroArchIcons::autodetect_prefersTheShorterNameOnATie() {
  const QStringList systems =
      RetroArchIcons::discoverSystems(m_assets, QStringLiteral("monochrome"));
  // "Game Boy" is contained in "Game Boy Advance" too. The shorter name spends
  // none of itself on something the collection did not ask for, so it wins.
  QCOMPARE(RetroArchIcons::autodetectSystem(QStringLiteral("Game Boy"), systems),
           QStringLiteral("Nintendo - Game Boy"));
}

void TestRetroArchIcons::autodetect_usesBuiltinShorthand() {
  const QStringList systems =
      RetroArchIcons::discoverSystems(m_assets, QStringLiteral("monochrome"));
  // "SNES" shares not one word with its libretro name — the whole reason the
  // shorthand table exists.
  QCOMPARE(RetroArchIcons::autodetectSystem(QStringLiteral("SNES"), systems),
           QStringLiteral("Nintendo - Super Nintendo Entertainment System"));
  QCOMPARE(RetroArchIcons::autodetectSystem(QStringLiteral("GBA"), systems),
           QStringLiteral("Nintendo - Game Boy Advance"));
  // Shorthand embedded in a longer collection name still reaches it.
  QCOMPARE(RetroArchIcons::autodetectSystem(QStringLiteral("My SNES Collection"), systems),
           QStringLiteral("Nintendo - Super Nintendo Entertainment System"));
  // And "Genesis" is the same machine as "Mega Drive".
  QCOMPARE(RetroArchIcons::autodetectSystem(QStringLiteral("Genesis"), systems),
           QStringLiteral("Sega - Mega Drive - Genesis"));
}

void TestRetroArchIcons::autodetect_usesCallerSuppliedAliases() {
  const QStringList systems =
      RetroArchIcons::discoverSystems(m_assets, QStringLiteral("monochrome"));
  // Nothing about "Super Famicom Library" matches by words, and the caller's
  // alias list (in production, ScreenScraper's own) is what bridges it.
  QCOMPARE(
      RetroArchIcons::autodetectSystem(QStringLiteral("Console 3"), systems,
                                       {QStringLiteral("Super Nintendo Entertainment System")}),
      QStringLiteral("Nintendo - Super Nintendo Entertainment System"));
}

void TestRetroArchIcons::autodetect_emptyWhenNothingMatches() {
  const QStringList systems =
      RetroArchIcons::discoverSystems(m_assets, QStringLiteral("monochrome"));
  QVERIFY(RetroArchIcons::autodetectSystem(QStringLiteral("Home Movies"), systems).isEmpty());
  QVERIFY(RetroArchIcons::autodetectSystem(QString(), systems).isEmpty());
  QVERIFY(RetroArchIcons::autodetectSystem(QStringLiteral("Game Boy"), {}).isEmpty());
}

void TestRetroArchIcons::autodetect_ignoresWordsInsideDatQualifiers() {
  // Field report 2026-08-22: "dont try to pair steam with the retroarch
  // icons". A collection named "Steam" was resolving to
  // "IBM - PC and Compatibles (Digital) (Steam) (Hentai)" — the ONLY entry in
  // the entire libretro set containing the word, and an adult-content DAT
  // scope at that. libretro puts CATALOGUE SCOPE in brackets, not system
  // identity, so nothing inside them may be matchable.
  const QStringList systems{
      QStringLiteral("IBM - PC and Compatibles"),
      QStringLiteral("IBM - PC and Compatibles (Digital) (Steam) (Hentai)"),
      QStringLiteral("Nintendo - Game Boy"),
  };
  QVERIFY2(RetroArchIcons::autodetectSystem(QStringLiteral("Steam"), systems).isEmpty(),
           "a storefront named inside a DAT qualifier must not be matchable");
  QVERIFY(RetroArchIcons::autodetectSystem(QStringLiteral("My Steam Games"), systems).isEmpty());
  // The qualifier is invisible to matching, not to the RESULT — the entry is
  // still reachable by what it actually is.
  QCOMPARE(RetroArchIcons::autodetectSystem(QStringLiteral("Game Boy"), systems),
           QStringLiteral("Nintendo - Game Boy"));
}

void TestRetroArchIcons::autodetect_prefersTheLeastQualifiedVariant() {
  // Stripping qualifiers collapses every DAT variant of one machine to the
  // same text, so the tie-break has to measure the FULL name or they would all
  // tie and the match would dissolve into "ambiguous". A real install carries
  // twenty variants of the IBM PC.
  const QStringList systems{
      QStringLiteral("IBM - PC and Compatibles (Digital) (Misc)"),
      QStringLiteral("IBM - PC and Compatibles"),
      QStringLiteral("IBM - PC and Compatibles (Flash Media)"),
  };
  QCOMPARE(RetroArchIcons::autodetectSystem(QStringLiteral("IBM PC"), systems),
           QStringLiteral("IBM - PC and Compatibles"));
}

void TestRetroArchIcons::autodetect_longestAliasWinsAndConsumesItsWords() {
  // Field report 2026-08-22: a collection named "Super Famicom - Super
  // Nintendo Entertainment System" resolved to the NES. Probing each word on
  // its own let "famicom" fire its own alias from inside "Super Famicom", and
  // that expansion matched the NES entry EXACTLY while the real answer only
  // matched by containment — so the wrong system outscored the right one.
  const QStringList systems{
      QStringLiteral("Nintendo - Nintendo Entertainment System"),
      QStringLiteral("Nintendo - Super Nintendo Entertainment System"),
  };
  QCOMPARE(RetroArchIcons::autodetectSystem(
               QStringLiteral("Super Famicom - Super Nintendo Entertainment System"), systems),
           QStringLiteral("Nintendo - Super Nintendo Entertainment System"));
  QCOMPARE(RetroArchIcons::autodetectSystem(QStringLiteral("Super Famicom"), systems),
           QStringLiteral("Nintendo - Super Nintendo Entertainment System"));
  // The shorter alias still works where it is the whole story.
  QCOMPARE(RetroArchIcons::autodetectSystem(
               QStringLiteral("Famicom - Nintendo Entertainment System"), systems),
           QStringLiteral("Nintendo - Nintendo Entertainment System"));
  QCOMPARE(RetroArchIcons::autodetectSystem(QStringLiteral("Famicom"), systems),
           QStringLiteral("Nintendo - Nintendo Entertainment System"));
  // And an alias embedded in a longer name is still reached, since the walk
  // tries every position rather than only the whole string.
  QCOMPARE(RetroArchIcons::autodetectSystem(QStringLiteral("My Super Famicom Shelf"), systems),
           QStringLiteral("Nintendo - Super Nintendo Entertainment System"));
}

QTEST_MAIN(TestRetroArchIcons)
#include "test_retroarchicons.moc"
