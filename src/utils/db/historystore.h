#ifndef HISTORYSTORE_H
#define HISTORYSTORE_H

#include <QDateTime>
#include <QList>
#include <QString>

#include "errorutils.h"

class QSqlDatabase;

/// Read/write helpers for the chronological launch-history log
/// Each successful launch appends one row to `launch_history`;
/// the table is intentionally append-only so the dialog can show "I launched
/// X, then Y, then X again" instead of the deduped aggregate that lives on
/// the `items` table.
///
/// Rows are keyed only by their auto-incrementing id — (collection_uuid,
/// path) is duplicated across entries on purpose. trimToMaxEntries deletes
/// the oldest rows once the user-configured cap is exceeded.
namespace HistoryStore {

/// One entry in the history log. `name` is denormalized at insert time so
/// the dialog can render rows without a follow-up `items` join (the row
/// stays meaningful even after the source item or collection is deleted).
struct HistoryEntry {
  qint64 id = 0;
  QString collectionUuid;
  QString path;
  QString name;
  /// ISO-8601 UTC timestamp.
  QString launchedAt;
};

/// Appends one history row. `name` is optional — pass the user-visible item
/// name when the caller already has it; otherwise the dialog falls back to
/// the path's basename. Failure is logged but non-fatal; the launch path
/// never blocks on history tracking.
///
/// Kartend-neb85: @p stamp is when the launch HAPPENED — see the identical
/// note on UsageStatsStore::recordLaunch. An invalid stamp (the default) falls
/// back to the current UTC time.
[[nodiscard]] ErrorUtils::Result<bool> recordLaunch(QSqlDatabase &db, const QString &collectionUuid,
                                                    const QString &path, const QString &name,
                                                    const QDateTime &stamp = {});

/// Most recent entries first, ordered by `launched_at` with id as tiebreaker.
///
/// Kartend-neb85: this used to order by id alone, on the reasoning that ids
/// are monotonic per session so id order matched launch order. That stopped
/// being true once launches began carrying their queue-time stamp: a write
/// deferred by lock contention inserts LATER (higher id) while carrying an
/// EARLIER launched_at, which would surface an older launch above a newer one.
/// The id tiebreaker still resolves same-second launches, which is the common
/// case given launched_at has one-second resolution.
///
/// `limit` is clamped internally to [1, 100000] so callers can pass
/// user-driven values without sanitizing.
[[nodiscard]] ErrorUtils::Result<QList<HistoryEntry>> loadRecent(QSqlDatabase &db, int limit);

/// Total number of rows currently in the table. Used by the dialog header
/// and by trim decisions.
[[nodiscard]] ErrorUtils::Result<qint64> count(QSqlDatabase &db);

/// Deletes the oldest rows so the table holds at most `maxEntries`. A
/// non-positive `maxEntries` is treated as "unlimited" and is a no-op so
/// the user can disable trimming by setting an absurd cap. Returns the
/// number of rows actually removed.
[[nodiscard]] ErrorUtils::Result<qint64> trimToMaxEntries(QSqlDatabase &db, qint64 maxEntries);

/// Wipes every row. Used by the "Clear history…" button.
[[nodiscard]] ErrorUtils::Result<bool> clearAll(QSqlDatabase &db);

} // namespace HistoryStore

#endif // HISTORYSTORE_H
