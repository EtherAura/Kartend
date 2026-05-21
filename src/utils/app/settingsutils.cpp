// Resolves settings file paths and provides INI file handling utilities.
#include "settingsutils.h"
#include "errorutils.h"
#include "pathutils.h"
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScrollArea>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

namespace {
bool readIniFile(QIODevice &device, QSettings::SettingsMap &map) {
  QTextStream in(&device);
  QString currentSection;
  while (!in.atEnd()) {
    QString line = in.readLine().trimmed();
    if (line.isEmpty() || line.startsWith(';') || line.startsWith('#')) continue;
    if (line.startsWith('[') && line.endsWith(']')) {
      currentSection = line.mid(1, line.length() - 2);
    } else {
      int eqPos = line.indexOf('=');
      if (eqPos != -1) {
        QString key = line.left(eqPos).trimmed();
        QString value = line.mid(eqPos + 1).trimmed();
        if (!currentSection.isEmpty()) {
          map[currentSection + "/" + key] = value;
        } else {
          map[key] = value;
        }
      }
    }
  }
  return true;
}

bool writeIniFile(QIODevice &device, const QSettings::SettingsMap &map) {
  QTextStream out(&device);
  QMap<QString, QMap<QString, QVariant>> sections;
  QMap<QString, QVariant> rootKeys;

  for (auto it = map.begin(); it != map.end(); ++it) {
    QString fullKey = it.key();
    int lastSlash = fullKey.lastIndexOf('/');
    if (lastSlash != -1) {
      QString section = fullKey.left(lastSlash);
      QString key = fullKey.mid(lastSlash + 1);
      sections[section][key] = it.value();
    } else {
      rootKeys[fullKey] = it.value();
    }
  }

  for (auto it = rootKeys.begin(); it != rootKeys.end(); ++it) {
    out << it.key() << "=" << it.value().toString() << "\n";
  }
  if (!rootKeys.isEmpty() && !sections.isEmpty()) out << "\n";

  QStringList sectionNames = sections.keys();
  // QMap keys are already sorted, but we can ensure specific order if needed.
  // For now, alphabetical is fine and matches previous behavior.

  for (const QString &sectionName : sectionNames) {
    out << "[" << sectionName << "]\n";
    const auto &group = sections[sectionName];
    for (auto it = group.begin(); it != group.end(); ++it) {
      out << it.key() << "=" << it.value().toString() << "\n";
    }
    out << "\n";
  }
  out.flush();
  return out.status() == QTextStream::Ok;
}
} // namespace

auto SettingsUtils::getFormat() -> QSettings::Format {
  static QSettings::Format format = QSettings::InvalidFormat;
  if (format == QSettings::InvalidFormat) {
    format = QSettings::registerFormat("conf", readIniFile, writeIniFile);
  }
  return format;
}

auto SettingsUtils::getConfigPath() -> QString {
  QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
  QDir configDir(configRoot);
  QString appConfigPath = configDir.filePath("kartend");

  QDir appConfigDir(appConfigPath);
  if (!appConfigDir.exists() && !appConfigDir.mkpath(".")) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::ConfigSaveFailed,
                                          "Failed to create application config directory",
                                          "SettingsUtils::getConfigPath")
            .withDetails(QString("Path: %1").arg(appConfigPath)));
  }
  return appConfigDir.absoluteFilePath("kartend.cfg");
}

auto SettingsUtils::tightenConfigPermissions() -> void {
  const QString path = getConfigPath();
  if (!QFile::exists(path)) {
    return;
  }
  if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::info(ErrorUtils::ErrorCode::FileWriteError,
                                       "Failed to tighten kartend.cfg permissions to 0600",
                                       "SettingsUtils::tightenConfigPermissions")
            .withDetails(QString("Path: %1").arg(path)));
  }
}

auto SettingsUtils::expandConfigVariables(const QString &input, const QString &collectionName)
    -> QString {
  return PathUtils::validateAndExpandPath(input, collectionName);
}

void SettingsUtils::applyHorizontalScrollbarSetting(QScrollArea *itemScrollArea,
                                                    int collectionIndex,
                                                    const QList<CollectionConfig> &collections) {
  if ((!itemScrollArea) || collectionIndex < 0 || collectionIndex >= collections.size()) {
    return;
  }
  const CollectionConfig &collection = collections[collectionIndex];
  if (collection.gridLayout.hideHorizontalScrollbar) {
    itemScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  } else {
    itemScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  }
}

void SettingsUtils::applyVerticalScrollbarSetting(QScrollArea *itemScrollArea, int collectionIndex,
                                                  const QList<CollectionConfig> &collections) {
  if ((!itemScrollArea) || collectionIndex < 0 || collectionIndex >= collections.size()) {
    return;
  }
  const CollectionConfig &collection = collections[collectionIndex];
  if (collection.gridLayout.hideVerticalScrollbar) {
    itemScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  } else {
    itemScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  }
}

auto SettingsUtils::exportConfig(const QString &destPath) -> ErrorUtils::Result<void> {
  if (destPath.trimmed().isEmpty()) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                           "Destination path is empty",
                                           "SettingsUtils::exportConfig");
  }

  const QString sourcePath = getConfigPath();
  if (!QFile::exists(sourcePath)) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileNotFound,
                                           "No configuration file to export",
                                           "SettingsUtils::exportConfig")
        .withDetails(QString("Expected: %1").arg(sourcePath));
  }

  // Flush any pending QSettings writes so the on-disk file reflects current
  // in-memory state before we copy it.
  {
    QSettings live(sourcePath, getFormat());
    live.sync();
  }

  // Atomic copy: write to a sibling .tmp, then rename onto the destination.
  const QString tempPath = destPath + ".tmp";
  if (QFile::exists(tempPath)) {
    QFile::remove(tempPath);
  }
  if (!QFile::copy(sourcePath, tempPath)) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                           "Failed to write export file",
                                           "SettingsUtils::exportConfig")
        .withDetails(QString("Source: %1, Temp: %2").arg(sourcePath, tempPath));
  }
  if (QFile::exists(destPath)) {
    QFile::remove(destPath);
  }
  if (!QFile::rename(tempPath, destPath)) {
    QFile::remove(tempPath);
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                           "Failed to finalize export file",
                                           "SettingsUtils::exportConfig")
        .withDetails(QString("Destination: %1").arg(destPath));
  }
  return ErrorUtils::Result<void>::success();
}

auto SettingsUtils::importConfig(const QString &sourcePath) -> ErrorUtils::Result<void> {
  if (sourcePath.trimmed().isEmpty()) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                           "Source path is empty", "SettingsUtils::importConfig");
  }
  if (!QFile::exists(sourcePath)) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileNotFound,
                                           "Selected file does not exist",
                                           "SettingsUtils::importConfig")
        .withDetails(QString("Path: %1").arg(sourcePath));
  }

  // Validate that the file parses as a Kartend INI and contains a [General]
  // group — guards against the user picking an unrelated text file.
  {
    QSettings probe(sourcePath, getFormat());
    if (probe.status() != QSettings::NoError) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::ConfigLoadFailed,
                                             "Selected file is not a valid Kartend config",
                                             "SettingsUtils::importConfig")
          .withDetails(QString("Path: %1, status: %2")
                           .arg(sourcePath)
                           .arg(static_cast<int>(probe.status())));
    }
    if (!probe.childGroups().contains("General")) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::ConfigLoadFailed,
                                             "Selected file is missing the [General] section",
                                             "SettingsUtils::importConfig")
          .withDetails(QString("Path: %1").arg(sourcePath));
    }
  }

  const QString livePath = getConfigPath();
  // Ensure the parent directory exists (getConfigPath creates it, but be
  // defensive in case of unusual filesystem state).
  QDir liveDir = QFileInfo(livePath).absoluteDir();
  if (!liveDir.exists() && !liveDir.mkpath(".")) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::ConfigSaveFailed,
                                           "Failed to create config directory",
                                           "SettingsUtils::importConfig")
        .withDetails(QString("Path: %1").arg(liveDir.absolutePath()));
  }

  // Back up the existing config alongside the live file so a user can recover
  // by hand if the imported settings turn out to be wrong.
  if (QFile::exists(livePath)) {
    const QString backupPath = livePath + ".bak";
    if (QFile::exists(backupPath)) {
      QFile::remove(backupPath);
    }
    if (!QFile::copy(livePath, backupPath)) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                            "Failed to back up existing config before import",
                                            "SettingsUtils::importConfig")
              .withDetails(QString("From: %1, To: %2").arg(livePath, backupPath)));
      // Continue: the import is still atomic via temp+rename, and refusing to
      // proceed would leave the user stuck if .bak is unwritable for some
      // unrelated reason.
    }
  }

  // Atomic replace: copy to .tmp next to the live config, then rename.
  const QString tempPath = livePath + ".import.tmp";
  if (QFile::exists(tempPath)) {
    QFile::remove(tempPath);
  }
  if (!QFile::copy(sourcePath, tempPath)) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                           "Failed to stage imported config",
                                           "SettingsUtils::importConfig")
        .withDetails(QString("Source: %1, Temp: %2").arg(sourcePath, tempPath));
  }
  if (QFile::exists(livePath)) {
    QFile::remove(livePath);
  }
  if (!QFile::rename(tempPath, livePath)) {
    QFile::remove(tempPath);
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                           "Failed to install imported config",
                                           "SettingsUtils::importConfig")
        .withDetails(QString("Destination: %1").arg(livePath));
  }
  // Imported file's permissions inherit from the source; clamp to 0600
  // so cleartext scraper credentials don't leak via an over-permissive import.
  tightenConfigPermissions();
  return ErrorUtils::Result<void>::success();
}
