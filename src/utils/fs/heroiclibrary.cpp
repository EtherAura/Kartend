#include "heroiclibrary.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace HeroicLibrary {

namespace {

/// One library cache file and the top-level key its game array hangs off.
/// Heroic writes these through two different store helpers, which is why the
/// key is not the same for every runner.
struct CacheFile {
  const char *relativePath;
  const char *arrayKey;
  const char *defaultRunner;
};

const QList<CacheFile> &cacheFiles() {
  static const QList<CacheFile> kFiles = {
      {"store_cache/legendary_library.json", "library", "legendary"},
      {"store_cache/gog_library.json", "games", "gog"},
      {"store_cache/nile_library.json", "library", "nile"},
      {"sideload_apps/library.json", "games", "sideload"},
  };
  return kFiles;
}

QJsonArray readGameArray(const QString &filePath, const char *arrayKey) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  if (!doc.isObject()) {
    return {};
  }
  return doc.object().value(QLatin1String(arrayKey)).toArray();
}

/// Art Heroic happens to have on disk. Two opportunistic probes, both cheap:
/// the icon it downloads when the user creates a desktop shortcut, and — for
/// GOG — the icon the installer itself drops in the game folder. Everything
/// else in a Heroic library entry is a remote URL.
QString localIcon(const QString &configDir, const QString &appName, const QString &runner,
                  const QString &installPath) {
  if (runner == QLatin1String("gog") && !installPath.isEmpty()) {
    const QString nativeIcon = installPath + QStringLiteral("/support/icon.png");
    if (QFileInfo::exists(nativeIcon)) {
      return nativeIcon;
    }
  }
  // Heroic writes .jpg for the default (art_square) icon and .png when it
  // found a transparent one.
  for (const QString &suffix : {QStringLiteral(".png"), QStringLiteral(".jpg")}) {
    const QString candidate = configDir + QStringLiteral("/icons/") + appName + suffix;
    if (QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

} // namespace

auto defaultConfigDir() -> QString {
  const QStringList candidates = {
      QDir::homePath() + QStringLiteral("/.config/heroic"),
      QDir::homePath() + QStringLiteral("/.var/app/com.heroicgameslauncher.hgl/config/heroic"),
  };
  for (const QString &dir : candidates) {
    for (const CacheFile &cache : cacheFiles()) {
      if (QFileInfo::exists(dir + QLatin1Char('/') + QLatin1String(cache.relativePath))) {
        return dir;
      }
    }
  }
  return {};
}

auto installedGames(const QString &configDir) -> QList<Game> {
  QList<Game> games;
  if (configDir.isEmpty()) {
    return games;
  }
  QStringList seenAppNames;
  for (const CacheFile &cache : cacheFiles()) {
    const QJsonArray entries = readGameArray(
        configDir + QLatin1Char('/') + QLatin1String(cache.relativePath), cache.arrayKey);
    for (const auto &value : entries) {
      const QJsonObject entry = value.toObject();
      if (!entry.value(QStringLiteral("is_installed")).toBool()) {
        continue;
      }
      Game game;
      game.appName = entry.value(QStringLiteral("app_name")).toString();
      game.title = entry.value(QStringLiteral("title")).toString();
      game.runner = entry.value(QStringLiteral("runner")).toString();
      if (game.runner.isEmpty()) {
        game.runner = QLatin1String(cache.defaultRunner);
      }
      if (game.appName.isEmpty() || seenAppNames.contains(game.appName)) {
        continue;
      }
      if (game.title.isEmpty()) {
        game.title = game.appName;
      }
      seenAppNames.append(game.appName);
      const QJsonObject install = entry.value(QStringLiteral("install")).toObject();
      game.iconPath = localIcon(configDir, game.appName, game.runner,
                                install.value(QStringLiteral("install_path")).toString());
      games.append(game);
    }
  }
  std::sort(games.begin(), games.end(),
            [](const Game &a, const Game &b) { return a.title.localeAwareCompare(b.title) < 0; });
  return games;
}

auto launchUri(const Game &game) -> QString {
  const auto encode = [](const QString &value) {
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
  };
  return QStringLiteral("heroic://launch?appName=%1&runner=%2")
      .arg(encode(game.appName), encode(game.runner));
}

} // namespace HeroicLibrary
