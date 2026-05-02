#ifndef ITEMMETADATA_H
#define ITEMMETADATA_H

#include <QList>
#include <QPair>
#include <QString>

#include "errorutils.h"

class QSqlDatabase;

namespace ItemMetadataStore {

/// List of (key, value) pairs for user-defined custom fields. Returned by
/// `parseCustomFields` in alphabetical key order (QJsonObject sorts keys
/// alphabetically), which gives a stable, predictable rendering in the
/// sidebar regardless of the order rows were edited.
using CustomFieldList = QList<QPair<QString, QString>>;

/// Extended per-item metadata stored in the `item_metadata` table.
///
/// Keyed on (collection_uuid, path). All structured fields are optional so
/// that partially-populated rows (e.g. only a description scraped) render
/// only the fields that are present.
struct ItemMetadata {
  QString collectionUuid;
  QString path;
  QString title;
  QString description;
  QString genre;
  QString developer;
  QString publisher;
  QString releaseDate;
  QString contentRating;
  QString players;
  /// Runtime in seconds. Negative means "unset" (the column is NULLable).
  int runtimeSeconds = -1;
  /// JSON array string (e.g. `["co-op","arcade"]`) or empty.
  QString tags;
  /// JSON object string for user-defined key/value pairs (Kartend-hpln).
  QString customFields;
  /// Optional override for the per-item manual file (Kartend-9jdv).
  QString manualPath;
  /// Origin identifier (e.g. "user", "screenscraper", "imdb").
  QString source;
  QString updatedAt;

  /// True when no extended fields have meaningful values. Used by the sidebar
  /// to skip rendering the Details section entirely on bare items.
  [[nodiscard]] bool isEmpty() const;
};

/// Loads metadata for the given (collectionUuid, path). Returns an empty
/// `ItemMetadata` (with the keys populated) when no row exists. Returns an
/// error only on actual database failures.
[[nodiscard]] ErrorUtils::Result<ItemMetadata> load(QSqlDatabase &db, const QString &collectionUuid,
                                                    const QString &path);

/// Inserts or updates metadata for (metadata.collectionUuid, metadata.path).
/// Always refreshes `updated_at` to the current UTC ISO timestamp.
[[nodiscard]] ErrorUtils::Result<bool> save(QSqlDatabase &db, const ItemMetadata &metadata);

/// Removes the metadata row for (collectionUuid, path). Succeeds even when
/// no matching row exists.
[[nodiscard]] ErrorUtils::Result<bool> remove(QSqlDatabase &db, const QString &collectionUuid,
                                              const QString &path);

/// Parses the `custom_fields` JSON string into an ordered key/value list.
/// Returns an empty list when the input is empty, malformed, or not a JSON
/// object. Non-string values are coerced via `QJsonValue::toString()`. Keys
/// that are empty after trimming are skipped.
[[nodiscard]] CustomFieldList parseCustomFields(const QString &json);

/// Serializes an ordered key/value list to a compact JSON object string.
/// Returns an empty string when the list contains no entries with a
/// non-empty trimmed key, so empty payloads round-trip to NULL in the DB.
[[nodiscard]] QString serializeCustomFields(const CustomFieldList &fields);

} // namespace ItemMetadataStore

#endif
