#ifndef SETTINGSUTILS_H
#define SETTINGSUTILS_H

#include "collectionutils.h"
#include "errorutils.h"
#include <QList>
#include <QSettings>
#include <QString>

class QWidget;
class QScrollArea;

class SettingsUtils {
public:
  [[nodiscard]] static auto getConfigPath() -> QString;
  [[nodiscard]] static auto getFormat() -> QSettings::Format;
  [[nodiscard]] static auto expandConfigVariables(const QString &input,
                                                  const QString &collectionName) -> QString;
  static auto applyHorizontalScrollbarSetting(QScrollArea *scrollArea, int collectionIndex,
                                              const QList<CollectionConfig> &collections) -> void;
  static auto applyVerticalScrollbarSetting(QScrollArea *scrollArea, int collectionIndex,
                                            const QList<CollectionConfig> &collections) -> void;

  /// copy the active kartend.cfg verbatim to @p destPath. Returns
  /// FileNotFound if the live config doesn't exist yet, FileWriteError if the
  /// destination can't be written.
  [[nodiscard]] static auto exportConfig(const QString &destPath) -> ErrorUtils::Result<void>;

  /// validate @p sourcePath as a Kartend INI, then atomically
  /// replace the live kartend.cfg with its contents. The previous config is
  /// preserved as kartend.cfg.bak in the same directory. Returns
  /// ConfigLoadFailed if the source can't be parsed as a Kartend config.
  /// Caller is expected to prompt the user to restart afterwards.
  [[nodiscard]] static auto importConfig(const QString &sourcePath) -> ErrorUtils::Result<void>;
};

#endif // SETTINGSUTILS_H
