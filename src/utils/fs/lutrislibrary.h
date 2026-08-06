#ifndef KARTEND_UTILS_FS_LUTRISLIBRARY_H
#define KARTEND_UTILS_FS_LUTRISLIBRARY_H

#include "errorutils.h"
#include <QList>
#include <QString>

/// Read-only discovery of a local Lutris install's game library from its
/// SQLite database (`pga.db`, table `games`) plus the banner/coverart files
/// Lutris keeps alongside it. The database is opened read-only on a private
/// connection so a running Lutris instance is never disturbed.
///
/// Functions take an explicit Lutris data dir so tests can stage a fixture
/// database; production callers use defaultDataDir().
namespace LutrisLibrary {

struct Game {
  QString name;
  QString slug;   ///< Stable id; also the artwork filename and the lutris: URI path.
  QString runner; ///< "wine", "linux", "steam", … — informational.
};

struct Artwork {
  QString cover;  ///< coverart/<slug> portrait art (Lutris ≥ 0.5.8).
  QString banner; ///< banners/<slug> wide banner.
};

/// First existing Lutris data dir: ~/.local/share/lutris, then the Flatpak
/// app-data location. Empty when Lutris isn't found (no pga.db).
[[nodiscard]] QString defaultDataDir();

/// Installed, non-hidden games from `<dataDir>/pga.db`, sorted by name.
/// Errors (missing/unopenable/malformed database) come back as a
/// DatabaseConnectionFailed / DatabaseQueryFailed Result rather than an
/// empty list, so "Lutris has no games" and "the read failed" stay
/// distinguishable to the import UI.
[[nodiscard]] ErrorUtils::Result<QList<Game>> installedGames(const QString &dataDir);

[[nodiscard]] Artwork artworkFor(const QString &dataDir, const QString &slug);

} // namespace LutrisLibrary

#endif
