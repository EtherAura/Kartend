// SQL constants for prepared statement caching, shared across the
// QueryManager translation units. Definitions used to live inline in
// querymanager.cpp; they were promoted to a header so additional TUs that
// implement QueryManager methods (e.g. querymanagerlifecycle.cpp) can
// reuse the same prepared-statement keys without duplicating literals.
#ifndef QUERYMANAGERSQL_H
#define QUERYMANAGERSQL_H

namespace QuerySQL {

constexpr const char *COLLECTION_INFO =
    "SELECT last_scanned, name, ext_signature, dir_signature FROM collections "
    "WHERE uuid = ?";
constexpr const char *ITEM_PATH_CHECK = "SELECT path FROM items WHERE collection_uuid = ? LIMIT 1";
constexpr const char *ITEMS_MODIFIED_COUNT =
    "SELECT COUNT(*) FROM items WHERE collection_uuid = ? AND last_modified > "
    "?";
constexpr const char *ITEMS_COUNT_BY_UUID = "SELECT COUNT(*) FROM items WHERE collection_uuid = ?";
constexpr const char *DELETE_ITEMS_BY_UUID = "DELETE FROM items WHERE collection_uuid = ?";
constexpr const char *DELETE_COLLECTION_BY_UUID = "DELETE FROM collections WHERE uuid = ?";
// Returns the media-dir-relative path (rel_path) so the C++-side subfolder
// filters in loadItems / loadItemsWithSubcollections keep working: items.path
// is absolute since v13, but the scan branch of loadOrScanCollection still
// yields relative paths, so the cached branch must match. COALESCE falls back
// to path for any row not yet touched by the v13 reconcile.
// appendFileMapsAndListCanonical re-absolutizes via QDir::absoluteFilePath, so
// a relative value here resolves correctly.
//
// last_modified / file_size ride along so the Date/Size sort modes in
// sortFiles can reuse the values already persisted by the scan instead of
// re-statting every file on each collection load (Kartend-m9r1s). The former
// single-column DISTINCT became a C++-side seen-set in
// loadItemsFromDatabaseByUuid: DISTINCT over three columns would stop
// collapsing rows that share a path but differ only in metadata.
constexpr const char *LOAD_ITEMS_BY_UUID =
    "SELECT COALESCE(rel_path, path), last_modified, file_size FROM items "
    "WHERE collection_uuid = ? ORDER BY name COLLATE NOCASE";
constexpr const char *UPDATE_COLLECTION_SCAN_METADATA =
    "UPDATE collections SET last_scanned = ?, dir_signature = ? WHERE uuid = ?";

// Added Kartend-o5mr — used to be ad-hoc prepares inside ScanService that
// re-prepared on every call. Routing them through PreparedStatementCache
// shares the prepared statement across the whole scan.
constexpr const char *UPDATE_COLLECTION_EXT_SIGNATURE =
    "UPDATE collections SET ext_signature = ? WHERE uuid = ?";
constexpr const char *SELECT_STAGED_SCAN_RESULTS =
    "SELECT rowid, path, rel_path, name, last_modified, file_size FROM scanned_items "
    "WHERE rowid > ? ORDER BY rowid LIMIT ?";

// Added Kartend-de4ft — the sorted-cache fast paths built and prepared these
// locally on every call (the range probe fires per scroll page, the position
// probe per selection restore). Routed through PreparedStatementCache the
// compiled statement is reused across the whole session; sorted_items_cache
// is DELETE-repopulated, never dropped, so the statements stay valid.
constexpr const char *SELECT_SORTED_CACHE_RANGE = "SELECT path, uuid FROM sorted_items_cache "
                                                  "WHERE position >= ? AND position < ? "
                                                  "ORDER BY position";
constexpr const char *SELECT_SORTED_CACHE_POSITION =
    "SELECT position FROM sorted_items_cache WHERE path = ?";

} // namespace QuerySQL

#endif // QUERYMANAGERSQL_H
