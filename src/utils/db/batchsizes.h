#ifndef KARTEND_BATCHSIZES_H
#define KARTEND_BATCHSIZES_H

namespace KartendDb::BatchSizes {

// SQLite caps the number of host parameters per prepared statement at 999
// (SQLITE_LIMIT_VARIABLE_NUMBER, default since 3.32.0). Every batched
// INSERT/UPSERT must keep its (rows * columns_per_row) bind count strictly
// below this — exceeding it manifests as "too many SQL variables" mid-flush.
inline constexpr int SqliteVariableLimit = 999;

// Column count for the staged_items upsert in commitStagedScanResults
// (collection_id, collection_uuid, path, rel_path, name, last_modified,
// file_size, date_added, artwork_path). Keep in sync with the SQL column
// list — if a new column is added, drop StagedScanApplyBatch accordingly to
// stay under SqliteVariableLimit.
inline constexpr int StagedScanColumns = 9;

// Apply-batch row count for the staged-scan upsert. 110 rows * 9 columns =
// 990 binds, leaving headroom under SqliteVariableLimit (999). Derived
// rather than written as a literal so a column addition can't silently
// blow the limit at runtime.
inline constexpr int StagedScanApplyBatch = SqliteVariableLimit / StagedScanColumns - 1;
static_assert(StagedScanApplyBatch * StagedScanColumns < SqliteVariableLimit,
              "staged-scan apply batch exceeds SQLite bind limit");

// Filesystem-scan batch size for the producer side of the scan pipeline
// (used in ScanService's scanMediaDirectory loops). NOT bind-derived —
// chosen for a balance between memory pressure (each row carries path +
// timestamp) and the cost of growing batchPaths/batchTimestamps reserves.
// 199 was the historical value; tuned downward from 200 only because
// other batches in scanservice run alongside it.
inline constexpr int FilesystemScanBatch = 199;

// Number of batches between WAL commits in the long scan loop. Larger
// values amortise the fsync cost over more inserts but extend the worst-
// case data-loss window if Kartend crashes mid-scan.
inline constexpr int ScanCommitInterval = 500;

// UUID list batch size for queries like `WHERE uuid IN (?, ?, …)` in
// querymanagercache. Each UUID consumes one bind, so 500 < 999 with
// plenty of headroom for additional filter binds.
inline constexpr int UuidListBatch = 500;

// Column count for the sorted_items_cache insert in
// QueryManager::insertSortedRows (position, path, uuid). Keep in sync with
// the SQL column list — a column addition shrinks the batch automatically.
inline constexpr int SortedCacheColumns = 3;

// Flush-batch row count for the sorted-items cache rebuild in
// querymanagercache. 332 rows * 3 columns = 996 binds, leaving headroom
// under SqliteVariableLimit (999). Derived rather than written as a literal
// so a column addition can't silently blow the limit at runtime.
inline constexpr int SortedCacheInsertBatch = SqliteVariableLimit / SortedCacheColumns - 1;
static_assert(SortedCacheInsertBatch * SortedCacheColumns < SqliteVariableLimit,
              "sorted-items cache insert batch exceeds SQLite bind limit");

} // namespace KartendDb::BatchSizes

#endif // KARTEND_BATCHSIZES_H
