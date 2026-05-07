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
  /// Optional per-item launcher override (Kartend-dnx4). When >= 0, indexes
  /// into the owning collection's unified launcher list (0 = primary, 1..N =
  /// additionalLaunchers[0..N-1]) and bypasses the multi-launcher chooser at
  /// launch. Negative means "no override" — fall through to the chooser /
  /// collection default.
  int launcherIndex = -1;
  /// Origin identifier (e.g. "user", "screenscraper", "imdb").
  QString source;
  QString updatedAt;

  /// True when no extended fields have meaningful values. Used by the sidebar
  /// to skip rendering the Details section entirely on bare items.
  [[nodiscard]] bool isEmpty() const;

  bool operator==(const ItemMetadata &other) const = default;
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

/// Canonical list of lowercase manual-file extensions (no dot, no wildcard).
/// Used both to auto-discover per-item manuals in `manualDirectory` and to
/// build file-dialog filters when the user picks an override path.
[[nodiscard]] const QStringList &manualExtensions();

/// Looks up a manual file in `manualDirectory` whose stem matches `baseName`.
/// Tries each extension from `manualExtensions()` in lower- and uppercase.
/// Returns an empty string when the directory is missing/empty or no match
/// is found. Does NOT recurse into subdirectories.
[[nodiscard]] QString findManualForBaseName(const QString &baseName,
                                            const QString &manualDirectory);

/// Resolves the manual file for an item: prefers a non-empty `overridePath`
/// (the per-item override stored in `item_metadata.manual_path`), falling
/// back to auto-discovery in `manualDirectory`. Tilde expansion is applied
/// to the override so paths saved as `~/manuals/foo.pdf` resolve at runtime.
/// Returns an empty string when nothing exists on disk.
[[nodiscard]] QString resolveManualFile(const QString &overridePath, const QString &baseName,
                                        const QString &manualDirectory);

} // namespace ItemMetadataStore

#endif
