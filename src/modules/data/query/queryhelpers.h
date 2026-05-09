#ifndef QUERYHELPERS_H
#define QUERYHELPERS_H

#include "collectiontypes.h"

#include <QString>

// Pure helpers extracted from QueryManager so SQL/FTS pre-processing and
// alphabetical sort-priority rules can be unit-tested without instantiating
// QueryManager (which owns a QSqlDatabase + worker-thread state).
//
// See bd for the broader extraction effort.
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

// Returns the ORDER BY clause for a given SortMode.
//
//   useSortPrefix=true emits the `sort_<col>` aliases used in the
//     deduplicated GROUP BY queries (fetchItemsRange) where MIN(name) etc.
//     are aliased to avoid SQLite's "no such column" error.
//   useSortPrefix=false emits the bare `<col>` references used in non-
//     aliased queries (fetchItemPosition, fetchItemCount).
//   withLimitOffset=true appends ` LIMIT ? OFFSET ?` for paginated queries.
//
// The collection_uuid column is never sort-aliased — it's the grouping key,
// so its name is the same in both styles.
//
// SortModes that don't have a dedicated SQL ORDER BY (NameAscending,
// ArtworkFirst, ArtworkLast, Random) fall through to alphabetical ascending
// since artwork-presence and randomness are handled by sorted_items_cache,
// not by SQL ORDER BY.
[[nodiscard]] auto orderByForSortMode(SortMode mode, bool useSortPrefix, bool withLimitOffset)
    -> QString;

// Returns a comma-separated placeholder list for SQL `IN (...)` clauses.
//
//   placeholderList(0) -> ""           (caller is expected to handle empty)
//   placeholderList(1) -> "?"
//   placeholderList(3) -> "?, ?, ?"
//
// Note: this is the bare list, no parentheses. For the temp-table-aware
// `EXISTS (SELECT ...) | IN (?, ?, ...)` switch, see
// QueryManager::buildUuidFilterClause which decides between the two forms
// based on uuid count vs SQLite's variable bind limit.
[[nodiscard]] auto placeholderList(int n) -> QString;

} // namespace QueryHelpers

#endif // QUERYHELPERS_H
