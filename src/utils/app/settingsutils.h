#ifndef SETTINGSUTILS_H
#define SETTINGSUTILS_H

#include "collection/collectionconfig.h"
#include "errorutils.h"
#include <QList>
#include <QSettings>
#include <QString>

class QWidget;
class QScrollArea;

class SettingsUtils {
public:
  [[nodiscard]] static auto getConfigPath() -> QString;
  /// Path to the JSON file storing user-defined layout / theme profiles.
  /// Sibling to kartend.cfg under the same per-user config dir. Caller
  /// writes via QSaveFile + syncDirectory; we do not pre-create an empty
  /// file so a fresh install stays clean.
  [[nodiscard]] static auto getLayoutProfilesPath() -> QString;
  /// Path to the JSON file storing presentation / attract-mode
  /// profiles (Kartend-6pp5). Sibling to kartend.cfg.
  [[nodiscard]] static auto getPresentationProfilesPath() -> QString;
  /// Path to the JSON file storing saved search presets (Kartend-jklv4) —
  /// a named query plus the filter/sort state around it. Sibling to
  /// kartend.cfg, same shape as the two profile registries above.
  [[nodiscard]] static auto getSearchPresetsPath() -> QString;
  [[nodiscard]] static auto getFormat() -> QSettings::Format;
  /// Restrict kartend.cfg to user-only read+write (0600). The INI carries
  /// scraper credentials in cleartext under [Scrapers]; default umask on
  /// most Linux setups leaves it world-readable. Call after every QSettings
  /// sync that touches kartend.cfg.
  static auto tightenConfigPermissions() -> void;
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
