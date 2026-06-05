/**
 * @file test_pathutils.cpp
 * @brief Unit tests for PathUtils functions
 *
 * Tests path validation, expansion, and display utilities.
 */

#include "pathutils.h"
#include <QDir>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

class TestPathUtils : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  // validateAndExpandPath tests
  void testValidateAndExpandPath_validAbsolutePath();
  void testValidateAndExpandPath_emptyPath();
  void testValidateAndExpandPath_relativePath();
  void testValidateAndExpandPath_nonExistentPath();
  void testValidateAndExpandPath_withPlaceholder();
  void testValidateAndExpandPath_tildeExpansion();
  void testValidateAndExpandPath_tildeOnly();

  // %collection% substitution seam guard (Kartend-2ml9)
  void testExpandPathSeam_collectionNameGuard_data();
  void testExpandPathSeam_collectionNameGuard();
  void testValidateAndExpandPath_traversalNameDoesNotResolve();

  // tryValidateAndExpandPath tests (Result version)
  void testTryValidateAndExpandPath_validPath();
  void testTryValidateAndExpandPath_emptyPath();
  void testTryValidateAndExpandPath_relativePath();
  void testTryValidateAndExpandPath_nonExistentPath();
  void testTryValidateAndExpandPath_errorDetails();

  // truncatePathForDisplay tests
  void testTruncatePathForDisplay_shortPath();
  void testTruncatePathForDisplay_exactLength();
  void testTruncatePathForDisplay_longPath();
  void testTruncatePathForDisplay_customLength();
  void testTruncatePathForDisplay_belowMinLength();

  // normalizeDisplayName tests
  void testNormalizeDisplayName_underscores();
  void testNormalizeDisplayName_dashes();
  void testNormalizeDisplayName_mixedCase();
  void testNormalizeDisplayName_extraSpaces();

  // validatePathSecurity tests
  void testValidatePathSecurity_validPath();
  void testValidatePathSecurity_emptyPath();
  void testValidatePathSecurity_shellMetacharacters_data();
  void testValidatePathSecurity_shellMetacharacters();
  void testValidatePathSecurity_nullBytes();
  void testValidatePathSecurity_newlines();
  void testValidatePathSecurity_backslash();
  void testValidatePathSecurity_nfdUnicodeAccepted();
  void testValidatePathSecurity_nfcUnicodeAccepted();
  void testValidatePathSecurity_traversalSegments_data();
  void testValidatePathSecurity_traversalSegments();

  // syncDirectory tests
  void testSyncDirectory_existingDir();
  void testSyncDirectory_emptyPath();
  void testSyncDirectory_nonExistentDir();

  // PathStatus tests (Kartend-qc1c)
  void testCheckLauncherPath_empty();
  void testCheckLauncherPath_missing();
  void testCheckLauncherPath_existsButNotExecutable();
  void testCheckLauncherPath_existsAndExecutable();
  void testCheckLauncherPath_bareCommandResolvesViaPath();
  void testCheckDirectoryPath_empty();
  void testCheckDirectoryPath_missing();
  void testCheckDirectoryPath_existingDir();
  void testCheckDirectoryPath_wrongTypeIsFile();
  void testCheckFilePath_empty();
  void testCheckFilePath_missing();
  void testCheckFilePath_existingFile();
  void testCheckFilePath_wrongTypeIsDir();
  void testPathStatusDescription_okAndEmptyAreBlank();
  void testPathStatusDescription_nonOkProducesText();

private:
  QTemporaryDir m_tempDir;
};

void TestPathUtils::initTestCase() {
  QVERIFY2(m_tempDir.isValid(), "Failed to create temporary directory");
}

void TestPathUtils::cleanupTestCase() {
  // QTemporaryDir auto-cleans
}

// ─────────────────────────────────────────────────────────────────────────────
// validateAndExpandPath tests (legacy function)
// ─────────────────────────────────────────────────────────────────────────────

void TestPathUtils::testValidateAndExpandPath_validAbsolutePath() {
  QString result = PathUtils::validateAndExpandPath(m_tempDir.path());
  QVERIFY2(!result.isEmpty(), "Valid absolute path should return non-empty");
  QCOMPARE(result, QDir(m_tempDir.path()).absolutePath());
}

void TestPathUtils::testValidateAndExpandPath_emptyPath() {
  QString result = PathUtils::validateAndExpandPath("");
  QVERIFY2(result.isEmpty(), "Empty path should return empty string");
}

void TestPathUtils::testValidateAndExpandPath_relativePath() {
  QString result = PathUtils::validateAndExpandPath("relative/path");
  QVERIFY2(result.isEmpty(), "Relative path should return empty string");
}

void TestPathUtils::testValidateAndExpandPath_nonExistentPath() {
  QString result = PathUtils::validateAndExpandPath("/nonexistent/path/12345");
  QVERIFY2(result.isEmpty(), "Non-existent path should return empty string");
}

void TestPathUtils::testValidateAndExpandPath_withPlaceholder() {
  // Create subdirectory named after "TestCollection"
  QDir tempDir(m_tempDir.path());
  QString subdir = "TestCollection";
  tempDir.mkdir(subdir);

  QString pathWithPlaceholder = m_tempDir.path() + "/%collection%";
  QString result = PathUtils::validateAndExpandPath(pathWithPlaceholder, subdir);

  QVERIFY2(!result.isEmpty(), "Path with placeholder should expand correctly");
  QVERIFY2(result.endsWith(subdir), "Expanded path should contain collection name");
}

void TestPathUtils::testValidateAndExpandPath_tildeExpansion() {
  // Test that ~/path expands to home directory + /path
  // We use home directory itself since it's guaranteed to exist
  QString pathWithTilde = "~";
  QString result = PathUtils::validateAndExpandPath(pathWithTilde);

  QVERIFY2(!result.isEmpty(), "~ should expand to home directory");
  QCOMPARE(result, QDir::homePath());
}

void TestPathUtils::testValidateAndExpandPath_tildeOnly() {
  // Test that ~ alone expands correctly
  auto result = PathUtils::tryValidateAndExpandPath("~");

  QVERIFY2(result.isOk(), "~ alone should expand to home directory");
  QCOMPARE(result.value(), QDir::homePath());
}

// ─────────────────────────────────────────────────────────────────────────────
// %collection% substitution seam guard (Kartend-2ml9)
// ─────────────────────────────────────────────────────────────────────────────

void TestPathUtils::testExpandPathSeam_collectionNameGuard_data() {
  QTest::addColumn<QString>("collectionName");
  QTest::addColumn<bool>("shouldSubstitute");

  QTest::newRow("safe-simple") << QStringLiteral("Movies") << true;
  QTest::newRow("safe-spaces") << QStringLiteral("Studio Ghibli Films") << true;
  QTest::newRow("safe-dot-inside") << QStringLiteral("Vol.2") << true;
  QTest::newRow("traversal-slashes") << QStringLiteral("../../etc") << false;
  QTest::newRow("dotdot") << QStringLiteral("..") << false;
  QTest::newRow("dot") << QStringLiteral(".") << false;
  QTest::newRow("forward-slash") << QStringLiteral("a/b") << false;
  QTest::newRow("backslash") << QStringLiteral("a\\b") << false;
}

void TestPathUtils::testExpandPathSeam_collectionNameGuard() {
  QFETCH(QString, collectionName);
  QFETCH(bool, shouldSubstitute);

  // expandPathWithoutExistenceCheck exercises the substitution seam without
  // requiring the target directory to exist on disk.
  const QString tmpl = m_tempDir.path() + QStringLiteral("/%collection%/media");

  if (!shouldSubstitute) {
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("Refusing to substitute")));
  }
  const QString result = PathUtils::expandPathWithoutExistenceCheck(tmpl, collectionName);

  if (shouldSubstitute) {
    QString expected = tmpl;
    expected.replace(QStringLiteral("%collection%"), collectionName);
    QCOMPARE(result, expected);
  } else {
    // Refused: the placeholder is left literal so the template is unchanged and
    // the unsafe name never reaches the resolved path.
    QCOMPARE(result, tmpl);
  }
}

void TestPathUtils::testValidateAndExpandPath_traversalNameDoesNotResolve() {
  // End-to-end: unguarded, "<temp>/../../etc" would canonicalize to an existing
  // path outside the root (e.g. /etc on Unix) and resolve. The seam refuses the
  // substitution, leaving "<temp>/%collection%" which does not exist -> "".
  const QString tmpl = m_tempDir.path() + QStringLiteral("/%collection%");
  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Refusing to substitute")));
  const QString result = PathUtils::validateAndExpandPath(tmpl, QStringLiteral("../../etc"));
  QVERIFY2(result.isEmpty(), "Traversal collection name must not resolve to a filesystem path");
}

// ─────────────────────────────────────────────────────────────────────────────
// tryValidateAndExpandPath tests (Result version)
// ─────────────────────────────────────────────────────────────────────────────

void TestPathUtils::testTryValidateAndExpandPath_validPath() {
  auto result = PathUtils::tryValidateAndExpandPath(m_tempDir.path());
  QVERIFY2(result.isOk(), "Valid path should return Ok result");
  QCOMPARE(result.value(), QDir(m_tempDir.path()).absolutePath());
}

void TestPathUtils::testTryValidateAndExpandPath_emptyPath() {
  auto result = PathUtils::tryValidateAndExpandPath("");
  QVERIFY2(result.isError(), "Empty path should return Error result");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
}

void TestPathUtils::testTryValidateAndExpandPath_relativePath() {
  auto result = PathUtils::tryValidateAndExpandPath("relative/path");
  QVERIFY2(result.isError(), "Relative path should return Error result");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
}

void TestPathUtils::testTryValidateAndExpandPath_nonExistentPath() {
  auto result = PathUtils::tryValidateAndExpandPath("/nonexistent/path/12345");
  QVERIFY2(result.isError(), "Non-existent path should return Error result");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::FileNotFound);
}

void TestPathUtils::testTryValidateAndExpandPath_errorDetails() {
  auto result = PathUtils::tryValidateAndExpandPath("/nonexistent/path/12345", "TestCollection");
  QVERIFY2(result.isError(), "Should return error for non-existent path");
  QVERIFY2(!result.error().details.isEmpty(), "Error should include details");
}

// ─────────────────────────────────────────────────────────────────────────────
// truncatePathForDisplay tests
// ─────────────────────────────────────────────────────────────────────────────

void TestPathUtils::testTruncatePathForDisplay_shortPath() {
  QString shortPath = "/home/user";
  QString result = PathUtils::truncatePathForDisplay(shortPath, 50);
  QCOMPARE(result, shortPath);
}

void TestPathUtils::testTruncatePathForDisplay_exactLength() {
  QString path = QString("/").leftJustified(50, 'a');
  QString result = PathUtils::truncatePathForDisplay(path, 50);
  QCOMPARE(result, path);
}

void TestPathUtils::testTruncatePathForDisplay_longPath() {
  QString longPath = "/home/user/very/long/path/to/some/deeply/nested/directory/structure";
  QString result = PathUtils::truncatePathForDisplay(longPath, 30);

  QCOMPARE(result.length(), 30);
  QVERIFY2(result.startsWith("..."), "Truncated path should start with ellipsis");
}

void TestPathUtils::testTruncatePathForDisplay_customLength() {
  QString path = "/home/user/documents/file.txt";
  QString result = PathUtils::truncatePathForDisplay(path, 20);

  QCOMPARE(result.length(), 20);
  QVERIFY(result.startsWith("..."));
}

void TestPathUtils::testTruncatePathForDisplay_belowMinLength() {
  // maxLength below 3 leaves no room for the "..." prefix. It must clamp to 3
  // so the result never exceeds the original — the old code passed a negative
  // to QString::right, which returned the whole (longer) string (Kartend-3pxm).
  const QString longPath = "/home/user/very/long/path/that/exceeds/any/tiny/maxLength";
  for (int maxLength : {0, 1, 2}) {
    const QString result = PathUtils::truncatePathForDisplay(longPath, maxLength);
    QVERIFY2(result.length() <= longPath.length(),
             qPrintable(QStringLiteral("maxLength=%1 produced a longer string").arg(maxLength)));
    QCOMPARE(result, QStringLiteral("..."));
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// normalizeDisplayName tests
// ─────────────────────────────────────────────────────────────────────────────

void TestPathUtils::testNormalizeDisplayName_underscores() {
  QString result = PathUtils::normalizeDisplayName("Game_Title_Name");
  QCOMPARE(result, "game title name");
}

void TestPathUtils::testNormalizeDisplayName_dashes() {
  QString result = PathUtils::normalizeDisplayName("Game-Title-Name");
  QCOMPARE(result, "game title name");
}

void TestPathUtils::testNormalizeDisplayName_mixedCase() {
  QString result = PathUtils::normalizeDisplayName("GameTitleNAME");
  QCOMPARE(result, "gametitlename");
}

void TestPathUtils::testNormalizeDisplayName_extraSpaces() {
  QString result = PathUtils::normalizeDisplayName("  Game   Title  ");
  QCOMPARE(result, "game title");
}

// ─────────────────────────────────────────────────────────────────────────────
// validatePathSecurity tests
// ─────────────────────────────────────────────────────────────────────────────

void TestPathUtils::testValidatePathSecurity_validPath() {
  auto result = PathUtils::validatePathSecurity("/home/user/games/rom.zip");
  QVERIFY2(result.isOk(), "Valid path should pass security check");
}

void TestPathUtils::testValidatePathSecurity_emptyPath() {
  auto result = PathUtils::validatePathSecurity("");
  QVERIFY2(result.isError(), "Empty path should fail security check");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
}

void TestPathUtils::testValidatePathSecurity_shellMetacharacters_data() {
  QTest::addColumn<QString>("path");
  QTest::addColumn<QString>("description");
  QTest::addColumn<bool>("shouldFail");

  QTest::newRow("semicolon") << "/path/with;command" << "semicolon" << true;
  QTest::newRow("pipe") << "/path/with|pipe" << "pipe" << true;
  QTest::newRow("ampersand") << "/path/with/Sonic & Knuckles.zip" << "ampersand" << false;
  QTest::newRow("backtick") << "/path/with`command`" << "backtick" << true;
  QTest::newRow("dollar") << "/path/with$VAR" << "dollar sign" << true;
  QTest::newRow("redirect-in") << "/path/with<input" << "input redirect" << true;
  QTest::newRow("redirect-out") << "/path/with>output" << "output redirect" << true;
}

void TestPathUtils::testValidatePathSecurity_shellMetacharacters() {
  QFETCH(QString, path);
  QFETCH(QString, description);
  QFETCH(bool, shouldFail);

  auto result = PathUtils::validatePathSecurity(path);
  if (shouldFail) {
    QVERIFY2(result.isError(), qPrintable(QString("Path with %1 should fail").arg(description)));
    QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
  } else {
    QVERIFY2(result.isOk(), qPrintable(QString("Path with %1 should be allowed").arg(description)));
  }
}

void TestPathUtils::testValidatePathSecurity_nullBytes() {
  QString pathWithNull = QString("/path/with") + QChar('\0') + QString("null");
  auto result = PathUtils::validatePathSecurity(pathWithNull);
  QVERIFY2(result.isError(), "Path with null bytes should fail");
}

void TestPathUtils::testValidatePathSecurity_newlines() {
  auto result1 = PathUtils::validatePathSecurity("/path/with\nnewline");
  QVERIFY2(result1.isError(), "Path with newline should fail");

  auto result2 = PathUtils::validatePathSecurity("/path/with\rcarriage");
  QVERIFY2(result2.isError(), "Path with carriage return should fail");
}

void TestPathUtils::testValidatePathSecurity_backslash() {
  auto result = PathUtils::validatePathSecurity("/path/with\\backslash");
#ifdef Q_OS_WIN
  // On Windows the backslash is the native path separator, so it's accepted —
  // the Windows cross-build's native CLI paths use it.
  QVERIFY2(result.isOk(), "Path with backslash should be allowed on Windows");
#else
  QVERIFY2(result.isError(), "Path with backslash should fail on non-Windows");
#endif
}

void TestPathUtils::testValidatePathSecurity_nfdUnicodeAccepted() {
  // A decomposed (NFD) accented filename — routine from macOS / cross-platform
  // media — must be accepted, not rejected as "non-canonical". "café" with the
  // accent as a combining mark: 'e' + U+0301 COMBINING ACUTE ACCENT.
  const QString nfd =
      QStringLiteral("/home/user/games/cafe") + QChar(0x0301) + QStringLiteral(".zip");
  // Guard the premise: this really is non-NFC, so the old code would have
  // rejected it at the `normalized != path` check.
  QVERIFY(nfd != nfd.normalized(QString::NormalizationForm_C));
  auto result = PathUtils::validatePathSecurity(nfd);
  QVERIFY2(result.isOk(), "NFD (decomposed) accented path should be accepted");
}

void TestPathUtils::testValidatePathSecurity_nfcUnicodeAccepted() {
  // The precomposed (NFC) form of the same name stays accepted (regression):
  // 'é' as the single codepoint U+00E9.
  const QString nfc =
      QStringLiteral("/home/user/games/caf") + QChar(0x00E9) + QStringLiteral(".zip");
  QCOMPARE(nfc, nfc.normalized(QString::NormalizationForm_C));
  auto result = PathUtils::validatePathSecurity(nfc);
  QVERIFY2(result.isOk(), "NFC (precomposed) accented path should be accepted");
}

void TestPathUtils::testValidatePathSecurity_traversalSegments_data() {
  QTest::addColumn<QString>("path");
  QTest::addColumn<bool>("shouldFail");

  // A standalone `..` segment traverses out of the intended directory and must
  // be rejected (Kartend-w13c) — including via the CLI seam that relies solely
  // on this validator.
  QTest::newRow("absolute-with-dotdot") << "/home/user/../etc/passwd" << true;
  QTest::newRow("leading-dotdot") << "../../etc/passwd" << true;
  QTest::newRow("dotdot-alone") << ".." << true;
  QTest::newRow("trailing-dotdot") << "/home/user/.." << true;
  // A literal `..` inside a filename is not a traversal and stays allowed.
  QTest::newRow("dotdot-inside-name") << "/home/user/my..file.zip" << false;
  // A single-dot `.` segment is the current dir, not traversal — allowed.
  QTest::newRow("single-dot-segment") << "/home/user/./games" << false;
  QTest::newRow("clean-absolute") << "/home/user/games/rom.zip" << false;
}

void TestPathUtils::testValidatePathSecurity_traversalSegments() {
  QFETCH(QString, path);
  QFETCH(bool, shouldFail);

  auto result = PathUtils::validatePathSecurity(path);
  if (shouldFail) {
    QVERIFY2(result.isError(), qPrintable(QStringLiteral("Path '%1' should fail").arg(path)));
    QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
  } else {
    QVERIFY2(result.isOk(), qPrintable(QStringLiteral("Path '%1' should pass").arg(path)));
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// syncDirectory tests
// ─────────────────────────────────────────────────────────────────────────────

void TestPathUtils::testSyncDirectory_existingDir() {
  // Sync of a real directory should succeed (or be a no-op on non-POSIX).
  QVERIFY2(PathUtils::syncDirectory(m_tempDir.path()),
           "Sync of an existing directory should succeed");
}

void TestPathUtils::testSyncDirectory_emptyPath() {
#if defined(Q_OS_UNIX)
  QVERIFY2(!PathUtils::syncDirectory(QString()), "Empty path should fail on POSIX");
#else
  QVERIFY2(PathUtils::syncDirectory(QString()), "Empty path is a no-op on non-POSIX");
#endif
}

void TestPathUtils::testSyncDirectory_nonExistentDir() {
#if defined(Q_OS_UNIX)
  QVERIFY2(!PathUtils::syncDirectory("/nonexistent/path/abcxyz"),
           "Non-existent path should fail on POSIX");
#else
  QVERIFY2(PathUtils::syncDirectory("/nonexistent/path/abcxyz"),
           "Non-existent path is a no-op on non-POSIX");
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// PathStatus tests (Kartend-qc1c)
// ─────────────────────────────────────────────────────────────────────────────

void TestPathUtils::testCheckLauncherPath_empty() {
  QCOMPARE(PathUtils::checkLauncherPath(QString()), PathUtils::PathStatus::Empty);
}

void TestPathUtils::testCheckLauncherPath_missing() {
  QCOMPARE(PathUtils::checkLauncherPath(QStringLiteral("/nonexistent/launcher/abcxyz123")),
           PathUtils::PathStatus::Missing);
}

void TestPathUtils::testCheckLauncherPath_existsButNotExecutable() {
  // Plain text file under the temp dir — exists, readable, not executable.
  const QString path = m_tempDir.path() + QStringLiteral("/notexec.txt");
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("hello");
  f.close();
  QCOMPARE(PathUtils::checkLauncherPath(path), PathUtils::PathStatus::NotExecutable);
}

void TestPathUtils::testCheckLauncherPath_existsAndExecutable() {
#ifdef Q_OS_WIN
  const QString path = m_tempDir.path() + QStringLiteral("/runner.bat");
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("@echo off\r\nexit /b 0\r\n");
  f.close();
#else
  const QString path = m_tempDir.path() + QStringLiteral("/runner.sh");
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("#!/bin/sh\nexit 0\n");
  f.close();
  QVERIFY(
      f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
#endif
  QCOMPARE(PathUtils::checkLauncherPath(path), PathUtils::PathStatus::OK);
}

void TestPathUtils::testCheckLauncherPath_bareCommandResolvesViaPath() {
  // `sh` (POSIX) / `cmd` (Windows) is essentially guaranteed to be on PATH
  // for any test environment we care about. Verifies the PATH-resolution
  // branch matches the reachable on-disk binary.
#ifdef Q_OS_WIN
  const QString bareCommand = QStringLiteral("cmd");
#else
  const QString bareCommand = QStringLiteral("sh");
#endif
  QCOMPARE(PathUtils::checkLauncherPath(bareCommand), PathUtils::PathStatus::OK);
}

void TestPathUtils::testCheckDirectoryPath_empty() {
  QCOMPARE(PathUtils::checkDirectoryPath(QString()), PathUtils::PathStatus::Empty);
}

void TestPathUtils::testCheckDirectoryPath_missing() {
  QCOMPARE(PathUtils::checkDirectoryPath(QStringLiteral("/nonexistent/dir/abcxyz123")),
           PathUtils::PathStatus::Missing);
}

void TestPathUtils::testCheckDirectoryPath_existingDir() {
  QCOMPARE(PathUtils::checkDirectoryPath(m_tempDir.path()), PathUtils::PathStatus::OK);
}

void TestPathUtils::testCheckDirectoryPath_wrongTypeIsFile() {
  const QString path = m_tempDir.path() + QStringLiteral("/not-a-dir.txt");
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x");
  f.close();
  QCOMPARE(PathUtils::checkDirectoryPath(path), PathUtils::PathStatus::WrongType);
}

void TestPathUtils::testCheckFilePath_empty() {
  QCOMPARE(PathUtils::checkFilePath(QString()), PathUtils::PathStatus::Empty);
}

void TestPathUtils::testCheckFilePath_missing() {
  QCOMPARE(PathUtils::checkFilePath(QStringLiteral("/nonexistent/file/abcxyz123.png")),
           PathUtils::PathStatus::Missing);
}

void TestPathUtils::testCheckFilePath_existingFile() {
  const QString path = m_tempDir.path() + QStringLiteral("/artwork.png");
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("\x89PNG\r\n");
  f.close();
  QCOMPARE(PathUtils::checkFilePath(path), PathUtils::PathStatus::OK);
}

void TestPathUtils::testCheckFilePath_wrongTypeIsDir() {
  QCOMPARE(PathUtils::checkFilePath(m_tempDir.path()), PathUtils::PathStatus::WrongType);
}

void TestPathUtils::testPathStatusDescription_okAndEmptyAreBlank() {
  QCOMPARE(PathUtils::pathStatusDescription(PathUtils::PathStatus::OK), QString());
  QCOMPARE(PathUtils::pathStatusDescription(PathUtils::PathStatus::Empty), QString());
}

void TestPathUtils::testPathStatusDescription_nonOkProducesText() {
  QVERIFY(!PathUtils::pathStatusDescription(PathUtils::PathStatus::Missing).isEmpty());
  QVERIFY(!PathUtils::pathStatusDescription(PathUtils::PathStatus::NotExecutable).isEmpty());
  QVERIFY(!PathUtils::pathStatusDescription(PathUtils::PathStatus::NotReadable).isEmpty());
  QVERIFY(!PathUtils::pathStatusDescription(PathUtils::PathStatus::WrongType).isEmpty());
}

QTEST_MAIN(TestPathUtils)
#include "test_pathutils.moc"
