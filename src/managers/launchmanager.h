#ifndef LAUNCHMANAGER_H
#define LAUNCHMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

#include "collectionutils.h"

class InteractionManager;
struct LaunchManagerSetup {
  QList<CollectionConfig> *collections = nullptr;
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

  /// Parses command-line parameters handling quoted strings
  [[nodiscard]] static QStringList parseParameters(const QString &paramString);

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
