#ifndef KARTEND_UTILS_FS_ESDELIBRARY_H
#define KARTEND_UTILS_FS_ESDELIBRARY_H

#include <QList>
#include <QString>
#include <QStringList>

/// Read-only discovery of an ES-DE (EmulationStation Desktop Edition) library
/// — its systems, their ROMs, and the metadata and scraped media ES-DE keeps
/// alongside them (Kartend-ilkne).
///
/// THE SHAPE OF THIS READER WAS DECIDED BY A REAL ES-DE 3.4.1 INSTALL, and it
/// is not the obvious one. Three facts drive it, each verified rather than
/// assumed:
///
///  1. `gamelist.xml` IS A METADATA SIDECAR, NOT THE LIBRARY. It contains only
///     entries that have metadata — after favouriting one game of four, the
///     gamelist held exactly one <game>. ES-DE itself lists games by scanning
///     the ROM directory. Reading the gamelist as the game list would import
///     only the games a user happens to have scraped.
///
///  2. THE AUTHORITATIVE SYSTEM DEFINITIONS ARE OUT OF REACH. es_systems.xml
///     (per-system ROM subdir, extensions, emulator command) ships inside the
///     ES-DE binary — for the AppImage, under a /tmp mount whose path changes
///     every launch and does not exist while ES-DE is closed. Only
///     `custom_systems/es_systems.xml` (user overrides, usually absent) is
///     stable. So systems are discovered as the SUBDIRECTORIES of the ROM
///     directory, which is what the user sees and what ES-DE keys off anyway.
///
///  3. THE SETTINGS STORE EMPTY-MEANS-DEFAULT. A stock es_settings.xml holds
///     `<string name="ROMDirectory" value="" />`; empty means ~/ROMs, not
///     "unset". Treating empty as absent finds nothing on a default install.
///
/// Functions take explicit directories so tests can stage a fixture tree;
/// production callers use defaultDataDir().
namespace EsdeLibrary {

/// One ES-DE system — a subdirectory of the ROM directory that holds games.
struct System {
  QString name;   ///< Directory name, which is ES-DE's system id ("nes").
  QString romDir; ///< Absolute path to that subdirectory.
  int gameCount = 0;
};

/// One game: the ROM plus whatever the gamelist and media tree add.
struct Game {
  QString romPath; ///< Absolute path to the ROM — the launch target.
  QString title;   ///< gamelist <name> when present, else the file's base name.
  QString description;
  QString developer;
  QString publisher;
  QString genre;
  QString players;
  QString releaseDate; ///< ES-DE's basic ISO8601 ("19910623T000000"), verbatim.
  QString coverPath;   ///< downloaded_media/<system>/covers/
  QString logoPath;    ///< …/marquees/
  QString screenshotPath;
  QString fanartPath;
};

/// First existing ES-DE data dir: ~/ES-DE (3.0 and later), then the legacy
/// ~/.emulationstation. Empty when neither is present.
[[nodiscard]] QString defaultDataDir();

/// The ROM root. Reads `ROMDirectory` from `<dataDir>/settings/es_settings.xml`
/// and falls back to ~/ROMs when the value is empty — which is how a stock
/// install stores it. `~` in a configured value is expanded.
[[nodiscard]] QString romDirectory(const QString &dataDir);

/// The scraped-media root: `MediaDirectory` when set, else
/// `<dataDir>/downloaded_media`. Empty on a fresh install, and ES-DE only
/// creates the per-system subdirectories when something is actually scraped.
[[nodiscard]] QString mediaDirectory(const QString &dataDir);

/// Subdirectories of `romDir` that contain at least one game file, sorted by
/// name. Directories holding only ES-DE's own bookkeeping are skipped.
[[nodiscard]] QList<System> systems(const QString &romDir);

/// The games of one system: every ROM file in its directory, with gamelist
/// metadata and scraped media applied where they exist. Games the gamelist
/// marks `<hidden>true</hidden>` are omitted, mirroring ES-DE's own display
/// rule. Sorted by title.
[[nodiscard]] QList<Game> games(const System &system, const QString &dataDir,
                                const QString &mediaDir);

/// Extensions treated as ROMs. Deliberately broad: the per-system lists live
/// in the unreachable es_systems.xml (see above), so this admits the common
/// cartridge/disc/archive forms and lets the collection's own extension list
/// narrow things afterwards. ES-DE's own bookkeeping files are excluded.
[[nodiscard]] const QStringList &romExtensions();

} // namespace EsdeLibrary

#endif
