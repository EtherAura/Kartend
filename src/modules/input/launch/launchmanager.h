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

// LaunchCommand and the pure command-construction functions live in
// launchcommandbuilder.h (the pure half of the launchmanager.cpp
// pure/process TU split); included here because LaunchManager's API returns
// LaunchCommand by value and the statics below delegate to the builder.
#include "launchcommandbuilder.h"

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
  /// substitution. Thin delegation to LaunchCommandBuilder::buildLaunchCommand,
  /// where the implementation lives.
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
  /// Thin delegation to LaunchCommandBuilder::previewLaunchCommand.
  [[nodiscard]] static LaunchPreview previewLaunchCommand(const CollectionConfig &collection,
                                                          const LauncherConfig &launcher,
                                                          const QString &filePath);

  /// Parses command-line parameters handling quoted strings
  /// Returns error if quotes are unclosed (potential injection vector)
  /// Thin delegation to LaunchCommandBuilder::parseParameters.
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
  /// extractor child and returns OperationCancelled.
  ///
  /// `maxDecompressedBytes` imposes an explicit cumulative-byte cap. Pass a
  /// negative value (the default) for the production bound: free space on the
  /// destination volume, less
  /// UIConstants::Launch::EXTRACTION_FREE_SPACE_MARGIN_BYTES (Kartend-si0p5).
  /// Tests pass a small explicit cap to exercise the watchdog without
  /// multi-GiB fixtures.
  ///
  /// `extractionBaseDir` is where extraction writes; empty (the default)
  /// resolves to LauncherSettings::extractionDirectory's default under
  /// QStandardPaths::CacheLocation. Deliberately not TempLocation — see
  /// uiconstants/launch.h.
  ///
  /// Breaching any bound kills the extractor and returns
  /// ResourceLimitExceeded; every abort path removes the partial extraction
  /// dir.
  [[nodiscard]] static ErrorUtils::Result<QString>
  extractArchiveToTemp(const QString &archivePath, const QString &targetExtension,
                       const std::atomic_bool *cancelRequested = nullptr,
                       qint64 maxDecompressedBytes = -1,
                       const QString &extractionBaseDir = QString());

  /// Finds a file with the given extension in a directory (recursive)
  [[nodiscard]] static QString findFileWithExtension(const QString &directory,
                                                     const QString &extension);

  /// Removes expired extractions under @p extractionBaseDir (empty = the
  /// default root). Call at startup and before each extraction
  /// (Kartend-2ygme, Kartend-ra8sf).
  ///
  /// @p retentionHours mirrors LauncherSettings::extractionRetentionHours:
  /// negative sweeps nothing, 0 removes every entry not currently in use, and
  /// a positive value removes entries whose last use is older than that many
  /// hours. Scoped to the two roots this module creates, never to the
  /// configured directory itself — the user may point extractionDirectory at a
  /// folder holding other things.
  ///
  /// @p inUseDirs are never removed regardless of age. Passing the running
  /// launches' extraction dirs is REQUIRED when a launch may be in flight: a
  /// play session can outlast the retention period, and expiring a running
  /// program's media out from under it is a hard crash.
  static void sweepStaleExtractions(const QString &extractionBaseDir = QString(),
                                    int retentionHours = 0, const QStringList &inUseDirs = {});

  /// Restarts an extraction's retention clock by rewriting its source marker
  /// (Kartend-ra8sf). Best-effort: a missing marker just means the sweep falls
  /// back to the directory's own mtime.
  static void touchExtractionMarker(const QString &extractionDir);

  /// The directory a launch owns — and may therefore reclaim — for the file an
  /// extraction produced (Kartend-dmg5y). That is the top-level entry the
  /// extraction created under the extraction root: `kartend_extract/<entry>`,
  /// `kartend_playlists/<release>`, or a per-run `kartend_extract_XXXXXX` root.
  /// Empty when @p launchFilePath does not lie inside one of those, so a caller
  /// handed a path the extraction did not create never deletes anything.
  ///
  /// Deliberately NOT the launch file's parent: an archive whose disc image
  /// sits in a subfolder would make that the owned dir, which the sweep's
  /// in-use exclusion then fails to match, and a playlist returned unchanged
  /// would make it the user's own library folder.
  [[nodiscard]] static QString ownedExtractionDir(const QString &launchFilePath,
                                                  const QString &extractionBaseDir = QString());

  /// True for an .m3u/.m3u8 playlist path.
  [[nodiscard]] static bool isPlaylistFile(const QString &filePath);

  /// Absolute paths listed by a playlist, in order. Blank lines and extended-
  /// m3u '#' directives are skipped; relative entries resolve against the
  /// playlist's own directory (which is how MultiDisc::buildM3uContents writes
  /// members that sit beside it). Bounded by MAX_PLAYLIST_ENTRIES /
  /// MAX_PLAYLIST_BYTES.
  [[nodiscard]] static ErrorUtils::Result<QStringList>
  readPlaylistEntries(const QString &playlistPath);

  /// True when @p playlistPath is a playlist listing at least one archive, and
  /// so needs resolvePlaylistForLaunch before a launcher can open it. Cheap
  /// enough for the GUI thread: one bounded read of a small text file.
  [[nodiscard]] static bool playlistNeedsExtraction(const QString &playlistPath);

  /// Extracts a playlist's archived members and returns a rewritten playlist
  /// pointing at the extracted discs (Kartend-ab8ri).
  ///
  /// A collapsed multi-disc item's launch path is the generated .m3u. That is
  /// not itself an archive, so launchItem's extraction branch never fires and
  /// the launcher would be handed a playlist of .zip paths — which is why
  /// collapsed releases did not launch, regardless of the collection's
  /// extractArchives flag.
  ///
  /// Members that are already plain disc images pass through untouched, and a
  /// playlist with no archived members is returned unchanged. Each archived
  /// member goes through extractArchiveToTemp, so the per-archive cache, the
  /// safety scan and the free-space bound all apply. `targetExtension` may be
  /// empty, in which case a disc-image preference list is used.
  ///
  /// Blocking — runs on the extraction worker, like extractArchiveToTemp.
  [[nodiscard]] static ErrorUtils::Result<QString>
  resolvePlaylistForLaunch(const QString &playlistPath, const QString &targetExtension,
                           const std::atomic_bool *cancelRequested = nullptr,
                           const QString &extractionBaseDir = QString());

  /// True while a runtime-tracked child process is currently running.
  /// Always false when runtime detection is disabled.
  [[nodiscard]] bool isRuntimeChildRunning() const { return m_trackedChild; }

  /// Lifts the detached single-child launch block before the child exits
  /// (Kartend-3232r.1). Called by MainWindow's focus backstop: once the user
  /// is demonstrably back at the frontend, a new launch is intentional even
  /// though the previous detached child (or its double-forked descendant)
  /// may still be running. Idempotent; the block also lifts on its own at
  /// the child's final finished.
  void releaseDetachedLaunchBlock() { m_detachedSessionActive = false; }

  /// True while a launch-time archive extraction is running on the worker
  /// thread. Only one extraction runs at a time; a second archive launch
  /// while one is in flight is rejected.
  [[nodiscard]] bool isExtractionRunning() const { return m_extractionActive; }

  /// Requests cancellation of the in-flight archive extraction (no-op when
  /// none is running). The extraction watchdog observes the flag within one
  /// poll interval, kills the extractor child, removes the partial
  /// extraction dir, and the pending launch is abandoned silently.
  void cancelExtraction();

  /// Test-only seam (Kartend-dhhh6). Substitutes the launcher spawn so a test
  /// can synthesize the QProcess started / finished / errorOccurred lifecycle
  /// WITHOUT fork()/exec(). libtsan's fork CHECK aborts the process when
  /// QProcess forks while worker threads are alive, so the launch-concurrency
  /// tests inject a non-forking spawner to run the cross-thread state machine
  /// (session-duration capture, process-tracking bookkeeping, the wired
  /// started/finished slots) under ThreadSanitizer. The fake is handed the
  /// SAME fully-wired QProcess the real path would have started. Null (the
  /// default) uses the real cmd.exe-aware spawn — production is unaffected.
  using LauncherSpawnFn =
      std::function<void(QProcess *child, const QString &launcherPath, const QStringList &args)>;
  void setLauncherSpawnerForTesting(LauncherSpawnFn spawner) {
    m_launcherSpawner = std::move(spawner);
  }

  /// Test-only seam (Kartend-dhhh6). Substitutes the archive-extraction body
  /// that runs on the QtConcurrent worker, so a test can exercise the
  /// extraction-worker / cancel-atomic / QFutureWatcher concurrency WITHOUT
  /// forking an extractor child (which aborts libtsan). The fake runs on the
  /// worker thread and is handed the same cancel flag the real extractor
  /// polls, so the GUI-thread cancelExtraction()/destructor hand-off is the
  /// genuine cross-thread interaction TSan observes. Null (the default) uses
  /// the real extractArchiveToTemp — production is unaffected.
  using ArchiveExtractFn = std::function<ErrorUtils::Result<QString>(
      const QString &archivePath, const QString &targetExtension,
      const std::atomic_bool *cancelRequested)>;
  void setArchiveExtractorForTesting(ArchiveExtractFn extractor) {
    m_archiveExtractor = std::move(extractor);
  }

  /// Test-only: live entry count of the double-launch debounce map, so the
  /// bounded pruning in recordLaunch (entries lapse after
  /// kDoubleLaunchGuardMs) is assertable without exposing the map itself.
  [[nodiscard]] int debounceEntryCountForTesting() const {
    return static_cast<int>(m_lastLaunchTimes.size());
  }

  /// Test-only: the extraction dirs the sweep currently treats as in use, so
  /// the Kartend-dmg5y bookkeeping (a refused launch must not clear the
  /// running one's entry) is assertable without exposing the members.
  [[nodiscard]] QStringList activeExtractionDirsForTesting() const {
    return activeExtractionDirs();
  }

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

  /// Emitted when a detached (fire-and-forget) launch is handed to the OS —
  /// the default path, runtime detection off (Kartend-3232r.1). Deliberately
  /// fired just BEFORE the spawn call: Windows delivers FailedToStart
  /// synchronously inside start(), and emitting after the spawn would invert
  /// the started/ended order the attract/gamepad suspend wiring relies on.
  /// A separate signal from runtimeStarted because that pair's consumers
  /// also show the "Now Playing" overlay and raise() the window when it
  /// closes — wrong for an unsupervised child (a double-forking launcher's
  /// QProcess exits seconds in while the program keeps running; raising
  /// would steal its focus).
  void detachedSessionStarted(const QString &filePath, const QString &displayName);

  /// Balanced counterpart of detachedSessionStarted, emitted at the child's
  /// FINAL QProcess::finished (or FailedToStart) — even long after the
  /// early-failure watcher forgot the child. Not emitted for a superseded
  /// child (a newer detached launch after releaseDetachedLaunchBlock): the
  /// pair always describes the most recent detached session. Children the
  /// destructor abandons at shutdown never emit it.
  void detachedSessionEnded(const QString &filePath);

private:
  const ApplicationContext *m_ctx = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  GeneralSettings *m_generalSettings = nullptr;
  std::function<void(const QString &, const QString &)> m_onLaunched;
  std::function<void(const QString &, const QString &, qint64)> m_onPlaySessionEnded;
  std::function<int(const QString &, const QString &)> m_resolveLauncherOverride;
  std::function<int(const QString &, const QStringList &, int)> m_chooseLauncher;

  /// Test seams (Kartend-dhhh6). Null in production — see the
  /// setLauncherSpawnerForTesting / setArchiveExtractorForTesting docs.
  LauncherSpawnFn m_launcherSpawner;
  ArchiveExtractFn m_archiveExtractor;

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
  /// already running is rejected. Ownership decision: a tracked child stays
  /// parented to this manager and therefore DIES with the frontend — a
  /// tracked session is supervised (now-playing overlay, play-time
  /// accounting), so the frontend closing ends the session by design. The
  /// detached path below makes the opposite choice.
  QPointer<QProcess> m_trackedChild;
  QString m_trackedFilePath;

  /// Fire-and-forget children that survived the early-failure window.
  /// launchDetachedWatched reparents them away from this manager at settle
  /// time: a still-owned QProcess would be destroyed in ~LaunchManager, and
  /// ~QProcess SIGKILLs a running child — closing the frontend must not take
  /// the user's launched program down with it (the historical startDetached
  /// contract). QPointer entries null out when a child exits and its
  /// finished→deleteLater fires; the destructor deletes only already-exited
  /// stragglers and intentionally abandons running ones to the OS.
  QList<QPointer<QProcess>> m_survivedDetachedChildren;

  /// Kartend-3232r.1: detached single-child launch block —
  /// launchDetachedWatched rejects a new launch while the previous detached
  /// session is still live (spawn → final finished), mirroring
  /// launchTracked's m_trackedChild rejection. m_detachedSessionActive flips
  /// false at the balanced detachedSessionEnded or via
  /// releaseDetachedLaunchBlock (MainWindow's focus backstop); the QPointer
  /// self-nulls if the child object dies, so a missed clear can never wedge
  /// launching permanently. m_detachedFilePath only feeds the rejection
  /// message.
  bool m_detachedSessionActive = false;
  QPointer<QProcess> m_activeDetachedChild;
  QString m_detachedFilePath;

  /// In-flight archive-extraction state (Kartend-mkcak). The cancel flag is
  /// shared with the worker lambda so it stays valid even if this manager
  /// dies first; the future lets the destructor wait for the worker to
  /// drain after requesting cancellation.
  bool m_extractionActive = false;
  QString m_extractionFilePath;
  // Kartend-ra8sf: extraction dirs a launched program is currently reading
  // from. sweepStaleExtractions must never expire these — a play session can
  // outlast the retention period, and deleting a running program's media is a
  // hard crash. One tracked child at a time (launchTracked refuses a second),
  // so one string; set only once launchTracked has taken the child, so a
  // refused launch cannot overwrite — and then clear — the running one's entry
  // (Kartend-dmg5y).
  //
  // Detached children are not exclusive — several can survive their watch
  // windows at once — so they need a list (Kartend-dmg5y). An entry leaves it
  // on an early failure or at the child's real exit while we are alive to see
  // it. A child that exits cleanly inside the watch window keeps its entry on
  // purpose: a launcher that hands off to another process and returns 0 may
  // leave that process reading the media.
  QString m_trackedExtractedDir;
  QStringList m_detachedExtractedDirs;

  /// The extraction dirs currently in use, for sweepStaleExtractions.
  [[nodiscard]] QStringList activeExtractionDirs() const {
    QStringList dirs = m_detachedExtractedDirs;
    if (!m_trackedExtractedDir.isEmpty()) {
      dirs << m_trackedExtractedDir;
    }
    return dirs;
  }

  /// True when @p dir is (canonically) one of activeExtractionDirs(). Every
  /// reclaim that can run while another launch is live consults this first:
  /// relaunching the running title hits the same per-archive cache entry, so
  /// the refused or failed second launch would otherwise delete the media the
  /// first one is reading (Kartend-dmg5y).
  [[nodiscard]] bool isExtractionDirInUse(const QString &dir) const;
  std::shared_ptr<std::atomic_bool> m_extractionCancel;
  QFuture<ErrorUtils::Result<QString>> m_extractionFuture;

  /// Collection UUID + start timestamp captured at runtimeStarted so the
  /// session duration can be accumulated on runtimeFinished.
  QString m_trackedCollectionUuid;
  QDateTime m_trackedStartTime;

  /// Returns true when the configured general settings request runtime
  /// detection. Safe to call before settings are wired (returns false).
  [[nodiscard]] bool runtimeDetectionEnabled() const;

  // The three process-plumbing members below (spawnLauncherProcess,
  // launchTracked, launchDetachedWatched) are defined in
  // launchmanager_process.cpp — the QProcess spawn/track/watch half of the
  // pure/process TU split (same partials convention as
  // launchmanagerarchive.cpp).

  /// Spawns the launcher child: the test seam (m_launcherSpawner) when set,
  /// otherwise the real cmd.exe-aware startLauncherProcess. Single chokepoint
  /// for both the tracked and detached spawn paths (Kartend-dhhh6).
  void spawnLauncherProcess(QProcess *child, const QString &launcherPath, const QStringList &args);

  /// Spawns `cmd` as a tracked child QProcess, emits runtimeStarted /
  /// runtimeFinished, and stamps the launch usage stat for `originalFilePath`
  /// once the child actually starts. `extractedDir` (empty for non-archive
  /// launches) is reclaimed on FailedToStart via a connect wired BEFORE
  /// start() — Windows delivers that error synchronously inside start(), so
  /// a post-spawn hook would miss it and orphan the extraction. Returns true
  /// when the spawn was issued (false only when another tracked child is
  /// already running).
  bool launchTracked(const QString &launcherPath, const LaunchCommand &cmd, const QString &filePath,
                     const QString &originalFilePath, const QString &extractedDir,
                     const QString &collectionUuid);

  /// Detached-path launch with a short-lived early-failure watcher
  /// (Kartend-fqsv0). Spawns `cmd` via an owned QProcess and keeps an
  /// errorOccurred / early-finished handler armed for `kEarlyFailureWindowMs`.
  /// If the child fails to start or exits non-zero within that window the
  /// failure is surfaced, the extracted dir (if any) is reclaimed, and
  /// recordSuccessfulLaunch is suppressed. Once the window elapses with the
  /// child still alive (the genuine-success case) the launch is recorded and
  /// the watcher detaches — it stops reporting and never measures a session.
  /// Kartend-3232r.1: emits the detachedSessionStarted/Ended pair around the
  /// child's whole life (spawn → final finished) so the UI can suspend
  /// attract/gamepad, and rejects a new detached launch while the previous
  /// session is still live (see m_detachedSessionActive), reclaiming
  /// `extractedDir` on that reject path. Returns true when the spawn was
  /// issued (mirrors the historical startDetached return contract well
  /// enough for the caller's scope guard).
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
