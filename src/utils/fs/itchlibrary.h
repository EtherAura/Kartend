#ifndef KARTEND_UTILS_FS_ITCHLIBRARY_H
#define KARTEND_UTILS_FS_ITCHLIBRARY_H

#include "errorutils.h"
#include <QList>
#include <QString>

/// Read-only discovery of the games the itch.io desktop app has installed,
/// from butler's SQLite database (`butler.db`) — the `caves` table (one row
/// per installed game) joined to the cached `games` metadata (Kartend-4cff2).
/// The database is opened read-only on a private connection so a running itch
/// app is never disturbed.
///
/// No local artwork is reported: itch caches cover art as remote URLs only, so
/// an imported collection starts with placeholder art.
///
/// Functions take an explicit itch config dir so tests can stage a fixture
/// database; production callers use defaultConfigDir().
namespace ItchLibrary {

struct Game {
  QString caveId; ///< Install id; the cave in the launch URI.
  QString title;
};

/// First existing itch config dir: ~/.config/itch, then the Flatpak app-config
/// location. Empty when the itch app isn't found (no butler.db).
[[nodiscard]] QString defaultConfigDir();

/// Installed games from `<configDir>/db/butler.db`, sorted by title. Caves
/// whose game metadata classifies them as something other than a game (tools,
/// asset packs, soundtracks) are skipped.
///
/// Errors (missing/unopenable/malformed database) come back as a
/// DatabaseConnectionFailed / DatabaseQueryFailed Result rather than an empty
/// list, so "itch has no games" and "the read failed" stay distinguishable to
/// the import UI.
[[nodiscard]] ErrorUtils::Result<QList<Game>> installedGames(const QString &configDir);

/// `itch://caves/<caveId>/launch` — the app's own launch URL for an install.
[[nodiscard]] QString launchUri(const Game &game);

} // namespace ItchLibrary

#endif
