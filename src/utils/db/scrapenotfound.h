#ifndef SCRAPENOTFOUND_H
#define SCRAPENOTFOUND_H

#include <QDateTime>
#include <QString>

#include <optional>

#include "errorutils.h"

class QSqlDatabase;

/// Row access to the `scrape_not_found` table (schema v30, Kartend-bb8df):
/// one stamp per (collection_uuid, path, provider) recording that the
/// provider did not recognise the item's lookup.
///
/// Every call runs one bound-parameter statement on the caller's connection,
/// which must belong to the calling thread. Nothing here opens, commits or
/// rolls back a transaction — a call made inside the caller's DbTxn is part of
/// it — and nothing touches items, item_metadata or item_artwork.
namespace ScrapeNotFoundStore {

/// One `scrape_not_found` row. `lookupKey` is opaque: the provider-serialised
/// key that was sent, stored and returned verbatim. `missedAt` is any valid
/// QDateTime; it is stored as UTC ISO-8601 WITH milliseconds
/// (`2026-11-01T10:00:00.123Z`) and loaded back as the same UTC instant.
struct Stamp {
  QString collectionUuid;
  QString path;
  QString provider;
  QString lookupKey;
  QDateTime missedAt;
};

/// Loads the stamp for (collectionUuid, path, provider). An empty optional
/// means "no stamp"; an error means the connection is closed, the query
/// failed, or the stored `missed_at` is not a readable timestamp.
[[nodiscard]] ErrorUtils::Result<std::optional<Stamp>>
load(QSqlDatabase &db, const QString &collectionUuid, const QString &path, const QString &provider);

/// Inserts or updates the stamp for (collectionUuid, path, provider),
/// replacing `lookup_key` and `missed_at` in place (same row id). Rejects an
/// empty `path`, an empty `provider` or an invalid `missedAt` without
/// writing.
[[nodiscard]] ErrorUtils::Result<bool> record(QSqlDatabase &db, const Stamp &stamp);

/// Removes the stamp for (collectionUuid, path, provider). Succeeds when no
/// such stamp exists.
[[nodiscard]] ErrorUtils::Result<bool> remove(QSqlDatabase &db, const QString &collectionUuid,
                                              const QString &path, const QString &provider);

} // namespace ScrapeNotFoundStore

#endif
