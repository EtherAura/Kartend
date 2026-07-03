#ifndef KARTEND_TESTS_SUPPORT_LAUNCHFAKES_H
#define KARTEND_TESTS_SUPPORT_LAUNCHFAKES_H

// Kartend-dhhh6: non-forking fakes for LaunchManager's test seams, so the
// launch-concurrency tests can run under ThreadSanitizer.
//
// libtsan's ForkAfter slot bookkeeping (GCC 13, tsan_rtl.cpp:253
// CHECK "!thr->slot") aborts the whole process when QProcess forks a child
// while a worker thread is alive — which is exactly the launch path's shape
// (a QtConcurrent extraction worker + the SQLite worker pool are live when the
// launcher/extractor would fork). The affected slots used to be wholesale
// QSKIP'd under TSan, leaving the launch concurrency state machine with zero
// race coverage. These fakes substitute the two spawn points (the launcher
// QProcess and the archive-extraction body) with non-forking stand-ins that
// drive the SAME cross-thread state — signal wiring, session-duration capture,
// the cancel atomic, the QFutureWatcher hand-off — so TSan can observe it.
//
// They are deliberately small and header-only (mirroring the other
// tests/support helpers). Production never sees them: the seams default to
// null and run the real fork()/exec() paths.

#include "errorutils.h"
#include "launchmanager.h"

#include <atomic>

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>

namespace KartendTest {

/// Non-forking launcher spawner. Synthesizes the QProcess lifecycle on the GUI
/// thread via queued single-shot timers parented to the child, so it fires
/// only while the event loop runs and dies with the child. @p runtimeMs is the
/// gap between `started` and `finished`; set it >= 1000 to exercise
/// whole-second session-duration measurement (QDateTime::secsTo from the
/// started stamp). @p exitCode / @p status drive the `finished` signal. The
/// child is the real, fully-wired QProcess the production path would have
/// started — emitting its signals exercises every connected slot, just without
/// a fork.
inline LaunchManager::LauncherSpawnFn
fakeLauncherSpawner(int runtimeMs = 0, int exitCode = 0,
                    QProcess::ExitStatus status = QProcess::NormalExit) {
  // QProcess's signals take a private QPrivateSignal arg, so they can't be
  // emitted with `emit` from outside the class — invoke them by name through
  // the meta-object (moc strips QPrivateSignal from the public signature).
  return [runtimeMs, exitCode, status](QProcess *child, const QString &, const QStringList &) {
    // Init-capture under a fresh name so the inner lambdas don't shadow the
    // enclosing `child` parameter (gcc -Werror=shadow).
    QTimer::singleShot(0, child, [proc = child]() { QMetaObject::invokeMethod(proc, "started"); });
    QTimer::singleShot(runtimeMs, child, [proc = child, exitCode, status]() {
      QMetaObject::invokeMethod(proc, "finished", Q_ARG(int, exitCode),
                                Q_ARG(QProcess::ExitStatus, status));
    });
  };
}

/// Non-forking launcher spawner that fails to start: emits
/// errorOccurred(FailedToStart) on the next event-loop turn, no fork. Drives
/// the FailedToStart cleanup path (extracted-dir reclaim, tracked-state clear).
inline LaunchManager::LauncherSpawnFn fakeFailingLauncherSpawner() {
  return [](QProcess *child, const QString &, const QStringList &) {
    QTimer::singleShot(0, child, [proc = child]() {
      QMetaObject::invokeMethod(proc, "errorOccurred",
                                Q_ARG(QProcess::ProcessError, QProcess::FailedToStart));
    });
  };
}

/// Non-forking launcher spawner that fails to start SYNCHRONOUSLY: emits
/// errorOccurred(FailedToStart) before the spawn call returns — the Windows
/// shape, where CreateProcess fails inside QProcess::start() and the signal
/// is delivered inline (the same synchronous delivery documented for
/// started()). Exercises failure hooks that must be wired BEFORE start();
/// the queued fakeFailingLauncherSpawner above would let a post-spawn
/// connect pass unnoticed.
inline LaunchManager::LauncherSpawnFn fakeSyncFailingLauncherSpawner() {
  return [](QProcess *child, const QString &, const QStringList &) {
    QMetaObject::invokeMethod(child, "errorOccurred",
                              Q_ARG(QProcess::ProcessError, QProcess::FailedToStart));
  };
}

/// Non-forking archive extractor. Runs on the QtConcurrent worker thread (the
/// same place the real extractArchiveToTemp runs) and busy-waits in bounded
/// 50 ms steps while polling the SAME cancel atomic the real extractor polls —
/// so the GUI-thread cancelExtraction()/destructor hand-off is the genuine
/// cross-thread interaction TSan observes, with no extractor child forked.
///
/// To mirror the real cleanup contract it creates @p extractionDir up front
/// and removes it on cancel; if it survives the full @p maxSleepMs it writes a
/// stub @p extractedFileName inside and returns that path (a successful
/// extraction). Pass a large @p maxSleepMs for "stays in flight until
/// cancelled" tests.
inline LaunchManager::ArchiveExtractFn fakeSleepyExtractor(const QString &extractionDir,
                                                           const QString &extractedFileName,
                                                           int maxSleepMs = 30000) {
  return [extractionDir, extractedFileName,
          maxSleepMs](const QString &, const QString &,
                      const std::atomic_bool *cancel) -> ErrorUtils::Result<QString> {
    QDir().mkpath(extractionDir);
    // Deliberately model a slow extraction so the launch path's cancel-token
    // polling is exercisable: sleep in 50ms steps (the cancel-check granularity)
    // up to the caller-supplied maxSleepMs, returning the instant cancel is
    // observed. This is bounded latency simulation, NOT a synchronization wait —
    // a consumer either cancels deterministically or runs the full bounded
    // maxSleepMs, so it can't flake under parallel ctest load (Kartend audit T-07).
    const int steps = maxSleepMs / 50;
    for (int i = 0; i < steps; ++i) {
      if (cancel != nullptr && cancel->load()) {
        QDir(extractionDir).removeRecursively();
        return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::OperationCancelled,
                                               QStringLiteral("extraction cancelled (fake)"),
                                               QStringLiteral("KartendTest::fakeSleepyExtractor"));
      }
      QThread::msleep(50);
    }
    const QString out = QDir(extractionDir).filePath(extractedFileName);
    QFile f(out);
    if (f.open(QIODevice::WriteOnly)) {
      f.write("FAKE");
      f.close();
    }
    return out;
  };
}

} // namespace KartendTest

#endif // KARTEND_TESTS_SUPPORT_LAUNCHFAKES_H
