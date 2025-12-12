#ifndef LAUNCHMANAGER_H
#define LAUNCHMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

#include "collectionutils.h"
#include "errorutils.h"
#include "setuputils.h"

struct ApplicationContext;

struct LaunchManagerSetup {
  const ApplicationContext *ctx = nullptr;

  QList<CollectionConfig> *collections = nullptr;

  SETUP_GETTER_DECL(QList<CollectionConfig>*, Collections)
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
  [[nodiscard]] static ErrorUtils::Result<LaunchCommand> buildLaunchCommand(
      const CollectionConfig &collection, const QString &filePath);

  /// Parses command-line parameters handling quoted strings
  /// Returns error if quotes are unclosed (potential injection vector)
  [[nodiscard]] static ErrorUtils::Result<QStringList> parseParameters(const QString &paramString);

  /// Validates a launcher path for security and resolves it to an absolute,
  /// canonical executable path.
  ///
  /// If the given path is not absolute, it is treated as a command name and
  /// resolved via PATH.
  [[nodiscard]] static ErrorUtils::Result<QString> validateLauncherPath(const QString &path);

  /// Validates that a path doesn't contain shell metacharacters or traversal
  [[nodiscard]] static ErrorUtils::Result<void> validatePathSecurity(const QString &path);

  /// Checks if launch is allowed (debounce guard)
  [[nodiscard]] bool canLaunch(const QString &filePath) const;

  /// Records launch time for debounce tracking
  void recordLaunch(const QString &filePath);

private:
  QList<CollectionConfig> *m_collections = nullptr;

  /// Tracks recent launches for debounce protection
  QHash<QString, qint64> m_lastLaunchTimes;
  static constexpr qint64 kDoubleLaunchGuardMs = 500;
};

#endif // LAUNCHMANAGER_H
