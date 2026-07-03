/**
 * @file test_launchcommandbuilder.cpp
 * @brief Direct unit tests for the LaunchCommandBuilder free functions — the
 * pure command-construction half split out of launchmanager.cpp.
 * LaunchManager's same-name statics delegate here unchanged (that surface is
 * covered by test_launchmanager.cpp); these tests exercise the builder entry
 * points directly: boundary-aware %1/%f and %core substitution, %collection%
 * expansion, preset resolution feeding the builder, and the append-media-path
 * fallback. Pure string/struct transforms, no process is ever spawned.
 */

#include "collection/launcherconfig.h"
#include "collection/launcherpreset.h"
#include "launchcommandbuilder.h"

#include <QString>
#include <QStringList>
#include <QTest>

namespace {
// Plain (non-libretro) launcher with the given parameter template. The
// launcher's existence is irrelevant — buildLaunchCommand never touches the
// filesystem for the program itself.
LauncherConfig plainLauncher(const QString &parameters, const QString &corePath = QString()) {
  LauncherConfig lc;
  lc.launcherPath = QStringLiteral("/usr/bin/player");
  lc.corePath = corePath;
  lc.launchParameters = parameters;
  return lc;
}
} // namespace

class TestLaunchCommandBuilder : public QObject {
  Q_OBJECT

private slots:
  // %1 / %f media-path placeholder: boundary-aware substitution
  void testFilePlaceholder_data();
  void testFilePlaceholder();
  void testFilePlaceholderKeepsSingleArgWithSpaces();

  // %core placeholder
  void testCorePlaceholder_data();
  void testCorePlaceholder();
  void testLibretroTripleKeepsParametersAhead();

  // %collection% expansion
  void testCollectionExpansion();
  void testCollectionExpansionDoesNotSplitArguments();
  void testCollectionExpansionRejectsTraversal();

  // preset resolution feeding the builder
  void testPresetResolutionFeedsBuilder();
  void testMissingPresetFallsBackToInlineFields();

  // append-media-path fallback
  void testNoPlaceholderAppendsMediaPath();

  // parseParameters entry point
  void testParseParametersUnclosedQuoteFails();
};

void TestLaunchCommandBuilder::testFilePlaceholder_data() {
  QTest::addColumn<QString>("parameters");
  QTest::addColumn<QStringList>("expectedArguments");

  const QString file = QStringLiteral("/tmp/media/concert.mp4");
  // %1 and %f (case-insensitive) substitute in place and suppress the
  // append-media-path fallback: the path lands exactly where the template
  // author put it, and nowhere else.
  QTest::newRow("percent-1") << QStringLiteral("--play %1") << QStringList{"--play", file};
  QTest::newRow("percent-f") << QStringLiteral("--play %f") << QStringList{"--play", file};
  QTest::newRow("percent-F-case-insensitive")
      << QStringLiteral("--play %F") << QStringList{"--play", file};
  QTest::newRow("mid-token") << QStringLiteral("--input=%f")
                             << QStringList{QStringLiteral("--input=") + file};
  // Boundary-aware (\b): a longer token merely starting with %f / %1 is NOT
  // the media placeholder. It passes through verbatim, sawFilePlaceholder
  // stays false, and the media path is appended at the end by the fallback.
  QTest::newRow("file-name-token") << QStringLiteral("%file%") << QStringList{"%file%", file};
  QTest::newRow("fullscreen-token")
      << QStringLiteral("--opt=%fullscreen%") << QStringList{"--opt=%fullscreen%", file};
  QTest::newRow("percent-10") << QStringLiteral("--seek=%10") << QStringList{"--seek=%10", file};
}

void TestLaunchCommandBuilder::testFilePlaceholder() {
  QFETCH(QString, parameters);
  QFETCH(QStringList, expectedArguments);

  const auto result = LaunchCommandBuilder::buildLaunchCommand(
      plainLauncher(parameters), QStringLiteral("Video"), QStringLiteral("/tmp/media/concert.mp4"));
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().program, QStringLiteral("/usr/bin/player"));
  QCOMPARE(result.value().arguments, expectedArguments);
}

void TestLaunchCommandBuilder::testFilePlaceholderKeepsSingleArgWithSpaces() {
  // Substitution happens inside the already-split token, so a quoted "%1"
  // stays one argv entry even when the substituted path contains spaces — the
  // template author's argument boundary survives.
  const QString file = QStringLiteral("/tmp/media/My Concert Set.mp4");
  const auto result = LaunchCommandBuilder::buildLaunchCommand(
      plainLauncher(QStringLiteral("--fullscreen \"%1\"")), QStringLiteral("Video"), file);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().arguments, (QStringList{"--fullscreen", file}));
}

void TestLaunchCommandBuilder::testCorePlaceholder_data() {
  QTest::addColumn<QString>("parameters");
  QTest::addColumn<QStringList>("expectedArguments");

  const QString file = QStringLiteral("/tmp/media/episode.mkv");
  const QString core = QStringLiteral("/tmp/cores/engine.so");
  // %core (case-insensitive) resolves to the launcher's configured core path
  // on the plain branch too. Core substitution never suppresses the
  // append-media-path fallback — only %1/%f does.
  QTest::newRow("standalone") << QStringLiteral("-L %core") << QStringList{"-L", core, file};
  QTest::newRow("case-insensitive") << QStringLiteral("-L %CORE") << QStringList{"-L", core, file};
  QTest::newRow("mid-token") << QStringLiteral("--core=%core")
                             << QStringList{QStringLiteral("--core=") + core, file};
  // Boundary-aware (\b): longer tokens starting with %core stay literal
  // instead of being corrupted into "<core path>opts%" / "<core path>s".
  QTest::newRow("coreopts-token") << QStringLiteral("--extra %coreopts%")
                                  << QStringList{"--extra", "%coreopts%", file};
  QTest::newRow("cores-token") << QStringLiteral("--list %cores")
                               << QStringList{"--list", "%cores", file};
}

void TestLaunchCommandBuilder::testCorePlaceholder() {
  QFETCH(QString, parameters);
  QFETCH(QStringList, expectedArguments);

  const auto result = LaunchCommandBuilder::buildLaunchCommand(
      plainLauncher(parameters, QStringLiteral("/tmp/cores/engine.so")), QStringLiteral("Video"),
      QStringLiteral("/tmp/media/episode.mkv"));
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().arguments, expectedArguments);
}

void TestLaunchCommandBuilder::testLibretroTripleKeepsParametersAhead() {
  // A launcher whose basename classifies as libretro takes the core branch:
  // configured parameters are parsed and inserted AHEAD of the fixed
  // `-L <core> <file>` triple so the ordering the frontend expects is intact.
  LauncherConfig lc;
  lc.launcherPath = QStringLiteral("/usr/bin/retroarch");
  lc.corePath = QStringLiteral("/tmp/cores/engine.so");
  lc.launchParameters = QStringLiteral("--fullscreen");

  const QString file = QStringLiteral("/tmp/media/episode.mkv");
  const auto result = LaunchCommandBuilder::buildLaunchCommand(lc, QStringLiteral("Video"), file);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().arguments,
           (QStringList{"--fullscreen", "-L", "/tmp/cores/engine.so", file}));
}

void TestLaunchCommandBuilder::testCollectionExpansion() {
  // %collection% (case-insensitive) expands in the program path and inside
  // each already-split parameter token.
  LauncherConfig lc;
  lc.launcherPath = QStringLiteral("/opt/%collection%/bin/player");
  lc.launchParameters = QStringLiteral("--profile %collection% --title=%COLLECTION%");

  const QString file = QStringLiteral("/tmp/media/track.flac");
  const auto result = LaunchCommandBuilder::buildLaunchCommand(lc, QStringLiteral("Music"), file);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().program, QStringLiteral("/opt/Music/bin/player"));
  QCOMPARE(result.value().arguments, (QStringList{"--profile", "Music", "--title=Music", file}));
}

void TestLaunchCommandBuilder::testCollectionExpansionDoesNotSplitArguments() {
  // Tokenize-then-substitute: a collection name containing spaces (or a
  // leading dash) lands as exactly ONE argument and can never introduce a new
  // argv boundary.
  const QString file = QStringLiteral("/tmp/media/track.flac");
  LauncherConfig lc = plainLauncher(QStringLiteral("--profile %collection%"));
  const auto result =
      LaunchCommandBuilder::buildLaunchCommand(lc, QStringLiteral("Live Recordings"), file);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().arguments, (QStringList{"--profile", "Live Recordings", file}));
}

void TestLaunchCommandBuilder::testCollectionExpansionRejectsTraversal() {
  // A collection name that would inject `..` / path separators into the
  // %collection% substitution is refused before any expansion happens.
  const auto result = LaunchCommandBuilder::buildLaunchCommand(
      plainLauncher(QString()), QStringLiteral("../escape"),
      QStringLiteral("/tmp/media/concert.mp4"));
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);
}

void TestLaunchCommandBuilder::testPresetResolutionFeedsBuilder() {
  // Mirrors launchItem(): resolve the preset reference first, then hand the
  // resolved config to the builder — a preset-backed entry launches with the
  // preset's path/parameters, not its (empty) inline fields.
  LauncherPreset preset;
  preset.id = QStringLiteral("preset-video-player");
  preset.name = QStringLiteral("Video Player");
  preset.launcherPath = QStringLiteral("/usr/bin/player");
  preset.launchParameters = QStringLiteral("--fullscreen \"%1\"");

  LauncherConfig entry;
  entry.presetId = preset.id;

  const LauncherConfig resolved = LauncherUtils::resolvePreset(entry, {preset});
  const QString file = QStringLiteral("/tmp/media/concert.mp4");
  const auto result =
      LaunchCommandBuilder::buildLaunchCommand(resolved, QStringLiteral("Video"), file);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().program, preset.launcherPath);
  QCOMPARE(result.value().arguments, (QStringList{"--fullscreen", file}));
}

void TestLaunchCommandBuilder::testMissingPresetFallsBackToInlineFields() {
  // A dangling presetId resolves to the inline fields, so the builder sees
  // the entry's own launcher path.
  LauncherConfig entry;
  entry.presetId = QStringLiteral("no-such-preset");
  entry.launcherPath = QStringLiteral("/usr/bin/fallback-player");

  const LauncherConfig resolved = LauncherUtils::resolvePreset(entry, {});
  const QString file = QStringLiteral("/tmp/media/concert.mp4");
  const auto result =
      LaunchCommandBuilder::buildLaunchCommand(resolved, QStringLiteral("Video"), file);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().program, QStringLiteral("/usr/bin/fallback-player"));
  QCOMPARE(result.value().arguments, QStringList{file});
}

void TestLaunchCommandBuilder::testNoPlaceholderAppendsMediaPath() {
  const QString file = QStringLiteral("/tmp/media/concert.mp4");

  // No launch parameters at all: the media path is the sole argument.
  auto result = LaunchCommandBuilder::buildLaunchCommand(plainLauncher(QString()),
                                                         QStringLiteral("Video"), file);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().arguments, QStringList{file});

  // Parameters without any %1/%f placeholder: historical append-at-end
  // behavior — parameters first, media path last.
  result = LaunchCommandBuilder::buildLaunchCommand(
      plainLauncher(QStringLiteral("--fullscreen --mute")), QStringLiteral("Video"), file);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().arguments, (QStringList{"--fullscreen", "--mute", file}));
}

void TestLaunchCommandBuilder::testParseParametersUnclosedQuoteFails() {
  // Direct namespace entry point: an unclosed quote is a hard parse error
  // (potential injection vector), which buildLaunchCommand propagates.
  const auto result = LaunchCommandBuilder::parseParameters(QStringLiteral("--title \"Unfinished"));
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);
}

QTEST_APPLESS_MAIN(TestLaunchCommandBuilder)
#include "test_launchcommandbuilder.moc"
