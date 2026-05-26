#ifndef LAUNCHMANAGER_H
#define LAUNCHMANAGER_H

#include <functional>

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

#include "collectionutils.h"
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

/// Read-only summary of what `launchItem()` would do for a given
/// (collection, launcher, file) triple, without spawning a child process.
/// Drives the launch-preview / dry-run validation UI: each warning surfaces
/// a specific user-fixable issue (missing executable, file gone, archive
/// extraction without a target extension, etc.). `buildOk` is false when
/// buildLaunchCommand itself rejected the input (e.g. missing libretro
/// core); `buildError` carries the message.
struct LaunchPreview {
  /// True iff buildLaunchCommand returned a valid program + arguments.
  /// When false, `program` and `arguments` are empty and `warnings` is the
  /// single-entry list `[buildError]`.
  bool buildOk = false;
  /// Diagnostic message from buildLaunchCommand when buildOk == false.
  QString buildError;
  /// Raw launcher path as configured (may be a bare command name).
  QString program;
  /// Argument list, with %collection% / preset substitutions applied.
  QStringList arguments;
  /// Path returned by validateLauncherPath — empty when the launcher is
  /// not on PATH / doesn't exist on disk.
  QString resolvedProgram;
  /// True when the file at `filePath` exists on disk at preview time.
  bool fileExists = false;
  /// True when the collection's archive-extraction toggle would apply to
  /// this file. UI shows the extracted-extension target alongside.
  bool wouldExtractArchive = false;
  /// The target extension the archive would be unpacked to (set when
  /// wouldExtractArchive is true). Empty string when extraction is on but
  /// no extension was configured — surfaced as a warning.
  QString archiveTargetExtension;
  /// Human-readable warnings — empty list means the command is ready.
  QStringList warnings;
};

/// Handles launching media items with their configured launchers.
/// Manages libretro cores, parameter parsing, and launch debouncing.
class LaunchManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(LaunchManager)
public:
  explicit LaunchManager(QObject *parent = nullptr);
  ~LaunchManager() override = default;

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
  /// target file matching the specified extension
  [[nodiscard]] static ErrorUtils::Result<QString>
  extractArchiveToTemp(const QString &archivePath, const QString &targetExtension);

  /// Finds a file with the given extension in a directory (recursive)
  [[nodiscard]] static QString findFileWithExtension(const QString &directory,
                                                     const QString &extension);

  /// True while a runtime-tracked child process is currently running.
  /// Always false when runtime detection is disabled.
  [[nodiscard]] bool isRuntimeChildRunning() const { return m_trackedChild; }

signals:
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

  /// The currently-tracked child process when runtime detection is enabled.
  /// Only one tracked child at a time — a second launch attempt while one is
  /// already running is rejected.
  QPointer<QProcess> m_trackedChild;
  QString m_trackedFilePath;
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
