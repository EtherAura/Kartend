// Launches media items with configured launchers, handling libretro cores and
// parameters.
#include "launchmanager.h"
#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/launcherpreset.h"
#include "collection/typehelpers.h"
#include "configvalidation.h"
#include "errorpresentation.h"
#include "errorutils.h"
#include "launchcommandbuilder.h"
#include "pathutils.h"
#include "setuputils.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <memory>
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcLaunchManager, "kartend.launchmanager")
#define debugLog(msg)                                                                              \
  do {                                                                                             \
    if (lcLaunchManager().isDebugEnabled()) {                                                      \
      qCDebug(lcLaunchManager) << msg;                                                             \
    }                                                                                              \
  } while (0)

SETUP_GETTER_DEF_COL_SAME(LaunchManagerSetup, QList<CollectionConfig> *, Collections, collections)

LaunchManager::LaunchManager(QObject *parent) : QObject(parent) {}

LaunchManager::~LaunchManager() {
  // Kartend-mkcak: never abandon a running extractor child. Setting the
  // cancel flag makes the extraction watchdog kill the child within one poll
  // interval; the waitForFinished below is therefore bounded (poll interval +
  // kill grace), not the full extraction time.
  if (m_extractionCancel) {
    m_extractionCancel->store(true);
  }
  if (m_extractionActive && m_extractionFuture.isValid()) {
    m_extractionFuture.waitForFinished();
  }
  // Fire-and-forget children were reparented away from this manager once they
  // survived their watch window (see launchDetachedWatched) precisely so this
  // destructor can't SIGKILL them. Delete only the ones that already exited —
  // their queued finished→deleteLater dies with the event loop, so without
  // this they'd leak even in clean shutdowns (and in tests). Still-running
  // children are intentionally abandoned: the OS reparents and reaps them,
  // the same accepted leak-at-shutdown idiom the manager threads use.
  for (const QPointer<QProcess> &child : std::as_const(m_survivedDetachedChildren)) {
    if (child && child->state() == QProcess::NotRunning) {
      delete child;
    }
  }
}

void LaunchManager::setupReferences(const LaunchManagerSetup &setup) {
  m_ctx = setup.ctx;
  m_collections = setup.getCollections();
  if (m_ctx) {
    m_generalSettings = m_ctx->collection.generalSettings;
  }
  m_onLaunched = setup.onLaunched;
  m_onPlaySessionEnded = setup.onPlaySessionEnded;
  m_resolveLauncherOverride = setup.resolveLauncherOverride;
  // Only overwrite a previously-injected chooser callback when the setup
  // struct actually carries one — setChooseLauncherCallback() may run first.
  if (setup.chooseLauncher) {
    m_chooseLauncher = setup.chooseLauncher;
  }
}

void LaunchManager::setChooseLauncherCallback(
    std::function<int(const QString &, const QStringList &, int)> callback) {
  m_chooseLauncher = std::move(callback);
}

int LaunchManager::promptLauncherChoice(const QString &collectionName,
                                        const QStringList &launcherNames, int defaultIndex) {
  if (!m_chooseLauncher) {
    return -1;
  }
  return m_chooseLauncher(collectionName, launcherNames, defaultIndex);
}

bool LaunchManager::runtimeDetectionEnabled() const {
  return m_generalSettings && m_generalSettings->runtimeDetection.runtimeDetectionEnabled;
}

QString LaunchManager::resolveCollectionUuid(int collectionIndex) const {
  if (!m_collections || collectionIndex < 0 || collectionIndex >= m_collections->size()) {
    return {};
  }
  const CollectionConfig &collection = (*m_collections)[collectionIndex];
  // Mirrors how DetailsPaneManager keys per-item rows: name + expanded media dir.
  // Uses the same helper so UUIDs stay consistent across reads/writes.
  const QString expandedMediaDir =
      PathUtils::validateAndExpandPath(collection.mediaDirectory, collection.name);
  return CollectionUtils::computeCollectionUuid(collection.name, expandedMediaDir);
}

void LaunchManager::recordSuccessfulLaunch(const QString &filePath, const QString &collectionUuid) {
  if (collectionUuid.isEmpty() || filePath.isEmpty()) {
    return;
  }
  if (m_onLaunched) {
    m_onLaunched(collectionUuid, filePath);
  }
}

bool LaunchManager::canLaunch(const QString &filePath) const {
  if (!m_lastLaunchTimes.contains(filePath)) {
    return true;
  }
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  return (now - m_lastLaunchTimes[filePath]) >= kDoubleLaunchGuardMs;
}

void LaunchManager::recordLaunch(const QString &filePath) {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  // Bounded pruning: an entry is meaningless kDoubleLaunchGuardMs after
  // insertion, so drop lapsed ones instead of accumulating one per
  // ever-launched path for the life of a (possibly kiosk-long) session.
  // Debounce semantics are untouched — canLaunch already treats a lapsed
  // entry exactly like a missing one.
  for (auto it = m_lastLaunchTimes.begin(); it != m_lastLaunchTimes.end();) {
    if (now - it.value() >= kDoubleLaunchGuardMs) {
      it = m_lastLaunchTimes.erase(it);
    } else {
      ++it;
    }
  }
  m_lastLaunchTimes[filePath] = now;
}

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using ErrorUtils::Result;

// The pure command/parameter-construction bodies live in
// launchcommandbuilder.cpp; these statics stay as thin delegations so every
// existing caller and test keeps its entry point (and signature) unchanged.
// The QProcess spawn/track/watch half (spawnLauncherProcess, launchTracked,
// launchDetachedWatched) lives in launchmanager_process.cpp.

auto LaunchManager::buildLaunchCommand(const LauncherConfig &launcher,
                                       const QString &collectionName, const QString &filePath)
    -> ErrorUtils::Result<LaunchCommand> {
  return LaunchCommandBuilder::buildLaunchCommand(launcher, collectionName, filePath);
}

auto LaunchManager::previewLaunchCommand(const CollectionConfig &collection,
                                         const LauncherConfig &launcher, const QString &filePath)
    -> LaunchPreview {
  return LaunchCommandBuilder::previewLaunchCommand(collection, launcher, filePath);
}

auto LaunchManager::validatePathSecurity(const QString &path) -> Result<void> {
  // Delegate to shared utility in PathUtils
  return PathUtils::validatePathSecurity(path);
}

auto LaunchManager::validateLauncherPath(const QString &path) -> Result<QString> {
  // First check for shell metacharacters and Unicode issues
  auto securityResult = validatePathSecurity(path);
  if (securityResult.isError()) {
    return securityResult.error();
  }

  QFileInfo info(path);
  QString resolvedPath = path;

  // If not an absolute path, try to resolve via PATH
  if (!info.isAbsolute()) {
    const QString executable = QStandardPaths::findExecutable(path);
    if (executable.isEmpty()) {
      return ErrorContext::error(ErrorCode::FileNotFound, "Launcher command not found in PATH",
                                 "LaunchManager::validateLauncherPath")
          .withDetails(QString("Command '%1' is not in PATH. Specify absolute "
                               "path or install the program.")
                           .arg(path));
    }

    // Resolve to an absolute path for further security validation.
    resolvedPath = executable;
    info.setFile(resolvedPath);
  }

  // Verify the file exists
  if (!info.exists()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "Launcher executable not found",
                               "LaunchManager::validateLauncherPath")
        .withDetails(resolvedPath);
  }

  // Verify the file is executable
  if (!info.isExecutable()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Launcher file is not executable",
                               "LaunchManager::validateLauncherPath")
        .withDetails(resolvedPath);
  }

  // Check original path against sensitive directories BEFORE canonicalization
  // This catches paths like /proc/self/exe which would otherwise resolve to a
  // valid location
  static const QStringList sensitiveDirectories = {"/proc", "/sys", "/dev"};
  for (const QString &sensitive : sensitiveDirectories) {
    if (resolvedPath.startsWith(sensitive + "/") || resolvedPath == sensitive) {
      return ErrorContext::error(ErrorCode::InvalidFilePath,
                                 "Launcher path is in restricted directory",
                                 "LaunchManager::validateLauncherPath")
          .withDetails(QString("Path: %1").arg(resolvedPath));
    }
  }

  // Resolve symlinks and verify the canonical path
  // This prevents symlink-based attacks where a symlink points to an unexpected
  // location
  QString canonicalPath = info.canonicalFilePath();
  if (canonicalPath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Could not resolve canonical path",
                               "LaunchManager::validateLauncherPath")
        .withDetails(path);
  }

  // Ensure canonical path is still in a reasonable location
  // Reject if it resolves to a sensitive system directory (double-check after
  // symlink resolution)
  for (const QString &sensitive : sensitiveDirectories) {
    if (canonicalPath.startsWith(sensitive + "/") || canonicalPath == sensitive) {
      return ErrorContext::error(ErrorCode::InvalidFilePath,
                                 "Launcher path resolves to restricted directory",
                                 "LaunchManager::validateLauncherPath")
          .withDetails(QString("Canonical path: %1").arg(canonicalPath));
    }
  }

  // Verify the canonical path also passes security checks
  // (in case symlink resolution introduced new issues)
  if (canonicalPath != path) {
    auto canonicalSecurityResult = validatePathSecurity(canonicalPath);
    if (canonicalSecurityResult.isError()) {
      return ErrorContext::error(ErrorCode::InvalidFilePath,
                                 "Resolved symlink path failed security validation",
                                 "LaunchManager::validateLauncherPath")
          .withDetails(QString("Original: %1, Canonical: %2").arg(path, canonicalPath));
    }
  }

  return canonicalPath;
}

void LaunchManager::launchItem(const QString &filePath, int collectionIndex, int launcherIndex) {
  if ((!m_collections) || collectionIndex < 0 || collectionIndex >= m_collections->size()) {
    ErrorPresentation::showError(
        nullptr, ErrorContext::warning(ErrorCode::InvalidCollectionContext,
                                       tr("Invalid collection specified."),
                                       QStringLiteral("LaunchManager::launchItem")));
    return;
  }

  // Snapshot everything this launch reads from the collection by value BEFORE
  // the chooser below can run. The chooser callback shows a modal dialog whose
  // nested event loop still runs timers and queued slots; any of them
  // appending to m_collections reallocates the list and dangles a reference
  // taken here. Same hardening as the extraction continuation below, which
  // captures the launch context by value for the same reason.
  const QString collectionName = (*m_collections)[collectionIndex].name;
  const LauncherProfile launcherProfile = (*m_collections)[collectionIndex].launcher;
  const ArchiveOptions archiveOptions = (*m_collections)[collectionIndex].archive;

  // Resolve UUID once: both the detached and tracked paths route stats
  // updates through the same key. Empty UUID skips tracking silently — the
  // launch itself still proceeds. Resolved here (not in finishLaunch) so
  // neither the modal chooser nor the async extraction continuation has to
  // re-dereference m_collections, which a settings edit could have mutated
  // meanwhile.
  const QString collectionUuid = resolveCollectionUuid(collectionIndex);

  // pick which launcher to use. When the caller forced an index
  // (>= 0) we honour it; otherwise prompt the user when there are multiple
  // launchers and fall through to the default for single-launcher collections.
  int resolvedLauncherIndex = launcherIndex;
  // per-item override silently bypasses the chooser. Only consult
  // when the caller hasn't already forced a pick — a UI-driven explicit launch
  // (e.g. context-menu "Launch with…") wins over the persisted override.
  //
  // playlist launches end up here too. The InteractionManager
  // launch surfaces (Enter, double-click, context menu "Launch") all resolve
  // `collectionIndex` to the *source* collection via
  // DatabaseManager::getCollectionIndexForFile(filePath), so by the time we
  // get here the chooser/override path is already keyed off the source — not
  // the playlist's synthetic CollectionConfig (which has launcherCount() == 1
  // and an empty mediaDirectory). No playlist-specific branch needed.
  if (resolvedLauncherIndex < 0 && m_resolveLauncherOverride) {
    if (!collectionUuid.isEmpty()) {
      const int overrideIndex = m_resolveLauncherOverride(collectionUuid, filePath);
      if (overrideIndex >= 0 && overrideIndex < launcherProfile.launcherCount()) {
        resolvedLauncherIndex = overrideIndex;
      }
    }
  }
  if (resolvedLauncherIndex < 0) {
    if (launcherProfile.launcherCount() > 1) {
      QStringList launcherNames;
      launcherNames.reserve(launcherProfile.launcherCount());
      for (int i = 0; i < launcherProfile.launcherCount(); ++i) {
        // resolve preset references so the chooser shows the
        // preset's current name instead of a stale inline copy.
        const LauncherConfig effective = LauncherUtils::resolvePreset(
            launcherProfile.launcherAt(i), m_generalSettings
                                               ? m_generalSettings->launchers.launcherPresets
                                               : QList<LauncherPreset>{});
        launcherNames << (effective.name.trimmed().isEmpty()
                              ? launcherProfile.launcherDisplayName(i)
                              : effective.name.trimmed());
      }
      // The chooser dialog lives in the UI layer; the owner injects a
      // callback that shows it. With no callback wired, fall back to the
      // collection's configured default launcher.
      const int defaultIndex =
          std::clamp(launcherProfile.defaultLauncherIndex, 0, launcherProfile.launcherCount() - 1);
      const int chosen = m_chooseLauncher
                             ? m_chooseLauncher(collectionName, launcherNames, defaultIndex)
                             : defaultIndex;
      if (chosen < 0) {
        return; // User cancelled.
      }
      resolvedLauncherIndex = chosen;
    } else {
      resolvedLauncherIndex = 0;
    }
  }
  if (resolvedLauncherIndex < 0 || resolvedLauncherIndex >= launcherProfile.launcherCount()) {
    resolvedLauncherIndex = 0;
  }
  // resolve preset references at launch time. When the
  // entry's presetId names a registered preset, its fields override the
  // inline ones; otherwise the inline fields are used as-is.
  const LauncherConfig launcher = LauncherUtils::resolvePreset(
      launcherProfile.launcherAt(resolvedLauncherIndex),
      m_generalSettings ? m_generalSettings->launchers.launcherPresets : QList<LauncherPreset>{});

  if (archiveOptions.extractArchives && !archiveOptions.extractedExtension.isEmpty() &&
      isArchiveFile(filePath)) {
    qCDebug(lcLaunchManager) << "Archive extraction enabled for" << filePath;
    // Kartend-mkcak: extraction blocks for up to the full extraction timeout,
    // so it runs on a worker thread; the launch continues in the completion
    // callback on the GUI thread. launchItem returns immediately.
    startExtractionAndLaunch(filePath, archiveOptions.extractedExtension, launcher, collectionName,
                             collectionUuid);
    return;
  }

  finishLaunch(launcher, collectionName, filePath, filePath, QString(), collectionUuid);
}

void LaunchManager::startExtractionAndLaunch(const QString &filePath,
                                             const QString &targetExtension,
                                             const LauncherConfig &launcher,
                                             const QString &collectionName,
                                             const QString &collectionUuid) {
  // Only one extraction at a time; overlapping archive launches would race
  // on the busy state (and potentially on the same cache dir).
  if (m_extractionActive) {
    ErrorPresentation::showError(
        nullptr,
        ErrorContext::info(
            ErrorCode::OperationCancelled,
            tr("Another archive is currently being extracted:\n%1").arg(m_extractionFilePath),
            QStringLiteral("LaunchManager::startExtractionAndLaunch")));
    return;
  }

  m_extractionActive = true;
  m_extractionFilePath = filePath;
  // Fresh flag per extraction, shared with the worker lambda so it outlives
  // this manager if teardown races the worker.
  m_extractionCancel = std::make_shared<std::atomic_bool>(false);
  const std::shared_ptr<std::atomic_bool> cancelFlag = m_extractionCancel;

  // Codebase-standard worker pattern (see coverflowwidget_artwork.cpp):
  // QtConcurrent::run + a QFutureWatcher parented to this manager delivers
  // the result back on the GUI thread; the connection dies with us, so the
  // continuation can safely touch members.
  auto *watcher = new QFutureWatcher<ErrorUtils::Result<QString>>(this);
  connect(watcher, &QFutureWatcher<ErrorUtils::Result<QString>>::finished, this,
          [this, watcher, cancelFlag, launcher, collectionName, collectionUuid, filePath]() {
            watcher->deleteLater();
            const ErrorUtils::Result<QString> result = watcher->result();
            m_extractionActive = false;
            m_extractionFilePath.clear();
            emit extractionFinished(filePath);
            if (result.isError()) {
              // A user-requested cancel is not an error condition — the
              // partial extraction was already cleaned up worker-side; just
              // drop the pending launch quietly.
              if (cancelFlag->load()) {
                qCDebug(lcLaunchManager) << "Archive extraction cancelled for" << filePath;
                return;
              }
              ErrorUtils::logError(result.error());
              ErrorPresentation::showError(nullptr, result.error());
              return;
            }
            const QString &launchFilePath = result.value();
            qCDebug(lcLaunchManager) << "Launching extracted file:" << launchFilePath;
            finishLaunch(launcher, collectionName, filePath, launchFilePath,
                         QFileInfo(launchFilePath).absolutePath(), collectionUuid);
          });

  emit extractionStarted(filePath, QFileInfo(filePath).completeBaseName());
  // Kartend-dhhh6: a test may substitute the extraction body (which runs here
  // on the worker) so the cancel-atomic / QFutureWatcher concurrency can run
  // under ThreadSanitizer without forking an extractor child. The seam is
  // captured by value so the worker holds its own copy. Null in production.
  m_extractionFuture =
      QtConcurrent::run([filePath, targetExtension, cancelFlag, extractor = m_archiveExtractor]() {
        if (extractor) {
          return extractor(filePath, targetExtension, cancelFlag.get());
        }
        return extractArchiveToTemp(filePath, targetExtension, cancelFlag.get());
      });
  watcher->setFuture(m_extractionFuture);
}

void LaunchManager::cancelExtraction() {
  if (m_extractionActive && m_extractionCancel) {
    m_extractionCancel->store(true);
  }
}

void LaunchManager::finishLaunch(const LauncherConfig &launcher, const QString &collectionName,
                                 const QString &originalFilePath, const QString &launchFilePath,
                                 const QString &extractedDir, const QString &collectionUuid) {
  // Cleanup guard for the extracted directory. Dismissed only on a successful
  // launch path; any earlier return below (validation failure, missing
  // launcher binary, failed startDetached) removes the extracted directory so
  // /tmp does not accumulate orphaned archive contents.
  auto cleanupExtraction = qScopeGuard([extractedDir]() {
    if (!extractedDir.isEmpty()) {
      QDir(extractedDir).removeRecursively();
    }
  });

  auto commandResult = buildLaunchCommand(launcher, collectionName, launchFilePath);
  if (commandResult.isError()) {
    ErrorUtils::logError(commandResult.error());
    ErrorPresentation::showError(nullptr, commandResult.error());
    return;
  }

  const LaunchCommand cmd = commandResult.value();

  // Validate launcher path for security before execution.
  // This also resolves PATH commands to a canonical executable.
  auto launcherPathResult = validateLauncherPath(cmd.program);
  if (launcherPathResult.isError()) {
    ErrorUtils::logError(launcherPathResult.error());
    ErrorPresentation::showError(nullptr, launcherPathResult.error());
    return;
  }

  const QString launcherPath = launcherPathResult.value();

  // TOCTOU mitigation: re-validate launcher right before execution. This
  // closes most of the validate-vs-exec window but cannot fully eliminate it
  // on POSIX systems — Linux has no atomic open-and-exec, so a symlink swap
  // between this check and QProcess::start() below is theoretically possible.
  // Acceptable because the user-supplied launcherPath is sourced from
  // settings.ini (under the user's own control) and validation rejects
  // non-canonical paths; a meaningful exploit requires a local attacker who
  // can already mutate the user's config directory.
  QFileInfo launcherCheck(launcherPath);
  if (!launcherCheck.exists() || !launcherCheck.isExecutable()) {
    ErrorPresentation::showError(
        nullptr, ErrorContext::critical(
                     ErrorCode::InvalidFilePath,
                     tr("Launcher is no longer accessible or executable:\n%1").arg(launcherPath),
                     QStringLiteral("LaunchManager::finishLaunch")));
    return;
  }

  // when runtime detection is enabled, route through a tracked
  // QProcess so we can emit started/finished signals and let the UI sleep
  // behind a "Now Playing" overlay. Otherwise fall back to the historical
  // detached launch which leaves Kartend ignorant of the child lifetime.
  if (runtimeDetectionEnabled()) {
    if (launchTracked(launcherPath, cmd, launchFilePath, originalFilePath, extractedDir,
                      collectionUuid)) {
      // The child reads the extracted file while it runs, so the scope guard
      // must release the directory on the spawn path regardless of outcome.
      // launchTracked wires the FailedToStart reclaim BEFORE start() — a
      // post-spawn connect here used to run after a synchronously-delivered
      // failure (the Windows shape) had already cleared m_trackedChild, so
      // the hook was never installed and the extraction dir leaked.
      cleanupExtraction.dismiss();
      return;
    }
    // launchTracked already showed a message box on failure to start; the
    // scope guard reclaims the extracted dir on this reject path.
    return;
  }

  // Detached launch with a short-lived early-failure watcher (Kartend-fqsv0).
  // The child reads the extracted file while it runs, so the directory must be
  // released to the spawn path regardless of outcome — launchDetachedWatched
  // owns reclaiming it on an early failure, so dismiss the guard here.
  cleanupExtraction.dismiss();
  launchDetachedWatched(launcherPath, cmd, originalFilePath, extractedDir, collectionUuid);
}

auto LaunchManager::parseParameters(const QString &paramString) -> ErrorUtils::Result<QStringList> {
  return LaunchCommandBuilder::parseParameters(paramString);
}
