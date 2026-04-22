#ifndef QUERYHELPERS_H
#define QUERYHELPERS_H

#include <QString>

// Pure helpers extracted from QueryManager so SQL/FTS pre-processing and
// alphabetical sort-priority rules can be unit-tested without instantiating
// QueryManager (which owns a QSqlDatabase + worker-thread state).
//
// See bd Kartend-tty for the broader extraction effort.
namespace QueryHelpers {

// Builds an FTS5 prefix query from a raw user search string.
//
// - Trims and sanitizes the input by collapsing any non-alphanumeric/
//   underscore runs to spaces.
// - Splits on whitespace, appends '*' to each token (prefix match), and
//   joins with " AND ".
// - Returns an empty string when the input is blank or fully-sanitized away.
[[nodiscard]] auto buildFtsPrefixQuery(const QString &raw) -> QString;

// Returns the human-readable display name for a file base name by replacing
// underscores with spaces and collapsing extra whitespace.
[[nodiscard]] auto displayNameForBase(const QString &baseName) -> QString;

// Returns the alphabetical sort priority bucket for a string's first
// character. Lower priorities sort first (ascending order).
//
// Buckets:
//   0 - Bracketed text starting with '[' or '('
//   1 - Other punctuation/symbol prefixes
//   2 - Digits (or apostrophe-prefixed digit, e.g. "'89")
//   3 - Letters, empty strings, or apostrophe-prefixed letters
[[nodiscard]] auto characterSortPriority(const QString &text) -> int;

} // namespace QueryHelpers

#endif // QUERYHELPERS_H
