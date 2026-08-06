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
  int gameCount = 0;
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

/// The source's installed games with launch targets and locally-cached
/// artwork resolved. Unknown source ids return an empty list.
[[nodiscard]] QList<GameEntry> listGames(const QString &sourceId);

/// listGames + syncEntries in one call — the normal production path.
[[nodiscard]] SyncResult syncSource(const QString &sourceId, const QString &stubDir,
                                    const QString &artworkDir);

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
/// app ids), and importSource set so the startup sync recognises it.
/// Mirrors SettingsDialog::addCollection's defaulted field set.
[[nodiscard]] CollectionConfig makeCollectionConfig(const QString &sourceId);

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

} // namespace LauncherImportService

#endif
