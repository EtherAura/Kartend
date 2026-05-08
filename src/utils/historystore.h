#ifndef HISTORYSTORE_H
#define HISTORYSTORE_H

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
[[nodiscard]] ErrorUtils::Result<bool> recordLaunch(QSqlDatabase &db, const QString &collectionUuid,
                                                    const QString &path, const QString &name);

/// Most recent entries first (descending id, which matches descending
/// launched_at because ids are monotonic per session). `limit` is clamped
/// internally to [1, 100000] so callers can pass user-driven values without
/// sanitizing.
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
