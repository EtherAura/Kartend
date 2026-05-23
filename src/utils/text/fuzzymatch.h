#ifndef KARTEND_UTILS_TEXT_FUZZYMATCH_H
#define KARTEND_UTILS_TEXT_FUZZYMATCH_H

#include <QString>

// Simple subsequence-based fuzzy matcher. Each character of the query
// must appear in the candidate in order (case-insensitive); consecutive
// matches and word-boundary matches score higher. Returns -1 when there
// is no match, which lets callers gate the result with a single
// comparison instead of a Result<T> dance.
//
// Designed for the command-palette use case where the candidate list is
// short enough that O(N*M) per keystroke is fine — a query of length M
// against a candidate of length N runs in linear time and allocates
// nothing.

namespace FuzzyMatch {

/// Returns -1 if `query` does not subsequence-match `candidate`,
/// otherwise a score where higher is a better match. Empty query
/// matches every candidate with score 0 so the palette can list every
/// command before the user types anything.
[[nodiscard]] int score(const QString &query, const QString &candidate);

} // namespace FuzzyMatch

#endif // KARTEND_UTILS_TEXT_FUZZYMATCH_H
