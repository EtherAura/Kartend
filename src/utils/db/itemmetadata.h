#ifndef ITEMMETADATA_H
#define ITEMMETADATA_H

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include "errorutils.h"

class QSqlDatabase;

namespace ItemMetadataStore {

/// List of (key, value) pairs for user-defined custom fields. Returned by
/// `parseCustomFields` in alphabetical key order (QJsonObject sorts keys
/// alphabetically), which gives a stable, predictable rendering in the
/// sidebar regardless of the order rows were edited.
using CustomFieldList = QList<QPair<QString, QString>>;

} // namespace ItemMetadataStore

/// Editable subset of `ItemMetadata` ferried across the
/// EditMetadataDialog ↔ InteractionManager boundary. Lives here (next to
/// ItemMetadata + parseCustomFields) so both the ui and input layers can
/// see the full type without crossing layer headers.
struct EditMetadataPayload {
  QString notes;
  /// Display-order tag list (the dialog parses the comma-separated input
  /// into trimmed, dedup'd entries before this struct is returned).
  QStringList tags;
  /// -1 = unrated; 0-10 = half-star precision.
  int rating = -1;
  QString sourceUrl;
  ItemMetadataStore::CustomFieldList customFields;
};

namespace ItemMetadataStore {

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
  /// JSON object string for user-defined key/value pairs.
  QString customFields;
  /// Free-form user-authored notes (multi-line). Empty means "unset".
  QString notes;
  /// Optional source URL (where the user discovered / acquired the item).
  QString sourceUrl;
  /// Personal rating 0..10 (rendered as five half-stars in the UI).
  /// Negative means "unset" (the column is NULLable).
  int rating = -1;
  /// User state flags (item_metadata v15). All default to false on rows
  /// without an explicit toggle. Favorites are not stored here — they
  /// continue to live as a reserved playlist (ensureFavoritesPlaylist) so
  /// existing membership-based browse code keeps working.
  bool isPinned = false;
  bool isHidden = false;
  bool continueLater = false;
  /// Optional override for the per-item manual file.
  QString manualPath;
  /// Optional per-item launcher override. When >= 0, indexes
  /// into the owning collection's unified launcher list (0 = primary, 1..N =
  /// launcher.additionalLaunchers[0..N-1]) and bypasses the multi-launcher chooser at
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

/// Batched counterpart to load(): runs `WHERE collection_uuid = ? AND path
/// IN (...)` queries (chunked under SQLite's SQLITE_LIMIT_VARIABLE_NUMBER)
/// and returns a (path -> ItemMetadata) hash covering exactly @p paths.
/// Paths without a row are returned as empty-but-keyed ItemMetadata, same
/// as load(). Returns an error only on actual database failures.
[[nodiscard]] ErrorUtils::Result<QHash<QString, ItemMetadata>>
loadBatch(QSqlDatabase &db, const QString &collectionUuid, const QStringList &paths);

/// Inserts or updates metadata for (metadata.collectionUuid, metadata.path).
/// Always refreshes `updated_at` to the current UTC ISO timestamp.
[[nodiscard]] ErrorUtils::Result<bool> save(QSqlDatabase &db, const ItemMetadata &metadata);

/// Removes the metadata row for (collectionUuid, path). Succeeds even when
/// no matching row exists.
[[nodiscard]] ErrorUtils::Result<bool> remove(QSqlDatabase &db, const QString &collectionUuid,
                                              const QString &path);

/// Parses the `tags` JSON string into a deduplicated, trimmed string list.
/// Returns an empty list when the input is empty, malformed, or not a JSON
/// array. Non-string values are coerced via `QJsonValue::toString()`. Order is
/// preserved (so the editor can round-trip the user's chosen ordering).
[[nodiscard]] QStringList parseTags(const QString &json);

/// Serializes a tag list to a compact JSON array string. Trims and drops
/// empty entries and case-insensitive duplicates so toggling the same tag
/// twice in the editor doesn't produce duplicate JSON entries. Returns an
/// empty string when the result has no entries, so empty payloads round-trip
/// to NULL in the DB.
[[nodiscard]] QString serializeTags(const QStringList &tags);

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
