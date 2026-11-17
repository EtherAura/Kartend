#include "test_launchflow.h"

#include "applicationcontext.h"
#include "applicationmanager.h"
#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "historystore.h"
#include "idatabasemanager.h"
#include "interactionmanager.h"
#include "launchmanager.h"
#include "mainwindow.h"
#include "mainwindowfixture.h"
#include "pathutils.h"
#include "settingsmanager.h"
#include "usagestatsstore.h"

#include "../support/launchfakes.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

/// One on-disk launch scenario: a media directory holding a single video
/// item plus an executable launcher script with caller-chosen body. The
/// QTemporaryDirs clean both up on destruction.
struct LaunchScenario {
  QTemporaryDir mediaDir;
  QTemporaryDir launcherDir;
  QString itemPath;
  QString launcherPath;
  bool valid = false;
};

// The launcher stands in for a media player. On POSIX it is an executable
// #!/bin/sh script; Windows has no exec bit, so it is a .cmd batch (the
// extension is what lets QProcess run it). Kartend-mhgzq.
#ifdef Q_OS_WIN
constexpr const char *kLauncherFileName = "noop-player.cmd";
QByteArray noopLauncherScript() {
  return QByteArrayLiteral("@echo off\r\nexit /b 0\r\n");
}
// `ping -n 3 127.0.0.1` blocks ~2s (three pings, ~1s apart) — the batch
// stand-in for `sleep 2`, leaving the tracked-launch timing headroom.
QByteArray sleepingLauncherScript() {
  return QByteArrayLiteral("@echo off\r\nping -n 3 127.0.0.1 > nul\r\nexit /b 0\r\n");
}
#else
constexpr const char *kLauncherFileName = "noop-player.sh";
QByteArray noopLauncherScript() {
  return QByteArrayLiteral("#!/bin/sh\nexit 0\n");
}
QByteArray sleepingLauncherScript() {
  return QByteArrayLiteral("#!/bin/sh\nsleep 2\nexit 0\n");
}
#endif

bool writeFile(const QString &path, const QByteArray &bytes, bool executable) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    return false;
  }
  f.write(bytes);
  f.close();
#ifndef Q_OS_WIN
  if (executable) {
    return QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  }
#else
  Q_UNUSED(executable); // Windows: the .cmd extension provides executability
#endif
  return true;
}

LaunchScenario makeScenario(const QByteArray &launcherScript) {
  LaunchScenario s;
  if (!s.mediaDir.isValid() || !s.launcherDir.isValid()) {
    return s;
  }
  s.itemPath = QDir(s.mediaDir.path()).filePath(QStringLiteral("nature-documentary.mp4"));
  s.launcherPath = QDir(s.launcherDir.path()).filePath(QString::fromLatin1(kLauncherFileName));
  s.valid = writeFile(s.itemPath, QByteArrayLiteral("fake video bytes"), /*executable=*/false) &&
            writeFile(s.launcherPath, launcherScript, /*executable=*/true);
  return s;
}

CollectionConfig makeCollection(const LaunchScenario &scenario) {
  CollectionConfig cfg;
  cfg.name = QStringLiteral("Videos");
  cfg.type = QStringLiteral("video");
  cfg.mediaDirectory = scenario.mediaDir.path();
  // Regression coverage for Kartend-693zb: this extension list round-trips
  // through the sandbox INI (saveCollections -> loadCollections normalize)
  // before the startup scan runs. The pre-fix glob canonical form ("*.mp4")
  // made ScanService::buildNameFilters double-prefix to the never-matching
  // "*.*.mp4", so the scan found ZERO items and the whole launch flow below
  // failed at scanAndResolveItemPath. Canonical form is now bare.
  cfg.extensions = QStringList{QStringLiteral("mp4")};
  cfg.launcher.launcherPath = scenario.launcherPath;
  return cfg;
}

/// The uuid LaunchManager itself resolves (resolveCollectionUuid mirrors
/// this exact pair) — recompute it here so the assertions key the same rows.
QString collectionUuidFor(const CollectionConfig &cfg) {
  const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
  return CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
}

/// Sandbox seed for the fixture's pre-construction hook: persists @p cfg
/// into the sandbox INI through the real SettingsManager so MainWindow
/// starts WITH a collection. Constructing first and appending afterwards
/// is not an option — an empty-collections startup queues the modal
/// "Create First Collection" QInputDialog, which deadlocks the suite as
/// soon as the test pumps the event loop.
void seedCollectionIntoSandbox(const CollectionConfig &cfg) {
  ApplicationContext seedCtx; // saveCollections never reads ctx
  SettingsManager seedSettings(&seedCtx);
  if (seedSettings.saveCollections({cfg}).isError()) {
    qFatal("failed to seed the sandbox collection INI");
  }
}

/// Runs the real scan so the item lands in the items table
/// (UsageStatsStore::recordLaunch is an UPDATE — without a scanned row,
/// launches vanish with a warning). Returns the scanned item path (the
/// canonical key recordItemLaunch will be called with), or an empty string
/// on failure.
QString scanAndResolveItemPath(MainWindow *win, const QString &uuid) {
  IDatabaseManager *db = win->getApplicationManager()->getDatabaseManager();
  if (!db) {
    return {};
  }
  db->loadAllCollections(win->m_collections);

  // The startup rescan runs on the DB worker thread; its items rows become
  // visible mid-scan (the staging pipeline commits in batches), so "row
  // exists" alone can leave the tail of the scan holding the SQLite write
  // lock. Launch writes issued inside that window survive via the bounded
  // runWrite retry ladder (regression-tested in test_databasemanager), but
  // this test asserts the launch landed within a fixed timeout — so wait on
  // the condition that actually ends the scan: the apply transaction's
  // commit, which stamps collections.last_scanned past its epoch-0 creation
  // seed and releases the write lock. A state query rather than a signal
  // spy / fixed quiet window: the scan can finish before a spy could be
  // armed, and the previous 1.5s-quiet loop just masked this condition with
  // wall-clock time.
  const bool scanCommitted = QTest::qWaitFor(
      [&]() {
        const QDateTime lastScanned = db->loadCollectionLastScanned(uuid);
        return lastScanned.isValid() && lastScanned.toSecsSinceEpoch() > 0;
      },
      30000);
  if (!scanCommitted) {
    return {};
  }

  // The stamp only ends the FIRST scan. This fixture's watcher/count-reload
  // plumbing triggers several follow-up rescans, and each one re-takes the
  // SQLite write lock — the launch write's retry ladder is bounded
  // (Kartend-cbtml), so a launch issued into sustained churn can be dropped.
  // On top of the stamp, wait for scan QUIESCENCE: no collectionScanCompleted
  // for 1.5s, bounded at 20s. This window is genuinely time-gated — "no
  // further scan is coming" has no positive signal to wait on.
  QSignalSpy scanCompleted(db, &IDatabaseManager::collectionScanCompleted);
  int settledCount = scanCompleted.count();
  for (int waited = 0; waited < 20000; waited += 1500) {
    QTest::qWait(1500);
    if (scanCompleted.count() == settledCount) {
      break;
    }
    settledCount = scanCompleted.count();
  }

  const auto rows = db->loadAllItemPathsForCollection(uuid);
  return rows.isEmpty() ? QString{} : rows.first().path;
}

// Kartend-rctcv: a queued worker write that lands inside a scan's write lock is
// retried on an inline ladder (~4s) and then REQUEUED five times at 1/2/4/8/16s,
// so the write settles at worst ~38s after the launch. The old 10s here was
// shorter than that budget: on a slow runner the assertion could fail while the
// write was still legitimately pending. Wait past the whole budget instead, so a
// failure means the write was genuinely lost rather than merely late. Costs
// nothing on a green run — QTRY returns as soon as the row appears.
constexpr int WORKER_WRITE_SETTLE_TIMEOUT_MS = 45000;

#ifndef Q_OS_WIN
/// One recorded stub-extractor invocation: its working directory and argv
/// (without argv[0]).
struct ToolInvocation {
  QString cwd;
  QStringList args;
};

/// Kartend-ecjq6: a stand-in for bsdtar / 7z that appends every invocation
/// (physical cwd, then one `arg=` line per argument, then `end`) to @p logPath
/// and then plays just enough of the tool's protocol for extractArchiveToTemp:
/// a clean single-entry listing for the pre-extraction scan, and for anything
/// else (the extraction) a `disc.iso` written into its cwd. Mode is picked
/// from the listing argv alone, so an extraction argv that grows an extra
/// switch still extracts and the argv assertions — not a stub failure — are
/// what catch it. Shell builtins only, so the isolated PATH needs nothing else.
QByteArray stubExtractorScript(const QString &tool, const QString &logPath) {
  QByteArray script =
      QByteArrayLiteral("#!/bin/sh\nlog='") + logPath.toUtf8() +
      QByteArrayLiteral("'\n"
                        "printf 'cwd=%s\\n' \"$(pwd -P)\" >> \"$log\"\n"
                        "for a in \"$@\"; do printf 'arg=%s\\n' \"$a\" >> \"$log\"; "
                        "done\n"
                        "printf 'end\\n' >> \"$log\"\n");
  if (tool == QLatin1String("bsdtar")) {
    script += QByteArrayLiteral(
        "case \"$1\" in\n"
        "  -tvf) printf '%s\\n' '-rw-r--r--  0 0      0           3 Jan  1  2026 disc.iso' ;;\n"
        "  -tf) printf '%s\\n' 'disc.iso' ;;\n"
        "  *) printf 'ISO' > disc.iso ;;\n"
        "esac\n");
  } else {
    script +=
        QByteArrayLiteral("case \"$1\" in\n"
                          "  l) printf '%s\\n' '' '--' 'Path = stub' 'Type = zip' '' '----------' "
                          "'Path = disc.iso' 'Size = 3' 'Attributes = A_ -rw-r--r--' '' ;;\n"
                          "  *) printf 'ISO' > disc.iso ;;\n"
                          "esac\n");
  }
  script += QByteArrayLiteral("exit 0\n");
  return script;
}

QList<ToolInvocation> readInvocationLog(const QString &logPath) {
  QList<ToolInvocation> invocations;
  QFile log(logPath);
  if (!log.open(QIODevice::ReadOnly)) {
    return invocations;
  }
  ToolInvocation current;
  const QStringList lines =
      QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (const QString &line : lines) {
    if (line.startsWith(QLatin1String("cwd="))) {
      current = ToolInvocation{line.mid(4), {}};
    } else if (line.startsWith(QLatin1String("arg="))) {
      current.args << line.mid(4);
    } else if (line == QLatin1String("end")) {
      invocations << current;
    }
  }
  return invocations;
}

/// Everything one stubbed extraction run leaves behind for the assertions.
struct StubbedExtraction {
  QString archivePath;   // the archive handed to extractArchiveToTemp
  QString extractionDir; // canonical per-archive dir it must extract into
  QString resultPath;    // what extractArchiveToTemp returned (empty on error)
  QString error;         // its error summary, if any
  QList<ToolInvocation> invocations;
};

/// Runs the production extractArchiveToTemp with PATH narrowed to a temp dir
/// holding only the @p tool stub, so that stub is the sole extractor found and
/// no host tool or user archive is involved. All files live in @p root.
StubbedExtraction runStubbedExtraction(const QTemporaryDir &root, const QString &tool) {
  StubbedExtraction run;
  const QDir rootDir(root.path());
  if (!rootDir.mkpath(QStringLiteral("bin")) || !rootDir.mkpath(QStringLiteral("media"))) {
    run.error = QStringLiteral("could not create the stub fixture dirs");
    return run;
  }
  const QString logPath = rootDir.filePath(QStringLiteral("argv.log"));
  run.archivePath =
      QFileInfo(rootDir.filePath(QStringLiteral("media/game.zip"))).absoluteFilePath();
  if (!writeFile(rootDir.filePath(QStringLiteral("bin/") + tool),
                 stubExtractorScript(tool, logPath),
                 /*executable=*/true) ||
      !writeFile(run.archivePath, QByteArrayLiteral("not a real archive"), /*executable=*/false)) {
    run.error = QStringLiteral("could not write the stub extractor or archive");
    return run;
  }
  const QString cacheBase = rootDir.filePath(QStringLiteral("cache"));

  const QByteArray oldPath = qgetenv("PATH");
  qputenv("PATH", rootDir.filePath(QStringLiteral("bin")).toUtf8());
  {
    const auto restorePath = qScopeGuard([&oldPath]() { qputenv("PATH", oldPath); });
    const auto result = LaunchManager::extractArchiveToTemp(
        run.archivePath, QStringLiteral(".iso"), nullptr, /*maxDecompressedBytes=*/4096, cacheBase);
    if (result.isError()) {
      run.error = result.error().userFacingSummary();
    } else {
      run.resultPath = QFileInfo(result.value()).canonicalFilePath();
    }
  }
  // <base>/kartend_extract/<archive completeBaseName>, as extractArchiveToTemp
  // lays it out.
  run.extractionDir =
      QFileInfo(cacheBase + QStringLiteral("/kartend_extract/game")).canonicalFilePath();
  run.invocations = readInvocationLog(logPath);
  return run;
}

/// Common proof that the extraction itself ran: it produced the launchable
/// file in the per-archive dir from the stub's write in its cwd.
void verifyExtractionReached(const StubbedExtraction &run, const ToolInvocation &extraction) {
  QVERIFY2(run.error.isEmpty(), qPrintable(run.error));
  QVERIFY2(!run.extractionDir.isEmpty(), "the per-archive extraction dir was not created");
  QCOMPARE(extraction.cwd, run.extractionDir);
  QCOMPARE(run.resultPath, QDir(run.extractionDir).filePath(QStringLiteral("disc.iso")));
  QFile extracted(run.resultPath);
  QVERIFY(extracted.open(QIODevice::ReadOnly));
  QCOMPARE(extracted.readAll(), QByteArrayLiteral("ISO"));
}
#endif

} // namespace

void TestLaunchFlow::testDetachedLaunchRecordsUsageStatsAndHistory() {
  LaunchScenario scenario = makeScenario(noopLauncherScript());
  QVERIFY2(scenario.valid, "failed to materialize media file + launcher script");
  const CollectionConfig cfg = makeCollection(scenario);
  const QString uuid = collectionUuidFor(cfg);

  KartendTest::MainWindowFixture fixture([&cfg]() { seedCollectionIntoSandbox(cfg); });
  MainWindow *win = fixture.window();
  QCOMPARE(win->m_collections.size(), 1);

  const QString scannedPath = scanAndResolveItemPath(win, uuid);
  QVERIFY2(!scannedPath.isEmpty(), "scan never produced an items row for the collection");

  IDatabaseManager *db = win->getApplicationManager()->getDatabaseManager();
  LaunchManager *lm = win->getApplicationManager()->getInteractionManager()->launchManager();
  QVERIFY(lm);

  // History tracking is settings-gated in the onLaunched callback; pin the
  // gate open regardless of defaults so the assertion is self-contained.
  win->m_generalSettings.history.historyEnabled = true;
  win->m_generalSettings.history.historyMaxEntries = 50;

  // Pre-flight: never launched.
  QCOMPARE(db->loadItemUsageStats(uuid, scannedPath).playCount, qint64(0));
  QCOMPARE(db->historyEntryCount(), qint64(0));

#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  // Kartend-dhhh6: under TSan, fork() inside QProcess aborts libtsan, so the
  // real spawn can't run. Drive the detached-launch state machine with a
  // non-forking spawner that emits a clean immediate exit — recordSuccessfulLaunch
  // still fires and the same playCount/history worker writes land, exercising
  // the launch → callback → cross-thread SQLite-write path under TSan. The real
  // fork+exec path is still covered in non-TSan builds.
  lm->setLauncherSpawnerForTesting(KartendTest::fakeLauncherSpawner(/*runtimeMs=*/0));
#endif

  // The launch. Runtime detection is off by default, so this takes the
  // detached path: startDetached succeeds (the no-op script), then
  // recordSuccessfulLaunch fires the InteractionManager-wired callbacks.
  lm->launchItem(scannedPath, /*collectionIndex=*/0);

  // recordItemLaunch / recordHistoryEntry are queued worker writes — QTRY
  // until they land in the real SQLite file.
  QTRY_COMPARE_WITH_TIMEOUT(db->loadItemUsageStats(uuid, scannedPath).playCount, qint64(1),
                            WORKER_WRITE_SETTLE_TIMEOUT_MS);
  QVERIFY(!db->loadItemUsageStats(uuid, scannedPath).lastPlayed.isEmpty());

  QTRY_COMPARE_WITH_TIMEOUT(db->historyEntryCount(), qint64(1), WORKER_WRITE_SETTLE_TIMEOUT_MS);
  const auto history = db->loadRecentHistory(10);
  QCOMPARE(history.size(), 1);
  QCOMPARE(history.first().path, scannedPath);
  QCOMPARE(history.first().collectionUuid, uuid);
  QVERIFY(!history.first().launchedAt.isEmpty());

  // The recently-played surface (statistics dialog / smart playlists) must
  // see the same launch.
  const auto recent = db->loadRecentlyPlayedItems(10);
  QCOMPARE(recent.size(), 1);
  QCOMPARE(recent.first().path, scannedPath);
  QCOMPARE(recent.first().playCount, qint64(1));

  // Detached launches cannot measure runtime; total stays zero.
  QCOMPARE(db->loadItemUsageStats(uuid, scannedPath).totalPlaySeconds, qint64(0));
}

void TestLaunchFlow::testTrackedLaunchRecordsPlaySession() {
  // The child must stay alive comfortably past 1s: LaunchManager measures
  // whole seconds (QDateTime::secsTo) FROM the QProcess::started signal
  // stamp, and recordPlaySession refuses non-positive durations. A `sleep 1`
  // child sat exactly on the truncation boundary — on a busy slot (real scan
  // + FTS rebuild churn) the started-signal delivery lands late enough that
  // 1s of child runtime truncates to 0 whole seconds and the session is
  // silently skipped (the elapsed > 0 gate). sleep 2 gives a full second of
  // headroom against delivery latency; the >= 1 assertion below is
  // unchanged.
  LaunchScenario scenario = makeScenario(sleepingLauncherScript());
  QVERIFY2(scenario.valid, "failed to materialize media file + launcher script");
  const CollectionConfig cfg = makeCollection(scenario);
  const QString uuid = collectionUuidFor(cfg);

  KartendTest::MainWindowFixture fixture([&cfg]() { seedCollectionIntoSandbox(cfg); });
  MainWindow *win = fixture.window();
  QCOMPARE(win->m_collections.size(), 1);

  const QString scannedPath = scanAndResolveItemPath(win, uuid);
  QVERIFY2(!scannedPath.isEmpty(), "scan never produced an items row for the collection");

  IDatabaseManager *db = win->getApplicationManager()->getDatabaseManager();
  LaunchManager *lm = win->getApplicationManager()->getInteractionManager()->launchManager();
  QVERIFY(lm);

  // Route through the tracked-QProcess path so the session duration is
  // measured and onPlaySessionEnded fires on child exit.
  win->m_generalSettings.runtimeDetection.runtimeDetectionEnabled = true;

  QSignalSpy started(lm, &LaunchManager::runtimeStarted);
  QSignalSpy finished(lm, &LaunchManager::runtimeFinished);

#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  // Kartend-dhhh6: under TSan, fork() aborts libtsan. Drive the tracked-launch
  // state machine with a non-forking spawner. The 1100 ms started→finished gap
  // is a REAL wait so QDateTime::secsTo measures >= 1 whole second — the
  // session-duration cross-thread bookkeeping (started stamp on one signal,
  // elapsed read + onPlaySessionEnded worker write on the next) runs unchanged,
  // just without a forked child. The real sleeping-launcher path stays covered
  // in non-TSan builds.
  lm->setLauncherSpawnerForTesting(KartendTest::fakeLauncherSpawner(/*runtimeMs=*/1100));
#endif

  lm->launchItem(scannedPath, /*collectionIndex=*/0);

  QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 10000);
  // launch is recorded when the child actually starts (Kartend-yu1e5).
  QTRY_COMPARE_WITH_TIMEOUT(db->loadItemUsageStats(uuid, scannedPath).playCount, qint64(1),
                            WORKER_WRITE_SETTLE_TIMEOUT_MS);

  // The script sleeps ~1s; wait for the child to exit and the queued
  // play-session write to land.
  QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
  QTRY_VERIFY_WITH_TIMEOUT(db->loadItemUsageStats(uuid, scannedPath).totalPlaySeconds >= 1,
                           WORKER_WRITE_SETTLE_TIMEOUT_MS);
}

void TestLaunchFlow::testArchiveExtractionBsdtarSafeArguments() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP("Kartend-dhhh6: extractArchiveToTemp forks the extractor synchronously on the test "
        "thread, and fork() aborts libtsan; the recorded argv is the assertion.");
#endif
#ifdef Q_OS_WIN
  QSKIP("The argv-recording stub extractor is a POSIX shell script");
#else
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const StubbedExtraction run = runStubbedExtraction(root, QStringLiteral("bsdtar"));

  // Listing (the ArchiveSafety pre-scan) and extraction are separate runs of
  // the same tool: two listings, then exactly one extraction.
  QCOMPARE(run.invocations.size(), 3);
  QCOMPARE(run.invocations.at(0).args, (QStringList{QStringLiteral("-tvf"), run.archivePath}));
  QCOMPARE(run.invocations.at(1).args, (QStringList{QStringLiteral("-tf"), run.archivePath}));
  const ToolInvocation &extraction = run.invocations.at(2);

  // bsdtar's default extract refuses ".." and absolute paths and will not
  // write through a symlink; -P (--absolute-paths) disables all three, and
  // --insecure is its long-standing alias. A short-option cluster carrying P
  // (e.g. -xPf) relaxes it just the same.
  for (const QString &arg : extraction.args) {
    QVERIFY2(arg != QLatin1String("--absolute-paths") && arg != QLatin1String("--insecure"),
             qPrintable(QStringLiteral("unsafe bsdtar extraction switch: ") + arg));
    const bool shortCluster =
        arg.startsWith(QLatin1Char('-')) && !arg.startsWith(QLatin1String("--"));
    QVERIFY2(!(shortCluster && arg.contains(QLatin1Char('P'))),
             qPrintable(QStringLiteral("unsafe bsdtar extraction switch: ") + arg));
  }
  QCOMPARE(extraction.args, (QStringList{QStringLiteral("-xf"), run.archivePath}));

  verifyExtractionReached(run, extraction);
#endif
}

void TestLaunchFlow::testArchiveExtraction7zSafeArguments() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP("Kartend-dhhh6: extractArchiveToTemp forks the extractor synchronously on the test "
        "thread, and fork() aborts libtsan; the recorded argv is the assertion.");
#endif
#ifdef Q_OS_WIN
  QSKIP("The argv-recording stub extractor is a POSIX shell script");
#else
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const StubbedExtraction run = runStubbedExtraction(root, QStringLiteral("7z"));

  // One listing (the ArchiveSafety pre-scan), then exactly one extraction.
  QCOMPARE(run.invocations.size(), 2);
  QCOMPARE(run.invocations.at(0).args,
           (QStringList{QStringLiteral("l"), QStringLiteral("-slt"), run.archivePath}));
  const ToolInvocation &extraction = run.invocations.at(1);

  // 7z's link-relaxing switches: -snld (and -snld20 etc.) lets extracted
  // symlinks point outside the target, -snl stores/restores links as links,
  // -snh does the same for hardlinks, and -spf keeps absolute paths. Matched
  // case-insensitively because 7z parses switches that way.
  const QStringList forbiddenPrefixes{QStringLiteral("-snl"), QStringLiteral("-snh"),
                                      QStringLiteral("-spf")};
  for (const QString &arg : extraction.args) {
    for (const QString &prefix : forbiddenPrefixes) {
      QVERIFY2(!arg.startsWith(prefix, Qt::CaseInsensitive),
               qPrintable(QStringLiteral("unsafe 7z extraction switch: ") + arg));
    }
  }
  QCOMPARE(extraction.args,
           (QStringList{QStringLiteral("x"), QStringLiteral("-y"), run.archivePath}));

  verifyExtractionReached(run, extraction);
#endif
}
