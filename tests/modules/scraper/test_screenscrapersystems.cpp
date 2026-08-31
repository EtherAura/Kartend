// Tests for ScreenScraperSystems autodetect heuristic. The module
// owns no hardcoded system catalog — these tests build small
// synthetic catalogs at the top of each case to exercise the
// scoring rules without baking platform names into the test
// fixtures themselves either.
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "screenscrapersystems.h"

class TestScreenScraperSystems : public QObject {
  Q_OBJECT
private slots:
  void emptyCatalog_returnsMinusOne();
  void find_returnsNullForUnknownIdAndNonNullForKnown();
  void autodetect_matchesByExtension();
  void autodetect_matchesByCollectionName();
  void autodetect_matchesByCollectionType();
  void autodetect_aliasOutweighsExtensionWhenTotalsTie();
  void autodetect_returnsMinusOneWhenNoMatch();
  void autodetect_normalizesExtensionPrefix();
  void autodetect_avoidsSubstringFalseMatchOnShortAliases();
  void autodetect_prefersLongerAliasWhenScoresTie();
};

namespace {

ScreenScraperSystems::System makeSystem(int id, const QString &display, const QStringList &exts,
                                        const QStringList &aliases) {
  ScreenScraperSystems::System s;
  s.id = id;
  s.displayName = display;
  s.extensions = exts;
  s.aliases = aliases;
  return s;
}

} // namespace

void TestScreenScraperSystems::emptyCatalog_returnsMinusOne() {
  // No catalog supplied (cache fetcher hasn't run / is offline) →
  // autodetect must return -1 so the provider falls back to
  // systemeid=0 instead of guessing wildly.
  const int id = ScreenScraperSystems::autodetect(
      QStringLiteral("anything"), QStringLiteral("anything"), {QStringLiteral("anything")}, {});
  QCOMPARE(id, -1);
}

void TestScreenScraperSystems::find_returnsNullForUnknownIdAndNonNullForKnown() {
  const QList<ScreenScraperSystems::System> catalog = {
      makeSystem(7, QStringLiteral("Display Foo"), {}, {}),
  };
  QCOMPARE(ScreenScraperSystems::find(catalog, 99999), nullptr);
  const auto *hit = ScreenScraperSystems::find(catalog, 7);
  QVERIFY(hit != nullptr);
  QCOMPARE(hit->id, 7);
}

void TestScreenScraperSystems::autodetect_matchesByExtension() {
  const QList<ScreenScraperSystems::System> catalog = {
      makeSystem(1, QStringLiteral("Display A"), {QStringLiteral("fmt-a")}, {}),
      makeSystem(2, QStringLiteral("Display B"), {QStringLiteral("fmt-b")}, {}),
  };
  QCOMPARE(
      ScreenScraperSystems::autodetect(QString(), QString(), {QStringLiteral("fmt-b")}, catalog),
      2);
}

void TestScreenScraperSystems::autodetect_matchesByCollectionName() {
  const QList<ScreenScraperSystems::System> catalog = {
      makeSystem(1, QStringLiteral("Display A"), {}, {QStringLiteral("alpha")}),
      makeSystem(2, QStringLiteral("Display B"), {}, {QStringLiteral("beta")}),
  };
  QCOMPARE(ScreenScraperSystems::autodetect(QStringLiteral("My Beta Roms"), QString(), {}, catalog),
           2);
}

void TestScreenScraperSystems::autodetect_matchesByCollectionType() {
  const QList<ScreenScraperSystems::System> catalog = {
      makeSystem(7, QStringLiteral("Display G"), {}, {QStringLiteral("gamma")}),
  };
  QCOMPARE(ScreenScraperSystems::autodetect(QString(), QStringLiteral("GAMMA"), {}, catalog), 7);
}

void TestScreenScraperSystems::autodetect_aliasOutweighsExtensionWhenTotalsTie() {
  // Both candidates share the same extension; only one's alias
  // matches the name. Alias gives +2, extension gives +1 — alias
  // candidate (id 5) wins.
  const QList<ScreenScraperSystems::System> catalog = {
      makeSystem(4, QStringLiteral("Plain"), {QStringLiteral("fmt-x")}, {}),
      makeSystem(5, QStringLiteral("Aliased"), {QStringLiteral("fmt-x")},
                 {QStringLiteral("alpha")}),
  };
  QCOMPARE(ScreenScraperSystems::autodetect(QStringLiteral("Alpha Things"), QString(),
                                            {QStringLiteral("fmt-x")}, catalog),
           5);
}

void TestScreenScraperSystems::autodetect_returnsMinusOneWhenNoMatch() {
  const QList<ScreenScraperSystems::System> catalog = {
      makeSystem(1, QStringLiteral("Display A"), {QStringLiteral("fmt-a")},
                 {QStringLiteral("alpha")}),
  };
  QCOMPARE(ScreenScraperSystems::autodetect(QStringLiteral("Random Stuff"), QStringLiteral("misc"),
                                            {QStringLiteral("zzz")}, catalog),
           -1);
}

void TestScreenScraperSystems::autodetect_normalizesExtensionPrefix() {
  const QList<ScreenScraperSystems::System> catalog = {
      makeSystem(1, QStringLiteral("Display A"), {QStringLiteral("fmt-a")}, {}),
  };
  QCOMPARE(
      ScreenScraperSystems::autodetect(QString(), QString(), {QStringLiteral(".fmt-a")}, catalog),
      1);
}

void TestScreenScraperSystems::autodetect_avoidsSubstringFalseMatchOnShortAliases() {
  // The "ab" alias is a substring of "abxy" but must not match it
  // as a whole word. Without the word-boundary fix the legacy
  // .contains() heuristic would falsely route "abxy" collections to
  // the "ab" system.
  const QList<ScreenScraperSystems::System> catalog = {
      makeSystem(1, QStringLiteral("Short"), {}, {QStringLiteral("ab")}),
      makeSystem(2, QStringLiteral("Long"), {}, {QStringLiteral("abxy")}),
  };
  QCOMPARE(
      ScreenScraperSystems::autodetect(QStringLiteral("My ABXY Stuff"), QString(), {}, catalog), 2);
}

void TestScreenScraperSystems::autodetect_prefersLongerAliasWhenScoresTie() {
  // A collection named after the LONGER of two nested multi-word aliases
  // ("alpha beta gamma" vs "alpha beta") matches both by substring at the
  // same +2. Catalog order must NOT decide that tie — the longer matched
  // alias is the more specific claim. This is the Game Boy Advance shape:
  // with the base system first in the catalog, its shorter alias used to
  // steal the collection.
  const QList<ScreenScraperSystems::System> catalog = {
      makeSystem(9, QStringLiteral("Base"), {}, {QStringLiteral("alpha beta")}),
      makeSystem(12, QStringLiteral("Extended"), {}, {QStringLiteral("alpha beta gamma")}),
  };
  QCOMPARE(
      ScreenScraperSystems::autodetect(QStringLiteral("Alpha Beta Gamma"), QString(), {}, catalog),
      12);
  // The base name keeps resolving to the base system: the extended alias is
  // not a substring of "alpha beta", so there is no tie to break.
  QCOMPARE(ScreenScraperSystems::autodetect(QStringLiteral("Alpha Beta"), QString(), {}, catalog),
           9);
  // Catalog order flipped: the longer alias must win from either position.
  const QList<ScreenScraperSystems::System> flipped = {
      makeSystem(12, QStringLiteral("Extended"), {}, {QStringLiteral("alpha beta gamma")}),
      makeSystem(9, QStringLiteral("Base"), {}, {QStringLiteral("alpha beta")}),
  };
  QCOMPARE(
      ScreenScraperSystems::autodetect(QStringLiteral("Alpha Beta Gamma"), QString(), {}, flipped),
      12);
}

QTEST_MAIN(TestScreenScraperSystems)
#include "test_screenscrapersystems.moc"
