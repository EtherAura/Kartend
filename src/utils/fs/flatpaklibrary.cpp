#include "flatpaklibrary.h"

#include <algorithm>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace FlatpakLibrary {

auto defaultExportRoots() -> QStringList {
  const QStringList candidates = {
      QStringLiteral("/var/lib/flatpak/exports/share"),
      QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share"),
  };
  QStringList existing;
  for (const QString &root : candidates) {
    if (QDir(root + QStringLiteral("/applications")).exists()) {
      existing.append(root);
    }
  }
  return existing;
}

auto parseDesktopFile(const QString &filePath) -> DesktopEntry {
  DesktopEntry entry;
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return entry;
  }
  QTextStream in(&file);
  bool inDesktopEntry = false;
  while (!in.atEnd()) {
    const QString line = in.readLine().trimmed();
    if (line.isEmpty() || line.startsWith('#')) {
      continue;
    }
    if (line.startsWith('[')) {
      inDesktopEntry = (line == QLatin1String("[Desktop Entry]"));
      continue;
    }
    if (!inDesktopEntry) {
      continue;
    }
    const qsizetype eq = line.indexOf('=');
    if (eq <= 0) {
      continue;
    }
    const QString key = line.left(eq).trimmed();
    const QString value = line.mid(eq + 1).trimmed();
    // Exact keys only — `Name[de]`-style locale variants fall through, so
    // the unlocalised Name is what lands in the stub filename and stays
    // stable across the user's locale changes.
    if (key == QLatin1String("Name")) {
      entry.name = value;
    } else if (key == QLatin1String("Icon")) {
      entry.icon = value;
    } else if (key == QLatin1String("Categories")) {
      entry.categories = value.split(';', Qt::SkipEmptyParts);
    } else if (key == QLatin1String("NoDisplay")) {
      entry.noDisplay = (value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);
    } else if (key == QLatin1String("X-Flatpak")) {
      entry.flatpakId = value;
    }
  }
  return entry;
}

auto findExportedIcon(const QString &exportShareRoot, const QString &appId) -> QString {
  // Largest-first so covers get the best raster Flatpak exported.
  static const QStringList kSizes = {
      QStringLiteral("512x512"), QStringLiteral("256x256"), QStringLiteral("192x192"),
      QStringLiteral("128x128"), QStringLiteral("96x96"),   QStringLiteral("64x64"),
  };
  for (const QString &size : kSizes) {
    const QString candidate = exportShareRoot + QStringLiteral("/icons/hicolor/") + size +
                              QStringLiteral("/apps/") + appId + QStringLiteral(".png");
    if (QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

auto installedGames(const QStringList &exportShareRoots) -> QList<App> {
  // Gaming-adjacent TOOLS also declare the Game category (ProtonUp-Qt is
  // "Game;Utility;", compatibility-layer managers and tweak utilities follow
  // the same shape). Real games never pair Game with one of these main
  // categories, so their presence is the tool signal.
  static const QStringList kToolCategories = {
      QStringLiteral("Utility"),
      QStringLiteral("Settings"),
      QStringLiteral("System"),
      QStringLiteral("Development"),
  };
  const auto isTool = [](const QStringList &categories) {
    return std::ranges::any_of(kToolCategories, [&categories](const QString &toolCategory) {
      return categories.contains(toolCategory);
    });
  };

  QList<App> apps;
  QStringList seenIds;
  for (const QString &root : exportShareRoots) {
    QDirIterator it(root + QStringLiteral("/applications"), {QStringLiteral("*.desktop")},
                    QDir::Files);
    while (it.hasNext()) {
      const QString desktopPath = it.next();
      const DesktopEntry entry = parseDesktopFile(desktopPath);
      if (entry.noDisplay || !entry.categories.contains(QLatin1String("Game")) ||
          isTool(entry.categories)) {
        continue;
      }
      App app;
      app.appId =
          entry.flatpakId.isEmpty() ? QFileInfo(desktopPath).completeBaseName() : entry.flatpakId;
      app.name = entry.name.isEmpty() ? app.appId : entry.name;
      if (app.appId.isEmpty() || seenIds.contains(app.appId)) {
        continue;
      }
      seenIds.append(app.appId);
      app.iconPath = findExportedIcon(root, app.appId);
      apps.append(app);
    }
  }
  std::sort(apps.begin(), apps.end(),
            [](const App &a, const App &b) { return a.name.localeAwareCompare(b.name) < 0; });
  return apps;
}

} // namespace FlatpakLibrary
