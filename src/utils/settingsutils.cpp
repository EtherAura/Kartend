#include "settingsutils.h"
#include "uiconstants.h"
#include "pathutils.h"
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QScrollArea>
#include <algorithm>

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
  QSettings settings(getConfigPath(), QSettings::IniFormat);
  settings.beginGroup("General");
  config.gridWidth = settings.value("MainScreen_gridWidth", UIConstants::DEFAULT_GRID_WIDTH).toInt();
  config.horizontalAlignment = stringToAlignment(settings.value("MainScreen_horizontalAlignment", "center").toString());
  config.showHiddenCollections = settings.value("MainScreen_showHiddenCollections", false).toBool();
  settings.endGroup();

  config.gridWidth = std::max(config.gridWidth, UIConstants::MIN_GRID_WIDTH);
  config.gridWidth = std::min(config.gridWidth, UIConstants::MAX_GRID_WIDTH);
}

void SettingsUtils::saveMainScreenSettings(const MainScreenConfig &config) {
  QSettings settings(getConfigPath(), QSettings::IniFormat);
  settings.beginGroup("General");
  settings.setValue("MainScreen_gridWidth", config.gridWidth);
  settings.setValue("MainScreen_horizontalAlignment", alignmentToString(config.horizontalAlignment));
  settings.setValue("MainScreen_showHiddenCollections", config.showHiddenCollections);
  settings.endGroup();
  settings.sync();
}

auto SettingsUtils::expandConfigVariables(const QString &input,
                                            const QString &collectionName)
    -> QString {
  return PathUtils::validateAndExpandPath(input, collectionName);
}

void SettingsUtils::applyHorizontalScrollbarSetting(
    QWidget *parent, int collectionIndex,
    const QList<CollectionConfig> &collections) {
  auto *itemScrollArea = parent->findChild<QScrollArea *>("itemScrollArea");
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
    QWidget *parent, int collectionIndex,
    const QList<CollectionConfig> &collections) {
  auto *itemScrollArea = parent->findChild<QScrollArea *>("itemScrollArea");
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
