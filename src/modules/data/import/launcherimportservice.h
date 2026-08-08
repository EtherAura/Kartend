#ifndef KARTEND_MODULES_DATA_IMPORT_LAUNCHERIMPORTSERVICE_H
#define KARTEND_MODULES_DATA_IMPORT_LAUNCHERIMPORTSERVICE_H

#include <QList>
#include <QString>
#include <QStringList>

#include "collection/collectionconfig.h"

/// Synchronises an external launcher's installed-game library (Steam /
/// Flatpak / Lutris) into a Kartend-managed folder of .kartlink shortcut
/// stubs plus fill-missing artwork, so the ordinary folder-scan pipeline can
/// present the games as launchable items (Kartend-wuq2c). Stateless free
/// functions with no manager dependencies: the heavy entry points
/// (listGames / syncSource) do only file and SQLite reads and atomic stub
/// writes, so callers may run them on a worker thread; nothing here touches
/// Kartend's own database or any widget.
///
/// Ownership contract: a sync pass only ever deletes stubs whose parsed
/// `source` matches the source being synced — hand-made stubs and other
/// sources' stubs in the same folder are left alone.
namespace LauncherImportService {

inline constexpr auto kSourceSteam = "steam";
inline constexpr auto kSourceFlatpak = "flatpak";
inline constexpr auto kSourceLutris = "lutris";

/// How much of a launcher's library to import (Kartend-el5st). Steam is the
/// only source that honours anything beyond Installed — Flatpak and Lutris
/// enumerate installed applications by definition and treat every value as
/// Installed.
///
/// The tiers trade recall for precision, and the middle one is the default
/// because the widest is not a superset of things the user actually has:
enum class ImportScope {
  /// appmanifest_*.acf only — games present on disk.
  Installed,
  /// Installed, plus apps with a local play record (SteamLibrary::
  /// playedAppIds) that Steam types as a game. Playtime implies ownership,
  /// so nothing here is unowned; an owned game never launched on this
  /// machine is the known blind spot.
  Owned,
  /// Every appinfo.vdf record typed "game". Widest, and deliberately NOT the
  /// default: the client caches metadata for apps merely seen in the store,
  /// so this includes titles the user does not own — notably Valve's default
  /// set (Counter-Strike, Ricochet, Spacewar), which every client carries.
  AllRecognized,
};

/// Round-trips ImportScope through CollectionConfig::importScope so a later
/// re-sync reproduces the tier the collection was imported with. Unknown or
/// empty text parses to the Installed tier, which is what pre-Kartend-el5st
/// collections (no persisted value) were imported with.
[[nodiscard]] QString scopeToString(ImportScope scope);
[[nodiscard]] ImportScope scopeFromString(const QString &text);

/// One importable game, source-agnostic. Artwork paths point at the
/// launcher's own cached files (may be empty) and are copied — not linked —
/// so the art survives the launcher pruning its cache.
struct GameEntry {
  QString title;
  QString target;    ///< What the stub resolves to at launch time.
  QString coverPath; ///< Portrait cover art → artwork/front/.
  QString logoPath;  ///< Transparent logo → artwork/logo/.
  QString heroPath;  ///< Wide banner art → artwork/fanart/.
};

struct SourceInfo {
  QString id;          ///< kSourceSteam / kSourceFlatpak / kSourceLutris.
  QString displayName; ///< Proper noun ("Steam"); not translated.
  bool available = false;
  int gameCount = 0; ///< ImportScope::Installed — the tier every source supports.
  /// Counts for the wider Steam tiers, so the picker can label them with real
  /// numbers instead of making the user import to find out. Both equal
  /// gameCount for sources that only have installed apps.
  int ownedGameCount = 0;
  int recognizedGameCount = 0;
};

/// One stub present after a sync (freshly written or already up to date) —
/// the bridge between the file-level sync and follow-up steps that key off
/// the stub path (metadata writes are keyed (collection_uuid, stub path)).
struct SyncedStub {
  QString path;   ///< Absolute stub path.
  QString target; ///< The stub's launch target (carries the Steam appid).
  QString title;
};

struct SyncResult {
  int written = 0;   ///< Stubs created or rewritten.
  int removed = 0;   ///< Stale stubs deleted (game uninstalled).
  int unchanged = 0; ///< Stubs already up to date.
  int artworkCopied = 0;
  QList<SyncedStub> syncedStubs; ///< Every stub present after the sync.
  QStringList errors;            ///< Log-grade strings; empty on full success.
  [[nodiscard]] bool changed() const { return written + removed > 0; }
  /// Games present after the sync. User-facing "imported" counts must use
  /// this, not `written`: re-importing over a surviving stub folder (delete
  /// the collection, keep the managed dir) writes nothing yet yields a
  /// fully-populated collection — reporting the write count there claims
  /// "0 games" about a collection that has them all (Kartend-wuq2c review).
  [[nodiscard]] int totalPresent() const { return written + unchanged; }
};

/// Probes all three sources. `available` reports whether the launcher's
/// data was found at all; `gameCount` may legitimately be 0 for an
/// installed-but-empty library.
[[nodiscard]] QList<SourceInfo> detectSources();

/// The source's games at `scope`, with launch targets and locally-cached
/// artwork resolved. Unknown source ids return an empty list.
/// `steam://rungameid/<appid>` is a valid target for a game that is not
/// installed — Steam offers to install it — so the wider tiers need no
/// special target handling.
[[nodiscard]] QList<GameEntry> listGames(const QString &sourceId,
                                         ImportScope scope = ImportScope::Installed);

/// listGames + syncEntries in one call — the normal production path.
///
/// Pass the scope the collection was imported with, not a fresh default:
/// syncEntries deletes this source's stubs for games the list no longer
/// carries, so re-syncing a wide-tier collection at Installed would delete
/// every not-installed game it holds. Callers read it back from
/// CollectionConfig::importScope (Kartend-el5st).
[[nodiscard]] SyncResult syncSource(const QString &sourceId, const QString &stubDir,
                                    const QString &artworkDir,
                                    ImportScope scope = ImportScope::Installed);

/// Diff-syncs `entries` into `stubDir`: writes new/changed stubs, deletes
/// this source's stubs for games no longer present, and copies artwork into
/// `{artworkDir}/front|logo|fanart/` only where no file with the stub's
/// basename exists yet (scraped or hand-placed art is never clobbered).
/// Separated from syncSource so tests can drive it with a synthetic list.
[[nodiscard]] SyncResult syncEntries(const QList<GameEntry> &entries, const QString &sourceId,
                                     const QString &stubDir, const QString &artworkDir);

/// `<AppDataLocation>/launcher-imports/<sourceId>`; stubs live in its
/// `games/` child and artwork in `artwork/` (stubDirFor / artworkDirFor).
[[nodiscard]] QString defaultBaseDir(const QString &sourceId);
[[nodiscard]] QString stubDirFor(const QString &baseDir);
[[nodiscard]] QString artworkDirFor(const QString &baseDir);

/// Title → stub basename: strips path separators and the characters the
/// launch-path security validator rejects, collapses whitespace, trims
/// trailing dots, caps the length. Never returns an empty string.
[[nodiscard]] QString sanitizeStubBaseName(const QString &title);

/// A ready-to-append CollectionConfig for the source: named after the
/// launcher, kartlink-only extensions, managed stub/artwork dirs, the
/// matching launcher template (xdg-open for URI targets, flatpak run for
/// app ids), and importSource + importScope set so the startup sync
/// recognises it and re-syncs at the tier it was imported with.
/// Mirrors SettingsDialog::addCollection's defaulted field set.
[[nodiscard]] CollectionConfig makeCollectionConfig(const QString &sourceId,
                                                    ImportScope scope = ImportScope::Installed);

/// Outcome of a Steam-metadata pass (Kartend-11elw).
struct MetadataApplyResult {
  int rowsWritten = 0;
  /// Stub paths whose item_metadata row was written — the caller must run
  /// these through IDatabaseManager::invalidateMetadataCacheItem on the GUI
  /// thread (the LRU is main-thread-only and this writer bypasses it).
  QStringList writtenPaths;
  QStringList errors;
};

/// Fills item_metadata rows for Steam stubs from the client's local
/// appinfo.vdf — developer, publisher, release date, genres, player modes,
/// content rating, plus Metacritic / review / controller-support custom
/// fields. Strictly FILL-MISSING per field: user edits and scraped values
/// are never overwritten, and `source` is stamped "steam" only on rows that
/// were empty before the merge. Descriptions are not local to Steam and are
/// left for the scraper.
///
/// Runs on any thread: opens its own throwaway connection to `dbPath`
/// (BulkEditService's pattern) and writes inside one transaction. `stubs`
/// is SyncResult::syncedStubs; non-Steam targets are ignored.
/// `appInfoPath` overrides the auto-detected
/// <SteamLibrary::defaultRoot()>/appcache/appinfo.vdf (tests).
[[nodiscard]] MetadataApplyResult applySteamMetadata(const QString &dbPath,
                                                     const QString &collectionUuid,
                                                     const QList<SyncedStub> &stubs,
                                                     const QString &appInfoPath = QString());

/// Stub paths whose item_metadata row still has an empty description — the
/// set the store-enrichment pass has not successfully covered yet.
///
/// The description is the right probe because it is the one field only the
/// store has (appinfo.vdf carries no descriptions at all, by design), so an
/// empty one means the network pass never landed for that item: interrupted,
/// errored, or never run. Everything appinfo *can* fill is already filled by
/// applySteamMetadata regardless of this.
///
/// Exists because enrichment is a fire-and-forget network batch. At the
/// handful of games an installed-only import produced it always finished, but
/// a wide-scope import is hundreds of items, and whatever was unfinished when
/// the app closed used to be stranded permanently — the re-sync path never
/// re-ran enrichment (Kartend-el5st follow-up). Feeding this list back in
/// makes each sync resume where the last one stopped.
///
/// Runs on any thread: its own throwaway connection, same shape as
/// applySteamMetadata. A row that does not exist yet counts as missing. On
/// any error every input path is returned unfiltered — re-fetching costs one
/// request, wrongly skipping loses the description until the next sync.
[[nodiscard]] QStringList stubsMissingDescription(const QString &dbPath,
                                                  const QString &collectionUuid,
                                                  const QList<SyncedStub> &stubs);

} // namespace LauncherImportService

#endif
