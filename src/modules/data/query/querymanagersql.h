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
constexpr const char *LOAD_ITEMS_BY_UUID =
    "SELECT DISTINCT path FROM items WHERE collection_uuid = ? ORDER BY name "
    "COLLATE NOCASE";
constexpr const char *UPDATE_COLLECTION_SCAN_METADATA =
    "UPDATE collections SET last_scanned = ?, dir_signature = ? WHERE uuid = ?";

} // namespace QuerySQL

#endif // QUERYMANAGERSQL_H
