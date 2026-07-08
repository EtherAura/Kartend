// QProcess spawn/track/watch half of LaunchManager, split out of
// launchmanager.cpp along its pure/process seam (the pure command
// construction lives in launchcommandbuilder.cpp). Same class, separate
// translation unit — the launchmanagerarchive.cpp / mainwindow_*.cpp partials
// convention — because this half is inseparable from the manager's QObject
// state: it emits the manager's runtimeStarted/runtimeFinished and
// detachedSessionStarted/Ended signals,
// mutates m_trackedChild / m_survivedDetachedChildren (which the destructor
// in launchmanager.cpp reaps), and routes every spawn through the
// m_launcherSpawner test seam. A standalone QObject runner would have needed
// signal re-plumbing through the manager plus a split ownership story for the
// survived-children list, for no behavioural gain.
#include "cmdexequoting.h"
#include "errorpresentation.h"
#include "errorutils.h"
#include "launchmanager.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

#include <memory>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace {
// Start a launcher process, routing Windows .cmd/.bat batch files through the
// command interpreter. On Windows a batch file is not an executable image:
// CreateProcess (hence QProcess::start, which uses no shell) cannot run it
// directly and fails with FailedToStart — which is why launching a .cmd
// launcher silently never started (Kartend-5i556). Invoke it via %COMSPEC%
// (cmd.exe) /c instead. Real executables and every non-Windows launcher start
// directly, unchanged.
//
// Kartend-9u29e: the QStringList overload's quoting is correct for the final
// CommandLineToArgvW pass but NOT for the intervening cmd.exe interpreter,
// which still acts on `& ( ) < > | % ! ^ "` — so a media name or
// `%collection%`-expanded value like "Sonic & Knuckles (USA).rom" would be
// mis-parsed or could inject into the batch interpreter. Build the command
// line by hand with CmdExeQuoting::quoteForCmdExe (CommandLineToArgvW quoting
// + cmd.exe caret-escaping) and hand it to QProcess verbatim via
// setNativeArguments; the QStringList overload would re-quote and corrupt our
// cmd-escaped tokens. Runtime confirmation of the cmd.exe round-trip is a
// manual MSVC-CI check (headless Linux CI cannot exercise it).
void startLauncherProcess(QProcess *child, const QString &launcherPath, const QStringList &args) {
#ifdef Q_OS_WIN
  const QString suffix = QFileInfo(launcherPath).suffix().toLower();
  if (suffix == QLatin1String("cmd") || suffix == QLatin1String("bat")) {
    QString comspec = qEnvironmentVariable("COMSPEC");
    if (comspec.isEmpty()) {
      comspec = QStringLiteral("cmd.exe");
    }
    QString nativeArgs = QStringLiteral("/c ") + CmdExeQuoting::quoteForCmdExe(launcherPath);
    for (const QString &arg : args) {
      nativeArgs += QLatin1Char(' ') + CmdExeQuoting::quoteForCmdExe(arg);
    }
    child->setProgram(comspec);
    child->setNativeArguments(nativeArgs);
    child->start();
    return;
  }
#endif
  child->start(launcherPath, args);
}
} // namespace

void LaunchManager::spawnLauncherProcess(QProcess *child, const QString &launcherPath,
                                         const QStringList &args) {
  // Kartend-dhhh6: a test may substitute the spawn to synthesize the
  // started/finished lifecycle without fork()/exec() (so the launch
  // concurrency state machine runs under ThreadSanitizer). Null in production.
  if (m_launcherSpawner) {
    m_launcherSpawner(child, launcherPath, args);
    return;
  }
  startLauncherProcess(child, launcherPath, args);
}

bool LaunchManager::launchDetachedWatched(const QString &launcherPath, const LaunchCommand &cmd,
                                          const QString &originalFilePath,
                                          const QString &extractedDir,
                                          const QString &collectionUuid) {
  // Kartend-fqsv0: the historical detached path used QProcess::startDetached,
  // which only reports whether the *spawn* succeeded — a launcher that starts
  // and immediately dies (bad core, unreadable media, missing codec) was
  // indistinguishable from success, surfaced no error, and still inflated
  // play_count. Spawn an OWNED QProcess instead and keep an errorOccurred /
  // early-finished handler armed for a short window so an immediate failure is
  // reported and the play_count increment suppressed. This is a watcher, not a
  // tracked session: we never measure duration, and once the window elapses
  // with the child alive we record success and let the child run on
  // (fire-and-forget) until it exits on its own.

  // Kartend-3232r.1: mirror launchTracked's single-child rejection while the
  // previous detached session is still live. Gamepad input is suspended for
  // the session's duration, but keyboard/mouse still reach the frontend and a
  // Confirm past the 500ms debounce used to spawn a second child (inflating
  // play_count). The block lifts at the child's final finished or when
  // MainWindow's focus backstop calls releaseDetachedLaunchBlock().
  if (m_detachedSessionActive && m_activeDetachedChild) {
    ErrorPresentation::showError(
        nullptr, ErrorContext::info(
                     ErrorCode::OperationCancelled,
                     tr("Another launched item appears to be running:\n%1").arg(m_detachedFilePath),
                     QStringLiteral("LaunchManager::launchDetachedWatched")));
    // The caller (finishLaunch) has already dismissed its extraction scope
    // guard for this path, so the reject owns reclaiming the extracted dir.
    if (!extractedDir.isEmpty()) {
      QDir(extractedDir).removeRecursively();
    }
    return false;
  }

  auto *child = new QProcess(this);

  // Mirror the tracked path: pin CWD to the launcher's own directory so
  // sibling DLLs / config files resolve (Kartend-bmvu), and detach stdio so a
  // chatty launcher can't fill our pipes.
  child->setWorkingDirectory(QFileInfo(launcherPath).absolutePath());
  child->setProcessChannelMode(QProcess::ForwardedChannels);
  child->setInputChannelMode(QProcess::ForwardedInputChannel);

  // Shared so the window timer and the QProcess handlers settle the outcome
  // exactly once — whichever fires first (early failure vs. window elapsing)
  // wins; the loser becomes a no-op.
  auto settled = std::make_shared<bool>(false);

  auto reclaimExtraction = [extractedDir]() {
    if (!extractedDir.isEmpty()) {
      QDir(extractedDir).removeRecursively();
    }
  };

  // The window timer is parented to the child so it dies with it; capture by
  // value detaches the QStrings from the (soon-dead) reference params.
  auto *window = new QTimer(child);
  window->setSingleShot(true);
  window->setInterval(kEarlyFailureWindowMs);

  connect(window, &QTimer::timeout, this,
          [this, settled, originalFilePath, collectionUuid, child]() {
            if (*settled) {
              return;
            }
            *settled = true;
            // Survived the window with the spawn intact → genuine launch.
            // Record it once, then forget the child: drop our slots so a later
            // exit is silent, and let the QProcess self-delete when it ends.
            recordSuccessfulLaunch(originalFilePath, collectionUuid);
            child->disconnect();
            // Sever QObject ownership NOW: left parented to this manager, the
            // child would be destroyed in ~LaunchManager at frontend shutdown,
            // and ~QProcess kills a still-running process — closing the
            // frontend must not terminate the user's program mid-session
            // (the historical startDetached contract). Track the orphan so
            // the destructor can reap it if it exits before we do.
            child->setParent(nullptr);
            m_survivedDetachedChildren.removeAll(nullptr);
            m_survivedDetachedChildren.append(child);
            connect(child, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), child,
                    [child](int, QProcess::ExitStatus) { child->deleteLater(); });
            // Kartend-3232r.1: re-arm the balanced detachedSessionEnded for
            // the child's REAL exit — the attract/gamepad suspend wiring
            // waits on it. Contexted on `this` (unlike the deleteLater above,
            // which must survive us): the orphan outlives the manager at
            // shutdown, and a `this` capture firing then would dangle.
            connect(child, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                    [this, child, originalFilePath](int, QProcess::ExitStatus) {
                      if (m_activeDetachedChild == child) {
                        m_detachedSessionActive = false;
                        emit detachedSessionEnded(originalFilePath);
                      }
                    });
          });

  connect(child, &QProcess::errorOccurred, this,
          [this, settled, child, cmd, launcherPath, originalFilePath,
           reclaimExtraction](QProcess::ProcessError error) {
            if (*settled) {
              return;
            }
            // FailedToStart is terminal here (parity with the old startDetached
            // false-return). Other errors before the window elapses are also
            // treated as an early failure — the child is gone or unusable.
            if (error == QProcess::FailedToStart) {
              *settled = true;
              const QString errorMsg =
                  QString("Failed to launch: %1\n\nCommand attempted:\n%2 %3\n\nMake "
                          "sure the launcher path is correct and the file is executable.")
                      .arg(launcherPath)
                      .arg(launcherPath)
                      .arg(cmd.arguments.join(" "));
              ErrorPresentation::showError(
                  nullptr,
                  ErrorContext::critical(ErrorCode::UnknownError, errorMsg,
                                         QStringLiteral("LaunchManager::launchDetachedWatched")));
              reclaimExtraction();
              // Balanced ended for the suspend wiring (started was emitted
              // just before the spawn call). Guarded so a superseded child
              // can't close a newer session's pair.
              if (m_activeDetachedChild == child) {
                m_detachedSessionActive = false;
                emit detachedSessionEnded(originalFilePath);
              }
              child->deleteLater();
            }
          });

  connect(child, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, settled, child, originalFilePath, collectionUuid,
           reclaimExtraction](int exitCode, QProcess::ExitStatus status) {
            if (*settled) {
              return;
            }
            // The child died WITHIN the watch window. Per Kartend-fqsv0 only a
            // non-zero exit (or a crash) is a demonstrable failure — report it
            // and suppress the play_count increment. A clean, zero exit this
            // fast is a legitimate short-lived launcher, recorded like a
            // survived-window launch so it isn't a false positive.
            *settled = true;
            const bool failed = (status != QProcess::NormalExit) || (exitCode != 0);
            if (failed) {
              ErrorPresentation::showError(
                  nullptr, ErrorContext::critical(
                               ErrorCode::UnknownError,
                               tr("The launcher started but exited immediately (code %1).\n%2")
                                   .arg(exitCode)
                                   .arg(originalFilePath),
                               QStringLiteral("LaunchManager::launchDetachedWatched")));
              reclaimExtraction();
            } else {
              recordSuccessfulLaunch(originalFilePath, collectionUuid);
            }
            // Balanced ended for the suspend wiring; guarded so a superseded
            // child can't close a newer session's pair.
            if (m_activeDetachedChild == child) {
              m_detachedSessionActive = false;
              emit detachedSessionEnded(originalFilePath);
            }
            child->deleteLater();
          });

  // Kartend-3232r.1: session bookkeeping + the suspend signal BEFORE the
  // spawn call — Windows delivers FailedToStart synchronously inside
  // start(), and the ended emission in the errorOccurred handler above must
  // follow (not precede) detachedSessionStarted.
  m_detachedSessionActive = true;
  m_activeDetachedChild = child;
  m_detachedFilePath = originalFilePath;
  emit detachedSessionStarted(originalFilePath, QFileInfo(originalFilePath).completeBaseName());

  spawnLauncherProcess(child, launcherPath, cmd.arguments);
  window->start();
  // start() is async on Unix; FailedToStart arrives via errorOccurred. Report
  // the spawn as issued (the caller has already dismissed its cleanup guard).
  return true;
}

bool LaunchManager::launchTracked(const QString &launcherPath, const LaunchCommand &cmd,
                                  const QString &filePath, const QString &originalFilePath,
                                  const QString &extractedDir, const QString &collectionUuid) {
  // Only one tracked child at a time; reject overlapping launches so the
  // overlay state stays coherent.
  if (m_trackedChild) {
    ErrorPresentation::showError(
        nullptr, ErrorContext::info(
                     ErrorCode::OperationCancelled,
                     tr("Another tracked item is currently running:\n%1").arg(m_trackedFilePath),
                     QStringLiteral("LaunchManager::launchTracked")));
    return false;
  }

  auto *child = new QProcess(this);
  m_trackedChild = child;
  m_trackedFilePath = filePath;
  m_trackedCollectionUuid = collectionUuid;
  m_trackedStartTime = QDateTime();

  // Detach the child from Kartend's stdio so a busy launcher doesn't fill
  // our pipes (and to avoid blocking on closed channels at exit).
  child->setProcessChannelMode(QProcess::ForwardedChannels);
  child->setInputChannelMode(QProcess::ForwardedInputChannel);

  const QString displayName = QFileInfo(filePath).completeBaseName();

  connect(child, &QProcess::started, this,
          [this, filePath, displayName, originalFilePath, collectionUuid]() {
            // Capture the start moment here rather than at child->start() so the
            // recorded duration reflects actual run time (not queueing delay).
            m_trackedStartTime = QDateTime::currentDateTimeUtc();
            emit runtimeStarted(filePath, displayName);
            // Kartend-yu1e5/5i556: stamp play_count/last_played only once the
            // child ACTUALLY starts. Wired BEFORE start() (unlike the old
            // connect in finishLaunch, made after launchTracked returned), so a
            // synchronously-delivered started() is never missed — QProcess::start
            // emits started inline on Windows (CreateProcess is synchronous),
            // which silently dropped the stat there.
            recordSuccessfulLaunch(originalFilePath, collectionUuid);
          });

  // QProcess emits exactly one of finished() or errorOccurred()-with-FailedToStart
  // before the object is safe to delete. Funnel both through a single cleanup
  // lambda so the UI always sees a balanced started/finished pair.
  auto cleanup = [this, child, filePath]() {
    if (m_trackedChild != child) {
      return; // Already cleaned up.
    }
    // record the session duration before clearing the tracked
    // state. Skip when the child never reached `started` (FailedToStart) —
    // m_trackedStartTime stays default-constructed in that case.
    if (m_trackedStartTime.isValid() && !m_trackedCollectionUuid.isEmpty() &&
        m_onPlaySessionEnded) {
      const qint64 elapsed = m_trackedStartTime.secsTo(QDateTime::currentDateTimeUtc());
      if (elapsed > 0) {
        m_onPlaySessionEnded(m_trackedCollectionUuid, filePath, elapsed);
      }
    }
    m_trackedChild.clear();
    m_trackedFilePath.clear();
    m_trackedCollectionUuid.clear();
    m_trackedStartTime = QDateTime();
    emit runtimeFinished(filePath);
    child->deleteLater();
  };

  connect(child, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [cleanup](int /*code*/, QProcess::ExitStatus /*status*/) { cleanup(); });

  connect(child, &QProcess::errorOccurred, this, [child, cleanup](QProcess::ProcessError error) {
    // Only treat FailedToStart as terminal here — finished() will fire
    // for crashes after start, and we want to keep the overlay up
    // until the process is actually gone.
    if (error == QProcess::FailedToStart) {
      ErrorPresentation::showError(
          nullptr, ErrorContext::critical(
                       ErrorCode::UnknownError,
                       tr("Failed to start tracked launcher:\n%1").arg(child->errorString()),
                       QStringLiteral("LaunchManager::launchTracked")));
      cleanup();
    }
  });

  // Reclaim the extracted dir if the child never runs (tracked + archive +
  // failed start). Wired BEFORE start(), like the started() connect above:
  // Windows delivers FailedToStart synchronously inside start(), and the old
  // post-spawn connect in finishLaunch ran after cleanup() had already
  // cleared m_trackedChild — the hook was never installed and the extraction
  // dir was orphaned. By-value capture detaches the QString from the
  // reference param, which dies before the lambda fires.
  if (!extractedDir.isEmpty()) {
    connect(child, &QProcess::errorOccurred, this, [extractedDir](QProcess::ProcessError error) {
      if (error == QProcess::FailedToStart) {
        QDir(extractedDir).removeRecursively();
      }
    });
  }

  // See the detached-start path above: pin CWD to the launcher's own
  // directory so sibling resources resolve the same way (Kartend-bmvu).
  child->setWorkingDirectory(QFileInfo(launcherPath).absolutePath());
  spawnLauncherProcess(child, launcherPath, cmd.arguments);
  // start() returns void; FailedToStart is reported via errorOccurred.
  return true;
}
