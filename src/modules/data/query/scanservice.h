#ifndef SCANSERVICE_H
#define SCANSERVICE_H

#include "collection/collectioncontext.h"
#include "errorutils.h"
#include "scanneditemstable.h"
#include "scanworkcontroller.h"
#include <functional>
#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

class PreparedStatementCache;

/**
 * @brief Owns QueryManager's collection-rescan subsystem.
 *
 * Extracted from QueryManager (the scan code was ~1200 LOC interwoven with
 * the load/fetch paths). ScanService is the scan half of that split:
 * filesystem walking, the streaming scanned_items staging pipeline, the
 * in-memory save pipeline, the per-session rescan-needed checks, and the
 * scan cancellation token.
 *
 * Like ScannedItemsTable, ScanService borrows its database connection,
 * prepared-statement cache and transaction-depth counter by reference — the
 * references bind to QueryManager's `m_db` / `m_statementCache` /
 * `m_txnDepth` members, whose addresses are stable even across reconnects.
 * Sharing the depth counter means every KartendDb::DbTransaction guard on
 * this connection coordinates (Kartend-l94tw): a scan transaction nested
 * under a QueryManager guard defers instead of silently breaking atomicity.
 * There is deliberately NO QueryManager back-pointer: the dependency is
 * strictly unidirectional (QueryManager -> ScanService), so the load/scan
 * boundary stays untangled.
 *
 * QueryManager keeps a ScanService member and a thin delegating surface:
 * its public scan slots forward to this object, and its scan signals are
 * re-emitted from this object's matching signals. DatabaseManager therefore
 * still wires to QueryManager exactly as before.
 *
 * QObject because it owns the scan signals (queued back to the main thread).
 * Lives on the same worker thread as its owning QueryManager.
 */
class ScanService : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ScanService)
public:
  explicit ScanService(QSqlDatabase &db, PreparedStatementCache &cache, int &txnDepth,
                       QObject *parent = nullptr);
  ~ScanService() override = default;

signals:
  /// Mirrors QueryManager::errorOccurred — surfaced to the main thread when a
  /// scan transaction/query fails. Re-emitted by QueryManager's signal.
  void errorOccurred(const ErrorUtils::ErrorContext &error);

  /// Emitted when a long-running scan is starting (allows UI to show overlay)
  void scanStarting(const QString &collectionName, int estimatedItems);

  /// Emitted periodically during scan with items processed so far
  void scanItemsProgress(int itemsProcessed, int totalItems);

  /// Emitted after a collection rescan has been applied to the database.
  /// Allows the UI to refresh counts without blocking on the scan.
  void collectionScanCompleted(const QString &collectionUuid);

  /// Emitted alongside collectionScanCompleted with per-scan item totals.
  /// Used by the settings dialog to display an "X of Y items added" summary
  /// after a newly-added collection finishes its first scan.
  /// @param itemsScanned  Files discovered on disk that matched the collection
  ///                       extension filter (i.e. the staged count).
  /// @param itemsApplied  Items successfully upserted into the items table.
  /// @param success       True if the scan committed cleanly (no
  ///                       errors/cancellation).
  void collectionScanSummary(const QString &collectionUuid, int itemsScanned, int itemsApplied,
                             bool success);

public:
  /// Request cancellation of any in-progress scan (thread-safe)
  void requestCancelScan();

  /// Check if scan cancellation was requested (thread-safe)
  [[nodiscard]] bool isScanCancelled() const;

  /// Reset cancellation flag (call before starting new scan)
  void resetScanCancellation();

  /// Returns true when the collection's on-disk state diverges from what the
  /// database has cached (signature change, mtime change, missing files, …).
  bool needsRescan(int collectionIndex, const CollectionConfig &collection);

  /// Walks the media directory (flat or recursive) and returns the discovered
  /// relative file paths plus their timestamps. Used by the in-memory save
  /// pipeline (loadOrScanCollection).
  ///
  /// @p scanCompletedOut (optional, Kartend-fys4o) is set true only when the
  /// directory EXISTS and the walk ran to completion uncancelled — i.e. an
  /// empty result genuinely means "zero files on disk" and is safe to
  /// persist (prune + scan-metadata update). It stays false for a missing /
  /// unmounted directory and for cancelled scans, where persisting an empty
  /// result would wrongly delete the collection's rows.
  QStringList scanMediaDirectory(const CollectionConfig &collection,
                                 QHash<QString, QDateTime> &timestamps, QString *dirSignatureOut,
                                 bool *scanCompletedOut = nullptr);

  /// In-memory save pipeline: stages the supplied file list into scanned_items
  /// then upserts into the persistent items table.
  void saveItemsToDatabase(int collectionIndex, const QStringList &filePaths,
                           const QHash<QString, QDateTime> &timestamps,
                           const CollectionConfig &collection, const QString &dirSignature);

  /// Rescans the given collection if needed, emitting scanStarting /
  /// collectionScanCompleted / collectionScanSummary around the scan. Returns
  /// true only on a clean commit.
  [[nodiscard]] bool ensureCollectionScanned(int collectionIndex,
                                             const CollectionConfig &collection);

  /// Scans the context's collection (and descendants when requested) if a
  /// rescan is needed. Caller is responsible for connection availability and
  /// WAL-view freshness before invoking this.
  void ensureScannedForContext(const CollectionContext &context,
                               const QList<CollectionConfig> &allCollections);

private:
  // Builds the QDir name-filter list ("*.ext") from a collection's extension
  // set. Empty extensions yield an empty filter list (i.e. match everything),
  // matching the legacy inline assembly in the scan/stage paths.
  [[nodiscard]] static QStringList buildNameFilters(const CollectionConfig &collection);

  // scanMediaDirectory's two arms, split out verbatim. scanSequential walks a
  // flat directory; scanParallel dispatches the recursive directory walk across
  // the worker pool. Both append discovered relative paths to filePaths and
  // their mtimes to timestamps, seed *dirSignatureOut, and clear both outputs on
  // cancellation (caller returns the resulting filePaths unchanged).
  void scanSequential(const QDir &dir, const QStringList &nameFilters,
                      QHash<QString, QDateTime> &timestamps, QStringList &filePaths,
                      QString *dirSignatureOut);
  void scanParallel(const QDir &dir, const QStringList &nameFilters,
                    const CollectionConfig &collection, QHash<QString, QDateTime> &timestamps,
                    QStringList &filePaths, QString *dirSignatureOut);

  // Per-file staging primitive shared by stageFilesystemScan's flat and
  // recursive arms: appends (relativePath, mtime) to the pending batch and
  // flushes via flushBatch() once BATCH_SIZE is reached. Returns false only when
  // flushBatch() fails (so the caller breaks out of its loop).
  [[nodiscard]] bool stageScannedFile(QStringList &batchPaths,
                                      QHash<QString, QDateTime> &batchTimestamps,
                                      const QString &relativePath, const QDateTime &mtime,
                                      const std::function<bool()> &flushBatch);

  [[nodiscard]] bool scanAndSaveItemsToDatabase(int collectionIndex,
                                                const CollectionConfig &collection,
                                                int *outItemsScanned = nullptr,
                                                int *outItemsApplied = nullptr);

  // stageFilesystemScan's two arms, split out verbatim (mirroring the
  // scanSequential/scanParallel split above). Both stream discovered files
  // into the pending batch via stageScannedFile()/flushBatch and seed
  // dirSignatureOut. stageRecursiveScan returns false only when the scan-work
  // cancel token is unavailable (controller torn down); stageFlatScan always
  // returns true — cancellation just stops the walk early and the caller's
  // final-commit logic handles the partial batch.
  [[nodiscard]] bool
  stageFlatScan(const QDir &dir, const QStringList &nameFilters, QStringList &batchPaths,
                QHash<QString, QDateTime> &batchTimestamps, const std::function<bool()> &flushBatch,
                const std::function<void(int, int)> &reportProgress, QString &dirSignatureOut);
  [[nodiscard]] bool stageRecursiveScan(const QDir &dir, const QStringList &nameFilters,
                                        QStringList &batchPaths,
                                        QHash<QString, QDateTime> &batchTimestamps,
                                        const std::function<bool()> &flushBatch,
                                        const std::function<void(int, int)> &reportProgress,
                                        QString &dirSignatureOut);

  // Phase 1: Walk the filesystem (flat or recursive) and stream discovered
  // files into the scanned_items temp table. Returns the number of files
  // staged and the computed directory signature. On cancellation the temp
  // table may be partially populated; the caller decides whether to proceed.
  [[nodiscard]] bool stageFilesystemScan(const CollectionConfig &collection,
                                         const QStringList &nameFilters, int &itemsStaged,
                                         QString &dirSignatureOut);

  // Phase 2: Upsert rows from the scanned_items temp table into the
  // persistent items table, delete items no longer on disk, and update
  // collection metadata. Must be called inside a valid staging window
  // (i.e. after stageFilesystemScan succeeded and before the temp table is
  // cleared). Returns the number of items applied.
  [[nodiscard]] bool commitStagedScanResults(const CollectionConfig &collection,
                                             const QString &uuid, const QString &extSignature,
                                             const QString &dirSignature, int &itemsApplied);

  [[nodiscard]] bool prepareCollectionForItemsInsert(const CollectionConfig &collection,
                                                     const QString &uuid,
                                                     const QString &extSignature, int &legacyIdOut);

  // Borrowed worker connection + prepared-statement cache — owned by the
  // QueryManager that owns this ScanService. The references stay valid across
  // reconnects (the QueryManager member objects' addresses are stable).
  QSqlDatabase &m_db;
  PreparedStatementCache &m_cache;
  // The owning QueryManager's m_txnDepth — shared so DbTransaction guards in
  // the scan pipeline coordinate with QueryManager's guards on the same
  // connection (see the class comment).
  int &m_txnDepth;

  // Owns the per-scan cancellation token + the dedicated worker pool that
  // dispatches directory-walk tasks.
  ScanWorkController m_scanWork;

  // Collection UUIDs whose scan failed earlier this session. A failed scan
  // rolls back without persisting dir_signature, so needsRescan() keeps
  // returning true; combined with the collectionScanCompleted-driven reload
  // that re-enters ensureCollectionScanned(), an always-failing scan spins an
  // unbreakable scan->fail->reload loop. Once a UUID lands here it is not
  // retried automatically — only worker-thread access, so no locking needed.
  QSet<QString> m_failedScanUuids;

  // scanned_items TEMP-table staging — borrows m_db by reference (see
  // scanneditemstable.h).
  ScannedItemsTable m_scannedItems{m_db};
};

#endif // SCANSERVICE_H
