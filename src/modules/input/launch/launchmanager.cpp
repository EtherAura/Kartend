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
#include <QRegularExpression>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QTemporaryDir>
#include <QTimer>

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
  m_lastLaunchTimes[filePath] = QDateTime::currentMSecsSinceEpoch();
}

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using ErrorUtils::Result;

namespace {
// Start a launcher process, routing Windows .cmd/.bat batch files through the
// command interpreter. On Windows a batch file is not an executable image:
// CreateProcess (hence QProcess::start, which uses no shell) cannot run it
// directly and fails with FailedToStart — which is why launching a .cmd
// launcher silently never started (Kartend-5i556). Invoke it via %COMSPEC%
// (cmd.exe) /c instead. Real executables and every non-Windows launcher start
// directly, unchanged. NOTE: full argument-quoting hardening for the cmd.exe
// path (media names containing & ( ) % ! ^ etc.) is tracked separately under
// Kartend-9u29e; this only ensures batch launchers start at all.
void startLauncherProcess(QProcess *child, const QString &launcherPath, const QStringList &args) {
#ifdef Q_OS_WIN
  const QString suffix = QFileInfo(launcherPath).suffix().toLower();
  if (suffix == QLatin1String("cmd") || suffix == QLatin1String("bat")) {
    QString comspec = qEnvironmentVariable("COMSPEC");
    if (comspec.isEmpty()) {
      comspec = QStringLiteral("cmd.exe");
    }
    child->start(comspec, QStringList{QStringLiteral("/c"), launcherPath} + args);
    return;
  }
#endif
  child->start(launcherPath, args);
}
} // namespace

auto LaunchManager::buildLaunchCommand(const LauncherConfig &launcher,
                                       const QString &collectionName, const QString &filePath)
    -> ErrorUtils::Result<LaunchCommand> {
  // Reject collection names that would inject `..`, `/`, or `\` segments into
  // the `%collection%` substitution. Defence-in-depth against malicious or
  // mistyped names entering via kart import or settings edits.
  auto nameValidation = PathUtils::validateCollectionNameForSubstitution(collectionName);
  if (nameValidation.isError()) {
    return nameValidation.error();
  }

  auto expandOnly = [&](const QString &text) -> QString {
    QString out = text;
    out.replace("%collection%", collectionName, Qt::CaseInsensitive);
    return out.trimmed();
  };

  const QString expandedLauncherPath = expandOnly(launcher.launcherPath);
  const QString expandedCorePath = expandOnly(launcher.corePath);

  if (expandedLauncherPath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "No launcher configured",
                               "LaunchManager::buildLaunchCommand")
        .withDetails(QString("Collection '%1'").arg(collectionName));
  }

  // Validate media file path for security.
  // Existence is not checked here; only character-level security checks.
  auto fileValidation = validatePathSecurity(filePath);
  if (fileValidation.isError()) {
    return fileValidation.error();
  }

  // The media path is appended as the final argument (both the libretro and
  // plain-launcher branches below). A path whose passed form starts with '-'
  // would be parsed by the launcher as an option, not a file operand
  // (argv-flag injection — no shell is involved, but launcher flags could be
  // flipped by an oddly/maliciously named file). Reject it, mirroring the
  // corePath leading-dash guard below. Absolute paths (the normal case) start
  // with '/', so this never triggers for them.
  if (filePath.startsWith('-')) {
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Media path cannot start with a dash",
                               "LaunchManager::buildLaunchCommand")
        .withDetails(QString("File path '%1' would be parsed as a launcher option").arg(filePath));
  }

  LaunchCommand cmd;
  cmd.program = expandedLauncherPath;

  // Parse + expand the user's launch parameters once, shared by both the
  // libretro and plain-launcher branches below (Kartend-q21fy).
  //
  // Kartend-nv9iw: tokenize the RAW template, THEN substitute %collection%
  // inside each already-split argument. Expanding before tokenizing let a
  // collection name containing spaces or a leading dash (which can arrive from
  // an imported .kart manifest) split into extra argv entries — injecting
  // attacker-chosen flags into the launcher. Per-token substitution can never
  // introduce a new argument boundary.
  //
  // Kartend-51d3e: the same per-token pass substitutes the media-path
  // placeholders (%1, %f) and the core placeholder (%core), so probe-/wizard-
  // seeded templates like ffmpeg's `-autoexit -nodisp "%1"` or RetroArch's
  // `-L %core "%1"` land the real paths in the placeholder position instead of
  // passing a literal token through. Replacing inside the already-split token
  // keeps the argv boundary the template author chose — `"%1"` stays one
  // argument even when filePath contains spaces.
  bool sawFilePlaceholder = false;
  auto expandLaunchParameters = [&](QStringList &out) -> ErrorUtils::Result<void> {
    const QString rawLaunchParameters = launcher.launchParameters.trimmed();
    if (rawLaunchParameters.isEmpty()) {
      return {};
    }
    auto parseResult = parseParameters(rawLaunchParameters);
    if (parseResult.isError()) {
      return parseResult.error();
    }
    QStringList expandedArgs = parseResult.value();
    for (QString &arg : expandedArgs) {
      arg.replace("%collection%", collectionName, Qt::CaseInsensitive);
      if (arg.contains("%1") || arg.contains("%f", Qt::CaseInsensitive)) {
        sawFilePlaceholder = true;
        arg.replace("%1", filePath);
        arg.replace("%f", filePath, Qt::CaseInsensitive);
      }
      arg.replace("%core", expandedCorePath, Qt::CaseInsensitive);
    }
    out.append(expandedArgs);
    return {};
  };

  if (LauncherUtils::usesLibretroCore(expandedLauncherPath)) {
    if (expandedCorePath.isEmpty()) {
      return ErrorContext::error(ErrorCode::InvalidArgument, "No libretro core configured",
                                 "LaunchManager::buildLaunchCommand")
          .withDetails(QString("Collection '%1'").arg(collectionName));
    }

    // Core path should be a file path, not a flag.
    if (expandedCorePath.startsWith("-")) {
      return ErrorContext::error(ErrorCode::InvalidFilePath, "Core path cannot start with a dash",
                                 "LaunchManager::buildLaunchCommand")
          .withDetails(QString("Core path '%1' looks like an option").arg(expandedCorePath));
    }

    auto coreValidation = validatePathSecurity(expandedCorePath);
    if (coreValidation.isError()) {
      return coreValidation.error();
    }

    // Kartend-q21fy: honor the user's launch parameters on the libretro path
    // too. They were previously discarded silently — a configured
    // --fullscreen / --config override simply never took effect. Insert the
    // parsed + expanded tokens AHEAD of the `-L <core> <file>` triple so the
    // triple ordering RetroArch expects stays intact.
    auto paramResult = expandLaunchParameters(cmd.arguments);
    if (paramResult.isError()) {
      return paramResult.error();
    }
    cmd.arguments << "-L" << expandedCorePath << filePath;
    return cmd;
  }

  // Plain launcher: parse optional launch parameters string.
  auto paramResult = expandLaunchParameters(cmd.arguments);
  if (paramResult.isError()) {
    return paramResult.error();
  }
  // Only fall back to appending the media path when no %1/%f placeholder set
  // its position explicitly — this preserves the historical append-at-end
  // behavior for the common templates that carry no media placeholder.
  if (!sawFilePlaceholder) {
    cmd.arguments << filePath;
  }
  return cmd;
}

auto LaunchManager::previewLaunchCommand(const CollectionConfig &collection,
                                         const LauncherConfig &launcher, const QString &filePath)
    -> LaunchPreview {
  LaunchPreview out;
  // Expand %collection% so the build-error early-return below reports the
  // resolved program rather than the raw template (the success path overwrites
  // this with cmd.value().program, which is the same expanded value).
  out.program =
      QString(launcher.launcherPath).replace("%collection%", collection.name, Qt::CaseInsensitive);

  auto cmd = buildLaunchCommand(launcher, collection.name, filePath);
  if (cmd.isError()) {
    out.buildOk = false;
    out.buildError = cmd.error().message;
    out.warnings << cmd.error().message;
    // Still report the file-existence + archive bits the user can fix from
    // the preview surface even when the command can't be assembled.
    out.fileExists = QFileInfo::exists(filePath);
    if (collection.archive.extractArchives && isArchiveFile(filePath)) {
      out.wouldExtractArchive = true;
      out.archiveTargetExtension = collection.archive.extractedExtension;
    }
    return out;
  }

  out.buildOk = true;
  out.program = cmd.value().program;
  out.arguments = cmd.value().arguments;

  // Resolve the launcher to an absolute executable so the preview shows
  // both the configured value and what the OS would actually invoke. An
  // empty resolved path => not on PATH and not at the given absolute
  // location; surface as a warning.
  auto resolved = validateLauncherPath(out.program);
  if (resolved.isOk()) {
    out.resolvedProgram = resolved.value();
  } else {
    out.warnings << QObject::tr("Launcher executable not found: %1").arg(out.program);
  }

  out.fileExists = QFileInfo::exists(filePath);
  if (!out.fileExists) {
    out.warnings << QObject::tr("File does not exist on disk: %1").arg(filePath);
  }

  // Archive-extraction warnings. When the toggle is on for the collection
  // and the file is recognised as an archive, the launcher would receive
  // the extracted file path at runtime instead of the original archive —
  // call that out explicitly so the preview doesn't lie about what gets
  // executed. The extension being empty is its own actionable warning.
  if (collection.archive.extractArchives && isArchiveFile(filePath)) {
    out.wouldExtractArchive = true;
    out.archiveTargetExtension = collection.archive.extractedExtension;
    if (out.archiveTargetExtension.trimmed().isEmpty()) {
      out.warnings << QObject::tr(
          "Archive extraction is enabled but the target extension is empty — "
          "the launcher would receive the archive path verbatim.");
    }
  }

  // Kartend-pgfks: a configured core path is only ever consumed by the
  // libretro branch of buildLaunchCommand (which keys off the launcher's
  // basename). When the launcher isn't classified libretro the core path is
  // silently dropped at launch — surface that here so a stray core path (e.g.
  // left over from a rename or arriving via an imported bundle) is visible in
  // the preview rather than mysteriously ignored. Preview-only; the actual
  // command above is unchanged.
  const QString previewCorePath =
      QString(launcher.corePath).replace("%collection%", collection.name, Qt::CaseInsensitive);
  if (!previewCorePath.trimmed().isEmpty() && !LauncherUtils::usesLibretroCore(out.program)) {
    out.warnings << QObject::tr(
                        "Core path will be ignored (launcher is not a libretro core launcher): %1")
                        .arg(previewCorePath.trimmed());
  }

  // Heuristic unresolved-placeholder check. We've already substituted
  // %collection%, %1/%f (media path) and %core; any remaining placeholder —
  // a %name% style token, or a bare %1/%f/%core that was never substituted
  // (e.g. %core left in a non-libretro launcher) — likely indicates a typo or
  // a placeholder the launcher won't honour. Flag it without claiming to know
  // what the user meant.
  static const QRegularExpression kPlaceholderRe(
      QStringLiteral("%[A-Za-z0-9_]+%|%(?:[0-9]+|f|core)\\b"),
      QRegularExpression::CaseInsensitiveOption);
  for (const QString &arg : out.arguments) {
    auto m = kPlaceholderRe.match(arg);
    if (m.hasMatch()) {
      out.warnings << QObject::tr("Unresolved placeholder in argument: %1").arg(m.captured(0));
      break; // one warning is enough; the dialog can show the full args
    }
  }

  return out;
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

  const CollectionConfig &collection = (*m_collections)[collectionIndex];

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
    const QString uuid = resolveCollectionUuid(collectionIndex);
    if (!uuid.isEmpty()) {
      const int overrideIndex = m_resolveLauncherOverride(uuid, filePath);
      if (overrideIndex >= 0 && overrideIndex < collection.launcher.launcherCount()) {
        resolvedLauncherIndex = overrideIndex;
      }
    }
  }
  if (resolvedLauncherIndex < 0) {
    if (collection.launcher.launcherCount() > 1) {
      QStringList launcherNames;
      launcherNames.reserve(collection.launcher.launcherCount());
      for (int i = 0; i < collection.launcher.launcherCount(); ++i) {
        // resolve preset references so the chooser shows the
        // preset's current name instead of a stale inline copy.
        const LauncherConfig effective = LauncherUtils::resolvePreset(
            collection.launcher.launcherAt(i), m_generalSettings
                                                   ? m_generalSettings->launchers.launcherPresets
                                                   : QList<LauncherPreset>{});
        launcherNames << (effective.name.trimmed().isEmpty()
                              ? collection.launcher.launcherDisplayName(i)
                              : effective.name.trimmed());
      }
      // The chooser dialog lives in the UI layer; the owner injects a
      // callback that shows it. With no callback wired, fall back to the
      // collection's configured default launcher.
      const int defaultIndex = std::clamp(collection.launcher.defaultLauncherIndex, 0,
                                          collection.launcher.launcherCount() - 1);
      const int chosen = m_chooseLauncher
                             ? m_chooseLauncher(collection.name, launcherNames, defaultIndex)
                             : defaultIndex;
      if (chosen < 0) {
        return; // User cancelled.
      }
      resolvedLauncherIndex = chosen;
    } else {
      resolvedLauncherIndex = 0;
    }
  }
  if (resolvedLauncherIndex < 0 || resolvedLauncherIndex >= collection.launcher.launcherCount()) {
    resolvedLauncherIndex = 0;
  }
  // resolve preset references at launch time. When the
  // entry's presetId names a registered preset, its fields override the
  // inline ones; otherwise the inline fields are used as-is.
  const LauncherConfig launcher = LauncherUtils::resolvePreset(
      collection.launcher.launcherAt(resolvedLauncherIndex),
      m_generalSettings ? m_generalSettings->launchers.launcherPresets : QList<LauncherPreset>{});

  // Resolve UUID once: both the detached and tracked paths route stats
  // updates through the same key. Empty UUID skips tracking silently — the
  // launch itself still proceeds. Resolved here (not in finishLaunch) so the
  // async extraction continuation doesn't have to re-derefence
  // m_collections, which a settings edit could have mutated meanwhile.
  const QString collectionUuid = resolveCollectionUuid(collectionIndex);

  if (collection.archive.extractArchives && !collection.archive.extractedExtension.isEmpty() &&
      isArchiveFile(filePath)) {
    qCDebug(lcLaunchManager) << "Archive extraction enabled for" << filePath;
    // Kartend-mkcak: extraction blocks for up to the full extraction timeout,
    // so it runs on a worker thread; the launch continues in the completion
    // callback on the GUI thread. launchItem returns immediately.
    startExtractionAndLaunch(filePath, collection.archive.extractedExtension, launcher,
                             collection.name, collectionUuid);
    return;
  }

  finishLaunch(launcher, collection.name, filePath, filePath, QString(), collectionUuid);
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
  m_extractionFuture = QtConcurrent::run([filePath, targetExtension, cancelFlag]() {
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
    if (launchTracked(launcherPath, cmd, launchFilePath, collectionUuid)) {
      // The child reads the extracted file while it runs, so the scope guard
      // must release the directory on the spawn path regardless of outcome.
      cleanupExtraction.dismiss();
      if (m_trackedChild) {
        QProcess *child = m_trackedChild;
        // Kartend-yu1e5: stamp play_count/last_played only once the child
        // ACTUALLY starts — QProcess::start() is asynchronous on Unix and
        // launchTracked returning true only means the spawn was issued;
        // FailedToStart arrives later via errorOccurred. A misconfigured
        // launcher no longer inflates usage stats (parity with the detached
        // path, which gates on startDetached's return).
        connect(child, &QProcess::started, this, [this, originalFilePath, collectionUuid]() {
          recordSuccessfulLaunch(originalFilePath, collectionUuid);
        });
        // And reclaim the extracted dir if the child never ran — the guard
        // above was already dismissed, so FailedToStart used to leak it.
        if (!extractedDir.isEmpty()) {
          // By-value capture detaches the QString from the reference param,
          // which dies with this frame while the lambda outlives it.
          connect(child, &QProcess::errorOccurred, this,
                  [extractedDir](QProcess::ProcessError error) {
                    if (error == QProcess::FailedToStart) {
                      QDir(extractedDir).removeRecursively();
                    }
                  });
        }
      }
      return;
    }
    // launchTracked already showed a message box on failure to start.
    return;
  }

  // Detached launch with a short-lived early-failure watcher (Kartend-fqsv0).
  // The child reads the extracted file while it runs, so the directory must be
  // released to the spawn path regardless of outcome — launchDetachedWatched
  // owns reclaiming it on an early failure, so dismiss the guard here.
  cleanupExtraction.dismiss();
  launchDetachedWatched(launcherPath, cmd, originalFilePath, extractedDir, collectionUuid);
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
  // tracked session: we never measure duration, never block a second launch,
  // and once the window elapses with the child alive we record success and let
  // the child run on (fire-and-forget) until it exits on its own.
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
            connect(child, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), child,
                    [child](int, QProcess::ExitStatus) { child->deleteLater(); });
          });

  connect(child, &QProcess::errorOccurred, this,
          [settled, child, cmd, launcherPath, reclaimExtraction](QProcess::ProcessError error) {
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
            child->deleteLater();
          });

  startLauncherProcess(child, launcherPath, cmd.arguments);
  window->start();
  // start() is async on Unix; FailedToStart arrives via errorOccurred. Report
  // the spawn as issued (the caller has already dismissed its cleanup guard).
  return true;
}

bool LaunchManager::launchTracked(const QString &launcherPath, const LaunchCommand &cmd,
                                  const QString &filePath, const QString &collectionUuid) {
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

  connect(child, &QProcess::started, this, [this, filePath, displayName]() {
    // Capture the start moment here rather than at child->start() so the
    // recorded duration reflects actual run time (not queueing delay).
    m_trackedStartTime = QDateTime::currentDateTimeUtc();
    emit runtimeStarted(filePath, displayName);
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

  // See the detached-start path above: pin CWD to the launcher's own
  // directory so sibling resources resolve the same way (Kartend-bmvu).
  child->setWorkingDirectory(QFileInfo(launcherPath).absolutePath());
  startLauncherProcess(child, launcherPath, cmd.arguments);
  // start() returns void; FailedToStart is reported via errorOccurred.
  return true;
}

auto LaunchManager::parseParameters(const QString &paramString) -> ErrorUtils::Result<QStringList> {
  QStringList result;
  if (paramString.trimmed().isEmpty()) {
    return result;
  }

  // Reject null bytes/newlines which can cause confusing log/diagnostic output.
  if (paramString.contains(QChar('\0')) || paramString.contains('\n') ||
      paramString.contains('\r')) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Launch parameters contain invalid control characters",
                               "LaunchManager::parseParameters");
  }

  QString params = paramString.trimmed();
  bool inQuotes = false;
  QString currentParam;
  QChar quoteChar;

  for (int i = 0; i < params.length(); ++i) {
    QChar currentChar = params[i];

    // Backslash escapes the next character when that character is one the parser
    // would otherwise treat specially — a quote, a separator space, or a
    // backslash — so a param can carry a literal quote (Kartend-xi2mj). Works
    // inside and outside quotes. A backslash before any other character (e.g. a
    // path separator) is left literal so existing params are unaffected.
    if (currentChar == '\\' && i + 1 < params.length()) {
      const QChar next = params[i + 1];
      if (next == '"' || next == '\'' || next == ' ' || next == '\\') {
        currentParam.append(next);
        ++i;
        continue;
      }
    }

    if (!inQuotes && (currentChar == '"' || currentChar == '\'')) {
      inQuotes = true;
      quoteChar = currentChar;
    } else if (inQuotes && currentChar == quoteChar) {
      inQuotes = false;
    } else if (currentChar == ' ' && !inQuotes) {
      if (!currentParam.isEmpty()) {
        result.append(currentParam);
        currentParam.clear();
      }
    } else {
      currentParam.append(currentChar);
    }
  }

  // Check for unclosed quotes - potential injection vulnerability
  if (inQuotes) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Unclosed quote in launch parameters",
                               "LaunchManager::parseParameters")
        .withDetails(QString("Quote character '%1' was not closed").arg(quoteChar));
  }

  if (!currentParam.isEmpty()) {
    result.append(currentParam);
  }

  return result;
}
