#ifndef KARTEND_UTILS_FS_DESKTOPENTRY_H
#define KARTEND_UTILS_FS_DESKTOPENTRY_H

#include <QString>
#include <QStringList>

/// Minimal freedesktop `.desktop` reading, shared by every import source that
/// discovers games through desktop entries (FlatpakLibrary's exports tree and
/// XdgGamesLibrary's menu scan — Kartend-4cff2).
///
/// The parser is deliberately not QSettings: QSettings' INI dialect mangles
/// desktop-file semantics (semicolon lists, `%` field codes, case-sensitive
/// keys). It reads exact keys from `[Desktop Entry]` only — `Name[de]`-style
/// locale variants fall through, so the unlocalised Name is what lands in a
/// stub filename and stays stable across the user's locale changes.
namespace DesktopEntryFile {

/// Parsed subset of one `[Desktop Entry]` section — the keys the import
/// sources actually key off, not the full spec.
struct Entry {
  QString name;
  QString icon;      ///< Theme icon name, or an absolute path.
  QString exec;      ///< Raw Exec value, field codes (%f, %U, …) included.
  QString tryExec;   ///< Presence test for the app's binary; often empty.
  QString type;      ///< "Application" for launchable entries.
  QString flatpakId; ///< X-Flatpak= value when present.
  QStringList categories;
  bool noDisplay = false;
  bool hidden = false; ///< `Hidden=true` means deleted, per the spec.
};

[[nodiscard]] Entry parse(const QString &filePath);

/// True when the entry describes a game rather than a gaming-adjacent tool.
///
/// Gaming tools also declare the Game category (ProtonUp-Qt is
/// "Game;Utility;", compatibility-layer managers and tweak utilities follow
/// the same shape). Real games never pair Game with one of those main
/// categories, so the pairing is the tool signal.
///
/// Emulators and emulator frontends ("Game;Emulator;" — RetroArch, Dolphin,
/// ES-DE) are deliberately NOT filtered: they are launchable programs, and
/// wanting one on the grid of a couch frontend is a real workflow. An
/// unwanted entry is one right-click away from hidden, whereas one the
/// importer refused cannot be recovered from the UI at all.
[[nodiscard]] bool isGame(const QStringList &categories);

/// The program token of an `Exec=` value: the first argument, unquoted, with
/// no field codes. Empty when Exec is empty or starts with a field code.
/// Used to recognise entries owned by another import source (a Steam-written
/// shortcut runs `steam`), not to build a launch command.
[[nodiscard]] QString execProgram(const QString &exec);

/// Largest raster icon for `iconName` under the given `share` roots, probing
/// `<root>/icons/hicolor/<size>/apps/` then `<root>/pixmaps/`. An absolute
/// path in `iconName` is returned as-is when it exists. SVG is deliberately
/// not considered: the artwork copier only accepts raster formats.
[[nodiscard]] QString findIcon(const QString &iconName, const QStringList &shareRoots);

/// The `share` roots a desktop-entry scan should consider: `$XDG_DATA_HOME`
/// (or ~/.local/share) followed by `$XDG_DATA_DIRS` (or the /usr/local/share
/// + /usr/share default), existing ones only, in order and deduplicated.
[[nodiscard]] QStringList defaultShareRoots();

} // namespace DesktopEntryFile

#endif
