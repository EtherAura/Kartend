#ifndef KARTEND_UTILS_FS_STEAMLIBRARY_H
#define KARTEND_UTILS_FS_STEAMLIBRARY_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

/// Read-only discovery of a local Steam install's *installed* game library.
/// Everything works from on-disk files — appmanifest_*.acf keyvalues,
/// libraryfolders.vdf, and the appcache/librarycache artwork Steam already
/// downloaded — so no network, no API key, and no credentials are involved.
/// Owned-but-not-installed titles are invisible by design (they only exist
/// in the web API; explicitly out of scope for launcher import v1).
///
/// Every function takes an explicit Steam root so tests can stage a fake
/// install tree; production callers get the root from defaultRoot().
namespace SteamLibrary {

struct Game {
  QString appId;
  QString name;
  QString installDir;  ///< `installdir` manifest value (folder name, not absolute).
  QString libraryRoot; ///< The library folder whose steamapps/ carried the manifest.
};

/// Artwork Steam has cached locally for one app. Any member may be empty.
struct Artwork {
  QString cover; ///< Portrait 600x900 library capsule (header.jpg fallback).
  QString logo;  ///< Transparent title logo.
  QString hero;  ///< Wide library hero banner.
};

/// Minimal parser for Valve's text KeyValues ("VDF") format — quoted or bare
/// tokens, `{}` nesting, `//` comments, `\"` `\\` `\n` `\t` escapes, and
/// `[$CONDITION]` tags (skipped). Duplicate keys keep the last value. Maps
/// nest as QVariantMap; every leaf is a QString. Forgiving by intent: it
/// parses what it can and returns an empty map only for hopeless input.
[[nodiscard]] QVariantMap parseVdf(const QString &text);

/// First existing Steam root among the conventional install locations
/// (~/.steam/steam, ~/.local/share/Steam, the Flatpak app dir, ~/.steam/root)
/// — "existing" meaning it has a steamapps/ subdirectory. Empty when Steam
/// isn't found.
[[nodiscard]] QString defaultRoot();

/// Library roots from `<steamRoot>/steamapps/libraryfolders.vdf` — both the
/// current ("path" sub-key) and legacy (plain string value) layouts — with
/// the Steam root itself always included and duplicates removed. Honouring
/// this list is what makes games installed on a second drive visible.
[[nodiscard]] QStringList libraryFolders(const QString &steamRoot);

/// Parses every appmanifest_*.acf across all library folders. Runtime
/// tooling (Proton builds, Steam Linux Runtime, Steamworks redistributables)
/// is filtered out via isRuntimeTool. Sorted by name.
[[nodiscard]] QList<Game> installedGames(const QString &steamRoot);

/// Locally-cached artwork for an app id, probing both librarycache layouts:
/// the post-2023 per-app subdirectory (librarycache/<appid>/library_600x900.jpg)
/// and the older flat naming (librarycache/<appid>_library_600x900.jpg).
[[nodiscard]] Artwork artworkFor(const QString &steamRoot, const QString &appId);

/// True for Steam's non-game support apps that ship as regular appmanifests:
/// Proton versions, Steam Linux Runtime containers, Steamworks common
/// redistributables. Matched on the manifest name.
[[nodiscard]] bool isRuntimeTool(const QString &name);

} // namespace SteamLibrary

#endif
