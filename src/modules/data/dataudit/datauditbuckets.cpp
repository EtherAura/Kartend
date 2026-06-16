#include "datauditbuckets.h"

namespace DatAudit {

void BucketCounts::add(Status status, bool isMia, int count) {
  if (count <= 0) {
    return;
  }
  total += count;
  if (isMia) {
    mia += count;
  }
  switch (status) {
  case Status::Have:
    have += count;
    break;
  case Status::WrongName:
    // Content is present (counts as Complete) and a rename can canonicalise it
    // (counts as Fixable) — like RomVault, the same entry feeds both chips.
    have += count;
    fixable += count;
    break;
  case Status::WrongHash:
  case Status::Duplicate:
    fixable += count;
    break;
  case Status::Missing:
    missing += count;
    break;
  case Status::Unknown:
    unknown += count;
    break;
  case Status::Corrupt:
  case Status::BadDump:
    corrupt += count;
    break;
  case Status::Unscanned:
    // Treated as not-yet-present for rollup purposes.
    missing += count;
    break;
  }
}

void BucketCounts::add(const BucketCounts &other) {
  have += other.have;
  fixable += other.fixable;
  missing += other.missing;
  unknown += other.unknown;
  corrupt += other.corrupt;
  mia += other.mia;
  total += other.total;
}

GameState gameStateOf(const BucketCounts &c) {
  // Catalogue entries the game declares = present + missing (Unknown files are
  // not part of any game, Corrupt is a read failure, not a completeness state).
  const int present = c.have;
  const int absent = c.missing;
  if (present <= 0) {
    return GameState::Empty;
  }
  if (absent > 0) {
    return GameState::Partial;
  }
  return GameState::Complete;
}

} // namespace DatAudit
