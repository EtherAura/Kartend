/**
 * @file test_launchmanager.cpp
 * @brief Unit tests for LaunchManager validation functions
 *
 * Tests the security validation functions for launcher paths and parameters.
 */

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "collection/launcherconfig.h"
#include "launchmanager.h"

#include "../../support/launchfakes.h"

#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>

#include <atomic>
#include <memory>

// Kartend-68wbk: a missing archive tool is a graceful QSKIP locally, but a hard
// QFAIL in CI (KARTEND_REQUIRE_ARCHIVE_TOOLS=1). CI installs the tools
// (Kartend-03lcs); this macro turns a silent ctest "pass" back into a failure if
// that ever regresses, so the archive-extraction coverage can't evaporate
// unnoticed. Both QFAIL and QSKIP return from the test, so this is drop-in for a
// bare QSKIP.
#define KARTEND_ARCHIVE_TOOL_SKIP(msg)                                                             \
  do {                                                                                             \
    if (!qEnvironmentVariableIsEmpty("KARTEND_REQUIRE_ARCHIVE_TOOLS"))                             \
      QFAIL("KARTEND_REQUIRE_ARCHIVE_TOOLS is set but " msg);                                      \
    QSKIP(msg);                                                                                    \
  } while (false)

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
  void testParseParameters_backslashEscapesSpecials();
  void testParseParameters_backslashBeforeOrdinaryCharPreserved();

  // buildLaunchCommand tests
  void testBuildLaunchCommand_nonRetroArch_usesLaunchParameters();
  void testBuildLaunchCommand_collectionSubstitutionDoesNotInjectArgs();
  void testBuildLaunchCommand_substitutesFilePlaceholder();
  void testBuildLaunchCommand_filePlaceholderKeepsSingleArgWithSpaces();
  void testBuildLaunchCommand_substitutesCorePlaceholderInPlainLauncher();
  void testBuildLaunchCommand_filePlaceholderIgnoresLongerTokens();
  void testBuildLaunchCommand_corePlaceholderIgnoresLongerTokens();
  void testBuildLaunchCommand_noPlaceholderStillAppendsFilePath();
  void testBuildLaunchCommand_retroArch_usesCorePath();
  void testBuildLaunchCommand_retroArch_includesLaunchParameters();
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
  void testPreview_detectsBareFilePlaceholderToken();
  void testPreview_substitutedFilePlaceholderProducesNoWarning();
  void testPreview_warnsCorePathIgnoredForNonLibretroLauncher();
  void testPreview_noCoreWarningForLibretroLauncher();
  void testPreview_unclosedQuoteParameterSurfacedAsWarning();

  // Archive extraction (extractArchiveToTemp) + isArchiveFile coverage.
  void testIsArchiveFile_recognizesKnownExtensions();
  void testExtractArchive_rejectsUnsafeArchivePath();
  void testExtractArchive_extractsTargetFile();
  void testExtractArchive_missingTargetExtensionCleansUpExtractionDir();
  void testExtractArchive_sameBaseNameDoesNotServeWrongContent();

  // launchItem extracted-dir cleanup when the launcher fails to start (Kartend-dyu1k).
  void testLaunchItem_failedStartRemovesExtractedDir();

  // Tracked-path variant: a FailedToStart delivered SYNCHRONOUSLY inside the
  // spawn (the Windows shape) must still reclaim the extraction dir. The old
  // post-spawn reclaim wiring in finishLaunch ran after the failure had
  // already cleared m_trackedChild, so the hook was never installed.
  void testLaunchItem_trackedSyncFailedStartReclaimsExtractedDir();

  // The double-launch debounce map must not grow one entry per ever-launched
  // path: recordLaunch prunes entries whose 500ms guard window has lapsed.
  void testRecordLaunch_prunesLapsedDebounceEntries();

  // launchItem must snapshot the collection by value before the launcher
  // chooser runs: the chooser's modal loop still services timers and queued
  // slots, and one of them mutating m_collections used to dangle the
  // reference the post-chooser reads went through.
  void testLaunchItem_chooserMutatingCollectionsLaunchesSnapshot();

  // A fire-and-forget child that survives its early-failure window must be
  // reparented away from the manager: a still-owned QProcess is destroyed by
  // ~LaunchManager, and ~QProcess kills a running child — closing the
  // frontend used to take the user's launched program down with it.
  void testDetachedChildReparentedAfterWatchWindow();

  // Kartend-ijglg: decompressed-size bound on launch-time extraction.
  void testExtractArchive_rejectsArchiveLargerThanCap();
  void testExtractArchive_enforcesDecompressedSizeCap();
  void testExtractArchive_sizeCapKillsRunawayExtractor();

  // Kartend-mkcak: cancellable worker-thread extraction.
  void testExtractArchive_preSetCancelReturnsCancelled();
  void testLaunchItem_cancelExtractionAbortsPendingLaunch();
  void testLaunchManagerDtor_drainsRunningExtraction();

private:
  // Creates an executable file whose shebang points at a nonexistent
  // interpreter: it passes validateLauncherPath (exists + executable) but
  // QProcess::startDetached fails to exec it, so launchItem reaches its
  // extracted-dir cleanup. Returns the absolute path (empty on failure); the
  // owning temp dir is tracked in m_fixtureDirs.
  QString makeFailingLauncher();

  // Builds a .zip named <baseName>.zip holding the given (name -> bytes) files
  // in a fresh temp dir. Returns the archive path, or an empty string when no
  // archive-creation tool (zip/bsdtar/7z) is available — the QSKIP at the call
  // site keeps the suite green on minimal CI images.
  QString makeZipFixture(const QString &baseName, const QList<QPair<QString, QByteArray>> &entries);
  // Kartend-dhhh6: a real but content-less <baseName>.zip in a fresh temp dir,
  // written with QFile (NO QProcess fork, unlike makeZipFixture). For TSan runs
  // where the fake-extractor seam ignores the archive bytes, so we still need a
  // valid on-disk path for launchItem but must not fork while worker threads
  // are live. Returns the path (empty on failure); temp dir tracked in
  // m_fixtureDirs.
  QString makeArchiveStub(const QString &baseName);
  // True when extractArchiveToTemp will find one of its extractors on PATH.
  static bool extractorAvailable();
  // Creates a directory holding a fake `7z` shell script that writes
  // `kibToWrite` KiB of zeros into its working directory (the extraction
  // dir) and then sleeps `sleepSecs`. Prepending the directory to PATH makes
  // extractArchiveToTemp pick it as its extractor, giving the watchdog tests
  // a deterministic slow/runaway child. Returns the directory path (empty on
  // failure); the owning temp dir is tracked in m_fixtureDirs. POSIX-only.
  QString makeFakeExtractorDir(int kibToWrite, int sleepSecs);
  // Absolute path of the per-archive extraction dir extractArchiveToTemp uses
  // for an archive whose completeBaseName is <baseName>.
  static QString extractionDirFor(const QString &baseName);

  QString m_tempExecutable;
  QString m_tempNonExecutable;
  // Owns the temp dirs holding zip fixtures so the archives outlive the call
  // that creates them; torn down in cleanupTestCase.
  QList<QTemporaryDir *> m_fixtureDirs;
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
  // extractArchiveToTemp writes under the real temp dir (test mode doesn't
  // redirect TempLocation); remove anything the extraction slots left behind.
  QDir(extractionDirFor("kartend_extract_ok")).removeRecursively();
  QDir(extractionDirFor("kartend_extract_miss")).removeRecursively();
  QDir(extractionDirFor("kartend_collide")).removeRecursively();
  QDir(extractionDirFor("kartend_cap_pre")).removeRecursively();
  QDir(extractionDirFor("kartend_cap_post")).removeRecursively();
  QDir(extractionDirFor("kartend_cap_runaway")).removeRecursively();
  QDir(extractionDirFor("kartend_cancel_pre")).removeRecursively();
  QDir(extractionDirFor("kartend_cancel_launch")).removeRecursively();
  QDir(extractionDirFor("kartend_dtor")).removeRecursively();
  QDir(extractionDirFor("kartend_tracked_sync_fail")).removeRecursively();
  qDeleteAll(m_fixtureDirs);
  m_fixtureDirs.clear();
}

bool TestLaunchManager::extractorAvailable() {
  static const QStringList kExtractors = {QStringLiteral("7z"), QStringLiteral("unzip"),
                                          QStringLiteral("bsdtar")};
  for (const QString &tool : kExtractors) {
    if (!QStandardPaths::findExecutable(tool).isEmpty()) {
      return true;
    }
  }
  return false;
}

QString TestLaunchManager::extractionDirFor(const QString &baseName) {
  return QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
         QStringLiteral("/kartend_extract/") + baseName;
}

QString TestLaunchManager::makeZipFixture(const QString &baseName,
                                          const QList<QPair<QString, QByteArray>> &entries) {
  auto *dir = new QTemporaryDir();
  if (!dir->isValid()) {
    delete dir;
    return {};
  }
  m_fixtureDirs.append(dir);

  QStringList entryNames;
  for (const auto &entry : entries) {
    const QString path = dir->filePath(entry.first);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return {};
    }
    f.write(entry.second);
    f.close();
    entryNames << entry.first;
  }

  const QString archivePath = dir->filePath(baseName + QStringLiteral(".zip"));

  // Prefer `zip`; fall back to bsdtar (libarchive infers zip from the
  // extension) or 7z. The result is a plain .zip so any of
  // extractArchiveToTemp's extractors (7z/unzip/bsdtar) can open it.
  auto tryCreate = [&](const char *toolName, const QStringList &args, bool inDir) -> bool {
    const QString tool = QStandardPaths::findExecutable(QLatin1String(toolName));
    if (tool.isEmpty()) {
      return false;
    }
    QProcess proc;
    if (inDir) {
      proc.setWorkingDirectory(dir->path());
    }
    proc.start(tool, args);
    return proc.waitForFinished(15000) && proc.exitCode() == 0 && QFileInfo::exists(archivePath);
  };

  const QStringList zipArgs = QStringList{QStringLiteral("-q"), archivePath} + entryNames;
  const QStringList bsdtarArgs = QStringList{QStringLiteral("-a"), QStringLiteral("-cf"),
                                             archivePath, QStringLiteral("-C"), dir->path()} +
                                 entryNames;
  const QStringList sevenZipArgs =
      QStringList{QStringLiteral("a"), QStringLiteral("-tzip"), archivePath} + entryNames;

  if (tryCreate("zip", zipArgs, true) || tryCreate("bsdtar", bsdtarArgs, false) ||
      tryCreate("7z", sevenZipArgs, true)) {
    return archivePath;
  }
  return {};
}

QString TestLaunchManager::makeArchiveStub(const QString &baseName) {
  auto *dir = new QTemporaryDir();
  if (!dir->isValid()) {
    delete dir;
    return {};
  }
  m_fixtureDirs.append(dir);
  const QString archivePath = dir->filePath(baseName + QStringLiteral(".zip"));
  QFile f(archivePath);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return {};
  }
  // Local-file-header magic so it reads as a plausible archive path; the fake
  // extractor seam never actually opens it.
  f.write(QByteArrayLiteral("PK\x03\x04 kartend-dhhh6 stub"));
  f.close();
  return archivePath;
}

QString TestLaunchManager::makeFailingLauncher() {
  auto *dir = new QTemporaryDir();
  if (!dir->isValid()) {
    delete dir;
    return {};
  }
  m_fixtureDirs.append(dir);

  const QString path = dir->filePath(QStringLiteral("broken_launcher"));
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return {};
  }
  f.write("#!/kartend-test/no/such/interpreter-xyz\n");
  f.close();
  if (!QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
    return {};
  }
  return path;
}

QString TestLaunchManager::makeFakeExtractorDir(int kibToWrite, int sleepSecs) {
  auto *dir = new QTemporaryDir();
  if (!dir->isValid()) {
    delete dir;
    return {};
  }
  m_fixtureDirs.append(dir);

  // The production extractor prefers bsdtar, then 7z — fake whichever exist
  // so the runaway behaviour is exercised regardless of preference order.
  // The pre-extraction safety scan lists the archive with the same binaries
  // (bsdtar -tf / 7z l), so list-mode invocations delegate to the REAL tool
  // (resolved now, before the caller prepends this dir to PATH); only the
  // extract mode misbehaves.
  const QString realBsdtar = QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
  const QString real7z = QStandardPaths::findExecutable(QStringLiteral("7z"));

  const auto writeFake = [&](const QString &name, const QByteArray &listDelegate) -> bool {
    const QString path = dir->filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return false;
    }
    // extractArchiveToTemp runs the extractor with CWD == the extraction dir,
    // so payload.bin lands exactly where the size watchdog measures.
    QByteArray script = "#!/bin/sh\n";
    script += listDelegate;
    if (kibToWrite > 0) {
      script += "dd if=/dev/zero of=payload.bin bs=1024 count=" + QByteArray::number(kibToWrite) +
                " 2>/dev/null\n";
    }
    script += "sleep " + QByteArray::number(sleepSecs) + "\n";
    f.write(script);
    f.close();
    return QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  };

  if (!realBsdtar.isEmpty()) {
    const QByteArray delegate =
        "case \"$1\" in -t*) exec \"" + realBsdtar.toUtf8() + "\" \"$@\" ;; esac\n";
    if (!writeFake(QStringLiteral("bsdtar"), delegate)) {
      return {};
    }
  }
  QByteArray sevenDelegate;
  if (!real7z.isEmpty()) {
    sevenDelegate = "case \"$1\" in l) exec \"" + real7z.toUtf8() + "\" \"$@\" ;; esac\n";
  }
  if (!writeFake(QStringLiteral("7z"), sevenDelegate)) {
    return {};
  }
  return dir->path();
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
#ifdef Q_OS_WIN
  // Backslash is the native path separator on Windows, so it's accepted (the
  // traversal check splits on both separators to stay safe there).
  QVERIFY2(result.isOk(), "Path with backslash should be allowed on Windows");
#else
  QVERIFY2(result.isError(), "Path with backslash should fail validation on non-Windows");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidFilePath);
#endif
}

void TestLaunchManager::testValidatePathSecurity_unicodeNormalization() {
  // Both composed (NFC) and decomposed (NFD) Unicode paths must be accepted.
  // The character 'é' can be represented as U+00E9 (composed) or U+0065 U+0301 (decomposed)
  // NFC normalization converts decomposed to composed form
  QString composedPath = "/path/to/caf\u00E9";    // é as single codepoint
  QString decomposedPath = "/path/to/cafe\u0301"; // e + combining acute

  auto composedResult = LaunchManager::validatePathSecurity(composedPath);
  QVERIFY2(composedResult.isOk(), "Composed Unicode path should pass");

  auto decomposedResult = LaunchManager::validatePathSecurity(decomposedPath);
  QVERIFY2(decomposedResult.isOk(), "Decomposed (NFD) Unicode path should now be accepted");
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

void TestLaunchManager::testParseParameters_backslashEscapesSpecials() {
  // A backslash makes the next special character literal: quotes no longer
  // toggle quoting, an escaped space joins the param, and \\ collapses to one
  // backslash (Kartend-xi2mj).
  auto result = LaunchManager::parseParameters("say \\\"hi\\\"");
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 2);
  QCOMPARE(result.value()[0], "say");
  QCOMPARE(result.value()[1], "\"hi\"");

  // Escaped single quote, literal, outside quoting.
  result = LaunchManager::parseParameters("it\\'s");
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 1);
  QCOMPARE(result.value()[0], "it's");

  // Escaped space keeps one argument together without needing quotes.
  result = LaunchManager::parseParameters("two\\ words");
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 1);
  QCOMPARE(result.value()[0], "two words");

  // Escaped backslash collapses to a single literal backslash.
  result = LaunchManager::parseParameters("a\\\\b");
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 1);
  QCOMPARE(result.value()[0], "a\\b");

  // An escaped quote inside a quoted run stays literal and does not close it.
  result = LaunchManager::parseParameters("\"a\\\"b\"");
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 1);
  QCOMPARE(result.value()[0], "a\"b");
}

void TestLaunchManager::testParseParameters_backslashBeforeOrdinaryCharPreserved() {
  // A backslash that doesn't precede a special char is kept literal, so existing
  // params with stray backslashes (paths, regex) are unchanged (Kartend-xi2mj).
  auto result = LaunchManager::parseParameters("C:\\path\\to");
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 1);
  QCOMPARE(result.value()[0], "C:\\path\\to");

  // A trailing lone backslash is also kept literal.
  result = LaunchManager::parseParameters("end\\");
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 1);
  QCOMPARE(result.value()[0], "end\\");
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

void TestLaunchManager::testBuildLaunchCommand_collectionSubstitutionDoesNotInjectArgs() {
  // Kartend-nv9iw: a %collection% value containing spaces / a leading-dash
  // token (a collection name can arrive from an imported .kart manifest) must
  // land as exactly ONE argument, not split into extra argv entries that would
  // inject attacker-chosen flags into the launcher. The name has no /, \, ..,
  // or . so it passes validateCollectionNameForSubstitution and actually
  // reaches the parameter expansion.
  LauncherConfig launcher;
  launcher.launcherPath = "mpv";
  launcher.launchParameters = "--title %collection%";

  const QString hostileName = "Live --fullscreen Sets";
  auto result = LaunchManager::buildLaunchCommand(launcher, hostileName, "/tmp/media/file.bin");
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().arguments,
           (QStringList{"--title", "Live --fullscreen Sets", "/tmp/media/file.bin"}));
}

void TestLaunchManager::testBuildLaunchCommand_substitutesFilePlaceholder() {
  // Kartend-51d3e: a probe-seeded ffmpeg template carries the media path as a
  // %1 placeholder. buildLaunchCommand must substitute the real path INTO the
  // placeholder position (not append it after a literal %1 token). %f is a
  // case-insensitive alias for the same placeholder.
  const QString filePath = "/tmp/clip.mp4";

  LauncherConfig ffmpeg{"ffmpeg", "ffmpeg", "", "-autoexit -nodisp \"%1\""};
  auto ffResult = LaunchManager::buildLaunchCommand(ffmpeg, "Audio", filePath);
  QVERIFY2(ffResult.isOk(), qPrintable(ffResult.isError() ? ffResult.error().message : QString()));
  // The media path lands where %1 was; no literal %1 token and no extra append.
  QCOMPARE(ffResult.value().arguments, (QStringList{"-autoexit", "-nodisp", filePath}));

  LauncherConfig fAlias{"mpv", "mpv", "", "--play %F"};
  auto fResult = LaunchManager::buildLaunchCommand(fAlias, "Audio", filePath);
  QVERIFY2(fResult.isOk(), qPrintable(fResult.isError() ? fResult.error().message : QString()));
  QCOMPARE(fResult.value().arguments, (QStringList{"--play", filePath}));
}

void TestLaunchManager::testBuildLaunchCommand_filePlaceholderKeepsSingleArgWithSpaces() {
  // Substitution happens per already-split token, so a quoted "%1" stays a
  // single argv entry even when the substituted path contains spaces — no new
  // argument boundary is introduced (mirrors the %collection% guarantee).
  const QString filePath = "/tmp/My Concert Set.mp4";
  LauncherConfig launcher{"mpv", "mpv", "", "--fullscreen \"%1\""};
  auto result = LaunchManager::buildLaunchCommand(launcher, "Video", filePath);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().arguments, (QStringList{"--fullscreen", filePath}));
}

void TestLaunchManager::testBuildLaunchCommand_substitutesCorePlaceholderInPlainLauncher() {
  // A libretro frontend whose basename is NOT "retroarch" takes the plain
  // branch, so its %core placeholder must be expanded there (with %1 for the
  // media path). Both placeholders resolve; nothing is appended after %1.
  const QString filePath = "/tmp/game.zip";
  LauncherConfig launcher{"libretro-fe", "/usr/bin/libretro-fe", "/cores/snes9x.so",
                          "-L %core \"%1\""};
  auto result = LaunchManager::buildLaunchCommand(launcher, "Retro", filePath);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().arguments, (QStringList{"-L", "/cores/snes9x.so", filePath}));
}

void TestLaunchManager::testBuildLaunchCommand_filePlaceholderIgnoresLongerTokens() {
  // %f is only the standalone token, never the prefix of a longer one:
  // substring replacement used to corrupt `%file%` into "<path>ile%" AND set
  // sawFilePlaceholder, suppressing the append-media-path fallback. The
  // unknown token must pass through verbatim (previewLaunchCommand flags it
  // as unresolved via the same \b boundary) and the media path still lands
  // at the end. `%10` likewise stays literal — its digit run is not the %1
  // token, so no partial "<path>0" substitution.
  const QString filePath = "/tmp/clip.mp4";
  LauncherConfig launcher{"mpv", "mpv", "", "--playlist=%file% --seek=%10"};
  auto result = LaunchManager::buildLaunchCommand(launcher, "Video", filePath);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().arguments, (QStringList{"--playlist=%file%", "--seek=%10", filePath}));

  // A genuine standalone %f still substitutes — lowercase here; the
  // uppercase %F alias is covered by
  // testBuildLaunchCommand_substitutesFilePlaceholder.
  LauncherConfig lower{"mpv", "mpv", "", "--input %f"};
  auto lowerResult = LaunchManager::buildLaunchCommand(lower, "Video", filePath);
  QVERIFY2(lowerResult.isOk(),
           qPrintable(lowerResult.isError() ? lowerResult.error().message : QString()));
  QCOMPARE(lowerResult.value().arguments, (QStringList{"--input", filePath}));
}

void TestLaunchManager::testBuildLaunchCommand_corePlaceholderIgnoresLongerTokens() {
  // Same boundary rule for %core: a longer %coreopts%-style token must not
  // be corrupted into "<core path>opts%". The standalone %core in the same
  // template still substitutes, and with no %1/%f present the media path is
  // appended at the end as usual.
  const QString filePath = "/tmp/episode.mkv";
  LauncherConfig launcher{"libretro-fe", "/usr/bin/libretro-fe", "/cores/engine.so",
                          "-L %core --extra %coreopts%"};
  auto result = LaunchManager::buildLaunchCommand(launcher, "Video", filePath);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().arguments,
           (QStringList{"-L", "/cores/engine.so", "--extra", "%coreopts%", filePath}));
}

void TestLaunchManager::testBuildLaunchCommand_noPlaceholderStillAppendsFilePath() {
  // Regression guard: a template WITHOUT %1/%f keeps the historical
  // append-filePath-at-end behavior unchanged.
  CollectionConfig config;
  config.name = "TestCollection";
  config.launcher.launcherPath = "echo";
  config.launcher.launchParameters = "--fullscreen --scale 2";
  const QString filePath = "/tmp/testfile.bin";
  auto result = LaunchManager::buildLaunchCommand(config, filePath);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().arguments, (QStringList{"--fullscreen", "--scale", "2", filePath}));
}

void TestLaunchManager::testBuildLaunchCommand_retroArch_usesCorePath() {
  CollectionConfig config;
  config.name = "TestCollection";
  config.launcher.launcherPath = "retroarch";
  config.launcher.corePath = "/tmp/core.so";
  // No launch parameters configured: the libretro branch still emits exactly
  // the `-L <core> <file>` triple.
  config.launcher.launchParameters = "";

  const QString filePath = "/tmp/testfile.bin";
  auto result = LaunchManager::buildLaunchCommand(config, filePath);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().program, QString("retroarch"));
  QCOMPARE(result.value().arguments, (QStringList{"-L", "/tmp/core.so", filePath}));
}

void TestLaunchManager::testBuildLaunchCommand_retroArch_includesLaunchParameters() {
  // Kartend-q21fy: the libretro branch used to silently DROP configured launch
  // parameters and emit only `-L <core> <file>`. They must now be parsed via
  // the same parseParameters + %collection% expansion the plain branch uses
  // and inserted AHEAD of the `-L <core> <file>` triple (which keeps the
  // ordering RetroArch expects intact).
  CollectionConfig config;
  config.name = "TestCollection";
  config.launcher.launcherPath = "retroarch";
  config.launcher.corePath = "/tmp/core.so";
  config.launcher.launchParameters = "--fullscreen --config /tmp/retro.cfg";

  const QString filePath = "/tmp/testfile.bin";
  auto result = LaunchManager::buildLaunchCommand(config, filePath);
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  QCOMPARE(result.value().program, QString("retroarch"));
  QCOMPARE(result.value().arguments, (QStringList{"--fullscreen", "--config", "/tmp/retro.cfg",
                                                  "-L", "/tmp/core.so", filePath}));
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

void TestLaunchManager::testPreview_detectsBareFilePlaceholderToken() {
  // Kartend-51d3e: the preview placeholder warning must also catch bare
  // %1-style tokens that survive substitution. A %core in a non-libretro
  // launcher is dropped (its expansion is empty there) only when no core is
  // set; with no core configured and %2 present, the %2 token is unresolved
  // and must surface so the user notices.
  CollectionConfig collection;
  collection.name = "Audio";
  LauncherConfig launcher;
  launcher.launcherPath = m_tempExecutable;
  launcher.launchParameters = "--track %2 --quiet";
  const auto preview = LaunchManager::previewLaunchCommand(collection, launcher, m_tempExecutable);
  QVERIFY(preview.buildOk);
  bool sawPlaceholderWarning = false;
  for (const QString &w : preview.warnings) {
    if (w.contains("placeholder")) {
      sawPlaceholderWarning = true;
      break;
    }
  }
  QVERIFY2(sawPlaceholderWarning,
           "bare %2-style token must be flagged as an unresolved placeholder");
}

void TestLaunchManager::testPreview_substitutedFilePlaceholderProducesNoWarning() {
  // The opposite of the above: once %1 is substituted with the real path, no
  // placeholder warning should fire — the token is fully resolved.
  CollectionConfig collection;
  collection.name = "Video";
  LauncherConfig launcher;
  launcher.launcherPath = m_tempExecutable;
  launcher.launchParameters = "--fullscreen \"%1\"";
  const auto preview = LaunchManager::previewLaunchCommand(collection, launcher, m_tempExecutable);
  QVERIFY(preview.buildOk);
  for (const QString &w : preview.warnings) {
    QVERIFY2(!w.contains("placeholder"),
             qPrintable(QString("Unexpected placeholder warning: %1").arg(w)));
  }
}

void TestLaunchManager::testPreview_warnsCorePathIgnoredForNonLibretroLauncher() {
  // Kartend-pgfks: a non-empty corePath on a launcher that isn't classified
  // libretro is silently dropped at launch. The preview must warn that the
  // core path will be ignored so the user can see the stray value.
  CollectionConfig collection;
  collection.name = "Video";
  LauncherConfig launcher;
  launcher.launcherPath = m_tempExecutable; // basename has no "retroarch"
  launcher.corePath = "/cores/leftover.so";
  const auto preview = LaunchManager::previewLaunchCommand(collection, launcher, m_tempExecutable);
  QVERIFY(preview.buildOk);
  bool sawCoreIgnored = false;
  for (const QString &w : preview.warnings) {
    if (w.contains("Core path will be ignored")) {
      sawCoreIgnored = true;
      break;
    }
  }
  QVERIFY2(sawCoreIgnored,
           "a core path on a non-libretro launcher must surface an 'ignored' warning");
}

void TestLaunchManager::testPreview_noCoreWarningForLibretroLauncher() {
  // A libretro-classified launcher consumes its core path, so no
  // "core path will be ignored" warning should be emitted. (A bare command
  // name keeps buildOk true even though it won't resolve on PATH.)
  CollectionConfig collection;
  collection.name = "Retro";
  LauncherConfig launcher;
  launcher.launcherPath = "retroarch";
  launcher.corePath = "/cores/snes9x.so";
  const auto preview = LaunchManager::previewLaunchCommand(collection, launcher, m_tempExecutable);
  for (const QString &w : preview.warnings) {
    QVERIFY2(!w.contains("Core path will be ignored"),
             qPrintable(QString("Unexpected core-ignored warning: %1").arg(w)));
  }
}

// ---------------------------------------------------------------------------
// Archive extraction (extractArchiveToTemp) + isArchiveFile
// ---------------------------------------------------------------------------

void TestLaunchManager::testIsArchiveFile_recognizesKnownExtensions() {
  QVERIFY(LaunchManager::isArchiveFile("/media/Backups/disc.zip"));
  QVERIFY(LaunchManager::isArchiveFile("/media/Backups/DISC.ZIP")); // case-insensitive
  QVERIFY(LaunchManager::isArchiveFile("/media/Backups/disc.7z"));
  QVERIFY(LaunchManager::isArchiveFile("/media/Backups/disc.tar"));
  QVERIFY(LaunchManager::isArchiveFile("/media/Backups/disc.tar.gz"));
  QVERIFY(!LaunchManager::isArchiveFile("/media/Movies/feature.iso"));
  QVERIFY(!LaunchManager::isArchiveFile("/media/Notes/readme.txt"));
  QVERIFY(!LaunchManager::isArchiveFile("/media/Notes/noextension"));
}

void TestLaunchManager::testExtractArchive_rejectsUnsafeArchivePath() {
  // The archive path goes through the same security gate as launch media, so a
  // shell-metacharacter path must be rejected before any extractor runs (this
  // branch needs no tool).
  auto result = LaunchManager::extractArchiveToTemp("/tmp/inject;rm -rf ~.zip", ".iso");
  QVERIFY2(result.isError(), "an archive path failing security validation must be rejected");
}

void TestLaunchManager::testExtractArchive_extractsTargetFile() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP(
      "Kartend-dhhh6: single-threaded real-extractor test — extractArchiveToTemp forks an "
      "external tool synchronously on the test thread, so there is no cross-thread state for a "
      "non-forking seam to cover and the fork itself is the assertion. The launchItem worker-path "
      "slots (failedStart/cancel/dtor) carry the seam-covered cross-thread coverage under TSan.");
#endif
  if (!extractorAvailable()) {
    KARTEND_ARCHIVE_TOOL_SKIP("No archive extractor (7z/unzip/bsdtar) on PATH");
  }
  const QString base = QStringLiteral("kartend_extract_ok");
  QDir(extractionDirFor(base)).removeRecursively(); // no stale cache from a prior run
  const QList<QPair<QString, QByteArray>> entries = {
      {QStringLiteral("disc.iso"), QByteArrayLiteral("ISO")},
      {QStringLiteral("manual.txt"), QByteArrayLiteral("x")}};
  const QString zip = makeZipFixture(base, entries);
  if (zip.isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("No archive-creation tool (zip/bsdtar/7z) on PATH");
  }

  auto result = LaunchManager::extractArchiveToTemp(zip, ".iso");
  QVERIFY2(result.isOk(), qPrintable(result.isError() ? result.error().message : QString()));
  const QString extracted = result.value();
  QVERIFY2(extracted.endsWith(QLatin1String("disc.iso")),
           qPrintable(QStringLiteral("expected the .iso, got: %1").arg(extracted)));
  QVERIFY(QFileInfo::exists(extracted));
  // The returned file must live under the per-archive extraction tree.
  QVERIFY(extracted.contains(QLatin1String("/kartend_extract/")));
}

void TestLaunchManager::testExtractArchive_missingTargetExtensionCleansUpExtractionDir() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP(
      "Kartend-dhhh6: single-threaded real-extractor test — extractArchiveToTemp forks an "
      "external tool synchronously on the test thread, so there is no cross-thread state for a "
      "non-forking seam to cover and the fork itself is the assertion. The launchItem worker-path "
      "slots (failedStart/cancel/dtor) carry the seam-covered cross-thread coverage under TSan.");
#endif
  // The leak-relevant path: when extraction yields no file with the requested
  // extension, extractArchiveToTemp must report an error AND remove the
  // per-archive extraction dir it created (the qScopeGuard), so /tmp doesn't
  // accumulate orphaned archive contents.
  if (!extractorAvailable()) {
    KARTEND_ARCHIVE_TOOL_SKIP("No archive extractor (7z/unzip/bsdtar) on PATH");
  }
  const QString base = QStringLiteral("kartend_extract_miss");
  const QString extractionDir = extractionDirFor(base);
  QDir(extractionDir).removeRecursively();
  const QList<QPair<QString, QByteArray>> entries = {
      {QStringLiteral("readme.txt"), QByteArrayLiteral("no disc image here")}};
  const QString zip = makeZipFixture(base, entries);
  if (zip.isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("No archive-creation tool (zip/bsdtar/7z) on PATH");
  }

  auto result = LaunchManager::extractArchiveToTemp(zip, ".iso");
  QVERIFY2(result.isError(), "extraction must fail when no target-extension file is present");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::FileNotFound);
  QVERIFY2(!QDir(extractionDir).exists(),
           "the per-archive extraction dir must be removed when no target file is found");
}

void TestLaunchManager::testExtractArchive_sameBaseNameDoesNotServeWrongContent() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP(
      "Kartend-dhhh6: single-threaded real-extractor test — extractArchiveToTemp forks an "
      "external tool synchronously on the test thread, so there is no cross-thread state for a "
      "non-forking seam to cover and the fork itself is the assertion. The launchItem worker-path "
      "slots (failedStart/cancel/dtor) carry the seam-covered cross-thread coverage under TSan.");
#endif
  // Regression for Kartend-nrykk: two distinct archives sharing a base name
  // (both "kartend_collide.zip", in different temp dirs) map to the same
  // per-archive cache dir. A cache hit must NOT serve the first archive's
  // contents for the second — the source marker forces a re-extract.
  if (!extractorAvailable()) {
    KARTEND_ARCHIVE_TOOL_SKIP("No archive extractor (7z/unzip/bsdtar) on PATH");
  }
  const QString base = QStringLiteral("kartend_collide");
  QDir(extractionDirFor(base)).removeRecursively(); // no stale cache from a prior run
  const QList<QPair<QString, QByteArray>> entriesA = {
      {QStringLiteral("disc.iso"), QByteArrayLiteral("AAA")}};
  const QList<QPair<QString, QByteArray>> entriesB = {
      {QStringLiteral("disc.iso"), QByteArrayLiteral("BBB")}};
  const QString zipA = makeZipFixture(base, entriesA);
  const QString zipB = makeZipFixture(base, entriesB);
  if (zipA.isEmpty() || zipB.isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("No archive-creation tool (zip/bsdtar/7z) on PATH");
  }
  QVERIFY2(zipA != zipB, "fixtures must be distinct archive files that share a base name");

  const auto readAll = [](const QString &path) -> QByteArray {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
  };

  auto rA = LaunchManager::extractArchiveToTemp(zipA, ".iso");
  QVERIFY2(rA.isOk(), qPrintable(rA.isError() ? rA.error().message : QString()));
  QCOMPARE(readAll(rA.value()), QByteArrayLiteral("AAA"));

  // B shares the cache dir; it must re-extract its own content, not serve AAA.
  auto rB = LaunchManager::extractArchiveToTemp(zipB, ".iso");
  QVERIFY2(rB.isOk(), qPrintable(rB.isError() ? rB.error().message : QString()));
  QCOMPARE(readAll(rB.value()), QByteArrayLiteral("BBB"));

  // And A again must re-extract AAA, not keep serving B's BBB.
  auto rA2 = LaunchManager::extractArchiveToTemp(zipA, ".iso");
  QVERIFY2(rA2.isOk(), qPrintable(rA2.isError() ? rA2.error().message : QString()));
  QCOMPARE(readAll(rA2.value()), QByteArrayLiteral("AAA"));
}

void TestLaunchManager::testLaunchItem_failedStartRemovesExtractedDir() {
#ifdef Q_OS_WIN
  QSKIP("The shebang-to-nonexistent-interpreter failing-launcher trick is POSIX-specific");
#else
  // End-to-end: an archive item whose launcher fails to start must not leave
  // its extracted contents behind in /tmp. launchItem extracts the archive,
  // builds + validates the (broken) launcher, then the spawn fails and the
  // FailedToStart handler removes the extraction dir. Error dialogs route
  // through ErrorPresentation (Kartend-dyu1k); the default override just logs,
  // so this headless run doesn't hang on a modal.
  const QString launcher = makeFailingLauncher();
  QVERIFY2(!launcher.isEmpty(), "could not create the failing-launcher fixture");
  QVERIFY2(LaunchManager::validateLauncherPath(launcher).isOk(),
           "the fixture launcher must pass validation so the only failure is the spawn");

  const QString base = QStringLiteral("kartend_launch_fail");
  const QString extractionDir = extractionDirFor(base);
  QDir(extractionDir).removeRecursively();

  QList<CollectionConfig> collections;
  CollectionConfig collection;
  collection.name = QStringLiteral("Archive Collection");
  collection.archive.extractArchives = true;
  collection.archive.extractedExtension = QStringLiteral(".iso");
  collection.launcher.launcherPath = launcher;
  collections.append(collection);

  LaunchManager manager;
  LaunchManagerSetup setup;
  setup.collections = &collections;
  manager.setupReferences(setup);

  QString zip;
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  // Kartend-dhhh6: under TSan, forking the extractor (and then the launcher)
  // aborts libtsan. Drive the SAME extract-succeeds → launch-fails → cleanup
  // path through the non-forking seams: a fake extractor that creates the
  // extraction dir + extracted file with no fork, then a fake spawner that
  // emits errorOccurred(FailedToStart). The FailedToStart cleanup that removes
  // the extraction dir — the behaviour under test — runs unchanged, now under
  // ThreadSanitizer.
  zip = makeArchiveStub(base);
  QVERIFY2(!zip.isEmpty(), "could not create the archive stub");
  manager.setArchiveExtractorForTesting(KartendTest::fakeSleepyExtractor(
      extractionDir, QStringLiteral("disc.iso"), /*maxSleepMs=*/300));
  manager.setLauncherSpawnerForTesting(KartendTest::fakeFailingLauncherSpawner());
#else
  if (!extractorAvailable()) {
    KARTEND_ARCHIVE_TOOL_SKIP("No archive extractor (7z/unzip/bsdtar) on PATH");
  }
  const QList<QPair<QString, QByteArray>> entries = {
      {QStringLiteral("disc.iso"), QByteArrayLiteral("ISO")}};
  zip = makeZipFixture(base, entries);
  if (zip.isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("No archive-creation tool (zip/bsdtar/7z) on PATH");
  }
#endif

  // Runtime detection stays off (no ctx/settings wired) -> the detached path,
  // whose failed spawn is the branch that cleans up the extraction.
  // Kartend-mkcak: extraction runs on a worker thread, so launchItem returns
  // immediately with it still in flight; the failure + cleanup land in the
  // completion callback on the GUI thread.
  QSignalSpy startedSpy(&manager, &LaunchManager::extractionStarted);
  QSignalSpy finishedSpy(&manager, &LaunchManager::extractionFinished);
  manager.launchItem(zip, 0);

  // The async contract: the watcher callback can only run once the event
  // loop spins, so right after launchItem returns the extraction must still
  // be flagged in-flight and the busy signal must have fired.
  QCOMPARE(startedSpy.count(), 1);
  QVERIFY2(manager.isExtractionRunning(),
           "launchItem must return while the extraction is still running on the worker");

  QVERIFY2(finishedSpy.count() == 1 || finishedSpy.wait(15000),
           "extractionFinished must fire once the worker completes");
  QVERIFY(!manager.isExtractionRunning());
  QTRY_VERIFY2(!QDir(extractionDir).exists(),
               "the extracted archive dir must be removed when the launcher fails to start");
#endif
}

void TestLaunchManager::testLaunchItem_trackedSyncFailedStartReclaimsExtractedDir() {
  // Windows delivers QProcess signals synchronously inside start()
  // (CreateProcess fails inline) — model that with a spawner that emits
  // errorOccurred(FailedToStart) BEFORE returning. On the tracked path the
  // synchronous failure runs launchTracked's cleanup, which clears
  // m_trackedChild before launchTracked even returns; the old reclaim hook
  // in finishLaunch (wired post-spawn behind an `if (m_trackedChild)`) was
  // therefore never installed and the extraction dir was orphaned into the
  // reusable cache. The reclaim connect now rides in launchTracked itself,
  // wired before the spawn, so even the synchronous shape removes the dir.
  // Everything here is seam-driven (fake extractor + fake spawner): no fork,
  // so the slot also runs under ThreadSanitizer.
  QVERIFY2(!m_tempExecutable.isEmpty(), "Test setup failed: no temp executable");

  const QString base = QStringLiteral("kartend_tracked_sync_fail");
  const QString extractionDir = extractionDirFor(base);
  QDir(extractionDir).removeRecursively();

  const QString zip = makeArchiveStub(base);
  QVERIFY2(!zip.isEmpty(), "could not create the archive stub");

  QList<CollectionConfig> collections;
  CollectionConfig collection;
  collection.name = QStringLiteral("Archive Collection");
  collection.archive.extractArchives = true;
  collection.archive.extractedExtension = QStringLiteral(".iso");
  // Passes validateLauncherPath (exists + executable); the fake spawner
  // below means it is never actually run.
  collection.launcher.launcherPath = m_tempExecutable;
  collections.append(collection);

  // Route through the tracked path: runtime detection reads
  // ctx->collection.generalSettings, so wire a minimal context.
  GeneralSettings settings;
  settings.runtimeDetection.runtimeDetectionEnabled = true;
  ApplicationContext ctx;
  ctx.collection.generalSettings = &settings;

  LaunchManager manager;
  LaunchManagerSetup setup;
  setup.ctx = &ctx;
  setup.collections = &collections;
  manager.setupReferences(setup);
  manager.setArchiveExtractorForTesting(KartendTest::fakeSleepyExtractor(
      extractionDir, QStringLiteral("disc.iso"), /*maxSleepMs=*/100));
  manager.setLauncherSpawnerForTesting(KartendTest::fakeSyncFailingLauncherSpawner());

  QSignalSpy extractionFinishedSpy(&manager, &LaunchManager::extractionFinished);
  QSignalSpy runtimeFinishedSpy(&manager, &LaunchManager::runtimeFinished);
  manager.launchItem(zip, 0);

  QVERIFY2(extractionFinishedSpy.count() == 1 || extractionFinishedSpy.wait(15000),
           "extractionFinished must fire once the worker completes");
  // The synchronous FailedToStart settles the tracked session before
  // launchTracked returns: no tracked child lingers and the balanced
  // runtimeFinished still fires.
  QTRY_COMPARE(runtimeFinishedSpy.count(), 1);
  QVERIFY(!manager.isRuntimeChildRunning());
  QTRY_VERIFY2(!QDir(extractionDir).exists(),
               "a synchronously-failed tracked spawn must reclaim the extraction dir");
}

void TestLaunchManager::testRecordLaunch_prunesLapsedDebounceEntries() {
  LaunchManager manager;
  manager.recordLaunch(QStringLiteral("/tmp/items/alpha.mp4"));
  manager.recordLaunch(QStringLiteral("/tmp/items/beta.mp4"));
  // Both entries sit inside their 500ms window: nothing is prunable and the
  // guard still holds per path (recording beta must not evict alpha).
  QCOMPARE(manager.debounceEntryCountForTesting(), 2);
  QVERIFY(!manager.canLaunch(QStringLiteral("/tmp/items/alpha.mp4")));
  QVERIFY(!manager.canLaunch(QStringLiteral("/tmp/items/beta.mp4")));
  QVERIFY(manager.canLaunch(QStringLiteral("/tmp/items/gamma.mp4")));

  // Wait out the 500ms guard window (extra headroom only makes the entries
  // MORE lapsed — no flake direction under load).
  QTest::qWait(600);
  QVERIFY(manager.canLaunch(QStringLiteral("/tmp/items/alpha.mp4")));

  // The next record prunes the lapsed entries instead of accumulating one
  // per ever-launched path for the life of the session.
  manager.recordLaunch(QStringLiteral("/tmp/items/gamma.mp4"));
  QCOMPARE(manager.debounceEntryCountForTesting(), 1);
  QVERIFY(!manager.canLaunch(QStringLiteral("/tmp/items/gamma.mp4")));
}

void TestLaunchManager::testLaunchItem_chooserMutatingCollectionsLaunchesSnapshot() {
  // The chooser callback stands in for the modal LauncherChooserDialog: its
  // nested event loop still services timers and queued slots, and any of them
  // mutating the owner's collections list reallocates it under launchItem's
  // feet. The post-chooser reads (chosen launcher entry, archive options,
  // collection name) used to go through a reference into that list — freed
  // memory right before the spawn. Mutate the list from inside the callback
  // and verify the launch still uses the values captured when it opened.
  auto *dir = new QTemporaryDir();
  QVERIFY(dir->isValid());
  m_fixtureDirs.append(dir);

#ifdef Q_OS_WIN
  static constexpr const char *kLauncherExt = ".bat";
  static constexpr const char *kLauncherContent = "@echo off\r\nexit /b 0\r\n";
#else
  static constexpr const char *kLauncherExt = "";
  static constexpr const char *kLauncherContent = "#!/bin/sh\nexit 0\n";
#endif
  // Two distinct on-disk launchers so the chooser genuinely has a pick to
  // make; the fake spawner below never runs them, they only need to pass
  // validateLauncherPath (exists + executable).
  auto makeLauncher = [&](const QString &baseName) -> QString {
    const QString path = dir->filePath(baseName + QLatin1String(kLauncherExt));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return {};
    }
    f.write(kLauncherContent);
    f.close();
    if (!QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
      return {};
    }
    return path;
  };
  const QString primaryLauncher = makeLauncher(QStringLiteral("primary_launcher"));
  const QString alternateLauncher = makeLauncher(QStringLiteral("alternate_launcher"));
  QVERIFY2(!primaryLauncher.isEmpty() && !alternateLauncher.isEmpty(),
           "could not create the launcher fixtures");

  const QString mediaFile = dir->filePath(QStringLiteral("item.bin"));
  {
    QFile f(mediaFile);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("MEDIA");
  }

  QList<CollectionConfig> collections;
  CollectionConfig collection;
  collection.name = QStringLiteral("Snapshot Collection");
  collection.launcher.launcherPath = primaryLauncher;
  LauncherConfig alternate;
  alternate.name = QStringLiteral("Alternate");
  alternate.launcherPath = alternateLauncher;
  collection.launcher.additionalLaunchers.append(alternate);
  collections.append(collection);
  // capacity == size, so the append inside the chooser must reallocate.
  collections.squeeze();

  LaunchManager manager;
  LaunchManagerSetup setup;
  setup.collections = &collections;
  QString chooserSawCollection;
  QStringList chooserSawNames;
  setup.chooseLauncher = [&](const QString &collectionName, const QStringList &launcherNames,
                             int) -> int {
    chooserSawCollection = collectionName;
    chooserSawNames = launcherNames;
    // What a settings edit landing mid-modal does: replace the launching
    // collection, then grow the list past its capacity so it reallocates
    // and the old buffer is freed.
    CollectionConfig replacement;
    replacement.name = QStringLiteral("Replaced");
    replacement.launcher.launcherPath = QStringLiteral("/nonexistent/replaced_launcher");
    collections[0] = replacement;
    for (int i = 0; i < 32; ++i) {
      collections.append(CollectionConfig{});
    }
    return 1; // pick the alternate launcher
  };
  manager.setupReferences(setup);

  // Runtime detection stays off (no ctx/settings wired) -> the detached path,
  // which hands the validated program + args to the spawner seam synchronously
  // inside launchItem. Recording them is the whole assertion; nothing forks.
  QString spawnedProgram;
  QStringList spawnedArgs;
  manager.setLauncherSpawnerForTesting(
      [&spawnedProgram, &spawnedArgs](QProcess *, const QString &program, const QStringList &args) {
        spawnedProgram = program;
        spawnedArgs = args;
      });

  manager.launchItem(mediaFile, 0);

  QCOMPARE(chooserSawCollection, QStringLiteral("Snapshot Collection"));
  QCOMPARE(chooserSawNames.size(), 2);
  QCOMPARE(collections.size(), 33); // the mid-chooser mutation really ran
  // The spawn must use the launcher the user picked as it existed when the
  // chooser opened — not the replaced element 0, and not freed memory.
  QCOMPARE(spawnedProgram, QFileInfo(alternateLauncher).canonicalFilePath());
  QVERIFY2(spawnedArgs.contains(mediaFile),
           "the launched item must ride along as an argument to the snapshotted launcher");
}

void TestLaunchManager::testDetachedChildReparentedAfterWatchWindow() {
  auto *dir = new QTemporaryDir();
  QVERIFY(dir->isValid());
  m_fixtureDirs.append(dir);

#ifdef Q_OS_WIN
  static constexpr const char *kLauncherExt = ".bat";
  static constexpr const char *kLauncherContent = "@echo off\r\nexit /b 0\r\n";
#else
  static constexpr const char *kLauncherExt = "";
  static constexpr const char *kLauncherContent = "#!/bin/sh\nexit 0\n";
#endif
  const QString launcher = dir->filePath(QStringLiteral("launcher") + QLatin1String(kLauncherExt));
  {
    QFile f(launcher);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(kLauncherContent);
  }
  QVERIFY(QFile::setPermissions(launcher, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
  const QString mediaFile = dir->filePath(QStringLiteral("item.bin"));
  {
    QFile f(mediaFile);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("MEDIA");
  }

  QList<CollectionConfig> collections;
  CollectionConfig collection;
  collection.name = QStringLiteral("Detached Collection");
  collection.launcher.launcherPath = launcher;
  collections.append(collection);

  QPointer<QProcess> child;
  {
    LaunchManager manager;
    LaunchManagerSetup setup;
    setup.collections = &collections;
    manager.setupReferences(setup);
    // The fake spawner never forks; the QProcess stays NotRunning, so neither
    // errorOccurred nor finished fires and only the watch window can settle.
    manager.setLauncherSpawnerForTesting([](QProcess *, const QString &, const QStringList &) {});

    manager.launchItem(mediaFile, 0);

    const QList<QProcess *> ownedBefore = manager.findChildren<QProcess *>();
    QCOMPARE(ownedBefore.size(), 1);
    child = ownedBefore.first();

    // Wait out the early-failure window: the settle handler must orphan the
    // child (no QProcess children left on the manager) without deleting it.
    QTRY_VERIFY_WITH_TIMEOUT(manager.findChildren<QProcess *>().isEmpty(), 5000);
    QVERIFY(child);
    QVERIFY2(!child->parent(), "a settled fire-and-forget child must not stay QObject-owned");
  }
  // ~LaunchManager must reap the never-started (NotRunning) orphan rather
  // than leaking it — and must not have crashed doing so.
  QVERIFY(child.isNull());
}

void TestLaunchManager::testExtractArchive_rejectsArchiveLargerThanCap() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP("Kartend-dhhh6: makeZipFixture forks zip/7z to build the fixture — a synchronous fork "
        "with no cross-thread state for a seam to cover. The pre-check under test runs before any "
        "extraction, so there is nothing for the worker-path seam to exercise here.");
#endif
  // Kartend-ijglg pre-check: compression only inflates, so an archive whose
  // on-disk size already exceeds the decompressed cap is rejected before any
  // extractor is spawned (no extraction dir is ever created).
  const QString base = QStringLiteral("kartend_cap_pre");
  const QString extractionDir = extractionDirFor(base);
  QDir(extractionDir).removeRecursively();
  const QList<QPair<QString, QByteArray>> entries = {
      {QStringLiteral("disc.iso"), QByteArrayLiteral("ISO")}};
  const QString zip = makeZipFixture(base, entries);
  if (zip.isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("No archive-creation tool (zip/bsdtar/7z) on PATH");
  }
  QVERIFY(QFileInfo(zip).size() > 16); // any real zip clears this

  auto result = LaunchManager::extractArchiveToTemp(zip, ".iso", nullptr, /*maxBytes=*/16);
  QVERIFY2(result.isError(), "an archive larger than the cap must be rejected outright");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::ResourceLimitExceeded);
  QVERIFY2(!QDir(extractionDir).exists(),
           "the pre-check must fire before any extraction dir is created");
}

void TestLaunchManager::testExtractArchive_enforcesDecompressedSizeCap() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP(
      "Kartend-dhhh6: single-threaded real-extractor test — extractArchiveToTemp forks an "
      "external tool synchronously on the test thread, so there is no cross-thread state for a "
      "non-forking seam to cover and the fork itself is the assertion. The launchItem worker-path "
      "slots (failedStart/cancel/dtor) carry the seam-covered cross-thread coverage under TSan.");
#endif
  // Kartend-ijglg: a small *compressed* archive that inflates past the cap
  // (64 KiB of zeros vs a 4 KiB cap — the zip-bomb shape) must fail with
  // ResourceLimitExceeded and leave no partial extraction behind. A tiny
  // archive can finish inside the watchdog's first poll window, so this
  // exercises the unconditional post-completion size check.
  if (!extractorAvailable()) {
    KARTEND_ARCHIVE_TOOL_SKIP("No archive extractor (7z/unzip/bsdtar) on PATH");
  }
  const QString base = QStringLiteral("kartend_cap_post");
  const QString extractionDir = extractionDirFor(base);
  QDir(extractionDir).removeRecursively();
  const QList<QPair<QString, QByteArray>> entries = {
      {QStringLiteral("disc.iso"), QByteArray(64 * 1024, '\0')}};
  const QString zip = makeZipFixture(base, entries);
  if (zip.isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("No archive-creation tool (zip/bsdtar/7z) on PATH");
  }
  QVERIFY2(QFileInfo(zip).size() <= 4096,
           "fixture must compress below the cap so the pre-check does not short-circuit");

  auto result = LaunchManager::extractArchiveToTemp(zip, ".iso", nullptr, /*maxBytes=*/4096);
  QVERIFY2(result.isError(), "extraction inflating past the cap must fail");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::ResourceLimitExceeded);
  QVERIFY2(!QDir(extractionDir).exists(), "the over-cap extraction dir must be removed");
}

void TestLaunchManager::testExtractArchive_sizeCapKillsRunawayExtractor() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP(
      "Kartend-dhhh6: single-threaded real-extractor test — extractArchiveToTemp forks an "
      "external tool synchronously on the test thread, so there is no cross-thread state for a "
      "non-forking seam to cover and the fork itself is the assertion. The launchItem worker-path "
      "slots (failedStart/cancel/dtor) carry the seam-covered cross-thread coverage under TSan.");
#endif
#ifdef Q_OS_WIN
  QSKIP("The fake shell-script extractor is POSIX-specific");
#else
  // Kartend-ijglg mid-flight arm: a fake `7z` writes 64 KiB into the
  // extraction dir and then sleeps far past the test horizon. The watchdog
  // must observe the over-cap growth within a poll interval and KILL the
  // child — if the kill path regresses, the extractor sleeps out its 30s and
  // the test fails on the error code (FileNotFound from the missing .iso).
  const QString base = QStringLiteral("kartend_cap_runaway");
  const QString extractionDir = extractionDirFor(base);
  QDir(extractionDir).removeRecursively();
  const QList<QPair<QString, QByteArray>> entries = {
      {QStringLiteral("disc.iso"), QByteArrayLiteral("ISO")}};
  const QString zip = makeZipFixture(base, entries); // before PATH is faked
  if (zip.isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("No archive-creation tool (zip/bsdtar/7z) on PATH");
  }
  const QString fakeDir = makeFakeExtractorDir(/*kibToWrite=*/64, /*sleepSecs=*/30);
  QVERIFY2(!fakeDir.isEmpty(), "could not create the fake-extractor fixture");

  const QByteArray oldPath = qgetenv("PATH");
  qputenv("PATH", fakeDir.toUtf8() + ":" + oldPath);
  auto restorePath = qScopeGuard([&oldPath]() { qputenv("PATH", oldPath); });

  QElapsedTimer clock;
  clock.start();
  auto result = LaunchManager::extractArchiveToTemp(zip, ".iso", nullptr, /*maxBytes=*/4096);
  QVERIFY2(result.isError(), "a runaway extraction must be aborted");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::ResourceLimitExceeded);
  QVERIFY2(clock.elapsed() < 20000,
           "the watchdog must kill the child within a poll interval, not wait out the sleep");
  QVERIFY2(!QDir(extractionDir).exists(), "the killed extraction must be cleaned up");
#endif
}

void TestLaunchManager::testExtractArchive_preSetCancelReturnsCancelled() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP("Kartend-dhhh6: makeZipFixture forks zip/7z to build the fixture — a synchronous fork "
        "with no cross-thread state for a seam to cover. The pre-check under test runs before any "
        "extraction, so there is nothing for the worker-path seam to exercise here.");
#endif
  // A cancellation that lands before the worker starts must short-circuit:
  // OperationCancelled, no extractor spawned, no extraction dir created.
  const QString base = QStringLiteral("kartend_cancel_pre");
  const QString extractionDir = extractionDirFor(base);
  QDir(extractionDir).removeRecursively();
  const QList<QPair<QString, QByteArray>> entries = {
      {QStringLiteral("disc.iso"), QByteArrayLiteral("ISO")}};
  const QString zip = makeZipFixture(base, entries);
  if (zip.isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("No archive-creation tool (zip/bsdtar/7z) on PATH");
  }

  std::atomic_bool cancelled{true};
  auto result = LaunchManager::extractArchiveToTemp(zip, ".iso", &cancelled);
  QVERIFY2(result.isError(), "a pre-set cancel flag must abort the extraction");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::OperationCancelled);
  QVERIFY(!QDir(extractionDir).exists());
}

void TestLaunchManager::testLaunchItem_cancelExtractionAbortsPendingLaunch() {
#ifdef Q_OS_WIN
  QSKIP("The fake shell-script extractor is POSIX-specific");
#else
  // Kartend-mkcak end-to-end cancel: a slow extractor keeps the extraction in
  // flight; cancelExtraction() must stop it within a poll interval, clean up
  // the partial extraction, and abandon the pending launch silently.
  const QString base = QStringLiteral("kartend_cancel_launch");
  const QString extractionDir = extractionDirFor(base);
  QDir(extractionDir).removeRecursively();

  QList<CollectionConfig> collections;
  CollectionConfig collection;
  collection.name = QStringLiteral("Archive Collection");
  collection.archive.extractArchives = true;
  collection.archive.extractedExtension = QStringLiteral(".iso");
  collection.launcher.launcherPath = makeFailingLauncher();
  QVERIFY2(!collection.launcher.launcherPath.isEmpty(), "could not create the launcher fixture");
  collections.append(collection);

  LaunchManager manager;
  LaunchManagerSetup setup;
  setup.collections = &collections;
  manager.setupReferences(setup);

  QString zip;
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  // Kartend-dhhh6: under TSan, forking a (real or fake-script) extractor child
  // aborts libtsan. Drive the same cancel hand-off through a non-forking
  // extractor seam that runs on the worker thread and polls the SAME cancel
  // atomic — so cancelExtraction()'s GUI→worker signalling, the QFutureWatcher
  // completion, and the worker-side dir cleanup all run under ThreadSanitizer.
  // No real archive is read, so a content-less stub stands in for the zip.
  zip = makeArchiveStub(base);
  QVERIFY2(!zip.isEmpty(), "could not create the archive stub");
  manager.setArchiveExtractorForTesting(
      KartendTest::fakeSleepyExtractor(extractionDir, QStringLiteral("disc.iso")));
#else
  // Real path: a fake `7z` that only sleeps keeps the extraction in flight so
  // cancelExtraction() must make the watchdog kill the child.
  const QList<QPair<QString, QByteArray>> entries = {
      {QStringLiteral("disc.iso"), QByteArrayLiteral("ISO")}};
  zip = makeZipFixture(base, entries); // before PATH is faked
  if (zip.isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("No archive-creation tool (zip/bsdtar/7z) on PATH");
  }
  const QString fakeDir = makeFakeExtractorDir(/*kibToWrite=*/0, /*sleepSecs=*/30);
  QVERIFY2(!fakeDir.isEmpty(), "could not create the fake-extractor fixture");
  const QByteArray oldPath = qgetenv("PATH");
  qputenv("PATH", fakeDir.toUtf8() + ":" + oldPath);
  auto restorePath = qScopeGuard([&oldPath]() { qputenv("PATH", oldPath); });
#endif

  QSignalSpy finishedSpy(&manager, &LaunchManager::extractionFinished);
  manager.launchItem(zip, 0);
  QVERIFY2(manager.isExtractionRunning(), "the sleeping extractor must still be running");

  QElapsedTimer clock;
  clock.start();
  manager.cancelExtraction();
  QVERIFY2(finishedSpy.count() == 1 || finishedSpy.wait(15000),
           "cancel must terminate the extraction promptly");
  QVERIFY2(clock.elapsed() < 20000,
           "cancel must stop the extractor within a poll interval, not wait out the sleep");
  QVERIFY(!manager.isExtractionRunning());
  QTRY_VERIFY2(!QDir(extractionDir).exists(), "the cancelled extraction dir must be removed");
#endif
}

void TestLaunchManager::testLaunchManagerDtor_drainsRunningExtraction() {
#ifdef Q_OS_WIN
  QSKIP("The fake shell-script extractor is POSIX-specific");
#else
  // Kartend-mkcak teardown contract: destroying the manager mid-extraction
  // must not abandon the extractor — the dtor requests cancellation and waits
  // (bounded by poll interval + grace) for the worker.
  const QString base = QStringLiteral("kartend_dtor");
  const QString extractionDir = extractionDirFor(base);
  QDir(extractionDir).removeRecursively();

  QList<CollectionConfig> collections;
  CollectionConfig collection;
  collection.name = QStringLiteral("Archive Collection");
  collection.archive.extractArchives = true;
  collection.archive.extractedExtension = QStringLiteral(".iso");
  collection.launcher.launcherPath = makeFailingLauncher();
  QVERIFY2(!collection.launcher.launcherPath.isEmpty(), "could not create the launcher fixture");
  collections.append(collection);

  QString zip;
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  // Kartend-dhhh6: under TSan, forking an extractor child aborts libtsan. Drive
  // the destructor's cancel-and-drain contract through a non-forking extractor
  // seam that runs on the worker and polls the SAME cancel atomic — so the
  // ~LaunchManager → worker hand-off and the worker-side cleanup run under
  // ThreadSanitizer.
  zip = makeArchiveStub(base);
  QVERIFY2(!zip.isEmpty(), "could not create the archive stub");
#else
  const QList<QPair<QString, QByteArray>> entries = {
      {QStringLiteral("disc.iso"), QByteArrayLiteral("ISO")}};
  zip = makeZipFixture(base, entries); // before PATH is faked
  if (zip.isEmpty()) {
    KARTEND_ARCHIVE_TOOL_SKIP("No archive-creation tool (zip/bsdtar/7z) on PATH");
  }
  const QString fakeDir = makeFakeExtractorDir(/*kibToWrite=*/0, /*sleepSecs=*/30);
  QVERIFY2(!fakeDir.isEmpty(), "could not create the fake-extractor fixture");
  const QByteArray oldPath = qgetenv("PATH");
  qputenv("PATH", fakeDir.toUtf8() + ":" + oldPath);
  auto restorePath = qScopeGuard([&oldPath]() { qputenv("PATH", oldPath); });
#endif

  QElapsedTimer clock;
  clock.start();
  {
    LaunchManager manager;
    LaunchManagerSetup setup;
    setup.collections = &collections;
    manager.setupReferences(setup);
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
    manager.setArchiveExtractorForTesting(
        KartendTest::fakeSleepyExtractor(extractionDir, QStringLiteral("disc.iso")));
#endif
    manager.launchItem(zip, 0);
    QVERIFY2(manager.isExtractionRunning(), "the sleeping extractor must still be running");
  } // dtor: cancel + bounded wait for the worker
  QVERIFY2(clock.elapsed() < 20000,
           "the dtor must drain the worker promptly instead of waiting out the sleep");
  QVERIFY2(!QDir(extractionDir).exists(),
           "the worker-side cleanup guard must have removed the partial extraction");
#endif
}

QTEST_MAIN(TestLaunchManager)
#include "test_launchmanager.moc"
