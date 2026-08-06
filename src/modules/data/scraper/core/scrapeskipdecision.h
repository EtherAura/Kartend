#ifndef SCRAPESKIPDECISION_H
#define SCRAPESKIPDECISION_H

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include "itemmetadata.h"
#include "scrapepersistence.h"
#include "scrapertypes.h"

/// Pure skip/coverage decision logic for the batch scraper's Skip and
/// FillMissing pre-filter, extracted from BatchScrapeRunner so its branches
/// are directly unit-testable without a runner instance, a DB, or (for the
/// pure decision) a filesystem. The runner's filterAlreadyScraped() stays an
/// orchestration method: it batch-loads DB metadata, builds the context via
/// these helpers, and applies the per-item predicate over its queue.
namespace Scraper {

/// Resolved inputs to the pure already-scraped skip decision.
struct SkipDecisionInputs {
  Scraper::RescrapeMode mode = Scraper::RescrapeMode::Overwrite;
  bool writeMetadata = true;
  bool metaPresent = false;          ///< A scraped metadata marker exists (DB row or sidecar).
  bool metaComplete = false;         ///< ...and every core scraped field is populated — the
                                     ///< FillMissing metadata gate (see coreScrapedFieldsComplete).
  bool metaWithinWindow = true;      ///< ...and within the refresh window (or no window).
  bool allWantedMediaCovered = true; ///< Every wanted media type already on disk.
};

/// The Skip / FillMissing / time-window decision, factored out of
/// shouldSkipScrapedItem so its branches are unit-testable without DB or
/// filesystem context (Kartend audit 2w4wz). Returns true when the item is
/// already covered and should be dropped from the queue.
[[nodiscard]] bool decideScrapeSkip(const SkipDecisionInputs &in);

/// True when every CORE scraped metadata field is populated: title, description,
/// genre, developer, publisher, releaseDate. This is the FillMissing "metadata
/// already complete → skip" predicate (Kartend-em3jc): a bare scraped row with
/// empty fields is NOT complete, so it stays queued to fill its gaps.
/// contentRating and players are excluded — providers usually leave them blank, so
/// requiring them would re-scrape nearly everything every run; user-authored
/// fields (notes, personal rating, pin/hide flags, sourceUrl) and optional
/// structured fields (tags, customFields, runtime) are excluded too.
[[nodiscard]] bool coreScrapedFieldsComplete(const ItemMetadataStore::ItemMetadata &md);

/// The wanted media types (lowercase) a provider returned NO asset for, given the
/// run's rescrape @p mode, the @p wantedTypes filter, and the @p media the
/// provider actually supplied (only assets with a valid URL count as supplied).
/// The producer half of the known-absent bookkeeping (Kartend-kihyx): its result
/// is persisted into ItemMetadata::mediaAbsent (accumulate + prune) so FillMissing
/// stops re-chasing media a provider doesn't supply. Empty unless the mode is
/// FillMissing with a non-empty filter — pure so the guard / URL-validity / set
/// difference are unit-testable without a runner. @p wantedTypes are lowercase.
[[nodiscard]] QStringList computeMediaAbsentTypes(Scraper::RescrapeMode mode,
                                                  const QSet<QString> &wantedTypes,
                                                  const QList<Scraper::MediaAsset> &media);

/// Basename indexes of media-on-disk for the coverage check, pre-built once
/// per run so the per-item probe is an O(1) hash lookup (Kartend audit 2w4wz).
struct MediaCoverageIndex {
  /// Per-wanted-type basename index of media files on disk (lowercase).
  QHash<QString, QSet<QString>> presentByType;
  /// `front` basenames in the flat artwork-dir mirror (lowercase).
  QSet<QString> frontFlatBases;
};

/// Scan `{artworkDir}/{type}/` for each wanted type (plus the flat artwork
/// dir mirror for `front`) and index the files by lowercase complete base
/// name. No-ops per branch when @p sidecarCheckPossible is false.
[[nodiscard]] MediaCoverageIndex buildMediaCoverageIndex(const QString &artworkDir,
                                                         const QSet<QString> &wantedTypes,
                                                         bool sidecarCheckPossible);

/// Precomputed, read-only context for the per-item skip predicate.
/// filterAlreadyScraped builds this once (the basename indexes, the
/// batch-loaded DB metadata, and the refresh window) so the per-item check
/// stays O(1) instead of re-scanning the directory / re-querying the DB per
/// path.
struct ScrapeSkipContext {
  Scraper::RescrapeMode mode = Scraper::RescrapeMode::Overwrite;
  bool writeMetadata = true;
  /// Collection artwork directory — used to probe the metadata sidecar.
  QString artworkDir;
  bool dbCheckPossible = false;
  bool sidecarCheckPossible = false;
  /// Effective "wanted" media types under FillMissing (lowercase).
  QSet<QString> wantedTypes;
  /// Batch-loaded item metadata keyed by item path.
  QHash<QString, ItemMetadataStore::ItemMetadata> metadataByPath;
  /// Per-wanted-type basename index of media files on disk (lowercase).
  QHash<QString, QSet<QString>> presentByType;
  /// `front` basenames in the flat artwork dir mirror (lowercase).
  QSet<QString> frontFlatBases;
  /// Wanted media types present for a large-enough share of the collection that
  /// their absence should block the FillMissing skip (Kartend-ib46d). A wanted
  /// type NOT in this set is treated as optional — the provider doesn't reliably
  /// supply it for this system (e.g. video/figurine/pictograms on PlayStation),
  /// so requiring it would keep every item unskippable. Built by
  /// computePrevalentMediaTypes from the collection-wide presence index. Only
  /// consulted for items that already have at least one wanted type on disk —
  /// an item with NO wanted media gets every non-known-absent wanted type
  /// treated as required, since an all-empty collection makes nothing prevalent
  /// and coverage would otherwise be vacuously true (Kartend-1wfi2).
  QSet<QString> requiredMediaTypes;
  bool hasWindow = false;
  QDateTime cutoff;
};

/// Fraction of the collection a wanted media type must be present for before its
/// absence blocks the FillMissing skip. Media on a system is bimodal — the types
/// a provider populates broadly sit near-universal (~85-100%), the rest are
/// sparse — so a high threshold cleanly separates "reliably supplied" from
/// "optional / never delivered" without a per-item learning pass (Kartend-ib46d).
inline constexpr double kMediaPrevalenceThreshold = 0.85;

/// The wanted media types present for at least @p threshold of @p itemCount items
/// (via @p presentByType, with @p frontFlatBases folded into `front`). These are
/// the types whose absence should block a FillMissing skip; sparse types the
/// provider doesn't reliably supply fall out. Pure so the threshold logic is
/// unit-testable. Returns empty when @p itemCount <= 0.
[[nodiscard]] QSet<QString>
computePrevalentMediaTypes(const QHash<QString, QSet<QString>> &presentByType,
                           const QSet<QString> &frontFlatBases, const QSet<QString> &wantedTypes,
                           int itemCount, double threshold);

/// Per-item predicate extracted from filterAlreadyScraped's loop. Returns
/// true when @p path should be dropped from the queue under the active
/// rescrape mode (Skip / FillMissing) given the precomputed @p ctx. Pure
/// read-only over @p ctx (the sidecar probe stats the filesystem).
[[nodiscard]] bool shouldSkipScrapedItem(const QString &path, const ScrapeSkipContext &ctx);

} // namespace Scraper

#endif // SCRAPESKIPDECISION_H
