#include "fuzzymatch.h"

namespace FuzzyMatch {

int score(const QString &query, const QString &candidate) {
  if (query.isEmpty()) {
    // Empty query matches everything so the palette can list every
    // command before the user types anything.
    return 0;
  }
  if (candidate.isEmpty()) {
    return -1;
  }
  // Lowercase the candidate once via a stack-only copy. QString stores
  // its data implicitly shared so this is cheap; we avoid mutating the
  // caller's value or building a new heap allocation per keystroke.
  const QString needle = query.toLower();
  const QString hay = candidate.toLower();

  int qi = 0;
  int total = 0;
  bool lastMatched = false;
  bool lastWasWordBoundary = true; // start-of-string counts as a boundary
  for (int hi = 0; hi < hay.size() && qi < needle.size(); ++hi) {
    const QChar h = hay.at(hi);
    if (h == needle.at(qi)) {
      // Base point for each matched char.
      int delta = 1;
      // Bonus: matches at the start of a word (after whitespace / hyphen
      // / underscore / start of string) are more meaningful than matches
      // mid-word.
      if (lastWasWordBoundary) {
        delta += 5;
      }
      // Bonus: a streak of consecutive matches usually means the user
      // is typing the literal prefix of the term. Larger weight than the
      // boundary bonus so "open" matches "Open settings" higher than
      // the contrived "o p e n" case where every char is a boundary.
      if (lastMatched) {
        delta += 10;
      }
      total += delta;
      lastMatched = true;
      ++qi;
    } else {
      lastMatched = false;
    }
    // Compute the boundary flag for the NEXT iteration based on the
    // current character. Anything non-alphanumeric (and underscore) is
    // treated as a boundary.
    lastWasWordBoundary = !h.isLetterOrNumber() && h != QLatin1Char('_');
  }
  if (qi < needle.size()) {
    return -1; // didn't consume the whole query → no match
  }
  return total;
}

} // namespace FuzzyMatch
