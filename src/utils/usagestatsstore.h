#ifndef USAGESTATSSTORE_H
#define USAGESTATSSTORE_H

#include <QHash>
#include <QList>
#include <QString>

#include "errorutils.h"

class QSqlDatabase;

/// Read/write helpers for per-item usage statistics stored on the existing
/// `items` table (Kartend-7vi).
///
/// Three columns drive everything:
///   - `play_count`         (INTEGER, since v1)        — incremented on launch
///   - `last_played`        (TEXT ISO-8601, since v1)  — stamped on launch
///   - `total_play_seconds` (INTEGER, since v7)        — accumulated session
///                                                       runtime when runtime
///                                                       detection is on
///
/// Items are keyed on (collection_uuid, path). Aggregates query the whole
/// `items` table; per-collection breakdown groups by collection_uuid.
namespace UsageStatsStore {

/// Per-item usage row. All fields default to "unset" so callers can detect
/// items that have never been launched without an extra existence check.
struct ItemUsageStats {
  QString collectionUuid;
  QString path;
  qint64 playCount = 0;
  /// ISO-8601 UTC timestamp; empty string means the item has never launched.
  QString lastPlayed;
  /// Cumulative runtime in seconds across all tracked sessions. 0 when the
  /// item has only been launched detached (no runtime detection).
  qint64 totalPlaySeconds = 0;

  [[nodiscard]] bool isEmpty() const {
    return playCount == 0 && lastPlayed.isEmpty() && totalPlaySeconds == 0;
  }
};

/// One row in the "Top played" / "Recently played" lists. The row carries
/// enough to render a list entry without a follow-up DB roundtrip — name +
/// owning collection uuid (so the dialog can show a friendly collection
/// label) + the stats fields.
struct ItemUsageRow {
  QString name;
  QString collectionUuid;
  QString path;
  qint64 playCount = 0;
  QString lastPlayed;
  qint64 totalPlaySeconds = 0;
};

/// Whole-library aggregate counters (used by the Statistics dialog header).
struct AggregateStats {
  qint64 totalItems = 0;
  qint64 totalLaunches = 0;
  qint64 totalPlaySeconds = 0;
  qint64 itemsLaunchedAtLeastOnce = 0;
};

/// Per-collection breakdown row. The dialog joins this with the in-memory
/// CollectionConfig list to label rows; we only return the uuid here so the
/// store has no dependency on collectionutils.
struct CollectionUsage {
  QString collectionUuid;
  qint64 itemCount = 0;
  qint64 totalLaunches = 0;
  qint64 totalPlaySeconds = 0;
};

/// Loads usage stats for one item. Returns a default-initialized struct (with
/// the keys preserved) when no row exists. Returns an error only on real DB
/// failures.
[[nodiscard]] ErrorUtils::Result<ItemUsageStats>
loadForItem(QSqlDatabase &db, const QString &collectionUuid, const QString &path);

/// Increments `play_count` and stamps `last_played` to the current UTC time.
/// Called from LaunchManager after a successful launch. Failure is logged but
/// non-fatal — usage tracking is best-effort.
[[nodiscard]] ErrorUtils::Result<bool> recordLaunch(QSqlDatabase &db, const QString &collectionUuid,
                                                    const QString &path);

/// Adds `seconds` to `total_play_seconds`. Called from LaunchManager when a
/// runtime-tracked child process exits (Kartend-qxv pipeline). `seconds` must
/// be non-negative; zero/negative inputs are rejected to keep noise out of
/// the table.
[[nodiscard]] ErrorUtils::Result<bool> recordPlaySession(QSqlDatabase &db,
                                                         const QString &collectionUuid,
                                                         const QString &path, qint64 seconds);

/// Whole-library aggregate counters. `totalItems` is `COUNT(*)`,
/// `itemsLaunchedAtLeastOnce` is `COUNT(*) WHERE play_count > 0`. The other
/// two are `SUM`s. Returns zeros when the table is empty.
[[nodiscard]] ErrorUtils::Result<AggregateStats> loadAggregate(QSqlDatabase &db);

/// Top items by play_count, descending. Items with zero play_count are
/// filtered out. `limit` is clamped to [1, 1000] internally so callers can
/// pass user-driven values without sanitizing.
[[nodiscard]] ErrorUtils::Result<QList<ItemUsageRow>> loadTopPlayed(QSqlDatabase &db, int limit);

/// Most recently played items (descending `last_played`). Items that have
/// never been launched are filtered out. `limit` is clamped to [1, 1000].
[[nodiscard]] ErrorUtils::Result<QList<ItemUsageRow>> loadRecentlyPlayed(QSqlDatabase &db,
                                                                         int limit);

/// Per-collection breakdown keyed by collection_uuid. The dialog filters this
/// down to known collections; uuids without a matching CollectionConfig (e.g.
/// stale rows from deleted collections) are still returned so the totals
/// stay self-consistent with the aggregate row.
[[nodiscard]] ErrorUtils::Result<QHash<QString, CollectionUsage>>
loadCollectionBreakdown(QSqlDatabase &db);

/// Resets every usage column to zero/NULL across the entire library. Used by
/// the dialog's "Reset usage stats…" button. Does not touch metadata or
/// artwork rows — only the three tracking columns.
[[nodiscard]] ErrorUtils::Result<bool> resetAll(QSqlDatabase &db);

/// Formats a duration in seconds as a compact human-readable string.
/// "1h 23m" for >=1h, "12m 34s" for >=1m, "45s" otherwise. Returns an empty
/// string for zero/negative input so callers can skip rendering blank rows.
[[nodiscard]] QString formatDuration(qint64 seconds);

/// Formats an ISO-8601 timestamp (assumed UTC) as a localized
/// short-date+time string for display. Returns the input verbatim when it
/// cannot be parsed so callers always get something renderable.
[[nodiscard]] QString formatTimestamp(const QString &isoUtc);

} // namespace UsageStatsStore

#endif // USAGESTATSSTORE_H
