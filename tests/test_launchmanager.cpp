/**
 * @file test_launchmanager.cpp
 * @brief Unit tests for LaunchManager validation functions
 *
 * Tests the security validation functions for launcher paths and parameters.
 */

#include "launchmanager.h"
#include <QTemporaryFile>
#include <QTest>
#include <QDir>

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

private:
  QString m_tempExecutable;
  QString m_tempNonExecutable;
};

void TestLaunchManager::initTestCase() {
  // Create a temporary executable file for testing
  QTemporaryFile tempExec;
  tempExec.setAutoRemove(false);
  tempExec.setFileTemplate(QDir::tempPath() + "/test_launcher_XXXXXX");
  if (tempExec.open()) {
    tempExec.write("#!/bin/sh\nexit 0\n");
    tempExec.close();
    m_tempExecutable = tempExec.fileName();
    QFile::setPermissions(m_tempExecutable,
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  }

  // Create a temporary non-executable file for testing
  QTemporaryFile tempNonExec;
  tempNonExec.setAutoRemove(false);
  tempNonExec.setFileTemplate(QDir::tempPath() + "/test_nonexec_XXXXXX");
  if (tempNonExec.open()) {
    tempNonExec.write("not executable");
    tempNonExec.close();
    m_tempNonExecutable = tempNonExec.fileName();
    QFile::setPermissions(m_tempNonExecutable,
                          QFile::ReadOwner | QFile::WriteOwner);
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

  QTest::newRow("semicolon") << "/path/to;command" << "semicolon injection";
  QTest::newRow("pipe") << "/path/to|command" << "pipe injection";
  QTest::newRow("ampersand") << "/path/to&command" << "background injection";
  QTest::newRow("backtick") << "/path/to`command`" << "command substitution";
  QTest::newRow("dollar") << "/path/to$HOME" << "variable expansion";
  QTest::newRow("parens") << "/path/to$(command)" << "subshell";
  QTest::newRow("braces") << "/path/to{a,b}" << "brace expansion";
  QTest::newRow("brackets") << "/path/to[abc]" << "glob pattern";
  QTest::newRow("redirect-in") << "/path/to<input" << "input redirect";
  QTest::newRow("redirect-out") << "/path/to>output" << "output redirect";
  QTest::newRow("bang") << "/path/to!command" << "history expansion";
}

void TestLaunchManager::testValidatePathSecurity_shellMetacharacters() {
  QFETCH(QString, path);
  QFETCH(QString, description);

  auto result = LaunchManager::validatePathSecurity(path);
  QVERIFY2(result.isError(),
           qPrintable(QString("Path with %1 should fail").arg(description)));
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
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
  QString composedPath = "/path/to/caf\u00E9";  // é as single codepoint
  QString decomposedPath = "/path/to/cafe\u0301";  // e + combining acute
  
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
  QVERIFY2(result.isOk(),
           qPrintable(QString("Valid executable should pass: %1")
                          .arg(result.isError() ? result.error().message : "")));
}

void TestLaunchManager::testValidateLauncherPath_relativePath() {
  auto result = LaunchManager::validateLauncherPath("relative/path/to/launcher");
  QVERIFY2(result.isError(), "Relative path should fail validation");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
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
  QStringList result = LaunchManager::parseParameters("");
  QVERIFY(result.isEmpty());

  result = LaunchManager::parseParameters("   ");
  QVERIFY(result.isEmpty());
}

void TestLaunchManager::testParseParameters_singleArg() {
  QStringList result = LaunchManager::parseParameters("arg1");
  QCOMPARE(result.size(), 1);
  QCOMPARE(result[0], "arg1");
}

void TestLaunchManager::testParseParameters_multipleArgs() {
  QStringList result = LaunchManager::parseParameters("arg1 arg2 arg3");
  QCOMPARE(result.size(), 3);
  QCOMPARE(result[0], "arg1");
  QCOMPARE(result[1], "arg2");
  QCOMPARE(result[2], "arg3");
}

void TestLaunchManager::testParseParameters_quotedArgs() {
  QStringList result = LaunchManager::parseParameters("\"arg with spaces\" arg2");
  QCOMPARE(result.size(), 2);
  QCOMPARE(result[0], "arg with spaces");
  QCOMPARE(result[1], "arg2");

  result = LaunchManager::parseParameters("'single quoted' arg2");
  QCOMPARE(result.size(), 2);
  QCOMPARE(result[0], "single quoted");
  QCOMPARE(result[1], "arg2");
}

void TestLaunchManager::testParseParameters_mixedQuotes() {
  QStringList result = LaunchManager::parseParameters("\"double\" 'single' plain");
  QCOMPARE(result.size(), 3);
  QCOMPARE(result[0], "double");
  QCOMPARE(result[1], "single");
  QCOMPARE(result[2], "plain");
}

QTEST_MAIN(TestLaunchManager)
#include "test_launchmanager.moc"
