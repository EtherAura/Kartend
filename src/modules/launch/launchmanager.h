#ifndef LAUNCHMANAGER_H
#define LAUNCHMANAGER_H

#include <functional>

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include "collectionutils.h"
#include "errorutils.h"
#include "setuputils.h"

QT_BEGIN_NAMESPACE
class QProcess;
QT_END_NAMESPACE

struct ApplicationContext;

struct LaunchManagerSetup {
  const ApplicationContext *ctx = nullptr;

  QList<CollectionConfig> *collections = nullptr;

  /// Invoked after a successful launch with (collectionUuid, filePath).
  /// Used by Kartend-7vi to record per-item play_count + last_played without
  /// LaunchManager taking a hard dependency on DatabaseManager (so launch
  /// unit tests don't pull the database module into their link). Optional —
  /// no-op when null.
  std::function<void(const QString &collectionUuid, const QString &filePath)> onLaunched;

  /// Invoked when a runtime-tracked child process exits with the elapsed
  /// session duration in seconds (Kartend-qxv → Kartend-7vi). Only fires
  /// when runtime detection is enabled and the child reached the started
  /// state. Optional.
  std::function<void(const QString &collectionUuid, const QString &filePath, qint64 seconds)>
      onPlaySessionEnded;

  SETUP_GETTER_DECL(QList<CollectionConfig> *, Collections)
};

struct LaunchCommand {
  QString program;
  QStringList arguments;
};

/// Handles launching media items with their configured launchers.
/// Manages RetroArch cores, parameter parsing, and launch debouncing.
class LaunchManager : public QObject {
  Q_OBJECT
public:
  explicit LaunchManager(QObject *parent = nullptr);
  ~LaunchManager() override = default;

  void setupReferences(const LaunchManagerSetup &setup);

  /// Launches a media item using the specified collection's launcher config
  void launchItem(const QString &filePath, int collectionIndex);

  /// Builds the program + argument list for a collection launch.
  ///
  /// This is a pure helper used by launchItem() and unit tests. It does NOT
  /// validate that the launcher exists/is executable (use validateLauncherPath
  /// for that); it only constructs and validates the argument semantics.
  [[nodiscard]] static ErrorUtils::Result<LaunchCommand>
  buildLaunchCommand(const CollectionConfig &collection, const QString &filePath);

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
  /// Always false when runtime detection is disabled. Kartend-qxv.
  [[nodiscard]] bool isRuntimeChildRunning() const { return m_trackedChild != nullptr; }

signals:
  /// Emitted when a runtime-tracked child process starts (Kartend-qxv).
  /// `displayName` is a human-readable label (typically the file basename)
  /// suitable for showing in a "Now Playing" overlay.
  void runtimeStarted(const QString &filePath, const QString &displayName);

  /// Emitted when a runtime-tracked child process finishes for any reason —
  /// normal exit, crash, or failure to start (Kartend-qxv).
  void runtimeFinished(const QString &filePath);

private:
  const ApplicationContext *m_ctx = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  GeneralSettings *m_generalSettings = nullptr;
  std::function<void(const QString &, const QString &)> m_onLaunched;
  std::function<void(const QString &, const QString &, qint64)> m_onPlaySessionEnded;

  /// Tracks recent launches for debounce protection
  QHash<QString, qint64> m_lastLaunchTimes;
  static constexpr qint64 kDoubleLaunchGuardMs = 500;

  /// The currently-tracked child process when runtime detection is enabled.
  /// Only one tracked child at a time — a second launch attempt while one is
  /// already running is rejected. Kartend-qxv.
  QPointer<QProcess> m_trackedChild;
  QString m_trackedFilePath;
  /// Collection UUID + start timestamp captured at runtimeStarted so the
  /// session duration can be accumulated on runtimeFinished (Kartend-7vi).
  QString m_trackedCollectionUuid;
  QDateTime m_trackedStartTime;

  /// Returns true when the configured general settings request runtime
  /// detection. Safe to call before settings are wired (returns false).
  [[nodiscard]] bool runtimeDetectionEnabled() const;

  /// Spawns `cmd` as a tracked child QProcess and emits runtimeStarted /
  /// runtimeFinished. Returns true on a successful start. Kartend-qxv.
  bool launchTracked(const QString &launcherPath, const LaunchCommand &cmd, const QString &filePath,
                     const QString &collectionUuid);

  /// Resolves the collection UUID for a given collection index using the
  /// collection name + expanded media directory. Returns empty when index is
  /// out of range. Used to key usage-stat updates (Kartend-7vi).
  [[nodiscard]] QString resolveCollectionUuid(int collectionIndex) const;

  /// Best-effort: increments play_count + last_played for the item via the
  /// DatabaseManager. Silently noops when the DB is unreachable so launches
  /// never block on stats tracking. Kartend-7vi.
  void recordSuccessfulLaunch(const QString &filePath, const QString &collectionUuid);
};

#endif // LAUNCHMANAGER_H
