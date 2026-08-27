#include "launcherimportservice.h"

#include <algorithm>
#include <atomic>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "bottleslibrary.h"
#include "dbtxn.h"
#include "desktopentry.h"
#include "esdelibrary.h"
#include "flatpaklibrary.h"
#include "heroiclibrary.h"
#include "itchlibrary.h"
#include "itemmetadata.h"
#include "kartlink.h"
#include "lutrislibrary.h"
#include "pathutils.h"
#include "steamappinfo.h"
#include "steamlibrary.h"
#include "xdggameslibrary.h"
#include <uiconstants/grid.h>
#include <uiconstants/item.h>

namespace LauncherImportService {

namespace {

/// The raster extensions ArtworkUtils::findArtworkForFile probes; a file
/// with any of these under the typed subdir means the slot is taken.
/// Function-local static (not namespace scope) so the QStringList
/// construction can't throw during static initialization.
const QStringList &artworkExtensions() {
  static const QStringList kExtensions = {
      QStringLiteral("png"),  QStringLiteral("jpg"), QStringLiteral("jpeg"),
      QStringLiteral("webp"), QStringLiteral("gif"), QStringLiteral("bmp"),
  };
  return kExtensions;
}

bool artworkSlotTaken(const QString &typedDir, const QString &baseName) {
  return std::ranges::any_of(artworkExtensions(), [&typedDir, &baseName](const QString &ext) {
    return QFileInfo::exists(typedDir + QLatin1Char('/') + baseName + QLatin1Char('.') + ext);
  });
}

/// Renders a scalable icon into `destPath` as PNG. Returns false when Qt has
/// no SVG reader, which is a legitimate runtime state rather than an error:
/// the format comes from the `qsvg` imageformats PLUGIN, so no Qt6::Svg link
/// dependency is taken and a build without the plugin simply gets no cover,
/// exactly as before (Kartend-0tddh).
///
/// Rendered through QImageReader::setScaledSize rather than QImage::scaled:
/// the SVG handler honours it by rasterising the VECTOR at that size, whereas
/// scaling afterwards would upscale the icon's small intrinsic raster (256px
/// is typical) and look soft on a 4K grid.
bool rasterizeScalableIcon(const QString &sourcePath, const QString &destPath) {
  constexpr int kRenderSize = 512; // matches the largest hicolor raster probed
  QImageReader reader(sourcePath);
  if (!reader.canRead()) {
    return false;
  }
  const QSize intrinsic = reader.size();
  QSize target(kRenderSize, kRenderSize);
  if (intrinsic.isValid() && !intrinsic.isEmpty()) {
    // Keep the icon's own aspect ratio; square is only the common case.
    target = intrinsic.scaled(kRenderSize, kRenderSize, Qt::KeepAspectRatio);
  }
  if (reader.supportsOption(QImageIOHandler::ScaledSize)) {
    reader.setScaledSize(target);
  }
  const QImage rendered = reader.read();
  if (rendered.isNull()) {
    return false;
  }
  return rendered.save(destPath, "PNG");
}

void copyArtworkIfMissing(const QString &sourcePath, const QString &typedDir,
                          const QString &baseName, SyncResult &result) {
  if (sourcePath.isEmpty() || artworkSlotTaken(typedDir, baseName)) {
    return;
  }
  QString suffix = QFileInfo(sourcePath).suffix().toLower();
  // Scalable sources are RENDERED, not copied — the scan reports them only
  // when the theme has no raster (Kartend-0tddh). The written file is a PNG
  // regardless of the source suffix, so the name never lies about content.
  const bool isScalable = (suffix == QLatin1String("svg") || suffix == QLatin1String("svgz"));
  if (!isScalable && !artworkExtensions().contains(suffix)) {
    return;
  }
  if (!QDir().mkpath(typedDir)) {
    result.errors.append(QStringLiteral("Cannot create artwork directory %1").arg(typedDir));
    return;
  }
  if (isScalable) {
    const QString destPath = typedDir + QLatin1Char('/') + baseName + QStringLiteral(".png");
    if (rasterizeScalableIcon(sourcePath, destPath)) {
      ++result.artworkCopied;
    }
    // No error recorded on failure: an absent SVG reader is a normal runtime
    // state, and a missing cover is not a failed import.
    return;
  }
  const QString destPath = typedDir + QLatin1Char('/') + baseName + QLatin1Char('.') + suffix;
  if (QFile::copy(sourcePath, destPath)) {
    ++result.artworkCopied;
  } else {
    result.errors.append(QStringLiteral("Cannot copy artwork %1 -> %2").arg(sourcePath, destPath));
  }
}

/// One entry from an appid + the title to show. `info` may be null (no
/// appinfo record): artwork then falls back to the guessed librarycache
/// filenames, which is the pre-appinfo behaviour.
GameEntry makeSteamEntry(const QString &root, const QString &appId, const QString &title,
                         const SteamAppInfo::AppMetadata *info) {
  SteamLibrary::Artwork art = SteamLibrary::artworkFor(root, appId);
  if (info != nullptr) {
    const QString cacheDir =
        root + QStringLiteral("/appcache/librarycache/") + appId + QLatin1Char('/');
    const auto preferExact = [&cacheDir](const QString &relative, QString &slot) {
      if (!relative.isEmpty() && QFileInfo::exists(cacheDir + relative)) {
        slot = cacheDir + relative;
      }
    };
    preferExact(info->libraryCoverPath, art.cover);
    preferExact(info->libraryHeroPath, art.hero);
    preferExact(info->libraryLogoPath, art.logo);
  }
  GameEntry entry;
  entry.title = title;
  // Valid for uninstalled games too — Steam offers to install on launch.
  entry.target = QStringLiteral("steam://rungameid/") + appId;
  entry.coverPath = art.cover;
  entry.logoPath = art.logo;
  entry.heroPath = art.hero;
  return entry;
}

QList<GameEntry> steamEntries(ImportScope scope) {
  QList<GameEntry> entries;
  const QString root = SteamLibrary::defaultRoot();
  if (root.isEmpty()) {
    return entries;
  }
  const QList<SteamLibrary::Game> games = SteamLibrary::installedGames(root);

  // Installed apps always need their appinfo record: exact librarycache asset
  // paths (newer Steam hash-names some assets, which filename guessing
  // misses — Kartend-40i6h) and the type field as a sturdier non-game
  // filter than the manifest-name regex. Absence of appinfo (or of one
  // app's record) degrades gracefully to the guessed filenames.
  QSet<QString> installedIds;
  for (const SteamLibrary::Game &game : games) {
    installedIds.insert(game.appId);
  }

  // Candidates beyond the installed set, per tier. Owned asks appinfo about a
  // known id list (records outside it are skipped wholesale, which is cheap);
  // AllRecognized has no list to ask about and must read every record.
  QSet<QString> wanted = installedIds;
  QSet<QString> extra;
  const bool allRecognized = scope == ImportScope::AllRecognized;
  if (scope == ImportScope::Owned) {
    extra = SteamLibrary::playedAppIds(root);
    extra.subtract(installedIds);
    wanted.unite(extra);
  }

  QHash<QString, SteamAppInfo::AppMetadata> apps;
  if (const auto parsed = SteamAppInfo::read(SteamAppInfo::defaultAppInfoPath(root),
                                             allRecognized ? QSet<QString>() : wanted);
      !parsed.isError()) {
    apps = parsed.value();
  }

  if (allRecognized) {
    for (auto it = apps.constBegin(); it != apps.constEnd(); ++it) {
      if (it->isGameType() && !installedIds.contains(it.key())) {
        extra.insert(it.key());
      }
    }
  }

  entries.reserve(games.size() + extra.size());
  for (const SteamLibrary::Game &game : games) {
    const auto appIt = apps.constFind(game.appId);
    if (appIt != apps.constEnd() && !appIt->type.isEmpty() && !appIt->isGameType()) {
      continue; // Steam itself says this isn't a game (Tool/Application/…)
    }
    // The manifest name is authoritative for an installed game.
    entries.append(
        makeSteamEntry(root, game.appId, game.name, appIt == apps.constEnd() ? nullptr : &*appIt));
  }

  // Not-installed candidates. These have no manifest, so appinfo is the only
  // source of a title — an app with no record (or no name) is unnameable and
  // dropped rather than imported as a bare appid. The type gate also keeps
  // DLC, soundtracks and tools out of the played-app list.
  QList<QString> extraIds(extra.constBegin(), extra.constEnd());
  std::sort(extraIds.begin(), extraIds.end());
  for (const QString &appId : extraIds) {
    const auto appIt = apps.constFind(appId);
    if (appIt == apps.constEnd() || !appIt->isGameType() || appIt->name.isEmpty() ||
        SteamLibrary::isRuntimeTool(appIt->name)) {
      continue;
    }
    entries.append(makeSteamEntry(root, appId, appIt->name, &*appIt));
  }

  std::sort(entries.begin(), entries.end(), [](const GameEntry &a, const GameEntry &b) {
    return a.title.localeAwareCompare(b.title) < 0;
  });
  return entries;
}

QList<GameEntry> flatpakEntries() {
  QList<GameEntry> entries;
  const QList<FlatpakLibrary::App> apps =
      FlatpakLibrary::installedGames(FlatpakLibrary::defaultExportRoots());
  entries.reserve(apps.size());
  // Launcher/store apps that export a Game-category desktop entry but are
  // covered by their own import source — importing them as Flatpak "games"
  // would just duplicate the Steam/Lutris integrations.
  static const QStringList kFlatpakLauncherApps = {
      QStringLiteral("com.valvesoftware.Steam"),
      QStringLiteral("net.lutris.Lutris"),
  };
  for (const FlatpakLibrary::App &app : apps) {
    if (kFlatpakLauncherApps.contains(app.appId)) {
      continue;
    }
    GameEntry entry;
    entry.title = app.name;
    entry.target = app.appId;
    entry.coverPath = app.iconPath; // square icon; best cover Flatpak exports
    entries.append(entry);
  }
  return entries;
}

QList<GameEntry> lutrisEntries() {
  QList<GameEntry> entries;
  const QString dataDir = LutrisLibrary::defaultDataDir();
  if (dataDir.isEmpty()) {
    return entries;
  }
  const auto games = LutrisLibrary::installedGames(dataDir);
  if (games.isError()) {
    return entries;
  }
  entries.reserve(games.value().size());
  for (const LutrisLibrary::Game &game : games.value()) {
    const LutrisLibrary::Artwork art = LutrisLibrary::artworkFor(dataDir, game.slug);
    GameEntry entry;
    entry.title = game.name;
    entry.target = QStringLiteral("lutris:rungame/") + game.slug;
    // No portrait cover before Lutris 0.5.8 — the wide banner is still a
    // better tile face than a blank placeholder.
    entry.coverPath = art.cover.isEmpty() ? art.banner : art.cover;
    entry.heroPath = art.banner;
    entries.append(entry);
  }
  return entries;
}

QList<GameEntry> heroicEntries() {
  QList<GameEntry> entries;
  const QString configDir = HeroicLibrary::defaultConfigDir();
  if (configDir.isEmpty()) {
    return entries;
  }
  const QList<HeroicLibrary::Game> games = HeroicLibrary::installedGames(configDir);
  entries.reserve(games.size());
  for (const HeroicLibrary::Game &game : games) {
    GameEntry entry;
    entry.title = game.title;
    entry.target = HeroicLibrary::launchUri(game);
    // Local icon when Heroic happens to have one on disk; otherwise the
    // remote cover, fetched later by the controller (Kartend-g1g30).
    entry.coverPath = game.iconPath;
    entry.coverUrl = game.coverUrl;
    entries.append(entry);
  }
  return entries;
}

QList<GameEntry> itchEntries() {
  QList<GameEntry> entries;
  const QString configDir = ItchLibrary::defaultConfigDir();
  if (configDir.isEmpty()) {
    return entries;
  }
  const auto games = ItchLibrary::installedGames(configDir);
  if (games.isError()) {
    return entries;
  }
  entries.reserve(games.value().size());
  for (const ItchLibrary::Game &game : games.value()) {
    GameEntry entry;
    entry.title = game.title;
    entry.target = ItchLibrary::launchUri(game);
    // itch keeps no local art at all — only the URL (Kartend-g1g30).
    entry.coverUrl = game.coverUrl;
    entries.append(entry);
  }
  return entries;
}

QList<GameEntry> bottlesEntries() {
  QList<GameEntry> entries;
  const QString dataDir = BottlesLibrary::defaultDataDir();
  if (dataDir.isEmpty()) {
    return entries;
  }
  const QList<BottlesLibrary::Program> programs = BottlesLibrary::installedPrograms(dataDir);
  entries.reserve(programs.size());
  QSet<QString> bottleNames;
  for (const BottlesLibrary::Program &program : programs) {
    bottleNames.insert(program.bottle);
  }
  const bool multipleBottles = bottleNames.size() > 1;
  for (const BottlesLibrary::Program &program : programs) {
    GameEntry entry;
    // Disambiguate across bottles only when there IS more than one: the same
    // program installed in two bottles is a normal Bottles workflow (a test
    // prefix beside a working one) and the collision suffix " (2)" would say
    // nothing about which is which.
    entry.title = multipleBottles ? QStringLiteral("%1 (%2)").arg(program.name, program.bottle)
                                  : program.name;
    // The program name identifies the game; the bottle rides along as a stub
    // argument, since `bottles-cli run` needs both (Kartend-4cff2).
    entry.target = program.name;
    entry.launchArgs = BottlesLibrary::stubLaunchArguments(program);
    entry.coverPath = program.iconPath;
    entries.append(entry);
  }
  return entries;
}

QList<GameEntry> xdgEntries() {
  QList<GameEntry> entries;
  const QList<XdgGamesLibrary::Game> games =
      XdgGamesLibrary::installedGames(DesktopEntryFile::defaultShareRoots());
  entries.reserve(games.size());
  for (const XdgGamesLibrary::Game &game : games) {
    GameEntry entry;
    entry.title = game.name;
    // The desktop file itself is the target: `gio launch` runs it exactly as
    // the menu would, honouring Terminal=, field codes and DBus activation —
    // none of which survive picking the Exec line apart into an argv.
    entry.target = game.desktopFile;
    entry.coverPath = game.iconPath; // square icon; the best art a menu entry has
    entries.append(entry);
  }
  return entries;
}

/// Games of ONE ES-DE system. The system is the sourceKey; an empty key would
/// be meaningless here, since ES-DE never imports as a single collection.
QList<GameEntry> esdeEntries(const QString &systemKey) {
  QList<GameEntry> entries;
  const QString dataDir = EsdeLibrary::defaultDataDir();
  if (dataDir.isEmpty() || systemKey.isEmpty()) {
    return entries;
  }
  const QString mediaDir = EsdeLibrary::mediaDirectory(dataDir);
  for (const EsdeLibrary::System &system :
       EsdeLibrary::systems(EsdeLibrary::romDirectory(dataDir))) {
    if (system.name != systemKey) {
      continue;
    }
    const QList<EsdeLibrary::Game> games = EsdeLibrary::games(system, dataDir, mediaDir);
    entries.reserve(games.size());
    for (const EsdeLibrary::Game &game : games) {
      GameEntry entry;
      entry.title = game.title;
      // The ROM path itself: an ES-DE collection is an ordinary ROM collection
      // whose launcher the user configures, because the emulator command lives
      // in a file only ES-DE can reach (Kartend-ilkne).
      entry.target = game.romPath;
      entry.coverPath = game.coverPath;
      entry.logoPath = game.logoPath;
      // ES-DE scrapes screenshots far more often than fanart, so a screenshot
      // is the better wide-art fallback when no fanart was downloaded.
      entry.heroPath = game.fanartPath.isEmpty() ? game.screenshotPath : game.fanartPath;
      entries.append(entry);
    }
    break;
  }
  return entries;
}

} // namespace

auto sourceSlices(const QString &sourceId) -> QList<SourceSlice> {
  QList<SourceSlice> slices;
  // Only ES-DE splits. Everything else is one collection per source, and an
  // empty list is how callers are told so.
  if (sourceId != QLatin1String(kSourceEsde)) {
    return slices;
  }
  const QString dataDir = EsdeLibrary::defaultDataDir();
  if (dataDir.isEmpty()) {
    return slices;
  }
  for (const EsdeLibrary::System &system :
       EsdeLibrary::systems(EsdeLibrary::romDirectory(dataDir))) {
    slices.append({system.name, system.name, system.gameCount});
  }
  return slices;
}

auto coverHostAllowlist(const QString &sourceId) -> QStringList {
  // Registrable domains rather than the exact CDN hostnames: the matcher on
  // the other end accepts a host that IS the suffix or is a subdomain of it,
  // so "itch.zone" covers img.itch.zone and any sibling shard the launcher
  // starts handing out, while still refusing the look-alikes a plain
  // endsWith would let through. Narrow enough to pin, wide enough that a CDN
  // reshuffle does not quietly stop covers from downloading.
  if (sourceId == QLatin1String(kSourceItch)) {
    // butler.db cover_url / still_cover_url. itch serves art from img.itch.zone;
    // itch.io itself is listed because uploaded covers have appeared on it.
    return {QStringLiteral("itch.zone"), QStringLiteral("itch.io")};
  }
  if (sourceId == QLatin1String(kSourceHeroic)) {
    // Heroic's art_square / art_cover come from whichever store the entry
    // belongs to: Epic (cdn*.epicgames.com, some assets on unrealengine.com),
    // GOG (images.gog.com, images-N.gog-statics.com) and Amazon via Nile
    // (m.media-amazon.com).
    return {QStringLiteral("epicgames.com"), QStringLiteral("unrealengine.com"),
            QStringLiteral("gog.com"), QStringLiteral("gog-statics.com"),
            QStringLiteral("media-amazon.com")};
  }
  // Every other source ships local artwork or none at all. Empty is a refusal
  // here — see the header.
  return {};
}

auto watchPaths(const QString &sourceId) -> QStringList {
  QStringList paths;
  const auto addExisting = [&paths](const QString &dir) {
    if (!dir.isEmpty() && QFileInfo(dir).isDir() && !paths.contains(dir)) {
      paths.append(dir);
    }
  };

  if (sourceId == QLatin1String(kSourceSteam)) {
    const QString root = SteamLibrary::defaultRoot();
    if (root.isEmpty()) {
      return paths;
    }
    // Every library folder, not just the default one: a game installed to a
    // second drive writes its appmanifest there and nowhere else.
    for (const QString &library : SteamLibrary::libraryFolders(root)) {
      addExisting(library + QStringLiteral("/steamapps"));
    }
  } else if (sourceId == QLatin1String(kSourceFlatpak)) {
    // Both roots: system-wide installs and --user installs land in different
    // trees and a user with only one of them is normal.
    addExisting(QStringLiteral("/var/lib/flatpak/exports/share/applications"));
    addExisting(QDir::homePath() +
                QStringLiteral("/.local/share/flatpak/exports/share/applications"));
  } else if (sourceId == QLatin1String(kSourceLutris)) {
    // pga.db is a file, and watching files is what this function refuses to
    // do — SQLite replaces it on write. The directory sees that replacement.
    addExisting(LutrisLibrary::defaultDataDir());
  } else if (sourceId == QLatin1String(kSourceHeroic)) {
    addExisting(HeroicLibrary::defaultConfigDir() + QStringLiteral("/store_cache"));
    addExisting(HeroicLibrary::defaultConfigDir() + QStringLiteral("/sideload_apps"));
  } else if (sourceId == QLatin1String(kSourceItch)) {
    addExisting(ItchLibrary::defaultConfigDir() + QStringLiteral("/db"));
  } else if (sourceId == QLatin1String(kSourceBottles)) {
    addExisting(BottlesLibrary::defaultDataDir() + QStringLiteral("/bottles"));
  } else if (sourceId == QLatin1String(kSourceXdg)) {
    for (const QString &root : DesktopEntryFile::defaultShareRoots()) {
      addExisting(root + QStringLiteral("/applications"));
    }
  } else if (sourceId == QLatin1String(kSourceEsde)) {
    // The ROM root, plus each system directory: a new ROM dropped into an
    // existing system changes that subdirectory, not the root above it.
    const QString dataDir = EsdeLibrary::defaultDataDir();
    if (dataDir.isEmpty()) {
      return paths;
    }
    const QString romDir = EsdeLibrary::romDirectory(dataDir);
    addExisting(romDir);
    for (const EsdeLibrary::System &system : EsdeLibrary::systems(romDir)) {
      addExisting(system.romDir);
    }
  }
  return paths;
}

auto detectSources() -> QList<SourceInfo> {
  QList<SourceInfo> sources;

  SourceInfo steam{QString::fromLatin1(kSourceSteam), QStringLiteral("Steam")};
  const QString steamRoot = SteamLibrary::defaultRoot();
  steam.available = !steamRoot.isEmpty();
  if (steam.available) {
    // One pass per tier so the picker can show real numbers. Only the widest
    // reads every appinfo record; on a typical install that is ~500 records
    // and sub-millisecond, and this runs when the import dialog opens, not
    // on the startup sync path.
    steam.gameCount = static_cast<int>(steamEntries(ImportScope::Installed).size());
    steam.ownedGameCount = static_cast<int>(steamEntries(ImportScope::Owned).size());
    steam.recognizedGameCount = static_cast<int>(steamEntries(ImportScope::AllRecognized).size());
  }
  sources.append(steam);

  SourceInfo flatpak{QString::fromLatin1(kSourceFlatpak), QStringLiteral("Flatpak")};
  flatpak.available = !FlatpakLibrary::defaultExportRoots().isEmpty();
  if (flatpak.available) {
    flatpak.gameCount = static_cast<int>(flatpakEntries().size());
  }
  sources.append(flatpak);

  SourceInfo lutris{QString::fromLatin1(kSourceLutris), QStringLiteral("Lutris")};
  const QString lutrisDir = LutrisLibrary::defaultDataDir();
  lutris.available = !lutrisDir.isEmpty();
  if (lutris.available) {
    const auto games = LutrisLibrary::installedGames(lutrisDir);
    lutris.gameCount = games.isError() ? 0 : static_cast<int>(games.value().size());
  }
  sources.append(lutris);

  // Kartend-4cff2. Each of these detects by the presence of its launcher's own
  // data, and counts by listing — the readers are all file reads over a few
  // hundred records at most, and this runs when the dialog opens.
  SourceInfo heroic{QString::fromLatin1(kSourceHeroic), QStringLiteral("Heroic")};
  heroic.available = !HeroicLibrary::defaultConfigDir().isEmpty();
  if (heroic.available) {
    heroic.gameCount = static_cast<int>(heroicEntries().size());
  }
  sources.append(heroic);

  SourceInfo itch{QString::fromLatin1(kSourceItch), QStringLiteral("itch.io")};
  itch.available = !ItchLibrary::defaultConfigDir().isEmpty();
  if (itch.available) {
    itch.gameCount = static_cast<int>(itchEntries().size());
  }
  sources.append(itch);

  SourceInfo bottles{QString::fromLatin1(kSourceBottles), QStringLiteral("Bottles")};
  bottles.available = !BottlesLibrary::defaultDataDir().isEmpty();
  if (bottles.available) {
    bottles.gameCount = static_cast<int>(bottlesEntries().size());
  }
  sources.append(bottles);

  // The menu scan has no "is it installed" probe of its own — a desktop entry
  // exists or it does not — so availability is simply whether the scan found
  // any game at all. Reporting it as available-with-zero would put a
  // permanent empty row in the picker on every machine.
  SourceInfo xdg{QString::fromLatin1(kSourceXdg), QStringLiteral("Desktop Menu")};
  xdg.gameCount = static_cast<int>(xdgEntries().size());
  xdg.available = xdg.gameCount > 0;
  sources.append(xdg);

  // ES-DE reports the TOTAL across its systems, while the import creates one
  // collection per system (Kartend-ilkne) — the picker row is a summary of what
  // ticking it brings in, not of one collection.
  SourceInfo esde{QString::fromLatin1(kSourceEsde), QStringLiteral("ES-DE")};
  const QList<SourceSlice> esdeSlices = sourceSlices(kSourceEsde);
  esde.available = !EsdeLibrary::defaultDataDir().isEmpty() && !esdeSlices.isEmpty();
  for (const SourceSlice &slice : esdeSlices) {
    esde.gameCount += slice.gameCount;
  }
  sources.append(esde);

  // Every source but Steam enumerates installed applications by definition —
  // the wider tiers have no meaning there, so every tier reports the same
  // count and the picker can read the fields uniformly.
  for (SourceInfo &source : sources) {
    if (source.id != QLatin1String(kSourceSteam)) {
      source.ownedGameCount = source.gameCount;
      source.recognizedGameCount = source.gameCount;
    }
  }
  return sources;
}

auto scopeToString(ImportScope scope) -> QString {
  switch (scope) {
  case ImportScope::Owned:
    return QStringLiteral("owned");
  case ImportScope::AllRecognized:
    return QStringLiteral("allRecognized");
  case ImportScope::Installed:
    break;
  }
  return QStringLiteral("installed");
}

auto scopeFromString(const QString &text) -> ImportScope {
  const QString value = text.trimmed();
  if (value.compare(QLatin1String("owned"), Qt::CaseInsensitive) == 0) {
    return ImportScope::Owned;
  }
  if (value.compare(QLatin1String("allRecognized"), Qt::CaseInsensitive) == 0) {
    return ImportScope::AllRecognized;
  }
  // Empty (pre-Kartend-el5st collections) and anything unrecognised fall back
  // to the narrowest tier — never widen a collection because a value did not
  // parse, since that would silently add games on the next sync.
  return ImportScope::Installed;
}

auto listGames(const QString &sourceId, ImportScope scope, const QString &sourceKey)
    -> QList<GameEntry> {
  if (sourceId == QLatin1String(kSourceSteam)) {
    return steamEntries(scope);
  }
  if (sourceId == QLatin1String(kSourceFlatpak)) {
    return flatpakEntries();
  }
  if (sourceId == QLatin1String(kSourceLutris)) {
    return lutrisEntries();
  }
  if (sourceId == QLatin1String(kSourceHeroic)) {
    return heroicEntries();
  }
  if (sourceId == QLatin1String(kSourceItch)) {
    return itchEntries();
  }
  if (sourceId == QLatin1String(kSourceBottles)) {
    return bottlesEntries();
  }
  if (sourceId == QLatin1String(kSourceXdg)) {
    return xdgEntries();
  }
  if (sourceId == QLatin1String(kSourceEsde)) {
    return esdeEntries(sourceKey);
  }
  return {};
}

auto syncSource(const QString &sourceId, const QString &stubDir, const QString &artworkDir,
                ImportScope scope, const QString &sourceKey) -> SyncResult {
  return syncEntries(listGames(sourceId, scope, sourceKey), sourceId, stubDir, artworkDir);
}

auto syncEntries(const QList<GameEntry> &entries, const QString &sourceId, const QString &stubDir,
                 const QString &artworkDir) -> SyncResult {
  SyncResult result;
  if (!QDir().mkpath(stubDir)) {
    result.errors.append(QStringLiteral("Cannot create stub directory %1").arg(stubDir));
    return result;
  }
  // Create the artwork root even when there is no local art to copy. Several
  // consumers resolve it through PathUtils::validateAndExpandPath, which
  // returns EMPTY for a non-existent directory — and an empty artwork dir
  // makes Scraper::writeMediaFiles silently write nothing. Without this a
  // machine whose librarycache is cold imports fine, scrapes "successfully",
  // and lands zero media files (Kartend-ksjx0 review finding).
  if (!artworkDir.isEmpty() && !QDir().mkpath(artworkDir)) {
    result.errors.append(QStringLiteral("Cannot create artwork directory %1").arg(artworkDir));
  }

  // Deterministic collision numbering: two titles that sanitize to the same
  // basename get " (2)", " (3)" suffixes in a stable (title, target) order,
  // so re-syncs assign the same names and don't churn the stubs.
  QList<GameEntry> sorted = entries;
  std::sort(sorted.begin(), sorted.end(), [](const GameEntry &a, const GameEntry &b) {
    if (const int byTitle = a.title.compare(b.title); byTitle != 0) {
      return byTitle < 0;
    }
    return a.target < b.target;
  });

  const QString dotExtension = QStringLiteral(".") + QLatin1String(KartLink::kExtension);
  QHash<QString, GameEntry> desired; // stub file name → entry
  for (const GameEntry &entry : sorted) {
    const QString base = sanitizeStubBaseName(entry.title);
    QString fileName = base + dotExtension;
    for (int n = 2; desired.contains(fileName); ++n) {
      fileName = base + QStringLiteral(" (%1)").arg(n) + dotExtension;
    }
    desired.insert(fileName, entry);
  }

  for (auto it = desired.constBegin(); it != desired.constEnd(); ++it) {
    const QString stubPath = stubDir + QLatin1Char('/') + it.key();
    KartLink::LinkData data;
    data.source = sourceId;
    data.target = it.value().target;
    data.title = it.value().title;
    data.args = it.value().launchArgs;
    // Kartend-g1g30: hand the controller only the covers it actually has to
    // fetch. Deciding here — where the artwork dir and the stub's basename are
    // both in hand — keeps the fill-missing rule in ONE place, so a scraped or
    // hand-placed cover is never re-downloaded over.
    QString pendingCoverUrl;
    if (!it.value().coverUrl.isEmpty() && !artworkDir.isEmpty() &&
        !artworkSlotTaken(artworkDir + QStringLiteral("/front"),
                          QFileInfo(it.key()).completeBaseName())) {
      pendingCoverUrl = it.value().coverUrl;
    }
    if (QFileInfo::exists(stubPath)) {
      const auto existing = KartLink::read(stubPath);
      if (!existing.isError() && existing.value() == data) {
        ++result.unchanged;
        result.syncedStubs.append({stubPath, data.target, data.title, pendingCoverUrl});
        continue;
      }
    }
    if (KartLink::write(stubPath, data)) {
      ++result.written;
      result.syncedStubs.append({stubPath, data.target, data.title, pendingCoverUrl});
    } else {
      result.errors.append(QStringLiteral("Cannot write stub %1").arg(stubPath));
    }
  }

  // Remove stale stubs — but only ones this source wrote (see the ownership
  // contract in the header): a hand-made stub or another source's stubs
  // sharing the folder must survive a sync.
  QDirIterator stale(stubDir, {QStringLiteral("*.") + QLatin1String(KartLink::kExtension)},
                     QDir::Files);
  while (stale.hasNext()) {
    const QString stubPath = stale.next();
    if (desired.contains(QFileInfo(stubPath).fileName())) {
      continue;
    }
    const auto parsed = KartLink::read(stubPath);
    if (parsed.isError() || parsed.value().source != sourceId) {
      continue;
    }
    if (QFile::remove(stubPath)) {
      ++result.removed;
    } else {
      result.errors.append(QStringLiteral("Cannot remove stale stub %1").arg(stubPath));
    }
  }

  // Artwork is optional: a collection configured without an artwork
  // directory simply gets no covers (matching the rest of the app's
  // "empty artworkDirectory means no artwork" behaviour).
  if (!artworkDir.isEmpty()) {
    for (auto it = desired.constBegin(); it != desired.constEnd(); ++it) {
      const QString base = QFileInfo(it.key()).completeBaseName();
      copyArtworkIfMissing(it.value().coverPath, artworkDir + QStringLiteral("/front"), base,
                           result);
      copyArtworkIfMissing(it.value().logoPath, artworkDir + QStringLiteral("/logo"), base, result);
      copyArtworkIfMissing(it.value().heroPath, artworkDir + QStringLiteral("/fanart"), base,
                           result);
    }
  }
  return result;
}

auto defaultBaseDir(const QString &sourceId, const QString &sourceKey) -> QString {
  // Built from KartLink::managedStubRoot() rather than re-deriving the path,
  // because that root is now the TRUST BOUNDARY the launch path checks against
  // (Kartend-1o1a1): a stub only keeps its args if it lives under it. Two
  // independent spellings of the same directory could drift and silently strip
  // args from stubs Kartend itself wrote.
  const QString base = KartLink::managedStubRoot() + QLatin1Char('/') + sourceId;
  if (sourceKey.isEmpty()) {
    return base;
  }
  // Underscore-prefixed and sanitized: the key is a directory name from the
  // user's ROM tree, so it must not be able to climb out of the managed root —
  // removeManagedImportDirs' containment check is what would otherwise be
  // asked to refuse a path this function built.
  return base + QStringLiteral("/_") + sanitizeStubBaseName(sourceKey);
}

auto stubDirFor(const QString &baseDir) -> QString {
  return baseDir + QStringLiteral("/games");
}

auto artworkDirFor(const QString &baseDir) -> QString {
  return baseDir + QStringLiteral("/artwork");
}

auto sanitizeStubBaseName(const QString &title) -> QString {
  // Kartend-tb5nb: the rule set moved to PathUtils::sanitizeFileBaseName so
  // this and MultiDisc::m3uFileNameFor cannot drift apart again — they had.
  // Behaviour here is unchanged: same space substitute, same "Untitled"
  // fallback, and no extraForbidden, so colons still survive as the
  // "colon kept" case pins ("Half-Life 2: Episode Two").
  return PathUtils::sanitizeFileBaseName(title, QStringLiteral(" "), QStringLiteral("Untitled"));
}

namespace {

/// Strict fill-missing merge of appinfo metadata into an item_metadata row:
/// only empty fields gain values, so user edits and scraped rows survive
/// (the scraper's own merge is scrape-wins; this one is deliberately
/// meeker). Returns true when anything changed. `source` is stamped
/// "steam" only when the row was empty before the merge — a filled-in
/// corner of a scraped row keeps its scraper attribution.
bool fillMissingFromSteam(ItemMetadataStore::ItemMetadata &row,
                          const SteamAppInfo::AppMetadata &info) {
  const bool wasEmpty = row.isEmpty();
  bool changed = false;
  const auto fill = [&changed](QString &field, const QString &value) {
    if (field.isEmpty() && !value.isEmpty()) {
      field = value;
      changed = true;
    }
  };
  fill(row.title, info.name);
  fill(row.developer, info.developer);
  fill(row.publisher, info.publisher);
  if (info.releaseDateUtc > 0) {
    fill(row.releaseDate,
         QDateTime::fromSecsSinceEpoch(info.releaseDateUtc).toUTC().date().toString(Qt::ISODate));
  }
  fill(row.genre, SteamAppInfo::genreNames(info.genreIds).join(QStringLiteral(", ")));
  fill(row.players, SteamAppInfo::playersDescription(info.categoryIds));
  fill(row.contentRating,
       SteamAppInfo::contentDescriptorNames(info.contentDescriptorIds).join(QStringLiteral(", ")));

  // Ratings/controller data have no dedicated columns — carried as custom
  // fields, added only when the key doesn't exist yet. Values are data, not
  // UI strings, so they are deliberately untranslated (same as scraped
  // values).
  ItemMetadataStore::CustomFieldList fields =
      ItemMetadataStore::parseCustomFields(row.customFields);
  const auto hasKey = [&fields](const QString &key) {
    return std::ranges::any_of(fields,
                               [&key](const QPair<QString, QString> &f) { return f.first == key; });
  };
  bool customChanged = false;
  const auto addCustom = [&fields, &hasKey, &customChanged](const QString &key,
                                                            const QString &value) {
    if (!value.isEmpty() && !hasKey(key)) {
      fields.append({key, value});
      customChanged = true;
    }
  };
  if (info.metacriticScore >= 0) {
    addCustom(QStringLiteral("Metacritic"), QString::number(info.metacriticScore));
  }
  if (info.reviewPercentage >= 0) {
    addCustom(QStringLiteral("Steam reviews"),
              QStringLiteral("%1% positive").arg(info.reviewPercentage));
  }
  if (!info.controllerSupport.isEmpty()) {
    QString support = info.controllerSupport;
    support[0] = support[0].toUpper();
    addCustom(QStringLiteral("Controller support"), support);
  }
  if (customChanged) {
    row.customFields = ItemMetadataStore::serializeCustomFields(fields);
    changed = true;
  }

  if (changed && wasEmpty) {
    row.source = QStringLiteral("steam");
  }
  return changed;
}

} // namespace

auto applySteamMetadata(const QString &dbPath, const QString &collectionUuid,
                        const QList<SyncedStub> &stubs, const QString &appInfoPath)
    -> MetadataApplyResult {
  MetadataApplyResult result;
  if (dbPath.isEmpty() || collectionUuid.isEmpty() || stubs.isEmpty()) {
    return result;
  }

  const QLatin1String kSteamPrefix("steam://rungameid/");
  QList<QPair<QString, QString>> pathAndAppId; // stub path → appid
  QSet<QString> wantedAppIds;
  for (const SyncedStub &stub : stubs) {
    if (!stub.target.startsWith(kSteamPrefix)) {
      continue;
    }
    const QString appId = stub.target.mid(kSteamPrefix.size());
    if (appId.isEmpty()) {
      continue;
    }
    pathAndAppId.append({stub.path, appId});
    wantedAppIds.insert(appId);
  }
  if (wantedAppIds.isEmpty()) {
    return result;
  }

  QString infoPath = appInfoPath;
  if (infoPath.isEmpty()) {
    const QString steamRoot = SteamLibrary::defaultRoot();
    if (steamRoot.isEmpty()) {
      return result; // no Steam install — nothing to fill, not an error
    }
    infoPath = SteamAppInfo::defaultAppInfoPath(steamRoot);
  }
  const auto parsed = SteamAppInfo::read(infoPath, wantedAppIds);
  if (parsed.isError()) {
    result.errors.append(
        QStringLiteral("Cannot read Steam metadata: %1").arg(parsed.error().message));
    return result;
  }
  if (parsed.value().isEmpty()) {
    return result;
  }

  // Throwaway connection, BulkEditService's shape: unique name, busy
  // timeout, one transaction, inner scope so the handles die before
  // removeDatabase. Safe on any thread — which is why this bypasses
  // DatabaseManager and the (main-thread-only) metadata LRU; callers
  // invalidate writtenPaths on the GUI thread afterwards.
  static std::atomic<quint64> connectionCounter{0};
  const QString connectionName =
      QStringLiteral("kartend_launcherimport_%1").arg(connectionCounter.fetch_add(1));
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
      result.errors.append(QStringLiteral("Cannot open %1: %2").arg(dbPath, db.lastError().text()));
    } else {
      QSqlQuery pragma(db);
      pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
      KartendDb::DbTransaction txn(db, "LauncherImportService::applySteamMetadata");
      if (!txn.active()) {
        result.errors.append(QStringLiteral("Cannot begin metadata transaction"));
      } else {
        for (const auto &entry : pathAndAppId) {
          const auto appIt = parsed.value().constFind(entry.second);
          if (appIt == parsed.value().constEnd()) {
            continue;
          }
          auto loaded = ItemMetadataStore::load(db, collectionUuid, entry.first);
          if (loaded.isError()) {
            result.errors.append(QStringLiteral("Cannot load metadata for %1").arg(entry.first));
            continue;
          }
          ItemMetadataStore::ItemMetadata row = loaded.value();
          if (!fillMissingFromSteam(row, appIt.value())) {
            continue;
          }
          const auto saved = ItemMetadataStore::save(db, row);
          if (saved.isError()) {
            result.errors.append(QStringLiteral("Cannot save metadata for %1").arg(entry.first));
            continue;
          }
          ++result.rowsWritten;
          result.writtenPaths.append(entry.first);
        }
        if (!txn.commit()) {
          result.errors.append(QStringLiteral("Metadata transaction commit failed"));
          result.rowsWritten = 0;
          result.writtenPaths.clear();
        }
      }
      db.close();
    }
  }
  QSqlDatabase::removeDatabase(connectionName);
  return result;
}

auto stubsMissingDescription(const QString &dbPath, const QString &collectionUuid,
                             const QList<SyncedStub> &stubs) -> QStringList {
  QStringList all;
  all.reserve(stubs.size());
  for (const SyncedStub &stub : stubs) {
    all.append(stub.path);
  }
  if (dbPath.isEmpty() || collectionUuid.isEmpty() || all.isEmpty()) {
    return all;
  }

  QStringList missing;
  static std::atomic<quint64> connectionCounter{0};
  const QString connectionName =
      QStringLiteral("kartend_launcherenrich_%1").arg(connectionCounter.fetch_add(1));
  bool failed = false;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
      failed = true;
    } else {
      QSqlQuery pragma(db);
      pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
      for (const QString &path : all) {
        const auto loaded = ItemMetadataStore::load(db, collectionUuid, path);
        // An unreadable row is treated as missing: enriching again is
        // harmless (FillMissing), losing the description is not.
        if (loaded.isError() || loaded.value().description.trimmed().isEmpty()) {
          missing.append(path);
        }
      }
    }
  }
  QSqlDatabase::removeDatabase(connectionName);
  return failed ? all : missing;
}

auto makeCollectionConfig(const QString &sourceId, ImportScope scope, const QString &sourceKey)
    -> CollectionConfig {
  const QString baseDir = defaultBaseDir(sourceId, sourceKey);

  // Mirror SettingsDialog::addCollection's defaulted field set (see
  // createCollectionForDat's provenance comment) so an imported collection
  // is indistinguishable from a hand-made one.
  CollectionConfig c;
  c.type = QStringLiteral("Games");
  c.mediaDirectory = stubDirFor(baseDir);
  c.artworkDirectory = artworkDirFor(baseDir);
  c.extensions = QStringList{QLatin1String(KartLink::kExtension)};
  c.gridLayout.gridWidth = UIConstants::Grid::DEFAULT_WIDTH;
  c.gridLayout.fontSize = UIConstants::Item::DEFAULT_FONT_SIZE;
  c.importSource = sourceId;
  // Which slice of the source this collection holds; empty for every
  // one-collection-per-source importer (Kartend-ilkne).
  c.importSourceKey = sourceKey;
  // Persisted so every later re-sync reproduces this tier; without it the
  // startup sync would re-list at Installed and delete the not-installed
  // stubs as "no longer present".
  c.importScope = scopeToString(scope);

  if (sourceId == QLatin1String(kSourceSteam)) {
    // Pin the Steam store scraper (Kartend-ksjx0): it resolves each stub's
    // exact appid, so a scrape adds descriptions/screenshots/trailers with
    // no name-matching risk. Category auto-selection would otherwise pick
    // ScreenScraper for games.
    c.scraperOverrides.scraperProviderId = QLatin1String(kSourceSteam);
  }

  if (sourceId == QLatin1String(kSourceFlatpak)) {
    c.name = QStringLiteral("Flatpak Games");
    // Pin the Flathub scraper (Kartend-2bzbu), mirroring the Steam pin
    // above: it resolves each stub's exact app id, so a scrape fills
    // descriptions with no name-matching risk — these apps are on neither
    // ScreenScraper nor the Steam store.
    c.scraperOverrides.scraperProviderId = QStringLiteral("flathub");
    // argv-style target: `flatpak run <app-id>`.
    c.launcher.launcherPath = QStringLiteral("flatpak");
    c.launcher.launchParameters = QStringLiteral("run %1");
    c.launcher.launcherName = QStringLiteral("Flatpak");
  } else if (sourceId == QLatin1String(kSourceBottles)) {
    // `bottles-cli run -p <program> -b <bottle> --`: the template carries the
    // program, and the stub's own args carry the bottle it lives in
    // (Kartend-4cff2). A Flatpak Bottles install exports bottles-cli onto the
    // PATH as well, so one template covers both install shapes.
    c.name = QStringLiteral("Bottles");
    c.launcher.launcherPath = QStringLiteral("bottles-cli");
    c.launcher.launchParameters = QStringLiteral("run -p %1");
    c.launcher.launcherName = QStringLiteral("Bottles");
  } else if (sourceId == QLatin1String(kSourceEsde)) {
    // One collection per ES-DE system, named after it. NO launcher is set: the
    // emulator command lives in es_systems.xml + es_find_rules.xml, both inside
    // the ES-DE binary and unreadable from here, so guessing one would ship a
    // collection that silently fails to launch. The user points it at their
    // emulator once, exactly as for any ROM collection, and the launcher probe
    // can suggest one.
    c.name = QStringLiteral("ES-DE: %1").arg(sourceKey);
    // Targets are real ROM paths rather than stubs, but they are still written
    // as .kartlink stubs so the managed folder, the sync diff and the artwork
    // fill-missing all behave exactly as for every other source.
  } else if (sourceId == QLatin1String(kSourceXdg)) {
    // The target is a .desktop file; `gio launch` runs it the way the menu
    // does. Picking the Exec line apart into an argv instead would drop
    // Terminal=, field codes and DBus activation.
    c.name = QStringLiteral("Desktop Games");
    c.launcher.launcherPath = QStringLiteral("gio");
    c.launcher.launchParameters = QStringLiteral("launch %1");
    c.launcher.launcherName = QStringLiteral("Desktop");
  } else {
    // URI-style targets (steam://, lutris:, heroic://, itch://) go through the
    // desktop URL-handler registry — that works for native AND Flatpak
    // installs of the launcher, where invoking its binary would not.
    if (sourceId == QLatin1String(kSourceSteam)) {
      c.name = QStringLiteral("Steam");
    } else if (sourceId == QLatin1String(kSourceHeroic)) {
      c.name = QStringLiteral("Heroic");
    } else if (sourceId == QLatin1String(kSourceItch)) {
      c.name = QStringLiteral("itch.io");
    } else {
      c.name = QStringLiteral("Lutris");
    }
    c.launcher.launcherPath = QStringLiteral("xdg-open");
    c.launcher.launchParameters = QStringLiteral("%1");
    c.launcher.launcherName = c.name;
  }
  return c;
}

auto removeManagedImportDirs(const CollectionConfig &config, const QString &managedBaseDirOverride)
    -> CleanupResult {
  CleanupResult result;
  if (config.importSource.trimmed().isEmpty()) {
    return result; // not an import collection — nothing is managed
  }
  // The per-slice base for a multi-collection source: removing the 'snes'
  // collection must clean launcher-imports/esde/_snes, not the shared
  // launcher-imports/esde — which would otherwise be left holding an empty
  // per-system directory after its contents went (Kartend-ilkne).
  const QString base = QDir::cleanPath(
      managedBaseDirOverride.isEmpty() ? defaultBaseDir(config.importSource, config.importSourceKey)
                                       : managedBaseDirOverride);
  if (base.isEmpty() || !QDir(base).isAbsolute()) {
    result.errors.append(
        QStringLiteral("managed base dir unresolved for source '%1'").arg(config.importSource));
    return result;
  }

  // Containment is the whole safety story: a user who re-pointed the
  // collection's artworkDirectory at their own folder must never lose it to
  // this checkbox. cleanPath collapses any ../ segments BEFORE the prefix
  // test, so a crafted "<base>/games/../../home" cannot pass as managed.
  const auto isManaged = [&base](const QString &dir) {
    return dir == base || dir.startsWith(base + QLatin1Char('/'));
  };

  for (const QString &raw : {config.mediaDirectory, config.artworkDirectory}) {
    const QString expanded =
        QDir::cleanPath(PathUtils::expandPathWithoutExistenceCheck(raw, config.name));
    if (expanded.isEmpty()) {
      continue;
    }
    if (!isManaged(expanded)) {
      result.skippedUnmanaged.append(expanded);
      continue;
    }
    QDir dir(expanded);
    if (!dir.exists()) {
      continue; // already gone — that's the desired end state
    }
    if (dir.removeRecursively()) {
      result.removedDirs.append(expanded);
    } else {
      result.errors.append(QStringLiteral("could not fully delete '%1'").arg(expanded));
    }
  }

  // Drop the now-empty base itself with a PLAIN (non-recursive) rmdir: if
  // the user stashed anything else in it, the rmdir fails and the extra
  // content survives.
  if (!result.removedDirs.isEmpty() && QDir(base).exists()) {
    QDir().rmdir(base);
  }
  return result;
}

} // namespace LauncherImportService
