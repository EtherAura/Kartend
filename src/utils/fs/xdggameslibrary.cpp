#include "xdggameslibrary.h"

#include <algorithm>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStandardPaths>

#include "desktopentry.h"

namespace XdgGamesLibrary {

namespace {

/// TryExec is the spec's "is this actually installed" probe: a bare name is
/// looked up in PATH, a path is tested directly. An unresolvable TryExec means
/// the entry is a leftover from an uninstalled package, which is exactly the
/// stale-stub case worth filtering.
bool tryExecResolves(const QString &tryExec) {
  if (tryExec.isEmpty()) {
    return true; // no claim made
  }
  if (tryExec.contains(QLatin1Char('/'))) {
    return QFileInfo(tryExec).isExecutable();
  }
  return !QStandardPaths::findExecutable(tryExec).isEmpty();
}

} // namespace

auto isFlatpakExportRoot(const QString &shareRoot) -> bool {
  const QString path = QDir::cleanPath(shareRoot);
  return path.contains(QLatin1String("/flatpak/exports/"));
}

auto isForeignLauncher(const QString &exec) -> bool {
  // The URI a launcher's own shortcut hands its client. Matched anywhere in
  // the line because the program may be a wrapper (`env … steam steam://…`,
  // `flatpak run com.valvesoftware.Steam steam://…`).
  static const QStringList kHandlerUris = {
      QStringLiteral("steam://"),
      QStringLiteral("lutris:"),
      QStringLiteral("heroic://"),
      QStringLiteral("itch://"),
  };
  for (const QString &uri : kHandlerUris) {
    if (exec.contains(uri, Qt::CaseInsensitive)) {
      return true;
    }
  }
  // Client binaries. Compared on the basename so an absolute
  // /usr/bin/bottles-cli matches the same way a bare name does.
  static const QStringList kLauncherPrograms = {
      QStringLiteral("steam"),   QStringLiteral("lutris"),      QStringLiteral("heroic"),
      QStringLiteral("itch"),    QStringLiteral("bottles-cli"), QStringLiteral("bottles"),
      QStringLiteral("flatpak"),
  };
  const QString program = QFileInfo(DesktopEntryFile::execProgram(exec)).fileName();
  return kLauncherPrograms.contains(program, Qt::CaseInsensitive);
}

auto installedGames(const QStringList &shareRoots) -> QList<Game> {
  QList<Game> games;
  QStringList seenIds;
  for (const QString &root : shareRoots) {
    if (isFlatpakExportRoot(root)) {
      continue;
    }
    QDirIterator it(root + QStringLiteral("/applications"), {QStringLiteral("*.desktop")},
                    QDir::Files);
    while (it.hasNext()) {
      const QString desktopPath = it.next();
      // The menu id is the basename: a user-level entry of the same id
      // overrides the system one, and only the winner may be imported.
      const QString id = QFileInfo(desktopPath).completeBaseName();
      if (id.isEmpty() || seenIds.contains(id)) {
        continue;
      }
      // Claim the id before any filtering: the menu resolves an id to exactly
      // one entry, first root wins. A user-level entry that is hidden, is not
      // a game, or points at an uninstalled binary still shadows the system
      // copy of the same id — falling through to that copy would resurrect
      // what the user's own data root deliberately overrides.
      seenIds.append(id);
      const DesktopEntryFile::Entry entry = DesktopEntryFile::parse(desktopPath);
      if (!DesktopEntryFile::isGame(entry.categories) || entry.noDisplay || entry.hidden ||
          entry.exec.isEmpty() || !entry.flatpakId.isEmpty() || isForeignLauncher(entry.exec) ||
          !tryExecResolves(entry.tryExec)) {
        continue;
      }
      if (!entry.type.isEmpty() && entry.type != QLatin1String("Application")) {
        continue; // Link/Directory entries are not launchable games
      }
      Game game;
      game.name = entry.name.isEmpty() ? id : entry.name;
      game.desktopFile = desktopPath;
      game.iconPath = DesktopEntryFile::findIcon(entry.icon, shareRoots);
      games.append(game);
    }
  }
  std::sort(games.begin(), games.end(),
            [](const Game &a, const Game &b) { return a.name.localeAwareCompare(b.name) < 0; });
  return games;
}

} // namespace XdgGamesLibrary
