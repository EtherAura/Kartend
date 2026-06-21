#ifndef LAUNCHMANAGER_H
#define LAUNCHMANAGER_H

#include <atomic>
#include <functional>
#include <memory>

#include <QDateTime>
#include <QFuture>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "errorutils.h"
#include "setuputils.h"

#include "applicationcontext_fwd.h"

struct LaunchManagerSetup {
  const ApplicationContext *ctx = nullptr;

  QList<CollectionConfig> *collections = nullptr;

  /// Invoked after a successful launch with (collectionUuid, filePath).
  /// Used by to record per-item play_count + last_played without
  /// LaunchManager taking a hard dependency on DatabaseManager (so launch
  /// unit tests don't pull the database module into their link). Optional —
  /// no-op when null.
  std::function<void(const QString &collectionUuid, const QString &filePath)> onLaunched;

  /// Invoked when a runtime-tracked child process exits with the elapsed
  /// session duration in seconds (→). Only fires
  /// when runtime detection is enabled and the child reached the started
  /// state. Optional.
  std::function<void(const QString &collectionUuid, const QString &filePath, qint64 seconds)>
      onPlaySessionEnded;

  /// Resolves the per-item launcher override. Called before
  /// the multi-launcher chooser dialog appears. Returns the unified launcher
  /// index (0 = primary, 1..N = launcher.additionalLaunchers[0..N-1]) when an override
  /// is set, or a negative value to fall through to the chooser / collection
  /// default. Indirection mirrors `onLaunched` so LaunchManager doesn't take
  /// a hard link-time dependency on DatabaseManager.
  std::function<int(const QString &collectionUuid, const QString &filePath)>
      resolveLauncherOverride;

  /// Shows the multi-launcher chooser and returns the user's pick, or a
  /// negative value if cancelled. Called only when a collection has more than
  /// one launcher and no index was otherwise resolved. The chooser dialog
  /// lives in the UI layer, so the owner supplies this callback rather than
  /// LaunchManager including the dialog header. Optional — when null, the
  /// collection's default launcher is used.
  std::function<int(const QString &collectionName, const QStringList &launcherNames,
                    int defaultIndex)>
      chooseLauncher;

  SETUP_GETTER_DECL(QList<CollectionConfig> *, Collections)
};

struct LaunchCommand {
  QString program;
  QStringList arguments;
};

// LaunchPreview moved to its own leaf header (launchpreview.h,
// Kartend-rq33v) so struct-only consumers no longer drag in this manager
// header; included here because LaunchManager's API returns it by value.
#include "launchpreview.h"

/// Handles launching media items with their configured launchers.
/// Manages libretro cores, parameter parsing, and launch debouncing.
class LaunchManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(LaunchManager)
public:
  explicit LaunchManager(QObject *parent = nullptr);
  /// Never abandons a running extractor child (Kartend-mkcak): requests
  /// cancellation (the watchdog loop kills the child within one poll
  /// interval) and waits for the worker to drain before destruction.
  ~LaunchManager() override;

  void setupReferences(const LaunchManagerSetup &setup);

  /// Injects the multi-launcher chooser callback. Separate from
  /// setupReferences so the owner (MainWindow) can supply a callback that
  /// shows the UI-layer LauncherChooserDialog without the input module
  /// including the dialog header. See `LaunchManagerSetup::chooseLauncher`.
  void
  setChooseLauncherCallback(std::function<int(const QString &collectionName,
                                              const QStringList &launcherNames, int defaultIndex)>
                                callback);

  /// Runs the injected launcher-chooser callback and returns the user's pick
  /// (negative = cancelled or no callback). Lets siblings reuse the same
  /// chooser plumbing without touching the UI-layer dialog directly.
  [[nodiscard]] int promptLauncherChoice(const QString &collectionName,
                                         const QStringList &launcherNames, int defaultIndex);

  /// Launches a media item using the specified collection's launcher config.
  /// When the collection has more than one launcher, a chooser
  /// dialog is shown unless `launcherIndex` is provided. Pass `launcherIndex`
  /// >= 0 to bypass the chooser and use a specific launcher directly (used by
  /// callers that have already resolved the user's pick).
  void launchItem(const QString &filePath, int collectionIndex, int launcherIndex = -1);

  /// Builds the program + argument list for a single launcher entry.
  ///
  /// This is a pure helper used by launchItem() and unit tests. It does NOT
  /// validate that the launcher exists/is executable (use validateLauncherPath
  /// for that); it only constructs and validates the argument semantics.
  /// `collectionName` is used solely for diagnostic messages and `%collection%`
  /// substitution.
  [[nodiscard]] static ErrorUtils::Result<LaunchCommand>
  buildLaunchCommand(const LauncherConfig &launcher, const QString &collectionName,
                     const QString &filePath);

  /// Convenience overload that builds the command for the collection's
  /// primary launcher (index 0). Retained for tests and callers that don't
  /// participate in the multi-launcher flow.
  [[nodiscard]] static ErrorUtils::Result<LaunchCommand>
  buildLaunchCommand(const CollectionConfig &collection, const QString &filePath) {
    return buildLaunchCommand(collection.launcher.launcherAt(0), collection.name, filePath);
  }

  /// Read-only dry-run: returns a `LaunchPreview` for the given launcher /
  /// collection / file triple without spawning a child process. The launcher
  /// arg is taken pre-resolved (preset resolution is the caller's
  /// responsibility — InteractionManager already does this for the real
  /// launch path) so the preview shows exactly what would be executed.
  [[nodiscard]] static LaunchPreview previewLaunchCommand(const CollectionConfig &collection,
                                                          const LauncherConfig &launcher,
                                                          const QString &filePath);

  /// Parses command-line parameters handling quoted strings
  /// Returns error if quotes are unclosed (potential injection vector)
  [[nodiscard]] static ErrorUtils::Result<QStringList> parseParameters(const QString &paramString);

  /// Validates a launcher path for security and resolves it to an absolute,
  /// canonical executable path.
  ///
  /// If the given path is not absolute, it is treated as a command name and
  /// resolved via PATH.
  [[nodiscard]] static ErrorUtils::Result<QString> validateLauncherPath(const QString &path);

  /// Validates that a path doesn't contain unsupported characters
  [[nodiscard]] static ErrorUtils::Result<void> validatePathSecurity(const QString &path);

  /// Checks if launch is allowed (debounce guard)
  [[nodiscard]] bool canLaunch(const QString &filePath) const;

  /// Records launch time for debounce tracking
  void recordLaunch(const QString &filePath);

  /// Checks if a file path is a supported archive format
  [[nodiscard]] static bool isArchiveFile(const QString &filePath);

  /// Extracts an archive to a temporary directory and returns the path to the
  /// target file matching the specified extension.
  ///
  /// Blocking — launchItem() runs it on a QtConcurrent worker thread
  /// (Kartend-mkcak); only tests call it synchronously. `cancelRequested`
  /// (optional) is polled by the extraction watchdog: setting it kills the
  /// extractor child and returns OperationCancelled. `maxDecompressedBytes`
  /// bounds the cumulative bytes written to the extraction dir
  /// (Kartend-ijglg); pass a negative value (the default) to use
  /// UIConstants::Launch::MAX_EXTRACTION_BYTES. Exceeding the cap kills the
  /// extractor and returns ResourceLimitExceeded; every abort path removes
  /// the partial extraction dir.
  [[nodiscard]] static ErrorUtils::Result<QString>
  extractArchiveToTemp(const QString &archivePath, const QString &targetExtension,
                       const std::atomic_bool *cancelRequested = nullptr,
                       qint64 maxDecompressedBytes = -1);

  /// Finds a file with the given extension in a directory (recursive)
  [[nodiscard]] static QString findFileWithExtension(const QString &directory,
                                                     const QString &extension);

  /// True while a runtime-tracked child process is currently running.
  /// Always false when runtime detection is disabled.
  [[nodiscard]] bool isRuntimeChildRunning() const { return m_trackedChild; }

  /// True while a launch-time archive extraction is running on the worker
  /// thread. Only one extraction runs at a time; a second archive launch
  /// while one is in flight is rejected.
  [[nodiscard]] bool isExtractionRunning() const { return m_extractionActive; }

  /// Requests cancellation of the in-flight archive extraction (no-op when
  /// none is running). The extraction watchdog observes the flag within one
  /// poll interval, kills the extractor child, removes the partial
  /// extraction dir, and the pending launch is abandoned silently.
  void cancelExtraction();

signals:
  /// Emitted when a launch-time archive extraction moves to the worker
  /// thread. `displayName` is a human-readable label (the archive basename)
  /// suitable for a busy overlay.
  void extractionStarted(const QString &filePath, const QString &displayName);

  /// Emitted when the in-flight extraction ends for any reason — success
  /// (the launch then continues), failure, or cancellation. Always paired
  /// with a preceding extractionStarted.
  void extractionFinished(const QString &filePath);

  /// Emitted when a runtime-tracked child process starts.
  /// `displayName` is a human-readable label (typically the file basename)
  /// suitable for showing in a "Now Playing" overlay.
  void runtimeStarted(const QString &filePath, const QString &displayName);

  /// Emitted when a runtime-tracked child process finishes for any reason —
  /// normal exit, crash, or failure to start.
  void runtimeFinished(const QString &filePath);

private:
  const ApplicationContext *m_ctx = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  GeneralSettings *m_generalSettings = nullptr;
  std::function<void(const QString &, const QString &)> m_onLaunched;
  std::function<void(const QString &, const QString &, qint64)> m_onPlaySessionEnded;
  std::function<int(const QString &, const QString &)> m_resolveLauncherOverride;
  std::function<int(const QString &, const QStringList &, int)> m_chooseLauncher;

  /// Tracks recent launches for debounce protection
  QHash<QString, qint64> m_lastLaunchTimes;
  static constexpr qint64 kDoubleLaunchGuardMs = 500;

  /// Kartend-fqsv0: how long the detached-path early-failure watcher stays
  /// armed after spawn. A child that dies non-zero within this window is
  /// reported as a launch failure and does NOT get its play_count stamped;
  /// surviving the window is treated as a successful launch and the watcher
  /// forgets the child (fire-and-forget). Kept short so a slow-but-healthy
  /// launcher (emulator splash screens, shader compile) is never mis-flagged.
  static constexpr int kEarlyFailureWindowMs = 1500;

  /// The currently-tracked child process when runtime detection is enabled.
  /// Only one tracked child at a time — a second launch attempt while one is
  /// already running is rejected.
  QPointer<QProcess> m_trackedChild;
  QString m_trackedFilePath;

  /// In-flight archive-extraction state (Kartend-mkcak). The cancel flag is
  /// shared with the worker lambda so it stays valid even if this manager
  /// dies first; the future lets the destructor wait for the worker to
  /// drain after requesting cancellation.
  bool m_extractionActive = false;
  QString m_extractionFilePath;
  std::shared_ptr<std::atomic_bool> m_extractionCancel;
  QFuture<ErrorUtils::Result<QString>> m_extractionFuture;

  /// Collection UUID + start timestamp captured at runtimeStarted so the
  /// session duration can be accumulated on runtimeFinished.
  QString m_trackedCollectionUuid;
  QDateTime m_trackedStartTime;

  /// Returns true when the configured general settings request runtime
  /// detection. Safe to call before settings are wired (returns false).
  [[nodiscard]] bool runtimeDetectionEnabled() const;

  /// Spawns `cmd` as a tracked child QProcess and emits runtimeStarted /
  /// runtimeFinished. Returns true on a successful start.
  bool launchTracked(const QString &launcherPath, const LaunchCommand &cmd, const QString &filePath,
                     const QString &collectionUuid);

  /// Detached-path launch with a short-lived early-failure watcher
  /// (Kartend-fqsv0). Spawns `cmd` via an owned QProcess and keeps an
  /// errorOccurred / early-finished handler armed for `kEarlyFailureWindowMs`.
  /// If the child fails to start or exits non-zero within that window the
  /// failure is surfaced, the extracted dir (if any) is reclaimed, and
  /// recordSuccessfulLaunch is suppressed. Once the window elapses with the
  /// child still alive (the genuine-success case) the launch is recorded and
  /// the watcher detaches — it stops reporting and never measures a session.
  /// Returns true when the spawn was issued (mirrors the historical
  /// startDetached return contract well enough for the caller's scope guard).
  bool launchDetachedWatched(const QString &launcherPath, const LaunchCommand &cmd,
                             const QString &originalFilePath, const QString &extractedDir,
                             const QString &collectionUuid);

  /// Runs extractArchiveToTemp on a QtConcurrent worker (Kartend-mkcak) and
  /// continues the launch in the completion callback on the GUI thread.
  /// The launch context (resolved launcher, collection name/uuid) is captured
  /// by value so a settings edit during extraction can't dangle references.
  void startExtractionAndLaunch(const QString &filePath, const QString &targetExtension,
                                const LauncherConfig &launcher, const QString &collectionName,
                                const QString &collectionUuid);

  /// Tail half of launchItem(): builds + validates the command and spawns the
  /// tracked or detached child. `originalFilePath` keys stats/debounce (the
  /// archive path for extracted launches); `launchFilePath` is what the
  /// launcher receives. `extractedDir` (empty for non-archive launches) is
  /// removed on every failure path before the child owns it.
  void finishLaunch(const LauncherConfig &launcher, const QString &collectionName,
                    const QString &originalFilePath, const QString &launchFilePath,
                    const QString &extractedDir, const QString &collectionUuid);

  /// Resolves the collection UUID for a given collection index using the
  /// collection name + expanded media directory. Returns empty when index is
  /// out of range. Used to key usage-stat updates.
  [[nodiscard]] QString resolveCollectionUuid(int collectionIndex) const;

  /// Best-effort: increments play_count + last_played for the item via the
  /// DatabaseManager. Silently noops when the DB is unreachable so launches
  /// never block on stats tracking.
  void recordSuccessfulLaunch(const QString &filePath, const QString &collectionUuid);
};

#endif // LAUNCHMANAGER_H
