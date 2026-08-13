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
#include "kartlink.h"
#include "launchcommandbuilder.h"

#include <QString>
#include <QStringList>
#include <QTemporaryDir>
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

  // Derived path tokens (Kartend-kvcxd): %name% / %filename% / %dir% / %ext%
  void testDerivedPathTokens_data();
  void testDerivedPathTokens();
  void testDerivedTokenLeadingDashIsRejected();
  void testDerivedTokensOnKartLinkStub();

  // %core placeholder
  void testCorePlaceholder_data();
  void testCorePlaceholder();
  void testLibretroTripleKeepsParametersAhead();

  // %collection% expansion
  void testCollectionExpansion();
  void testCollectionExpansionDoesNotSplitArguments();
  void testMissingLauncherErrorNamesCollectionAndNextStep();
  void testCollectionExpansionRejectsTraversal();

  // preset resolution feeding the builder
  void testPresetResolutionFeedsBuilder();
  void testMissingPresetFallsBackToInlineFields();

  // append-media-path fallback
  void testNoPlaceholderAppendsMediaPath();

  // parseParameters entry point
  void testParseParametersUnclosedQuoteFails();
  void testParseParametersKeepsQuotedEmptyArgument();

  // .kartlink launcher-import stubs (Kartend-wuq2c): the stub's target — not
  // the stub's own path — is what substitutes into the command.
  void testKartLinkStubResolvesToTarget();
  void testKartLinkStubAppendFallbackUsesTarget();
  void testKartLinkBrokenStubFailsToBuild();
  void testKartLinkTargetIsSecurityChecked();
  // Stub-carried launcher arguments (Kartend-4cff2) — the Bottles shape.
  void testKartLinkStubArgumentsFollowTheTemplate();
  void testKartLinkStubArgumentsAreSecurityChecked();
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

void TestLaunchCommandBuilder::testDerivedPathTokens_data() {
  QTest::addColumn<QString>("parameters");
  QTest::addColumn<QStringList>("expectedArguments");

  const QString file = QStringLiteral("/tmp/media/concert.mp4");
  // Each token names one PART of the item path. None of them places the media
  // argument, so the append-at-end fallback still fires — the trailing `file`
  // in these expectations is that fallback, not the token.
  QTest::newRow("name") << QStringLiteral("--title=%name%")
                        << QStringList{QStringLiteral("--title=concert"), file};
  QTest::newRow("filename") << QStringLiteral("--as %filename%")
                            << QStringList{"--as", "concert.mp4", file};
  QTest::newRow("dir") << QStringLiteral("--cwd %dir%") << QStringList{"--cwd", "/tmp/media", file};
  QTest::newRow("ext") << QStringLiteral("--kind %ext%") << QStringList{"--kind", "mp4", file};
  QTest::newRow("case-insensitive")
      << QStringLiteral("--title=%NAME%") << QStringList{QStringLiteral("--title=concert"), file};
  // Composed inside one token: substitution happens after the split, so the
  // sibling-file idiom stays a single argv entry even though it names two
  // tokens and the value contains a path separator.
  QTest::newRow("composed") << QStringLiteral("--sub=%dir%/%name%.srt")
                            << QStringList{QStringLiteral("--sub=/tmp/media/concert.srt"), file};
  // %filename% must survive the %name% pass. "%name%" is not a substring of
  // "%filename%" and the two must never be allowed to alias.
  QTest::newRow("filename-not-eaten-by-name")
      << QStringLiteral("%filename% %name%") << QStringList{"concert.mp4", "concert", file};
  // Naming a part of the path is not placing the media argument: %1 still
  // decides that, and here it suppresses the fallback so the path appears once.
  QTest::newRow("with-explicit-media") << QStringLiteral("--title=%name% %1")
                                       << QStringList{QStringLiteral("--title=concert"), file};
  // An unrecognised %word% token is passed through verbatim rather than
  // blanked, so a typo stays visible (the preview flags it as unresolved).
  QTest::newRow("unknown-token") << QStringLiteral("--x %nope%")
                                 << QStringList{"--x", "%nope%", file};
}

void TestLaunchCommandBuilder::testDerivedPathTokens() {
  QFETCH(QString, parameters);
  QFETCH(QStringList, expectedArguments);

  const auto result = LaunchCommandBuilder::buildLaunchCommand(
      plainLauncher(parameters), QStringLiteral("Talks"), QStringLiteral("/tmp/media/concert.mp4"));
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().arguments, expectedArguments);
}

void TestLaunchCommandBuilder::testDerivedTokenLeadingDashIsRejected() {
  // A file named "-rf.mkv" yields %name% == "-rf". Substitution can't add an
  // argument, but it can still hand the launcher a flag where it expected a
  // value, so a template that names the token is refused outright.
  const QString hostile = QStringLiteral("/tmp/media/-rf.mkv");
  const auto rejected = LaunchCommandBuilder::buildLaunchCommand(
      plainLauncher(QStringLiteral("--title %name%")), QStringLiteral("Talks"), hostile);
  QVERIFY(rejected.isError());

  // The guard is scoped to tokens the template actually names — the same file
  // launches normally when nothing asks for that part of the path. Without
  // this, one oddly-named file would break every item in the collection.
  const auto allowed = LaunchCommandBuilder::buildLaunchCommand(
      plainLauncher(QStringLiteral("--fullscreen")), QStringLiteral("Talks"), hostile);
  QVERIFY2(allowed.isOk(), qPrintable(allowed.isError() ? allowed.error().message : QString()));
  QCOMPARE(allowed.value().arguments, (QStringList{"--fullscreen", hostile}));
}

void TestLaunchCommandBuilder::testDerivedTokensOnKartLinkStub() {
  QTemporaryDir dir;
  KartLink::LinkData data;
  data.source = QStringLiteral("steam");
  data.target = QStringLiteral("steam://rungameid/400");
  const QString stub = dir.filePath(QStringLiteral("Studio Session.kartlink"));
  QVERIFY(KartLink::write(stub, data));

  // %name% follows the grid's display title, which for a stub is its filename —
  // never a fragment of the resolved target. The path-part tokens describe a
  // media file the stub does not have, so they expand to empty rather than to
  // something misleading like "400" or the managed stub folder. The title also
  // shows the boundary rule holding: a space in the value does not split it.
  const auto cmd = LaunchCommandBuilder::buildLaunchCommand(
      plainLauncher(QStringLiteral("--title=%name% --dir=%dir% --ext=%ext%")),
      QStringLiteral("Sessions"), stub);
  QVERIFY2(cmd.isOk(), qPrintable(cmd.isError() ? cmd.error().message : QString()));
  const QStringList expected = {QStringLiteral("--title=Studio Session"), QStringLiteral("--dir="),
                                QStringLiteral("--ext="), QStringLiteral("steam://rungameid/400")};
  QCOMPARE(cmd.value().arguments, expected);
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

void TestLaunchCommandBuilder::testMissingLauncherErrorNamesCollectionAndNextStep() {
  // Kartend-6tj2v: this error is the FIRST thing a user meets after importing
  // an ES-DE library, because those collections ship without a launcher by
  // design. A bare "No launcher configured" read as a broken import, so the
  // message must name the collection and say where to fix it. Asserting on the
  // text is deliberate — the wording IS the fix.
  LauncherConfig noLauncher; // launcherPath deliberately left empty
  const auto result = LaunchCommandBuilder::buildLaunchCommand(
      noLauncher, QStringLiteral("ES-DE: nes"), QStringLiteral("/tmp/roms/nes/game.nes"));
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);
  QVERIFY2(result.error().message.contains(QStringLiteral("ES-DE: nes")),
           qPrintable(QStringLiteral("message must name the collection, got: ") +
                      result.error().message));
  QVERIFY2(result.error().message.contains(QStringLiteral("Settings")),
           qPrintable(QStringLiteral("message must point at where to set one, got: ") +
                      result.error().message));
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

void TestLaunchCommandBuilder::testParseParametersKeepsQuotedEmptyArgument() {
  // An explicitly quoted empty argument is a real positional token: dropping
  // it would shift subsequent argv for launchers that require it.
  auto result =
      LaunchCommandBuilder::parseParameters(QStringLiteral("--profile \"\" --fullscreen"));
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value(), (QStringList{"--profile", "", "--fullscreen"}));

  // Sole parameter, double- and single-quoted.
  result = LaunchCommandBuilder::parseParameters(QStringLiteral("\"\""));
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value(), QStringList{QString()});

  result = LaunchCommandBuilder::parseParameters(QStringLiteral("''"));
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value(), QStringList{QString()});

  // Quoted-empty at the end of the template (exercises the end-of-string
  // append) and adjacent to a non-empty token.
  result = LaunchCommandBuilder::parseParameters(QStringLiteral("--profile \"\""));
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value(), (QStringList{"--profile", ""}));
}

void TestLaunchCommandBuilder::testKartLinkStubResolvesToTarget() {
  QTemporaryDir dir;
  KartLink::LinkData data;
  data.source = QStringLiteral("steam");
  data.target = QStringLiteral("steam://rungameid/220");
  const QString stub = dir.filePath(QStringLiteral("Half-Life 2.kartlink"));
  QVERIFY(KartLink::write(stub, data));

  const auto cmd = LaunchCommandBuilder::buildLaunchCommand(plainLauncher(QStringLiteral("%1")),
                                                            QStringLiteral("Steam"), stub);
  QVERIFY(!cmd.isError());
  // The stub's own path never reaches the argv — only the target does.
  QCOMPARE(cmd.value().arguments, QStringList{QStringLiteral("steam://rungameid/220")});
}

void TestLaunchCommandBuilder::testKartLinkStubAppendFallbackUsesTarget() {
  QTemporaryDir dir;
  KartLink::LinkData data;
  data.source = QStringLiteral("flatpak");
  data.target = QStringLiteral("net.supertuxkart.SuperTuxKart");
  const QString stub = dir.filePath(QStringLiteral("SuperTuxKart.kartlink"));
  QVERIFY(KartLink::write(stub, data));

  // Template carries no %1/%f — the append-at-end fallback must append the
  // resolved target, mirroring `flatpak run <app-id>`.
  const auto cmd = LaunchCommandBuilder::buildLaunchCommand(plainLauncher(QStringLiteral("run")),
                                                            QStringLiteral("Flatpak Games"), stub);
  QVERIFY(!cmd.isError());
  const QStringList expected = {QStringLiteral("run"),
                                QStringLiteral("net.supertuxkart.SuperTuxKart")};
  QCOMPARE(cmd.value().arguments, expected);
}

void TestLaunchCommandBuilder::testKartLinkStubArgumentsFollowTheTemplate() {
  QTemporaryDir dir;
  KartLink::LinkData data;
  data.source = QStringLiteral("bottles");
  data.target = QStringLiteral("The Game");
  data.args = {QStringLiteral("-b"), QStringLiteral("My Bottle"), QStringLiteral("--")};
  const QString stub = dir.filePath(QStringLiteral("The Game.kartlink"));
  QVERIFY(KartLink::write(stub, data));

  // `bottles-cli run -p "The Game" -b "My Bottle" --`: the template places the
  // target, the stub's args close the invocation. Each arg is one argv slot,
  // so the bottle name's space cannot split it.
  const auto cmd = LaunchCommandBuilder::buildLaunchCommand(
      plainLauncher(QStringLiteral("run -p %1")), QStringLiteral("Bottles"), stub);
  QVERIFY(!cmd.isError());
  const QStringList expected = {QStringLiteral("run"),       QStringLiteral("-p"),
                                QStringLiteral("The Game"),  QStringLiteral("-b"),
                                QStringLiteral("My Bottle"), QStringLiteral("--")};
  QCOMPARE(cmd.value().arguments, expected);

  // A stub without args is byte-identical to one written before the field
  // existed, and must build exactly as it did then.
  KartLink::LinkData plain;
  plain.source = QStringLiteral("steam");
  plain.target = QStringLiteral("steam://rungameid/400");
  const QString plainStub = dir.filePath(QStringLiteral("Portal.kartlink"));
  QVERIFY(KartLink::write(plainStub, plain));
  const auto plainCmd = LaunchCommandBuilder::buildLaunchCommand(
      plainLauncher(QStringLiteral("%1")), QStringLiteral("Steam"), plainStub);
  QVERIFY(!plainCmd.isError());
  QCOMPARE(plainCmd.value().arguments, QStringList{QStringLiteral("steam://rungameid/400")});
}

void TestLaunchCommandBuilder::testKartLinkStubArgumentsAreSecurityChecked() {
  QTemporaryDir dir;
  KartLink::LinkData data;
  data.source = QStringLiteral("bottles");
  data.target = QStringLiteral("The Game");
  data.args = {QStringLiteral("-b"), QStringLiteral("Bottle; rm -rf /")};
  const QString stub = dir.filePath(QStringLiteral("evil-args.kartlink"));
  QVERIFY(KartLink::write(stub, data));

  // Same gate as the target: a hand-edited stub can't smuggle metacharacters
  // in through the argument list either.
  const auto cmd = LaunchCommandBuilder::buildLaunchCommand(
      plainLauncher(QStringLiteral("run -p %1")), QStringLiteral("Bottles"), stub);
  QVERIFY(cmd.isError());
}

void TestLaunchCommandBuilder::testKartLinkBrokenStubFailsToBuild() {
  QTemporaryDir dir;
  // Stub file doesn't exist — the build must fail with the stub's read
  // error, not silently pass the stub path through to the launcher.
  const auto cmd = LaunchCommandBuilder::buildLaunchCommand(
      plainLauncher(QStringLiteral("%1")), QStringLiteral("Steam"),
      dir.filePath(QStringLiteral("gone.kartlink")));
  QVERIFY(cmd.isError());
}

void TestLaunchCommandBuilder::testKartLinkTargetIsSecurityChecked() {
  QTemporaryDir dir;
  KartLink::LinkData data;
  data.source = QStringLiteral("steam");
  data.target = QStringLiteral("steam://rungameid/220;rm -rf /");
  const QString stub = dir.filePath(QStringLiteral("evil.kartlink"));
  QVERIFY(KartLink::write(stub, data));

  // The resolved target goes through the same validatePathSecurity gate a
  // plain media path does — a hostile stub can't smuggle metacharacters.
  const auto cmd = LaunchCommandBuilder::buildLaunchCommand(plainLauncher(QStringLiteral("%1")),
                                                            QStringLiteral("Steam"), stub);
  QVERIFY(cmd.isError());
}

QTEST_APPLESS_MAIN(TestLaunchCommandBuilder)
#include "test_launchcommandbuilder.moc"
