#ifndef KARTEND_UTILS_FS_XDGGAMESLIBRARY_H
#define KARTEND_UTILS_FS_XDGGAMESLIBRARY_H

#include <QList>
#include <QString>
#include <QStringList>

/// Read-only discovery of natively-installed games from the freedesktop
/// application menu — the `.desktop` files under each XDG data root's
/// `applications/` directory whose Categories declare `Game` (Kartend-4cff2).
/// This is the source that covers distro-packaged games (the package manager
/// wrote the menu entry) and anything else that installed a menu entry
/// without being a launcher Kartend integrates with directly.
///
/// Entries OWNED BY ANOTHER IMPORT SOURCE are excluded, because importing them
/// here would duplicate that source's collection with worse metadata: Steam
/// writes `Exec=steam steam://rungameid/<id>` shortcuts into
/// ~/.local/share/applications, Flatpak exports its apps into a data root that
/// is on XDG_DATA_DIRS, and Lutris / Heroic / Bottles / itch all write menu
/// entries that shell out to their own client. See isForeignLauncher().
///
/// Fully offline; no process is spawned. Functions take explicit share roots
/// so tests can stage a fake tree; production callers use
/// DesktopEntryFile::defaultShareRoots().
namespace XdgGamesLibrary {

struct Game {
  QString name;
  QString desktopFile; ///< Absolute path — also the launch target (`gio launch`).
  QString iconPath;    ///< Largest raster icon found; empty when none.
};

/// Games from `<root>/applications/*.desktop` across the given share roots,
/// sorted by name. The first occurrence of a desktop-file id wins, so a
/// user-level override of a system entry shadows it exactly as the menu spec
/// says (roots are probed in the order given).
[[nodiscard]] QList<Game> installedGames(const QStringList &shareRoots);

/// True when this Exec line belongs to a launcher Kartend imports separately,
/// so the dedicated source owns the game instead. Matches both the program
/// (`steam`, `lutris`, `heroic`, `bottles-cli`, `itch`, `flatpak`) and the
/// handler URIs those clients are invoked with.
[[nodiscard]] bool isForeignLauncher(const QString &exec);

/// Flatpak's exported applications live on XDG_DATA_DIRS, so a plain menu
/// scan would re-import every Flatpak game. True for the export roots
/// FlatpakLibrary owns.
[[nodiscard]] bool isFlatpakExportRoot(const QString &shareRoot);

} // namespace XdgGamesLibrary

#endif
