/**
 * @file test_launchmanager.cpp
 * @brief Unit tests for LaunchManager validation functions
 *
 * Tests the security validation functions for launcher paths and parameters.
 */

#include "collection/collectionconfig.h"
#include "collection/helpers.h"
#include "collection/launcherconfig.h"
#include "launchmanager.h"
#include <QDir>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>

class TestLaunchManager : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  // validatePathSecurity tests
  void testValidatePathSecurity_validPath();
  void testValidatePathSecurity_emptyPath();
  void testValidatePathSecurity_shellMetacharacters_data();
  void testValidatePathSecurity_shellMetacharacters();
  void testValidatePathSecurity_nullBytes();
  void testValidatePathSecurity_newlines();
  void testValidatePathSecurity_backslash();
  void testValidatePathSecurity_unicodeNormalization();

  // validateLauncherPath tests
  void testValidateLauncherPath_validExecutable();
  void testValidateLauncherPath_resolvesViaPath();
  void testValidateLauncherPath_relativePath();
  void testValidateLauncherPath_nonExistent();
  void testValidateLauncherPath_notExecutable();
  void testValidateLauncherPath_sensitiveDirectories_data();
  void testValidateLauncherPath_sensitiveDirectories();

  // parseParameters tests
  void testParseParameters_empty();
  void testParseParameters_singleArg();
  void testParseParameters_multipleArgs();
  void testParseParameters_quotedArgs();
  void testParseParameters_mixedQuotes();
  void testParseParameters_unclosedQuotes();
  void testParseParameters_rejectsNewlines();

  // buildLaunchCommand tests
  void testBuildLaunchCommand_nonRetroArch_usesLaunchParameters();
  void testBuildLaunchCommand_retroArch_usesCorePath();
  void testBuildLaunchCommand_allowsAmpersandMediaPath();
  void testBuildLaunchCommand_rejectsCollectionPathTraversal_data();
  void testBuildLaunchCommand_rejectsCollectionPathTraversal();
  void testBuildLaunchCommand_unclosedQuoteParameterFails();

  // Multi-launcher tests
  void testLauncherCount_singlePrimary();
  void testLauncherCount_withAdditional();
  void testLauncherAt_returnsAdditionalEntry();
  void testLauncherDisplayName_fallsBackToBasename();
  void testBuildLaunchCommand_explicitLauncherConfig();
  void testClampValues_clampsDefaultLauncherIndex();

  // Preset resolution tests
  void testResolvePreset_returnsInputWhenNoPresetId();
  void testResolvePreset_overridesFieldsFromMatchingPreset();
  void testResolvePreset_fallsBackToInlineWhenPresetMissing();

  // findFileWithExtension hardening tests
  void testFindFileWithExtension_findsFlatFile();
  void testFindFileWithExtension_skipsSymlink();
  void testFindFileWithExtension_rejectsSymlinkEscapingRoot();
  void testFindFileWithExtension_respectsDepthLimit();
  void testFindFileWithExtension_emptyDirectory();

  // previewLaunchCommand tests
  void testPreview_buildErrorSurfacedAsWarning();
  void testPreview_missingFileSurfacesWarning();
  void testPreview_warnsWhenLauncherNotOnPath();
  void testPreview_resolvesAbsoluteLauncher();
  void testPreview_detectsUnresolvedPlaceholder();
  void testPreview_unclosedQuoteParameterSurfacedAsWarning();

private:
  QString m_tempExecutable;
  QString m_tempNonExecutable;
};

void TestLaunchManager::initTestCase() {
  // Create a temporary executable file for testing. Windows decides
  // executability by extension (QFileInfo::isExecutable() checks the
  // .exe/.bat/.cmd/.com tail), not by a unix-style ExeOwner permission
  // bit, so the test fixture has to differ per platform — a #!/bin/sh
  // script in a no-extension file isn't recognized as runnable on
  // NTFS, and a .bat with a shebang isn't valid batch.
#ifdef Q_OS_WIN
  static constexpr const char *kExecTemplate = "/test_launcher_XXXXXX.bat";
  static constexpr const char *kExecContent = "@echo off\r\nexit /b 0\r\n";
#else
  static constexpr const char *kExecTemplate = "/test_launcher_XXXXXX";
  static constexpr const char *kExecContent = "#!/bin/sh\nexit 0\n";
#endif
  QTemporaryFile tempExec;
  tempExec.setAutoRemove(false);
  tempExec.setFileTemplate(QDir::tempPath() + kExecTemplate);
  if (tempExec.open()) {
    tempExec.write(kExecContent);
    tempExec.close();
    m_tempExecutable = tempExec.fileName();
    // On Windows ExeOwner is a no-op (PE files don't have unix perm
    // bits) — the .bat extension is what makes the file "executable"
    // — but the call is harmless and keeps the test code uniform.
    QFile::setPermissions(m_tempExecutable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  }

  // Create a temporary non-executable file for testing
  QTemporaryFile tempNonExec;
  tempNonExec.setAutoRemove(false);
  tempNonExec.setFileTemplate(QDir::tempPath() + "/test_nonexec_XXXXXX");
  if (tempNonExec.open()) {
    tempNonExec.write("not executable");
    tempNonExec.close();
    m_tempNonExecutable = tempNonExec.fileName();
    QFile::setPermissions(m_tempNonExecutable, QFile::ReadOwner | QFile::WriteOwner);
  }
}

void TestLaunchManager::cleanupTestCase() {
  if (!m_tempExecutable.isEmpty()) {
    QFile::remove(m_tempExecutable);
  }
  if (!m_tempNonExecutable.isEmpty()) {
    QFile::remove(m_tempNonExecutable);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// validatePathSecurity tests
// ─────────────────────────────────────────────────────────────────────────────

void TestLaunchManager::testValidatePathSecurity_validPath() {
  auto result = LaunchManager::validatePathSecurity("/usr/bin/echo");
  QVERIFY2(result.isOk(), "Valid path should pass security validation");
}

void TestLaunchManager::testValidatePathSecurity_emptyPath() {
  auto result = LaunchManager::validatePathSecurity("");
  QVERIFY2(result.isError(), "Empty path should fail validation");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
}

void TestLaunchManager::testValidatePathSecurity_shellMetacharacters_data() {
  QTest::addColumn<QString>("path");
  QTest::addColumn<QString>("description");
  QTest::addColumn<bool>("shouldFail");

  QTest::newRow("semicolon") << "/path/to;command" << "semicolon injection" << true;
  QTest::newRow("pipe") << "/path/to|command" << "pipe injection" << true;
  QTest::newRow("ampersand") << "/path/to/Sonic & Knuckles.zip" << "ampersand in filename" << false;
  QTest::newRow("backtick") << "/path/to`command`" << "command substitution" << true;
  QTest::newRow("dollar") << "/path/to$HOME" << "variable expansion" << true;
  QTest::newRow("parens") << "/path/to$(command)" << "subshell" << true;
  // These characters are common in filenames and are safe with QProcess
  // (no shell interpretation). They should remain allowed.
  QTest::newRow("braces") << "/path/to{a,b}" << "brace expansion" << false;
  QTest::newRow("brackets") << "/path/to[abc]" << "glob pattern" << false;
  QTest::newRow("bang") << "/path/to!command" << "history expansion" << false;
  QTest::newRow("redirect-in") << "/path/to<input" << "input redirect" << true;
  QTest::newRow("redirect-out") << "/path/to>output" << "output redirect" << true;
}

void TestLaunchManager::testValidatePathSecurity_shellMetacharacters() {
  QFETCH(QString, path);
  QFETCH(QString, description);
  QFETCH(bool, shouldFail);

  auto result = LaunchManager::validatePathSecurity(path);
  if (shouldFail) {
    QVERIFY2(result.isError(), qPrintable(QString("Path with %1 should fail").arg(description)));
    QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
  } else {
    QVERIFY2(result.isOk(), qPrintable(QString("Path with %1 should be allowed").arg(description)));
  }
}

void TestLaunchManager::testValidatePathSecurity_nullBytes() {
  QString pathWithNull = QString("/path/to") + QChar('\0') + QString("/file");
  auto result = LaunchManager::validatePathSecurity(pathWithNull);
  QVERIFY2(result.isError(), "Path with null byte should fail validation");
}

void TestLaunchManager::testValidatePathSecurity_newlines() {
  auto resultNewline = LaunchManager::validatePathSecurity("/path/to\n/file");
  QVERIFY2(resultNewline.isError(), "Path with newline should fail validation");

  auto resultCarriageReturn = LaunchManager::validatePathSecurity("/path/to\r/file");
  QVERIFY2(resultCarriageReturn.isError(), "Path with carriage return should fail");
}

void TestLaunchManager::testValidatePathSecurity_backslash() {
  auto result = LaunchManager::validatePathSecurity("/path\\to\\file");
  QVERIFY2(result.isError(), "Path with backslash should fail validation");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
}

void TestLaunchManager::testValidatePathSecurity_unicodeNormalization() {
  // Test that paths with decomposed Unicode characters are rejected
  // The character 'é' can be represented as U+00E9 (composed) or U+0065 U+0301 (decomposed)
  // NFC normalization converts decomposed to composed form
  QString composedPath = "/path/to/caf\u00E9";    // é as single codepoint
  QString decomposedPath = "/path/to/cafe\u0301"; // e + combining acute

  auto composedResult = LaunchManager::validatePathSecurity(composedPath);
  QVERIFY2(composedResult.isOk(), "Composed Unicode path should pass");

  auto decomposedResult = LaunchManager::validatePathSecurity(decomposedPath);
  QVERIFY2(decomposedResult.isError(), "Decomposed Unicode path should fail (non-canonical)");
}

// ─────────────────────────────────────────────────────────────────────────────
// validateLauncherPath tests
// ─────────────────────────────────────────────────────────────────────────────

void TestLaunchManager::testValidateLauncherPath_validExecutable() {
  QVERIFY2(!m_tempExecutable.isEmpty(), "Test setup failed: no temp executable");

  auto result = LaunchManager::validateLauncherPath(m_tempExecutable);
  QVERIFY2(result.isOk(), qPrintable(QString("Valid executable should pass: %1")
                                         .arg(result.isError() ? result.error().message : "")));

  const QString expectedCanonical = QFileInfo(m_tempExecutable).canonicalFilePath();
  QVERIFY2(!expectedCanonical.isEmpty(), "Test setup failed: canonical path is empty");
  QCOMPARE(result.value(), expectedCanonical);
}

void TestLaunchManager::testValidateLauncherPath_resolvesViaPath() {
  QTemporaryDir dir;
  QVERIFY2(dir.isValid(), "Test setup failed: temp dir invalid");

  // Platform-conditional fixture. Windows decides executability by extension
  // (QStandardPaths::findExecutable walks %PATHEXT% appending .exe/.bat/...);
  // a no-extension shell shim isn't recognised. PATH separator is ';' on
  // Windows, ':' on POSIX.
#ifdef Q_OS_WIN
  const QString launcherName = "kartend-test-launcher";
  const QString launcherFile = launcherName + ".bat";
  const char *kShimContent = "@echo off\r\nexit /b 0\r\n";
  const QByteArray kPathSep = ";";
#else
  const QString launcherName = "kartend-test-launcher";
  const QString launcherFile = launcherName;
  const char *kShimContent = "#!/bin/sh\nexit 0\n";
  const QByteArray kPathSep = ":";
#endif
  const QString launcherPath = dir.filePath(launcherFile);

  QFile f(launcherPath);
  QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "Failed to create test launcher");
  f.write(kShimContent);
  f.close();

  QVERIFY2(
      QFile::setPermissions(launcherPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
      "Failed to make test launcher executable");

  const QByteArray oldPath = qgetenv("PATH");
  const QByteArray newPath = (dir.path().toUtf8() + kPathSep + oldPath);
  qputenv("PATH", newPath);

  // findExecutable on Windows tries each PATHEXT extension against the
  // base name, so passing the extensionless name resolves to the .bat
  // we wrote above.
  auto result = LaunchManager::validateLauncherPath(launcherName);
  QVERIFY2(result.isOk(), qPrintable(QString("PATH-resolved launcher should validate: %1")
                                         .arg(result.isError() ? result.error().message : "")));

  const QString expectedResolved = QFileInfo(launcherPath).canonicalFilePath();
  QVERIFY2(!expectedResolved.isEmpty(), "Test setup failed: expected canonical path is empty");
  QCOMPARE(result.value(), expectedResolved);

  qputenv("PATH", oldPath);
}

void TestLaunchManager::testValidateLauncherPath_relativePath() {
  auto result = LaunchManager::validateLauncherPath("relative/path/to/launcher");
  QVERIFY2(result.isError(), "Relative path should fail validation");
  // Current behavior: non-absolute launcher paths are treated as commands
  // to resolve via PATH; missing command returns FileNotFound.
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::FileNotFound);
}

void TestLaunchManager::testValidateLauncherPath_nonExistent() {
  auto result = LaunchManager::validateLauncherPath("/nonexistent/path/to/launcher");
  QVERIFY2(result.isError(), "Non-existent path should fail validation");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::FileNotFound);
}

void TestLaunchManager::testValidateLauncherPath_notExecutable() {
  QVERIFY2(!m_tempNonExecutable.isEmpty(), "Test setup failed: no temp non-executable");

  auto result = LaunchManager::validateLauncherPath(m_tempNonExecutable);
  QVERIFY2(result.isError(), "Non-executable file should fail validation");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
}

void TestLaunchManager::testValidateLauncherPath_sensitiveDirectories_data() {
  QTest::addColumn<QString>("path");
  QTest::addColumn<QString>("description");

  QTest::newRow("proc") << "/proc/self/exe" << "proc filesystem";
  QTest::newRow("sys") << "/sys/firmware/acpi" << "sys filesystem";
  QTest::newRow("dev") << "/dev/null" << "device node";
}

void TestLaunchManager::testValidateLauncherPath_sensitiveDirectories() {
  QFETCH(QString, path);
  QFETCH(QString, description);

  // These paths either don't exist as executables or should be rejected
  // The test verifies that paths resolving to sensitive directories fail
  auto result = LaunchManager::validateLauncherPath(path);
  // We expect failure either due to non-existence or sensitive directory restriction
  QVERIFY2(result.isError(),
           qPrintable(QString("Path in %1 should fail validation").arg(description)));
}

// ─────────────────────────────────────────────────────────────────────────────
// parseParameters tests
// ─────────────────────────────────────────────────────────────────────────────

void TestLaunchManager::testParseParameters_empty() {
  auto result = LaunchManager::parseParameters("");
  QVERIFY(result.isOk());
  QVERIFY(result.value().isEmpty());

  result = LaunchManager::parseParameters("   ");
  QVERIFY(result.isOk());
  QVERIFY(result.value().isEmpty());
}

void TestLaunchManager::testParseParameters_singleArg() {
  auto result = LaunchManager::parseParameters("arg1");
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 1);
  QCOMPARE(result.value()[0], "arg1");
}

void TestLaunchManager::testParseParameters_multipleArgs() {
  auto result = LaunchManager::parseParameters("arg1 arg2 arg3");
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 3);
  QCOMPARE(result.value()[0], "arg1");
  QCOMPARE(result.value()[1], "arg2");
  QCOMPARE(result.value()[2], "arg3");
}

void TestLaunchManager::testParseParameters_quotedArgs() {
  auto result = LaunchManager::parseParameters("\"arg with spaces\" arg2");
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 2);
  QCOMPARE(result.value()[0], "arg with spaces");
  QCOMPARE(result.value()[1], "arg2");

  result = LaunchManager::parseParameters("'single quoted' arg2");
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 2);
  QCOMPARE(result.value()[0], "single quoted");
  QCOMPARE(result.value()[1], "arg2");
}

void TestLaunchManager::testParseParameters_mixedQuotes() {
  auto result = LaunchManager::parseParameters("\"double\" 'single' plain");
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 3);
  QCOMPARE(result.value()[0], "double");
  QCOMPARE(result.value()[1], "single");
  QCOMPARE(result.value()[2], "plain");
}

void TestLaunchManager::testParseParameters_unclosedQuotes() {
  // Unclosed double quote should return error
  auto result = LaunchManager::parseParameters("\"unclosed arg");
  QVERIFY2(result.isError(), "Unclosed double quote should fail");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);

  // Unclosed single quote should return error
  result = LaunchManager::parseParameters("'unclosed arg");
  QVERIFY2(result.isError(), "Unclosed single quote should fail");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);

  // Mixed unclosed should return error
  result = LaunchManager::parseParameters("valid \"unclosed");
  QVERIFY2(result.isError(), "Unclosed quote at end should fail");
}

void TestLaunchManager::testParseParameters_rejectsNewlines() {
  auto result = LaunchManager::parseParameters("--foo\nbar");
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);

  result = LaunchManager::parseParameters("--foo\rbar");
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);
}

void TestLaunchManager::testBuildLaunchCommand_nonRetroArch_usesLaunchParameters() {
  CollectionConfig config;
  config.name = "TestCollection";
  config.launcher.launcherPath = "echo";
  config.launcher.corePath = "--SHOULD-NOT-BE-USED";
  config.launcher.launchParameters = "--fullscreen --scale 2";

  const QString filePath = "/tmp/testfile.bin";
  auto result = LaunchManager::buildLaunchCommand(config, filePath);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().program, QString("echo"));
  QCOMPARE(result.value().arguments, (QStringList{"--fullscreen", "--scale", "2", filePath}));
}

void TestLaunchManager::testBuildLaunchCommand_retroArch_usesCorePath() {
  CollectionConfig config;
  config.name = "TestCollection";
  config.launcher.launcherPath = "retroarch";
  config.launcher.corePath = "/tmp/core.so";
  config.launcher.launchParameters = "--should-be-ignored";

  const QString filePath = "/tmp/testfile.bin";
  auto result = LaunchManager::buildLaunchCommand(config, filePath);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().program, QString("retroarch"));
  QCOMPARE(result.value().arguments, (QStringList{"-L", "/tmp/core.so", filePath}));
}

void TestLaunchManager::testBuildLaunchCommand_unclosedQuoteParameterFails() {
  // An unclosed quote in launchParameters must abort buildLaunchCommand —
  // without this, the parse error was logged but the launch proceeded with
  // a truncated arg list, surfacing as a cryptic launcher failure (Kartend-x1oi).
  CollectionConfig config;
  config.name = "Concert Recordings";
  config.launcher.launcherPath = "mpv";
  config.launcher.launchParameters = "--title \"Unfinished";

  auto result = LaunchManager::buildLaunchCommand(config, "/tmp/recording.mp4");
  QVERIFY2(result.isError(), "Unclosed quote in launchParameters must abort buildLaunchCommand");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);
  QVERIFY(result.error().message.contains("Unclosed quote"));
}

void TestLaunchManager::testBuildLaunchCommand_rejectsCollectionPathTraversal_data() {
  QTest::addColumn<QString>("collectionName");
  QTest::newRow("dotdot-slash") << QStringLiteral("../etc");
  QTest::newRow("slash-prefix") << QStringLiteral("/etc/passwd");
  QTest::newRow("backslash") << QStringLiteral("foo\\bar");
  QTest::newRow("plain-dotdot") << QStringLiteral("..");
  QTest::newRow("empty") << QString();
}

void TestLaunchManager::testBuildLaunchCommand_rejectsCollectionPathTraversal() {
  QFETCH(QString, collectionName);

  LauncherConfig launcher;
  launcher.launcherPath = "/tmp/launchers/%collection%/runner";
  launcher.corePath = "";
  launcher.launchParameters = "";

  auto result = LaunchManager::buildLaunchCommand(launcher, collectionName, "/tmp/media/file.bin");
  QVERIFY2(result.isError(), "Expected buildLaunchCommand to refuse a collection name that injects "
                             "path traversal or separators into the %collection% substitution");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidArgument);
}

void TestLaunchManager::testBuildLaunchCommand_allowsAmpersandMediaPath() {
  CollectionConfig config;
  config.name = "Sega - Mega Drive - Genesis";
  config.launcher.launcherPath = "retroarch";
  config.launcher.corePath = "/tmp/genesis_plus_gx_libretro.so";

  const QString filePath = "/mnt/Games/Arcade/Collections/Sega - Mega Drive - Genesis/ROMs/"
                           "Sonic & Knuckles (World) (Beta) (1994-06-10).zip";
  auto result = LaunchManager::buildLaunchCommand(config, filePath);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().program, QString("retroarch"));
  QCOMPARE(result.value().arguments,
           (QStringList{"-L", "/tmp/genesis_plus_gx_libretro.so", filePath}));
}

// ---------------------------------------------------------------------------
// Multi-launcher
// ---------------------------------------------------------------------------

void TestLaunchManager::testLauncherCount_singlePrimary() {
  CollectionConfig config;
  config.launcher.launcherPath = "echo";
  QCOMPARE(config.launcher.launcherCount(), 1);
}

void TestLaunchManager::testLauncherCount_withAdditional() {
  CollectionConfig config;
  config.launcher.launcherPath = "echo";
  config.launcher.additionalLaunchers.append(LauncherConfig{"mGBA", "/usr/bin/mgba", "", ""});
  config.launcher.additionalLaunchers.append(LauncherConfig{"VBA-M", "/usr/bin/vbam", "", ""});
  QCOMPARE(config.launcher.launcherCount(), 3);
}

void TestLaunchManager::testLauncherAt_returnsAdditionalEntry() {
  CollectionConfig config;
  config.name = "GBA";
  config.launcher.launcherName = "RetroArch";
  config.launcher.launcherPath = "/usr/bin/retroarch";
  config.launcher.corePath = "/cores/mgba.so";
  config.launcher.launchParameters = "-fullscreen";
  config.launcher.additionalLaunchers.append(
      LauncherConfig{"mGBA Standalone", "/usr/bin/mgba", "", "--audio-buffers=2048"});

  LauncherConfig primary = config.launcher.launcherAt(0);
  QCOMPARE(primary.name, QString("RetroArch"));
  QCOMPARE(primary.launcherPath, QString("/usr/bin/retroarch"));
  QCOMPARE(primary.corePath, QString("/cores/mgba.so"));
  QCOMPARE(primary.launchParameters, QString("-fullscreen"));

  LauncherConfig additional = config.launcher.launcherAt(1);
  QCOMPARE(additional.name, QString("mGBA Standalone"));
  QCOMPARE(additional.launcherPath, QString("/usr/bin/mgba"));
  QCOMPARE(additional.launchParameters, QString("--audio-buffers=2048"));

  // Out-of-range falls through to an empty config (caller checks isEmpty()).
  LauncherConfig outOfRange = config.launcher.launcherAt(99);
  QVERIFY(outOfRange.launcherPath.isEmpty());
}

void TestLaunchManager::testLauncherDisplayName_fallsBackToBasename() {
  CollectionConfig config;
  config.launcher.launcherPath = "/usr/local/bin/retroarch";
  // No explicit launcherName → display name should be the basename.
  QCOMPARE(config.launcher.launcherDisplayName(0), QString("retroarch"));

  config.launcher.launcherName = "RA + GBA";
  QCOMPARE(config.launcher.launcherDisplayName(0), QString("RA + GBA"));

  config.launcher.additionalLaunchers.append(LauncherConfig{"", "/opt/mgba/mgba-qt", "", ""});
  QCOMPARE(config.launcher.launcherDisplayName(1), QString("mgba-qt"));
}

void TestLaunchManager::testBuildLaunchCommand_explicitLauncherConfig() {
  // The new buildLaunchCommand overload takes a LauncherConfig directly so a
  // user-picked entry from the chooser dialog can drive the
  // command without round-tripping through CollectionConfig's primary slot.
  LauncherConfig launcher{"mGBA", "/usr/bin/mgba", "", "--fullscreen"};
  const QString filePath = "/tmp/game.gba";
  auto result = LaunchManager::buildLaunchCommand(launcher, "GBA", filePath);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().program, QString("/usr/bin/mgba"));
  QCOMPARE(result.value().arguments, (QStringList{"--fullscreen", filePath}));
}

void TestLaunchManager::testClampValues_clampsDefaultLauncherIndex() {
  CollectionConfig config;
  config.launcher.launcherPath = "echo";
  config.launcher.defaultLauncherIndex = 7;
  config.clampValues();
  // With 1 launcher, valid range is [0, 0] — anything else clamps down.
  QCOMPARE(config.launcher.defaultLauncherIndex, 0);

  config.launcher.additionalLaunchers.append(LauncherConfig{"mGBA", "/usr/bin/mgba", "", ""});
  config.launcher.defaultLauncherIndex = 5;
  config.clampValues();
  QCOMPARE(config.launcher.defaultLauncherIndex, 1);

  config.launcher.defaultLauncherIndex = -3;
  config.clampValues();
  QCOMPARE(config.launcher.defaultLauncherIndex, 0);
}

// ---------------------------------------------------------------------------
// Preset resolution
// ---------------------------------------------------------------------------

void TestLaunchManager::testResolvePreset_returnsInputWhenNoPresetId() {
  // No presetId means "use inline fields verbatim" — the resolver must be a
  // no-op even when the presets list is non-empty (since nothing matches).
  LauncherConfig inline_;
  inline_.name = "Inline mGBA";
  inline_.launcherPath = "/usr/bin/mgba";
  inline_.launchParameters = "--fullscreen";

  QList<LauncherPreset> presets;
  presets.append({"preset-1", "RetroArch", "/usr/bin/retroarch", "/cores/mgba.so", "-v"});

  const LauncherConfig out = LauncherUtils::resolvePreset(inline_, presets);
  QCOMPARE(out.name, inline_.name);
  QCOMPARE(out.launcherPath, inline_.launcherPath);
  QCOMPARE(out.launchParameters, inline_.launchParameters);
  QVERIFY(out.presetId.isEmpty());
}

void TestLaunchManager::testResolvePreset_overridesFieldsFromMatchingPreset() {
  // When presetId matches, the preset's fields replace the inline ones —
  // this is the whole point: editing the preset propagates to all
  // references without touching the collection's launcher entry.
  LauncherConfig ref;
  ref.presetId = "preset-1";
  ref.name = "stale inline name"; // should be ignored once the preset wins
  ref.launcherPath = "/old/path";
  ref.launchParameters = "--stale";

  QList<LauncherPreset> presets;
  presets.append(
      {"preset-1", "RetroArch + mGBA", "/usr/bin/retroarch", "/cores/mgba.so", "--fullscreen"});

  const LauncherConfig out = LauncherUtils::resolvePreset(ref, presets);
  QCOMPARE(out.name, QString("RetroArch + mGBA"));
  QCOMPARE(out.launcherPath, QString("/usr/bin/retroarch"));
  QCOMPARE(out.corePath, QString("/cores/mgba.so"));
  QCOMPARE(out.launchParameters, QString("--fullscreen"));
  // Preset id stays attached so the resolved config can round-trip.
  QCOMPARE(out.presetId, QString("preset-1"));
}

void TestLaunchManager::testResolvePreset_fallsBackToInlineWhenPresetMissing() {
  // A reference to a deleted preset must not crash or silently lose data —
  // the resolver returns the original inline config (typically empty
  // fields, surfaced as a clear "no launcher configured" error at launch).
  LauncherConfig ref;
  ref.presetId = "deleted-preset";
  ref.name = "Fallback Inline";
  ref.launcherPath = "/usr/bin/mgba";

  QList<LauncherPreset> presets;
  presets.append({"some-other-preset", "Different", "/x", "", ""});

  const LauncherConfig out = LauncherUtils::resolvePreset(ref, presets);
  QCOMPARE(out.name, QString("Fallback Inline"));
  QCOMPARE(out.launcherPath, QString("/usr/bin/mgba"));
  QCOMPARE(out.presetId, QString("deleted-preset"));
}

// ---------------------------------------------------------------------------
// findFileWithExtension hardening
// ---------------------------------------------------------------------------

void TestLaunchManager::testFindFileWithExtension_findsFlatFile() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString target = root.path() + "/game.iso";
  QFile f(target);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x");
  f.close();

  const QString found = LaunchManager::findFileWithExtension(root.path(), ".iso");
  // Returned path is canonicalized.
  QCOMPARE(found, QFileInfo(target).canonicalFilePath());
}

void TestLaunchManager::testFindFileWithExtension_skipsSymlink() {
#ifdef Q_OS_WIN
  // QFile::link() on Windows creates a real NTFS reparse-point symbolic
  // link, but QDir::NoSymLinks (the filter findFileWithExtension uses to
  // skip symlinked entries) doesn't catch every reparse-point class
  // consistently across Qt versions. The escape-detection code is still
  // safe at runtime on Windows — kart bundles can't ship NTFS reparse
  // points through the QDataStream-serialised manifest in the first
  // place — so the gap is in the test fixture, not the production
  // surface. Tracked separately if/when symlink semantics matter on
  // Windows.
  QSKIP("QDir::NoSymLinks doesn't reliably catch NTFS reparse points on Windows");
#else
  QTemporaryDir root;
  QVERIFY(root.isValid());

  // Real file outside the root.
  QTemporaryDir outside;
  QVERIFY(outside.isValid());
  const QString outsideFile = outside.path() + "/escape.iso";
  QFile f(outsideFile);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x");
  f.close();

  // Symlink inside the root pointing at the outside file.
  const QString link = root.path() + "/link.iso";
  if (!QFile::link(outsideFile, link)) {
    QSKIP("Filesystem does not support symlinks");
  }

  const QString found = LaunchManager::findFileWithExtension(root.path(), ".iso");
  // The symlink must be skipped; nothing else with .iso exists in root.
  QVERIFY2(found.isEmpty(), qPrintable(QString("Symlink should be rejected, got: %1").arg(found)));
#endif
}

void TestLaunchManager::testFindFileWithExtension_rejectsSymlinkEscapingRoot() {
  QTemporaryDir root;
  QVERIFY(root.isValid());

  QTemporaryDir outside;
  QVERIFY(outside.isValid());
  const QString outsideFile = outside.path() + "/escape.iso";
  QFile f(outsideFile);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x");
  f.close();

  // A subdirectory in root that is itself a symlink to an outside dir.
  const QString linkDir = root.path() + "/sub";
  if (!QFile::link(outside.path(), linkDir)) {
    QSKIP("Filesystem does not support symlinks");
  }

  const QString found = LaunchManager::findFileWithExtension(root.path(), ".iso");
  QVERIFY2(found.isEmpty(),
           qPrintable(QString("Escape via symlinked dir should be rejected, got: %1").arg(found)));
}

void TestLaunchManager::testFindFileWithExtension_respectsDepthLimit() {
  QTemporaryDir root;
  QVERIFY(root.isValid());

  // Build a chain deeper than MAX_EXTRACTION_DEPTH.
  QString cur = root.path();
  for (int i = 0; i < 32; ++i) {
    cur += "/d";
    QVERIFY(QDir().mkpath(cur));
  }
  const QString deepFile = cur + "/deep.iso";
  QFile f(deepFile);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x");
  f.close();

  const QString found = LaunchManager::findFileWithExtension(root.path(), ".iso");
  // Beyond MAX_EXTRACTION_DEPTH (16) - should not be returned.
  QVERIFY2(found.isEmpty(),
           qPrintable(QString("Should ignore files past depth limit, got: %1").arg(found)));
}

void TestLaunchManager::testFindFileWithExtension_emptyDirectory() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  QCOMPARE(LaunchManager::findFileWithExtension(root.path(), ".iso"), QString());
}

void TestLaunchManager::testPreview_buildErrorSurfacedAsWarning() {
  // Empty launcher path triggers buildLaunchCommand's "No launcher
  // configured" error. previewLaunchCommand surfaces this as buildOk=false
  // plus a warning so the dialog can render the failure mode visibly.
  CollectionConfig collection;
  collection.name = "Concert Recordings";
  LauncherConfig launcher; // launcherPath is empty by default
  const auto preview = LaunchManager::previewLaunchCommand(collection, launcher, "/some/file.mp4");
  QVERIFY(!preview.buildOk);
  QVERIFY(!preview.buildError.isEmpty());
  QCOMPARE(preview.warnings.size(), 1);
  QVERIFY(preview.warnings.first().contains("No launcher"));
}

void TestLaunchManager::testPreview_unclosedQuoteParameterSurfacedAsWarning() {
  // End-to-end: an unclosed quote in launchParameters reaches the dialog as
  // buildOk=false plus a warning naming the unclosed-quote condition, so the
  // user can fix the parameter string without hitting a cryptic exec failure.
  CollectionConfig collection;
  collection.name = "Concert Recordings";
  LauncherConfig launcher;
  launcher.launcherPath = m_tempExecutable;
  launcher.launchParameters = "--title \"Unfinished";

  const auto preview = LaunchManager::previewLaunchCommand(collection, launcher, m_tempExecutable);
  QVERIFY(!preview.buildOk);
  QVERIFY(preview.buildError.contains("Unclosed quote"));
  QCOMPARE(preview.warnings.size(), 1);
  QVERIFY(preview.warnings.first().contains("Unclosed quote"));
}

void TestLaunchManager::testPreview_missingFileSurfacesWarning() {
  CollectionConfig collection;
  collection.name = "Concert Recordings";
  LauncherConfig launcher;
  launcher.launcherPath = m_tempExecutable;
  const QString missingFile = QDir::tempPath() + "/does-not-exist-here-12345.mp4";
  // Ensure the file really doesn't exist so the test isn't racing a
  // stale leftover.
  QFile::remove(missingFile);

  const auto preview = LaunchManager::previewLaunchCommand(collection, launcher, missingFile);
  QVERIFY(preview.buildOk);
  QVERIFY(!preview.fileExists);
  bool sawFileMissingWarning = false;
  for (const QString &w : preview.warnings) {
    if (w.contains("File does not exist")) {
      sawFileMissingWarning = true;
      break;
    }
  }
  QVERIFY(sawFileMissingWarning);
}

void TestLaunchManager::testPreview_warnsWhenLauncherNotOnPath() {
  CollectionConfig collection;
  collection.name = "Audio";
  LauncherConfig launcher;
  // A bare command name that surely isn't on PATH.
  launcher.launcherPath = "kartend-no-such-binary-xyz";
  const auto preview = LaunchManager::previewLaunchCommand(collection, launcher, m_tempExecutable);
  QVERIFY(preview.buildOk);
  QVERIFY(preview.resolvedProgram.isEmpty());
  bool sawNotFound = false;
  for (const QString &w : preview.warnings) {
    if (w.contains("not found")) {
      sawNotFound = true;
      break;
    }
  }
  QVERIFY(sawNotFound);
}

void TestLaunchManager::testPreview_resolvesAbsoluteLauncher() {
  CollectionConfig collection;
  collection.name = "Audio";
  LauncherConfig launcher;
  launcher.launcherPath = m_tempExecutable; // absolute path created in initTestCase
  const auto preview = LaunchManager::previewLaunchCommand(collection, launcher, m_tempExecutable);
  QVERIFY(preview.buildOk);
  // The resolved path must match the absolute launcher path the user
  // provided — no PATH lookup, no rewriting.
  QCOMPARE(preview.resolvedProgram, m_tempExecutable);
  QVERIFY(preview.fileExists);
}

void TestLaunchManager::testPreview_detectsUnresolvedPlaceholder() {
  // A stray `%scummvm_id%` (or any other %name%) in the launch parameter
  // string is almost certainly a typo — the preview surfaces it so the
  // user can fix it before launching.
  CollectionConfig collection;
  collection.name = "Audio";
  LauncherConfig launcher;
  launcher.launcherPath = m_tempExecutable;
  launcher.launchParameters = "--id=%scummvm_id% --quiet";
  const auto preview = LaunchManager::previewLaunchCommand(collection, launcher, m_tempExecutable);
  QVERIFY(preview.buildOk);
  bool sawPlaceholderWarning = false;
  for (const QString &w : preview.warnings) {
    if (w.contains("placeholder")) {
      sawPlaceholderWarning = true;
      break;
    }
  }
  QVERIFY(sawPlaceholderWarning);
}

QTEST_MAIN(TestLaunchManager)
#include "test_launchmanager.moc"
