#ifndef KARTEND_UTILS_FS_HEROICLIBRARY_H
#define KARTEND_UTILS_FS_HEROICLIBRARY_H

#include <QList>
#include <QString>

/// Read-only discovery of a local Heroic Games Launcher library — the Epic
/// (legendary), GOG, Amazon (nile) and sideloaded games Heroic manages
/// (Kartend-4cff2). Reads Heroic's own JSON library caches under its config
/// directory; no `heroic` process is started and nothing is fetched.
///
/// WHY THE CACHES AND NOT THE PER-RUNNER installed.json FILES: the caches are
/// the files Heroic's own library view renders from, they carry the display
/// title and the runner in one uniform shape across all four runners, and each
/// entry states `is_installed` — whereas the installed.json files have a
/// different layout per runner and carry no titles. An entry is imported only
/// when its cache record says it is installed.
///
/// Functions take an explicit config dir so tests can stage a fixture tree;
/// production callers use defaultConfigDir().
namespace HeroicLibrary {

struct Game {
  QString appName; ///< Heroic's internal id; the `appName` in the launch URI.
  QString runner;  ///< "legendary" | "gog" | "nile" | "sideload".
  QString title;
  /// Icon Heroic already cached on disk (shortcut icons, or a GOG install's
  /// own icon). Empty for most games — Heroic's cover art lives at a URL,
  /// not on disk.
  QString iconPath;
  /// Remote cover, preferring the PORTRAIT art_square over the wide
  /// art_cover: the grid renders portrait tiles. Heroic writes a literal
  /// "{ext}" placeholder into some Epic URLs, already substituted here.
  /// Downloading is the caller's business — the reader stays offline
  /// (Kartend-g1g30).
  QString coverUrl;
};

/// First existing Heroic config dir: ~/.config/heroic, then the Flatpak
/// app-config location. Empty when Heroic isn't found (no library cache).
[[nodiscard]] QString defaultConfigDir();

/// Installed games across every runner's library cache, sorted by title.
/// Unreadable or malformed caches are skipped, so one corrupt runner file
/// cannot hide the others' games.
[[nodiscard]] QList<Game> installedGames(const QString &configDir);

/// `heroic://launch?appName=<id>&runner=<runner>`, percent-encoded. This is
/// the current protocol shape; Heroic also still accepts the older
/// `heroic://launch/<runner>/<appName>` path form.
[[nodiscard]] QString launchUri(const Game &game);

} // namespace HeroicLibrary

#endif
