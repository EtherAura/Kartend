#include "flatpaklibrary.h"

#include <algorithm>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include "desktopentry.h"

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
  // Kartend-4cff2: the parser itself now lives in DesktopEntryFile, shared
  // with the XDG menu scan. This struct stays as the Flatpak-shaped view of
  // it — the exports tree only ever needs these five keys.
  const DesktopEntryFile::Entry parsed = DesktopEntryFile::parse(filePath);
  DesktopEntry entry;
  entry.name = parsed.name;
  entry.icon = parsed.icon;
  entry.flatpakId = parsed.flatpakId;
  entry.categories = parsed.categories;
  entry.noDisplay = parsed.noDisplay;
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
  QList<App> apps;
  QStringList seenIds;
  for (const QString &root : exportShareRoots) {
    QDirIterator it(root + QStringLiteral("/applications"), {QStringLiteral("*.desktop")},
                    QDir::Files);
    while (it.hasNext()) {
      const QString desktopPath = it.next();
      const DesktopEntry entry = parseDesktopFile(desktopPath);
      if (entry.noDisplay || !DesktopEntryFile::isGame(entry.categories)) {
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
