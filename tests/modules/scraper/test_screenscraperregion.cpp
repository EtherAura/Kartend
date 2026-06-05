// Unit tests for ScreenScraperRegion::detectFromFilename — the No-Intro
// region heuristic extracted from ScreenScraperProvider into its own TU.
// Pure, network-free: every assertion is on the short-name the heuristic
// derives from a filename.
#include <QString>
#include <QTest>

#include "screenscraperregion.h"

class TestScreenScraperRegion : public QObject {
  Q_OBJECT
private slots:
  void detectFromFilename_data();
  void detectFromFilename();
};

void TestScreenScraperRegion::detectFromFilename_data() {
  QTest::addColumn<QString>("input");
  QTest::addColumn<QString>("expected");

  // Parenthesised full names (No-Intro convention).
  QTest::newRow("full-japan") << QStringLiteral("Chrono Trigger (Japan).sfc")
                              << QStringLiteral("jp");
  QTest::newRow("full-usa") << QStringLiteral("Chrono Trigger (USA).sfc") << QStringLiteral("us");
  QTest::newRow("full-europe") << QStringLiteral("Sonic (Europe).md") << QStringLiteral("eu");
  QTest::newRow("full-world") << QStringLiteral("Tetris (World).gb") << QStringLiteral("wor");
  QTest::newRow("full-korea") << QStringLiteral("Game (Korea).rom") << QStringLiteral("kr");

  // Single-letter shorthand common in older releases.
  QTest::newRow("shorthand-j") << QStringLiteral("Game (J).rom") << QStringLiteral("jp");
  QTest::newRow("shorthand-u") << QStringLiteral("Game (U).rom") << QStringLiteral("us");
  QTest::newRow("shorthand-e") << QStringLiteral("Game (E).rom") << QStringLiteral("eu");

  // Case-insensitive matching.
  QTest::newRow("case-insensitive") << QStringLiteral("game (japan).sfc") << QStringLiteral("jp");

  // Multi-region tags: the first matching region in the confidence order
  // wins (Japan's "(Japan" pattern is tried before USA's).
  QTest::newRow("multi-region-first-wins")
      << QStringLiteral("Game (Japan, USA).sfc") << QStringLiteral("jp");

  // The single-letter pattern is anchored to "(X)" exactly so incidental
  // parenthesised text doesn't produce a false region.
  QTest::newRow("shorthand-anchored-no-false-match")
      << QStringLiteral("(J-Pop) Mix.mp3") << QString();

  // No recognisable region tag -> empty (caller trusts ScreenScraper).
  QTest::newRow("no-region") << QStringLiteral("Super Mario Bros.zip") << QString();
}

void TestScreenScraperRegion::detectFromFilename() {
  QFETCH(QString, input);
  QFETCH(QString, expected);
  QCOMPARE(ScreenScraperRegion::detectFromFilename(input), expected);
}

QTEST_MAIN(TestScreenScraperRegion)
#include "test_screenscraperregion.moc"
