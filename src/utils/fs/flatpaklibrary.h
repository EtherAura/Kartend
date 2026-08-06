#ifndef KARTEND_UTILS_FS_FLATPAKLIBRARY_H
#define KARTEND_UTILS_FS_FLATPAKLIBRARY_H

#include <QList>
#include <QString>
#include <QStringList>

/// Read-only discovery of installed Flatpak games via the exports tree —
/// the `.desktop` files Flatpak writes under
/// `{installation}/exports/share/applications/` plus the matching exported
/// hicolor icons. Fully offline; no `flatpak` binary is invoked. Games are
/// recognised by the freedesktop `Categories=` field containing `Game`,
/// which also picks up natively-themed launchers like Lutris or Steam
/// installed as Flatpaks — callers wanting only directly-launchable titles
/// still get correct behaviour because launching is `flatpak run <app-id>`
/// either way.
///
/// Functions take explicit export roots so tests can stage a fake tree;
/// production callers use defaultExportRoots().
namespace FlatpakLibrary {

struct App {
  QString appId;    ///< Reverse-DNS Flatpak application id.
  QString name;     ///< Desktop-entry Name (unlocalised key).
  QString iconPath; ///< Largest exported raster icon; empty when none found.
};

/// Parsed subset of a `.desktop` file's `[Desktop Entry]` section. The
/// parser is deliberately minimal (exact keys, no locale variants) — it
/// exists because QSettings' INI dialect mangles desktop-file semantics
/// (semicolon lists, `%` field codes, case-sensitive keys).
struct DesktopEntry {
  QString name;
  QString icon;
  QString flatpakId; ///< X-Flatpak= value when present.
  QStringList categories;
  bool noDisplay = false;
};

/// The system and per-user Flatpak `exports/share` roots, existing ones only.
[[nodiscard]] QStringList defaultExportRoots();

/// Enumerates `<root>/applications/*.desktop` across the given
/// `exports/share` roots, keeping entries whose Categories contain "Game"
/// and which aren't NoDisplay. Gaming-adjacent tools that declare Game
/// alongside a tool-signal main category (Utility / Settings / System /
/// Development — e.g. ProtonUp-Qt's "Game;Utility;") are excluded: real
/// games never carry that pairing. The app id comes from X-Flatpak when
/// present, else the desktop file's basename. First occurrence of an id
/// wins (roots are probed in the order given).
[[nodiscard]] QList<App> installedGames(const QStringList &exportShareRoots);

[[nodiscard]] DesktopEntry parseDesktopFile(const QString &filePath);

/// Largest exported raster icon for the app id under
/// `<exportShareRoot>/icons/hicolor/<size>/apps/`. Empty when none.
[[nodiscard]] QString findExportedIcon(const QString &exportShareRoot, const QString &appId);

} // namespace FlatpakLibrary

#endif
