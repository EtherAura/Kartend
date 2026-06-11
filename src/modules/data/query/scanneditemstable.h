#ifndef SCANNEDITEMSTABLE_H
#define SCANNEDITEMSTABLE_H

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QSqlDatabase;
QT_END_NAMESPACE

/**
 * @brief Owns the per-connection `scanned_items` SQLite TEMP table — the
 * staging area for a collection rescan.
 *
 * Extracted from QueryManager: these five operations were loosely-related
 * QueryManager members that touch only the worker QSqlDatabase (raw
 * QSqlQuery, never the prepared-statement cache or any other manager), so
 * they form a clean leaf. The borrowed QSqlDatabase reference stays valid
 * across reconnects — it binds to QueryManager's `m_db` member object,
 * whose address is stable even when its contents are reassigned.
 *
 * Staging flow during a rescan: ensure() → clear() → insertBatch()×N →
 * applyToItems() (upsert into `items`) → deleteItemsMissingFromScan()
 * (prune rows whose files vanished from disk).
 */
class ScannedItemsTable {
public:
  explicit ScannedItemsTable(QSqlDatabase &db) : m_db(db) {}

  /// CREATE TEMP TABLE IF NOT EXISTS scanned_items. False if the
  /// connection is closed or the DDL fails.
  [[nodiscard]] bool ensure();

  /// DELETE FROM scanned_items — empties the staging table for a fresh scan.
  void clear();

  /// INSERT OR REPLACE one batch of discovered files: absolute `path`
  /// (PK, shared with items.path), media-dir-relative `rel_path`, name,
  /// last-modified and file size.
  void insertBatch(const QStringList &paths, const QHash<QString, QDateTime> &timestamps,
                   const QString &mediaRoot);

  /// Upsert every staged row into the persistent `items` table for the
  /// given collection. False on prepare/exec failure.
  [[nodiscard]] bool applyToItems(int legacyId, const QString &collectionUuid);

  /// Delete `items` rows for `collectionUuid` whose path is absent from
  /// scanned_items — i.e. files removed from disk since the last scan.
  /// @p errorDetailsOut (optional, Kartend-kt39d) receives the driver error
  /// text on failure so callers can classify lock contention
  /// (KartendDb::isLockContentionError) for their retry ladders.
  [[nodiscard]] bool deleteItemsMissingFromScan(const QString &collectionUuid,
                                                QString *errorDetailsOut = nullptr);

private:
  QSqlDatabase &m_db;
};

#endif // SCANNEDITEMSTABLE_H
