#ifndef CONFIGUTILS_H
#define CONFIGUTILS_H

#include <QString>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>

namespace ConfigUtils {

/// Returns the path to the application's configuration file
inline auto getConfigPath() -> QString {
  QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  QDir configDir(configPath);
  if (!configDir.exists()) {
    configDir.mkpath(".");
  }
  return configPath + "/kartend.cfg";
}

/// Returns the settings format used for configuration files
inline auto getFormat() -> QSettings::Format {
  return QSettings::IniFormat;
}

/// Expands special variables in config strings (e.g., {collection} -> collection name)
inline auto expandConfigVariables(const QString &input, const QString &collectionName) -> QString {
  QString result = input;
  result.replace("{collection}", collectionName);
  result.replace("{COLLECTION}", collectionName.toUpper());
  return result;
}

} // namespace ConfigUtils

#endif // CONFIGUTILS_H
