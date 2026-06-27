#ifndef SCRAPEPERSISTENCE_H
#define SCRAPEPERSISTENCE_H

#include <atomic>
#include <memory>

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

#include "errorutils.h"
#include "scrapertypes.h"

class IDatabaseManager;
class QSqlDatabase;

namespace Scraper {

/// One downloaded media payload to persist alongside the scraped
/// metadata. Mirrors ScrapeResultDialog::MediaDownload but kept in the
/// data layer so this module doesn't depend on UI headers.
struct PendingMediaWrite {
  MediaAsset asset;
  QByteArray bytes;
};

/// (artworkType, absolute on-disk path) rows collected by the file-write
/// phase for non-standard artwork types — needs an `item_artwork` row
/// because the sidebar gallery only auto-discovers standard types from
/// subdirectories. Aliased so it can survive Qt's queued-invocation
/// metatype machinery (Q_ARG / qRegisterMetaType need a single token,
/// not a comma-bearing template specialization). Q_DECLARE_METATYPE
/// declaration sits at the bottom of the header, outside this namespace.
using NonStandardArtworkList = QList<QPair<QString, QString>>;

/// Counters returned from `applyScrapedItem`. Used by the caller to
/// surface a "wrote N images and updated metadata" summary to the user
/// — nothing here is fatal in itself, so we report rather than throw.
struct ApplyResult {
  int mediaWritten = 0;
  int mediaSkipped = 0;
  bool metadataSaved = false;
  /// Whether the on-disk JSON sidecar (re-scrape detection + portable metadata)
  /// was written. False on a QSaveFile open/write/commit failure; independent of
  /// metadataSaved (the DB save). Kartend-v5788.
  bool sidecarWritten = false;
  /// First handful of failure descriptions (e.g. "back: write failed")
  /// for the caller's summary message. Capped to keep the summary
  /// readable.
  QStringList firstFailures;
  /// Absolute paths the apply step wrote to disk. Mirrors the order
  /// of the input PendingMediaWrite list (skipped writes don't add
  /// entries). The Scraper UI uses these for the Live view's recent-
  /// media thumbnail strip without having to re-scan the artwork
  /// directory.
  QStringList writtenPaths;
};

/// Persist a ScrapedItem + downloaded media for the item identified by
/// (collectionUuid, sourcePath, artworkDirectory, baseName).
///
/// Layout used for each media asset:
///   * `front`             → primary cover at
///                           `{artworkDirectory}/{baseName}.png`
///                           (the slot the grid auto-discovers)
///   * `box` / `screenshot` / `title` / `marquee` / `fanart` / `logo`
///                         → standard-type subdir at
///                           `{artworkDirectory}/{type}/{baseName}.png`
///                           (auto-discovered via
///                           ItemArtworkStore::findStandardArtwork).
///   * any other type      → same per-type subdir layout PLUS an
///                           `item_artwork` row whose `manualPath`
///                           points at the file, so the sidebar
///                           gallery surfaces it without the user
///                           having to add the type to
///                           customArtworkTypes manually.
///
/// Metadata mapping: ScrapedItem typed fields land on ItemMetadata
/// typed columns (title / description / genre / developer /
/// publisher / releaseDate / contentRating / players /
/// runtimeSeconds); ScrapedItem.tagsJson goes into ItemMetadata.tags;
/// ScrapedItem.customFields is JSON-merged with any pre-existing
/// ItemMetadata.customFields so the scrape doesn't clobber
/// user-entered fields. ItemMetadata.source is set to
/// ScrapedItem.sourceProviderId.
///
/// `databaseManager` may be null — in which case the metadata save
/// and item_artwork rows are skipped (file writes still happen) and
/// the caller gets ApplyResult.metadataSaved=false. Useful for tests.
///
/// **Single-directory model.** The whole tree of scraped media lives
/// under `{artworkDirectory}/`:
///   - images  → `{artworkDirectory}/<type>/{baseName}.<ext>`
///   - videos  → `{artworkDirectory}/video/{baseName}.<ext>`
///   - manuals → `{artworkDirectory}/manual/{baseName}.<ext>`
///   - the `"front"` primary cover ALSO mirrors to
///     `{artworkDirectory}/{baseName}.<ext>` for the grid tile.
/// Subdirectories are created on demand; callers don't have to
/// pre-create them. The collection only needs `artworkDirectory`
/// set — kinds organise themselves underneath.
/// Per-asset re-scrape policy. Applied independently to each
/// PendingMediaWrite — so an item with an existing cover but a new
/// fanart in `FillMissing` mode keeps the cover and writes the fanart.
/// `Overwrite` is unconditional (today's behavior); `FillMissing`
/// skips assets whose destination file already exists; `UpdateChanged`
/// reads the existing bytes, compares them, and only writes if they
/// differ (slowest mode because we still pay the download cost above).
/// `Skip` is a *batch-level* signal handled in the runner — never
/// reached at this layer.
enum class RescrapeMode {
  Overwrite = 0,
  FillMissing = 1,
  UpdateChanged = 2,
  Skip = 3,
};

[[nodiscard]] ApplyResult applyScrapedItem(IDatabaseManager *databaseManager,
                                           const QString &collectionUuid, const QString &sourcePath,
                                           const QString &artworkDirectory, const QString &baseName,
                                           const ScrapedItem &scraped,
                                           const QList<PendingMediaWrite> &media,
                                           RescrapeMode rescrapeMode = RescrapeMode::Overwrite);

/// Result of `writeMediaFiles` — the file-I/O half of applyScrapedItem.
/// Carries the counters + paths the dialog surfaces to the user plus
/// the non-standard artwork rows the DB phase still needs to persist
/// (standard types are auto-discovered by subdir and don't need a row).
struct MediaWriteResult {
  int mediaWritten = 0;
  int mediaSkipped = 0;
  QStringList writtenPaths;
  QStringList firstFailures;
  /// (artworkType, absolute on-disk path) pairs for newly-written
  /// non-standard images. The DB phase saves one `item_artwork` row
  /// per entry so the sidebar gallery surfaces them.
  NonStandardArtworkList nonStandardArtwork;
};

/// File-I/O phase of applyScrapedItem. Thread-safe — never touches a
/// QSqlDatabase. Decodes / writes artwork bytes to disk under the
/// per-kind layout and computes any non-standard `item_artwork` rows
/// the DB phase should save. Safe to call from a worker thread; pair
/// with `saveScrapedMetadata` back on the thread that owns the DB
/// connection (the main thread for now). Empty `artworkDirectory` or
/// `baseName` short-circuits the write loop and returns an empty
/// result. A set @p cancelToken (Kartend-vi76q) is polled between
/// assets so a cancelled/destructing owner stops the write fan-out
/// promptly instead of finishing every remaining multi-MB asset; the
/// partial result reflects only what was written.
[[nodiscard]] MediaWriteResult
writeMediaFiles(const QString &artworkDirectory, const QString &baseName,
                const QList<PendingMediaWrite> &media,
                RescrapeMode rescrapeMode = RescrapeMode::Overwrite,
                const std::shared_ptr<std::atomic<bool>> &cancelToken = {});

/// Outcome of writeMetadataSidecar — distinguishes a deliberate skip (empty
/// scrape, or a FillMissing run that found an existing sidecar) from a real
/// filesystem failure (mkpath / atomic write), so a caller can surface a
/// failure without mistaking the common skip for one (Kartend audit E-02).
enum class SidecarWriteOutcome { Written, Skipped, Failed };

/// Write a human-readable JSON metadata sidecar for the scraped item
/// at `{artworkDirectory}/metadata/{baseName}.json` — the scraped
/// title / description / genre / etc. plus any provider customFields.
/// Thread-safe (file I/O only, no DB). `rescrapeMode` mirrors the per-asset
/// policy writeMediaFiles applies. Returns Written on a fresh write, Skipped
/// for an empty scrape / FillMissing-existing, Failed on mkpath / write error.
[[nodiscard]] SidecarWriteOutcome
writeMetadataSidecar(const QString &artworkDirectory, const QString &baseName,
                     const ScrapedItem &scraped,
                     RescrapeMode rescrapeMode = RescrapeMode::Overwrite);

/// DB-write phase. Saves the merged metadata row (existing × scraped)
/// plus one `item_artwork` row per entry in `nonStandardArtwork`.
/// Returns the metadataSaved flag (mirrors `ApplyResult.metadataSaved`).
/// Must run on the thread that owns `databaseManager`'s connection;
/// no-ops gracefully when `databaseManager` is null (mirrors the
/// existing "tests without a DB" carve-out).
[[nodiscard]] bool saveScrapedMetadata(IDatabaseManager *databaseManager,
                                       const QString &collectionUuid, const QString &sourcePath,
                                       const ScrapedItem &scraped,
                                       const NonStandardArtworkList &nonStandardArtwork);

/// DB-write phase against a raw `QSqlDatabase`, bypassing IDatabaseManager
/// and its main-thread metadata cache. Used by the BatchScrapeRunner write
/// worker — runs on a dedicated worker thread that owns its own connection
/// to the SQLite file (WAL mode lets one writer + UI-thread readers coexist).
/// The caller is responsible for posting cache invalidations back to the
/// DatabaseManager that owns the metadata cache; this function will not.
/// Same merge semantics as `saveScrapedMetadata` (pick non-empty fields,
/// merge customFields). Returns false on connection problems or save
/// failures; the underlying error is logged via the same channel as the
/// IDatabaseManager path.
[[nodiscard]] bool saveScrapedMetadataDirect(::QSqlDatabase &db, const QString &collectionUuid,
                                             const QString &sourcePath, const ScrapedItem &scraped,
                                             const NonStandardArtworkList &nonStandardArtwork);

} // namespace Scraper

// Required for queued cross-thread invocation of ScrapeWriteWorker::performWrite
// via Q_ARG. Declared next to the type alias so every TU that uses the alias
// also sees the metatype declaration.
Q_DECLARE_METATYPE(Scraper::NonStandardArtworkList)

#endif // SCRAPEPERSISTENCE_H
