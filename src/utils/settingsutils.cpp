#include "settingsutils.h"
#include "uiconstants.h"
#include "pathutils.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QScrollArea>
#include <algorithm>

auto SettingsUtils::getConfigPath() -> QString {
  QDir configDir(QDir::homePath() + "/.config/kartend");
  return configDir.absoluteFilePath("kartend.cfg");
}

void SettingsUtils::loadMainScreenSettings(MainScreenConfig &config) {
  QFile file(getConfigPath());
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    config.gridWidth = UIConstants::DEFAULT_GRID_WIDTH;
    config.horizontalAlignment = HorizontalAlignment::Center;
    config.showHiddenCollections = false;
    return;
  }
  QTextStream inputStream(&file);
  bool inGeneral = false;
  while (!inputStream.atEnd()) {
    QString line = inputStream.readLine().trimmed();
    if (line.startsWith('[') && line.endsWith(']')) {
      QString section = line.mid(1, line.length() - 2);
      inGeneral = (section == "General");
      continue;
    }
    if (!inGeneral) {
      continue;
    }
    int equalPos = line.indexOf('=');
    if (equalPos == -1) {
      continue;
    }
    QString key = line.left(equalPos);
    QString value = line.mid(equalPos + 1);
    if (key == "MainScreen_gridWidth") {
      config.gridWidth = value.toInt();
    } else if (key == "MainScreen_horizontalAlignment") {
      config.horizontalAlignment = stringToAlignment(value);
    } else if (key == "MainScreen_showHiddenCollections") {
      config.showHiddenCollections = (value == "true");
    }
  }
  file.close();
  config.gridWidth = std::max(config.gridWidth, UIConstants::MIN_GRID_WIDTH);
  config.gridWidth = std::min(config.gridWidth, UIConstants::MAX_GRID_WIDTH);
}

namespace {
// Reads all lines and removes existing MainScreen_* keys from [General]
auto readAndFilterGeneralSection(QFile &file, QStringList &lines,
                                 bool &foundGeneral) -> void {
  bool inGeneral = false;
  QTextStream inputStream(&file);
  while (!inputStream.atEnd()) {
    const QString line = inputStream.readLine();
    const QString trimmed = line.trimmed();
    if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
      const QString section = trimmed.mid(1, trimmed.length() - 2);
      inGeneral = (section == "General");
      if (inGeneral) {
        foundGeneral = true;
      }
      lines.append(line);
      continue;
    }
    if (inGeneral) {
      if (trimmed.startsWith("MainScreen_gridWidth=") ||
          trimmed.startsWith("MainScreen_horizontalAlignment=") ||
          trimmed.startsWith("MainScreen_showHiddenCollections=")) {
        continue;
      }
    }
    lines.append(line);
  }
}

// Inserts the three MainScreen_* entries immediately after [General]
void insertMainScreenEntries(QStringList &lines,
                             const MainScreenConfig &config) {
  for (int i = 0; i < lines.size(); ++i) {
    if (lines[i].trimmed() == "[General]") {
      lines.insert(i + 1,
                   QString("MainScreen_gridWidth=%1").arg(config.gridWidth));
      lines.insert(i + 2,
                   QString("MainScreen_horizontalAlignment=%1")
                       .arg(alignmentToString(config.horizontalAlignment)));
      lines.insert(i + 3,
                   QString("MainScreen_showHiddenCollections=%1")
                       .arg(config.showHiddenCollections ? "true" : "false"));
      break;
    }
  }
}
} // namespace

void SettingsUtils::saveMainScreenSettings(const MainScreenConfig &config) {
  QString configPath = getConfigPath();
  QStringList lines;
  bool foundGeneral = false;

  QFile file(configPath);
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    readAndFilterGeneralSection(file, lines, foundGeneral);
    file.close();
  }

  if (!foundGeneral) {
    lines.prepend("");
    lines.prepend("[General]");
  }

  insertMainScreenEntries(lines, config);

  if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&file);
    for (const QString &line : lines) {
      out << line << "\n";
    }
    file.close();
  }
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
