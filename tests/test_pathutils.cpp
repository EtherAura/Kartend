/**
 * @file test_pathutils.cpp
 * @brief Unit tests for PathUtils functions
 *
 * Tests path validation, expansion, and display utilities.
 */

#include "pathutils.h"
#include <QDir>
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
  QTest::newRow("ampersand") << "/path/with/Sonic & Knuckles.zip" << "ampersand"
                             << false;
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
    QVERIFY2(result.isError(),
             qPrintable(QString("Path with %1 should fail").arg(description)));
    QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
  } else {
    QVERIFY2(result.isOk(),
             qPrintable(QString("Path with %1 should be allowed").arg(description)));
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
  QVERIFY2(result.isError(), "Path with backslash should fail");
}

QTEST_MAIN(TestPathUtils)
#include "test_pathutils.moc"
