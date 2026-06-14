#ifndef DATCOLLECTIONMATCH_H
#define DATCOLLECTIONMATCH_H

#include <QList>
#include <QString>
#include <QStringList>

/// DAT-to-collection match scoring.
///
/// Ranks which existing collection a DAT catalogue most plausibly
/// belongs to, for the DAT-library review flow: the library scans a
/// watched folder, probes each DAT's header (DatLookup::probeHeader /
/// DatCache), and asks this module which collections to *propose*.
/// The user confirms every attachment — this module only scores and
/// ranks, it never attaches anything itself.
///
/// Pure value-struct API: no Qt Widgets, no manager dependencies, and
/// no I/O beyond a QFileInfo::canonicalFilePath() probe for the
/// already-attached check, so it unit-tests headlessly. Inputs are
/// deliberately *not* CollectionConfig — that struct is the settings
/// god-object and depending on it would couple a pure scorer to the
/// whole persistence surface. Callers copy the three fields this
/// scorer needs into CollectionInfo instead.
///
/// Scoring formula, per collection:
///
///   nameScore  = Jaccard(datTokens, collectionTokens), raised to
///                kContainmentNameScore when every collection-name
///                token appears in the DAT-name tokens
///   extOverlap = |datExts ∩ collExts| / |datExts|
///   score      = kNameWeight * nameScore
///              + kExtWeight * extOverlap   (both ext sets non-empty, overlap > 0)
///              - kZeroExtOverlapPenalty    (both ext sets non-empty, overlap == 0)
///              + 0                         (either ext set empty — neutral)
///   clamped to [0, 1]; candidates below kScoreThreshold are dropped.
///
/// datTokens come from headerName, else headerDescription, else the
/// path's base filename — lowercased, parenthesized/bracketed
/// qualifiers stripped (catalogue names often carry revision suffixes
/// like "(2026 Refresh)" or "Set - Subset" dashes), then split on any
/// non-alphanumeric run. Name similarity dominates by construction:
/// extension overlap alone (at most kExtWeight) can never clear
/// kScoreThreshold, so it acts as a tiebreaker/booster — and as a
/// penalty when both sides declare extensions yet share none.
namespace DatCollectionMatch {

/// Weight of normalized-name similarity in the combined score. The
/// name is the dominant signal — extension overlap corroborates but
/// can never substitute for it.
inline constexpr double kNameWeight = 0.85;

/// Weight of the extension-overlap ratio when both sides declare
/// extensions and share at least one.
inline constexpr double kExtWeight = 0.15;

/// Flat penalty when both sides declare extensions but share none — a
/// name lookalike whose files the collection wouldn't even accept is
/// probably the wrong home, but the name signal may still be strong
/// enough to propose (demote, don't veto).
inline constexpr double kZeroExtOverlapPenalty = 0.15;

/// Name-score floor when every collection-name token appears in the
/// DAT name. Catalogue names tend to be the longer side ("Audio -
/// Lossless Masters" vs collection "Lossless Masters"); plain Jaccard
/// would punish the extra tokens even though full containment is a
/// strong signal. Kept below 1.0 so an exact token-set match still
/// outranks a containment match.
inline constexpr double kContainmentNameScore = 0.9;

/// Minimum combined score for a candidate to be proposed at all.
/// Chosen so that: extension overlap alone (max kExtWeight = 0.15)
/// never proposes; an exact or qualifier-stripped name match with no
/// extension signal (0.85) clears comfortably; a containment name
/// match survives even the zero-overlap penalty (0.9 * 0.85 - 0.15 =
/// 0.615); and a half-token name overlap (0.5 * 0.85 = 0.425) needs
/// extension corroboration to be proposed.
inline constexpr double kScoreThreshold = 0.55;

/// The slice of a collection this scorer needs. Mirrors the
/// CollectionConfig conventions without including it: `extensions` is
/// the allow-list as persisted by collectionscalars_persistence —
/// lowercase, no leading dots, deduped (e.g. "mkv", "flac").
struct CollectionInfo {
  QString uuid;
  QString name;
  QStringList extensions;
  /// DAT files already attached to this collection. Compared against
  /// DatInfo::path by canonical path — see
  /// MatchResult::alreadyAttachedTo for the exact rules.
  QStringList attachedDatPaths;
};

/// What is known about one DAT file, typically sourced from
/// DatCache::peek / openOrIngest. `romExtensions` is derived by the
/// caller from the cached records' romName extensions — lowercase, no
/// dots, deduped, matching the CollectionInfo::extensions convention.
struct DatInfo {
  QString path;
  QString headerName;
  QString headerDescription;
  QStringList romExtensions;
};

/// One proposed collection. `reason` is a short, stable English
/// summary of which signals fired ("name match + extension overlap")
/// — meant for the review dialog's detail column, not for parsing.
struct Candidate {
  QString collectionUuid;
  double score = 0.0;
  QString reason;
};

/// Ranked proposals for one DAT.
struct MatchResult {
  /// Candidates scoring >= kScoreThreshold, ordered score-descending,
  /// then collection name ascending (case-insensitive, then
  /// case-sensitive, then uuid) so equal-score ties are deterministic.
  /// Near-ties are intentionally *both* present — the review flow
  /// shows every surviving candidate and never auto-picks, so
  /// ambiguity is the caller's to surface, not this module's to
  /// resolve.
  QList<Candidate> candidates;
  /// Uuid of the collection whose attachedDatPaths already lists this
  /// DAT's path (empty when none does; first in input order when
  /// several do). That collection is excluded from `candidates` — an
  /// attached DAT is a fact, not a proposal. Paths are compared via
  /// QFileInfo::canonicalFilePath() with fallback to the raw string
  /// (mirroring DatCache::peek), so the same file reached through a
  /// different relative/symlinked path still counts; missing files
  /// degrade to plain string comparison.
  QString alreadyAttachedTo;
};

/// Score every collection against `dat` and return the ranked
/// proposals. Pure function of its inputs apart from the
/// canonical-path probe described on MatchResult::alreadyAttachedTo.
/// Empty inputs are safe: no collections, or a DatInfo with every
/// field empty, simply yield an empty result.
[[nodiscard]] MatchResult rankCollections(const DatInfo &dat,
                                          const QList<CollectionInfo> &collections);

} // namespace DatCollectionMatch

#endif // DATCOLLECTIONMATCH_H
