#include "settingsutils.h"
#include "uiconstants.h"
#include "pathutils.h"
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QScrollArea>
#include <QTextStream>
#include <algorithm>

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
    return true;
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
  if (!appConfigDir.exists()) {
    appConfigDir.mkpath(".");
  }
  return appConfigDir.absoluteFilePath("kartend.cfg");
}

void SettingsUtils::loadMainScreenSettings(MainScreenConfig &config) {
  QSettings settings(getConfigPath(), getFormat());
  // Load from "Games" section instead of "General"
  settings.beginGroup("Games");
  config.gridWidth = settings.value("gridWidth", UIConstants::DEFAULT_GRID_WIDTH).toInt();
  config.horizontalAlignment = CollectionUtils::stringToAlignment(settings.value("horizontalAlignment", "center").toString());
  // showHiddenCollections might not be in CollectionConfig, so we keep it here or assume it's custom
  config.showHiddenCollections = settings.value("showHiddenCollections", false).toBool();
  settings.endGroup();

  config.gridWidth = std::max(config.gridWidth, UIConstants::MIN_GRID_WIDTH);
  config.gridWidth = std::min(config.gridWidth, UIConstants::MAX_GRID_WIDTH);
}

void SettingsUtils::saveMainScreenSettings(const MainScreenConfig &config) {
  QSettings settings(getConfigPath(), getFormat());
  // Save to "Games" section instead of "General"
  settings.beginGroup("Games");
  settings.setValue("gridWidth", config.gridWidth);
  settings.setValue("horizontalAlignment", CollectionUtils::alignmentToString(config.horizontalAlignment));
  settings.setValue("showHiddenCollections", config.showHiddenCollections);
  settings.endGroup();
  settings.sync();
}

auto SettingsUtils::expandConfigVariables(const QString &input,
                                            const QString &collectionName)
    -> QString {
  return PathUtils::validateAndExpandPath(input, collectionName);
}

void SettingsUtils::applyHorizontalScrollbarSetting(
    QScrollArea *itemScrollArea, int collectionIndex,
    const QList<CollectionConfig> &collections) {
  if ((itemScrollArea == nullptr) || collectionIndex < 0 ||
      collectionIndex >= collections.size()) {
    return;
  }
  const CollectionConfig &collection = collections[collectionIndex];
  if (collection.hideHorizontalScrollbar) {
    itemScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  } else {
    itemScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  }
}

void SettingsUtils::applyVerticalScrollbarSetting(
    QScrollArea *itemScrollArea, int collectionIndex,
    const QList<CollectionConfig> &collections) {
  if ((itemScrollArea == nullptr) || collectionIndex < 0 ||
      collectionIndex >= collections.size()) {
    return;
  }
  const CollectionConfig &collection = collections[collectionIndex];
  if (collection.hideVerticalScrollbar) {
    itemScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  } else {
    itemScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  }
}
