#ifndef SCRAPESKIPDECISION_H
#define SCRAPESKIPDECISION_H

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>

#include "itemmetadata.h"
#include "scrapepersistence.h"

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
  bool metaPresent = false;          ///< Metadata exists (DB row or sidecar).
  bool metaWithinWindow = true;      ///< ...and within the refresh window (or no window).
  bool allWantedMediaCovered = true; ///< Every wanted media type already on disk.
};

/// The Skip / FillMissing / time-window decision, factored out of
/// shouldSkipScrapedItem so its branches are unit-testable without DB or
/// filesystem context (Kartend audit 2w4wz). Returns true when the item is
/// already covered and should be dropped from the queue.
[[nodiscard]] bool decideScrapeSkip(const SkipDecisionInputs &in);

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
  bool hasWindow = false;
  QDateTime cutoff;
};

/// Per-item predicate extracted from filterAlreadyScraped's loop. Returns
/// true when @p path should be dropped from the queue under the active
/// rescrape mode (Skip / FillMissing) given the precomputed @p ctx. Pure
/// read-only over @p ctx (the sidecar probe stats the filesystem).
[[nodiscard]] bool shouldSkipScrapedItem(const QString &path, const ScrapeSkipContext &ctx);

} // namespace Scraper

#endif // SCRAPESKIPDECISION_H
