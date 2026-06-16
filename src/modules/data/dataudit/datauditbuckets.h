#ifndef DATAUDITBUCKETS_H
#define DATAUDITBUCKETS_H

#include <QMetaType>

#include "dataudittypes.h"

/// RomVault-style rollup buckets for the audit browser (Kartend-34lab).
///
/// The browser's tree and game list summarise many per-entry statuses into a
/// handful of coloured counts. This maps Kartend's nine-status taxonomy
/// (dataudittypes.h) onto that smaller, glance-able set and accumulates the
/// counts. Pure value logic (QtCore only) so it stays unit-testable.
namespace DatAudit {

/// The coarse buckets a node's contents roll up into. `Mia` is orthogonal to
/// the rest — an entry can be both Missing and Mia — so it is tracked as a
/// separate count, not a mutually-exclusive category.
enum class Bucket {
  Complete, ///< Content present (Have / WrongName).
  Fixable,  ///< Recoverable in place (WrongName / WrongHash / Duplicate).
  Missing,  ///< Catalogue entry with no content.
  Unknown,  ///< On-disk file matching no entry.
  Corrupt,  ///< Unreadable / bad dump.
};

/// Accumulated counts for one tree node / game, in the buckets the chips show.
/// `have`/`fixable` deliberately overlap (a WrongName entry is both present and
/// rename-fixable) — the chips read like RomVault's, where the same entry can
/// contribute to more than one coloured total.
struct BucketCounts {
  int have = 0;    ///< Complete bucket (content present).
  int fixable = 0; ///< Recoverable-in-place.
  int missing = 0; ///< Absent catalogue entries.
  int unknown = 0; ///< Unmatched on-disk files.
  int corrupt = 0; ///< Unreadable.
  int mia = 0;     ///< Flagged MIA in the DAT (orthogonal).
  int total = 0;   ///< Every counted row (sum of the per-status counts).

  /// Fold `count` rows of one (status, mia) combination into the buckets.
  void add(Status status, bool mia, int count);
  /// Sum another node's counts into this one (child → parent rollup).
  void add(const BucketCounts &other);

  [[nodiscard]] bool isEmpty() const { return total == 0; }
};

/// Per-game completeness state, for the Complete/Partial/Empty filter. Derived
/// from a game's BucketCounts once all its rows are folded in.
enum class GameState {
  Empty,    ///< No content present (all Missing).
  Partial,  ///< Some content present, some absent.
  Complete, ///< All entries present.
};

/// Classify a game from its rolled-up counts. A game with zero catalogue
/// entries (only Unknown files) reads as Empty.
[[nodiscard]] GameState gameStateOf(const BucketCounts &c);

} // namespace DatAudit

// Travels through QVariant (tree/table model roles → the badge delegate).
Q_DECLARE_METATYPE(DatAudit::BucketCounts)

#endif // DATAUDITBUCKETS_H
