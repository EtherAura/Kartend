#include "test_launchflow.h"

#include "applicationcontext.h"
#include "applicationmanager.h"
#include "archivesafety.h"
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
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include <utility>

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

/// Kartend-ecjq6: one member of a POSIX ustar archive built at runtime, so the
/// hostile layouts are spelled out here rather than committed as binaries.
struct TarMember {
  QByteArray name;
  char type = '0'; // '0' regular file, '2' symlink
  QByteArray linkTarget;
  QByteArray content;
};

void putTarOctal(QByteArray &header, int offset, int width, qint64 value) {
  // width - 1 zero-padded octal digits; the field's last byte stays NUL.
  header.replace(offset, width - 1, QByteArray::number(value, 8).rightJustified(width - 1, '0'));
}

/// Serialises @p members as a ustar stream. Empty when a name or link target
/// does not fit its 100-byte header field (caller asserts non-empty).
QByteArray buildTar(const QList<TarMember> &members) {
  constexpr int kBlock = 512;
  QByteArray tar;
  for (const TarMember &m : members) {
    if (m.name.size() >= 100 || m.linkTarget.size() >= 100) {
      return {};
    }
    QByteArray header(kBlock, '\0');
    header.replace(0, m.name.size(), m.name);
    putTarOctal(header, 100, 8, m.type == '2' ? 0777 : 0644);
    putTarOctal(header, 108, 8, 0);
    putTarOctal(header, 116, 8, 0);
    putTarOctal(header, 124, 12, m.content.size());
    putTarOctal(header, 136, 12, 1767225600); // 2026-01-01T00:00:00Z
    header[156] = m.type;
    header.replace(157, m.linkTarget.size(), m.linkTarget);
    header.replace(257, 6, QByteArray("ustar\0", 6));
    header.replace(263, 2, QByteArrayLiteral("00"));
    // The checksum is summed with its own field read as eight spaces.
    header.replace(148, 8, QByteArray(8, ' '));
    unsigned sum = 0;
    for (const char c : std::as_const(header)) {
      sum += static_cast<unsigned char>(c);
    }
    header.replace(148, 6, QByteArray::number(sum, 8).rightJustified(6, '0'));
    header[154] = '\0';
    header[155] = ' ';
    tar += header;
    tar += m.content;
    tar += QByteArray((kBlock - m.content.size() % kBlock) % kBlock, '\0');
  }
  tar += QByteArray(2 * kBlock, '\0');
  return tar;
}

QByteArray shellSingleQuoted(const QString &text) {
  QByteArray quoted = text.toUtf8();
  quoted.replace('\'', "'\\''");
  return QByteArrayLiteral("'") + quoted + QByteArrayLiteral("'");
}

/// Kartend-ecjq6: a TEST-ONLY bsdtar wrapper for the real-extractor cases. It
/// logs every invocation like stubExtractorScript, answers the two ArchiveSafety
/// listing argvs (-tvf / -tf) with a clean one-entry listing, and hands every
/// other invocation — the extraction — to @p realBsdtar via exec with argv,
/// cwd, stdio and exit status untouched. The listing answer deliberately
/// bypasses the pre-extraction scan (which would refuse the hostile archives
/// before bsdtar ever wrote a byte), so what these cases prove is bsdtar's own
/// extract-time containment on the production extractArchiveToTemp path.
QByteArray realBsdtarWrapperScript(const QString &realBsdtar, const QString &logPath) {
  return QByteArrayLiteral("#!/bin/sh\nlog=") + shellSingleQuoted(logPath) +
         QByteArrayLiteral("\nreal=") + shellSingleQuoted(realBsdtar) +
         QByteArrayLiteral(
             "\n"
             "printf 'cwd=%s\\n' \"$(pwd -P)\" >> \"$log\"\n"
             "for a in \"$@\"; do printf 'arg=%s\\n' \"$a\" >> \"$log\"; done\n"
             "printf 'end\\n' >> \"$log\"\n"
             "case \"$1\" in\n"
             "  -tvf) printf '%s\\n' '-rw-r--r--  0 0      0           3 Jan  1  2026 disc.iso'; "
             "exit 0 ;;\n"
             "  -tf) printf '%s\\n' 'disc.iso'; exit 0 ;;\n"
             "esac\n"
             "exec \"$real\" \"$@\"\n");
}

/// The installed bsdtar, resolved on the unmodified PATH (before any test
/// narrows it), so the wrapper can never exec itself. Empty when absent.
QString resolveRealBsdtar() {
  const QString found = QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
  return found.isEmpty() ? QString() : QFileInfo(found).canonicalFilePath();
}

QString realBsdtarVersion(const QString &realBsdtar) {
  QProcess probe;
  probe.start(realBsdtar, {QStringLiteral("--version")});
  if (!probe.waitForFinished(10000)) {
    return QStringLiteral("<no answer>");
  }
  return QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
}

constexpr auto kOutsideDirName = "outside dir";
constexpr auto kInvocationLogName = "bsdtar invocations.log";

/// Every path under @p rootPath, relative and sorted. Symlinks are listed, not
/// followed, so a dangling or escaping link the extraction left behind shows up.
QStringList snapshotTree(const QString &rootPath) {
  QStringList paths;
  const QDir rootDir(rootPath);
  QDirIterator it(rootPath, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    paths << rootDir.relativeFilePath(it.next());
  }
  paths.sort();
  return paths;
}

/// Everything one real-bsdtar extraction run leaves behind for the assertions.
struct RealBsdtarRun {
  QString setupError;
  QString archivePath;      // the archive handed to extractArchiveToTemp
  QString extractionParent; // canonical <cache>/kartend_extract
  QString extractionDir;    // <cache>/kartend_extract/<base>; gone again on failure
  QString resultPath;       // canonical result on success, empty on error
  QString errorMessage;
  QString errorDetails;
  QList<ToolInvocation> invocations;
};

/// Writes @p tar as media/@p archiveFileName under @p root and runs the
/// production extractArchiveToTemp with PATH narrowed to root/bin, which holds
/// only the realBsdtarWrapperScript — so bsdtar is the sole extractor found (7z
/// never joins the fallback) and the bytes are written by @p realBsdtar.
RealBsdtarRun runRealBsdtarExtraction(const QTemporaryDir &root, const QString &realBsdtar,
                                      const QString &archiveFileName, const QByteArray &tar) {
  RealBsdtarRun run;
  const QDir rootDir(root.path());
  if (tar.isEmpty() || !rootDir.mkpath(QStringLiteral("bin")) ||
      !rootDir.mkpath(QStringLiteral("media"))) {
    run.setupError = QStringLiteral("could not build the archive or create the fixture dirs");
    return run;
  }
  const QString logPath = rootDir.filePath(QLatin1String(kInvocationLogName));
  run.archivePath =
      QFileInfo(rootDir.filePath(QStringLiteral("media/") + archiveFileName)).absoluteFilePath();
  if (!writeFile(rootDir.filePath(QStringLiteral("bin/bsdtar")),
                 realBsdtarWrapperScript(realBsdtar, logPath), /*executable=*/true) ||
      !writeFile(run.archivePath, tar, /*executable=*/false)) {
    run.setupError = QStringLiteral("could not write the bsdtar wrapper or the archive");
    return run;
  }
  const QString cacheBase = rootDir.filePath(QStringLiteral("cache"));
  run.extractionDir = cacheBase + QStringLiteral("/kartend_extract/") +
                      QFileInfo(run.archivePath).completeBaseName();

  const QByteArray oldPath = qgetenv("PATH");
  qputenv("PATH", rootDir.filePath(QStringLiteral("bin")).toUtf8());
  {
    const auto restorePath = qScopeGuard([&oldPath]() { qputenv("PATH", oldPath); });
    const auto result =
        LaunchManager::extractArchiveToTemp(run.archivePath, QStringLiteral(".iso"), nullptr,
                                            /*maxDecompressedBytes=*/1 << 20, cacheBase);
    if (result.isError()) {
      run.errorMessage = result.error().message;
      run.errorDetails = result.error().details;
    } else {
      run.resultPath = QFileInfo(result.value()).canonicalFilePath();
    }
  }
  run.extractionParent =
      QFileInfo(cacheBase + QStringLiteral("/kartend_extract")).canonicalFilePath();
  run.invocations = readInvocationLog(logPath);
  return run;
}

/// The run went through the wrapper exactly as production drives bsdtar: both
/// pre-scan listings, then one forwarded extraction in the per-archive dir with
/// the plain `-xf <archive>` argv (never -P).
void verifyRealExtractionInvoked(const RealBsdtarRun &run) {
  QVERIFY2(run.setupError.isEmpty(), qPrintable(run.setupError));
  QCOMPARE(run.invocations.size(), 3);
  QCOMPARE(run.invocations.at(0).args, (QStringList{QStringLiteral("-tvf"), run.archivePath}));
  QCOMPARE(run.invocations.at(1).args, (QStringList{QStringLiteral("-tf"), run.archivePath}));
  const ToolInvocation &extraction = run.invocations.at(2);
  QCOMPARE(extraction.args, (QStringList{QStringLiteral("-xf"), run.archivePath}));
  QVERIFY2(!run.extractionParent.isEmpty(), "the extraction root was never created");
  QCOMPARE(extraction.cwd,
           run.extractionParent + QLatin1Char('/') + QFileInfo(run.extractionDir).fileName());
}

/// Shared proof for the hostile cases: the real extractor was reached and
/// failed (not the pre-scan, which the wrapper answered), and the sandbox
/// outside the per-archive dir is exactly what it was before — no new file,
/// directory or link anywhere in it, and the sentinel's bytes untouched.
void verifyHostileExtractionContained(const RealBsdtarRun &run, const QString &rootPath,
                                      const QStringList &treeBefore) {
  verifyRealExtractionInvoked(run);
  if (QTest::currentTestFailed()) {
    return;
  }
  QVERIFY2(run.resultPath.isEmpty(),
           qPrintable(QStringLiteral("hostile archive extracted to ") + run.resultPath));
  QCOMPARE(run.errorMessage, QStringLiteral("Archive extraction failed"));
  QVERIFY2(run.errorDetails.startsWith(QLatin1String("bsdtar exit code: ")) &&
               !run.errorDetails.startsWith(QLatin1String("bsdtar exit code: 0,")),
           qPrintable(run.errorDetails));
  qInfo().noquote() << "real bsdtar refused the hostile archive:" << run.errorDetails;

  // Atomic-on-failure cleanup removed the per-archive dir (links included,
  // never followed); whatever remains must be the fixture plus the dirs and
  // log this harness itself creates.
  const QString base = QFileInfo(run.extractionDir).fileName();
  const QString baseRel = QStringLiteral("cache/kartend_extract/") + base;
  const QStringList harnessOwned{QStringLiteral("bin"),
                                 QStringLiteral("bin/bsdtar"),
                                 QStringLiteral("media"),
                                 QStringLiteral("media/") + QFileInfo(run.archivePath).fileName(),
                                 QLatin1String(kInvocationLogName),
                                 QStringLiteral("cache"),
                                 QStringLiteral("cache/kartend_extract")};
  QStringList treeAfter;
  for (const QString &path : snapshotTree(rootPath)) {
    if (!harnessOwned.contains(path) && path != baseRel &&
        !path.startsWith(baseRel + QLatin1Char('/'))) {
      treeAfter << path;
    }
  }
  QCOMPARE(treeAfter, treeBefore);

  QFile sentinel(QDir(rootPath).filePath(QLatin1String(kOutsideDirName)) +
                 QStringLiteral("/sentinel.txt"));
  QVERIFY(sentinel.open(QIODevice::ReadOnly));
  QCOMPARE(sentinel.readAll(), QByteArrayLiteral("SENTINEL"));
}

/// Creates root/"outside dir"/sentinel.txt — the only thing outside the
/// extraction destination the hostile archives aim at — and returns the
/// sandbox's tree as it stands before the run.
QStringList plantOutsideSentinel(const QTemporaryDir &root) {
  const QDir rootDir(root.path());
  if (!rootDir.mkpath(QLatin1String(kOutsideDirName)) ||
      !writeFile(rootDir.filePath(QLatin1String(kOutsideDirName)) + QStringLiteral("/sentinel.txt"),
                 QByteArrayLiteral("SENTINEL"), /*executable=*/false)) {
    return {};
  }
  return snapshotTree(root.path());
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

// Kartend-ecjq6: the three cases below drive the production extractArchiveToTemp
// into the INSTALLED bsdtar (see realBsdtarWrapperScript). Everything they
// create — archive, wrapper, log, extraction root and the "outside" sentinel
// the hostile archives aim at — sits inside one QTemporaryDir, and no member
// reaches above it.

void TestLaunchFlow::testArchiveExtractionRealBsdtarExtractsBenignArchive() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP("Kartend-dhhh6: extractArchiveToTemp forks the extractor synchronously on the test "
        "thread, and fork() aborts libtsan.");
#endif
#ifdef Q_OS_WIN
  QSKIP("The bsdtar wrapper is a POSIX shell script");
#else
  const QString realBsdtar = resolveRealBsdtar();
  if (realBsdtar.isEmpty()) {
    QSKIP("bsdtar is not installed");
  }
  qInfo().noquote() << "real bsdtar:" << realBsdtar << "-" << realBsdtarVersion(realBsdtar);
  QTemporaryDir root(QDir::tempPath() + QStringLiteral("/kartend ecjq6 XXXXXX"));
  QVERIFY(root.isValid());

  // Bytes the wrapper never writes: finding them extracted proves the real
  // bsdtar ran, so a missing tool or a broken wrapper cannot pass as
  // "containment" in the hostile cases.
  const QByteArray payload = QByteArrayLiteral("payload written by the real bsdtar\n");
  const QByteArray nested = QByteArrayLiteral("nested member\n");
  const QByteArray tar = buildTar({{QByteArrayLiteral("disc.iso"), '0', {}, payload},
                                   {QByteArrayLiteral("extras dir/read me.txt"), '0', {}, nested}});
  const RealBsdtarRun run =
      runRealBsdtarExtraction(root, realBsdtar, QStringLiteral("benign game.tar"), tar);

  verifyRealExtractionInvoked(run);
  QVERIFY2(run.errorMessage.isEmpty(),
           qPrintable(run.errorMessage + QStringLiteral(": ") + run.errorDetails));
  const QString extractionDir = QFileInfo(run.extractionDir).canonicalFilePath();
  QVERIFY(!extractionDir.isEmpty());
  QCOMPARE(run.resultPath, extractionDir + QStringLiteral("/disc.iso"));
  QFile disc(run.resultPath);
  QVERIFY(disc.open(QIODevice::ReadOnly));
  QCOMPARE(disc.readAll(), payload);
  QFile readMe(extractionDir + QStringLiteral("/extras dir/read me.txt"));
  QVERIFY(readMe.open(QIODevice::ReadOnly));
  QCOMPARE(readMe.readAll(), nested);

  // The production pre-scan passes this archive with the real bsdtar too.
  QVERIFY(!ArchiveSafety::scanArchiveEntriesWithTool(run.archivePath, QStringLiteral("bsdtar"))
               .isError());
#endif
}

void TestLaunchFlow::testArchiveExtractionRealBsdtarRefusesSymlinkChainEscape() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP("Kartend-dhhh6: extractArchiveToTemp forks the extractor synchronously on the test "
        "thread, and fork() aborts libtsan.");
#endif
#ifdef Q_OS_WIN
  QSKIP("The bsdtar wrapper is a POSIX shell script");
#else
  const QString realBsdtar = resolveRealBsdtar();
  if (realBsdtar.isEmpty()) {
    QSKIP("bsdtar is not installed");
  }
  qInfo().noquote() << "real bsdtar:" << realBsdtar << "-" << realBsdtarVersion(realBsdtar);
  QTemporaryDir root(QDir::tempPath() + QStringLiteral("/kartend ecjq6 XXXXXX"));
  QVERIFY(root.isValid());
  const QStringList treeBefore = plantOutsideSentinel(root);
  QVERIFY(!treeBefore.isEmpty());

  // hop1 -> hop2 -> hop3 -> ../../../outside dir. Seen from the per-archive
  // dir <root>/cache/kartend_extract/<base>, three ".." land exactly on the
  // sandbox root, so the chain ends at the sentinel dir and nowhere above.
  const QString archiveName = QStringLiteral("symlink chain.tar");
  const QByteArray escape = QByteArrayLiteral("../../../") + kOutsideDirName;
  const QString destination =
      QDir(root.path()).filePath(QStringLiteral("cache/kartend_extract/symlink chain"));
  const QString outsideDir = QDir(root.path()).filePath(QLatin1String(kOutsideDirName));
  QCOMPARE(QDir::cleanPath(destination + QLatin1Char('/') + QString::fromUtf8(escape)),
           QDir::cleanPath(outsideDir));

  const QByteArray tar = buildTar({
      {QByteArrayLiteral("disc.iso"), '0', {}, QByteArrayLiteral("ISO")},
      {QByteArrayLiteral("hop1"), '2', QByteArrayLiteral("hop2"), {}},
      {QByteArrayLiteral("hop2"), '2', QByteArrayLiteral("hop3"), {}},
      {QByteArrayLiteral("hop3"), '2', escape, {}},
      // Written THROUGH the chain: a new file, and an overwrite of the sentinel.
      {QByteArrayLiteral("hop1/escaped.txt"), '0', {}, QByteArrayLiteral("ESCAPED")},
      {QByteArrayLiteral("hop1/sentinel.txt"), '0', {}, QByteArrayLiteral("OVERWRITTEN")},
  });
  const RealBsdtarRun run = runRealBsdtarExtraction(root, realBsdtar, archiveName, tar);

  // An escape would add outside dir/escaped.txt (tree comparison) or change
  // the sentinel's bytes (content comparison); a success return would mean
  // bsdtar accepted writing through the chain.
  verifyHostileExtractionContained(run, root.path(), treeBefore);
  QVERIFY(!QFileInfo::exists(outsideDir + QStringLiteral("/escaped.txt")));

  // The layer the wrapper bypassed: the real pre-scan refuses this archive.
  const auto scan =
      ArchiveSafety::scanArchiveEntriesWithTool(run.archivePath, QStringLiteral("bsdtar"));
  QVERIFY(scan.isError());
  QVERIFY(ArchiveSafety::isSecurityRejection(scan.error()));
#endif
}

void TestLaunchFlow::testArchiveExtractionRealBsdtarRefusesDotDotEscape() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP("Kartend-dhhh6: extractArchiveToTemp forks the extractor synchronously on the test "
        "thread, and fork() aborts libtsan.");
#endif
#ifdef Q_OS_WIN
  QSKIP("The bsdtar wrapper is a POSIX shell script");
#else
  const QString realBsdtar = resolveRealBsdtar();
  if (realBsdtar.isEmpty()) {
    QSKIP("bsdtar is not installed");
  }
  qInfo().noquote() << "real bsdtar:" << realBsdtar << "-" << realBsdtarVersion(realBsdtar);
  QTemporaryDir root(QDir::tempPath() + QStringLiteral("/kartend ecjq6 XXXXXX"));
  QVERIFY(root.isValid());
  const QStringList treeBefore = plantOutsideSentinel(root);
  QVERIFY(!treeBefore.isEmpty());

  // One member per ".." depth from <root>/cache/kartend_extract/<base>: into
  // kartend_extract, into cache, and (three up, the sandbox root) into the
  // sentinel dir — never deeper than the QTemporaryDir.
  const QDir rootDir(root.path());
  const QByteArray up3 = QByteArrayLiteral("../../../") + kOutsideDirName;
  const QByteArray tar = buildTar({
      {QByteArrayLiteral("disc.iso"), '0', {}, QByteArrayLiteral("ISO")},
      {QByteArrayLiteral("../d1.txt"), '0', {}, QByteArrayLiteral("ESCAPED")},
      {QByteArrayLiteral("../../d2.txt"), '0', {}, QByteArrayLiteral("ESCAPED")},
      {up3 + QByteArrayLiteral("/escaped.txt"), '0', {}, QByteArrayLiteral("ESCAPED")},
      {up3 + QByteArrayLiteral("/sentinel.txt"), '0', {}, QByteArrayLiteral("OVERWRITTEN")},
  });
  const RealBsdtarRun run =
      runRealBsdtarExtraction(root, realBsdtar, QStringLiteral("dot dot.tar"), tar);

  verifyHostileExtractionContained(run, root.path(), treeBefore);
  QVERIFY(!QFileInfo::exists(rootDir.filePath(QStringLiteral("cache/kartend_extract/d1.txt"))));
  QVERIFY(!QFileInfo::exists(rootDir.filePath(QStringLiteral("cache/d2.txt"))));
  QVERIFY(!QFileInfo::exists(rootDir.filePath(QLatin1String(kOutsideDirName)) +
                             QStringLiteral("/escaped.txt")));

  // The layer the wrapper bypassed: the real pre-scan refuses this archive.
  const auto scan =
      ArchiveSafety::scanArchiveEntriesWithTool(run.archivePath, QStringLiteral("bsdtar"));
  QVERIFY(scan.isError());
  QVERIFY(ArchiveSafety::isSecurityRejection(scan.error()));
#endif
}
