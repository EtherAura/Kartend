#ifndef ENTITYMETADATA_H
#define ENTITYMETADATA_H

#include <QString>

#include "errorutils.h"

class QSqlDatabase;

/// Store for the `entity_metadata` table (schema v27, Kartend-ckepd.3):
/// textual metadata scraped for NON-GAME entities — a platform/console, a
/// whole collection, or a category — keyed by
/// (entity_type, entity_identity, collection_uuid).
///
/// The table sat scaffolded with no producer or reader until Kartend-445su:
/// the entity-scrape coordinator now writes a row per successful entity
/// fetch, and the details pane's collection summary reads the newest row
/// for the collection it is describing. Same shape as ItemMetadataStore —
/// free functions over the caller's QSqlDatabase, no mocking (see
/// docs/dev/testing.md).
namespace EntityMetadataStore {

/// Well-known custom_fields keys, shared between the providers that fill
/// them and the summary card that renders them. JSON-object keys inside
/// `customFields`, same serialization helpers as ItemMetadataStore's.
inline constexpr char kFieldManufacturer[] = "manufacturer";
inline constexpr char kFieldReleaseDate[] = "releaseDate";
// Kartend-6i10t: the wider fact set the Wikidata data hop resolves for
// collections. All optional; providers set what the entity actually has.
inline constexpr char kFieldCountry[] = "country";
inline constexpr char kFieldWebsite[] = "website";
inline constexpr char kFieldDeveloper[] = "developer";
inline constexpr char kFieldPublisher[] = "publisher";
inline constexpr char kFieldGenre[] = "genre";
// Kartend-5b5r1: the game-platform spec sheet (Wikidata property ids
// verified live 2026-08-23: P880/P2560/P2664/P361/P155/P156). Memory and
// display resolution are NOT structured claims on console entities — the
// registry leaves room if another source appears.
inline constexpr char kFieldCpu[] = "cpu";
inline constexpr char kFieldGpu[] = "gpu";
inline constexpr char kFieldUnitsSold[] = "unitsSold";
inline constexpr char kFieldGeneration[] = "generation";
inline constexpr char kFieldPredecessor[] = "predecessor";
inline constexpr char kFieldSuccessor[] = "successor";

/// Canonical entity_type column values. Lowercase on purpose — the column
/// is a wire format (rows survive app upgrades), so the scraper maps its
/// ScrapeEntityType enum onto these strings rather than persisting enum
/// integers that could be reordered.
inline constexpr char kTypePlatform[] = "platform";
inline constexpr char kTypeCollection[] = "collection";
inline constexpr char kTypeCategory[] = "category";

struct EntityMetadata {
  QString entityType;     ///< kTypePlatform / kTypeCollection / kTypeCategory.
  QString entityIdentity; ///< Provider identity: systemeid, collection uuid, …
  QString collectionUuid; ///< Owning collection (routing + display key).
  QString title;
  QString description;
  /// Absolute path of the entity's representative art on disk (the logo the
  /// scrape wired into the collection config), when one was written.
  QString artPath;
  /// JSON object string for provider fields the typed columns don't cover —
  /// the kField* keys above plus anything provider-specific. Round-trips
  /// through ItemMetadataStore::parseCustomFields / serializeCustomFields.
  QString customFields;
  QString source; ///< Provider id ("screenscraper", "wikipedia", …).
  QString updatedAt;

  /// True when no scraped value is present (keys excluded) — the summary
  /// card skips its scraped rows entirely for such rows.
  [[nodiscard]] bool isEmpty() const;

  bool operator==(const EntityMetadata &other) const = default;
};

/// Loads the row for the exact (entityType, entityIdentity, collectionUuid)
/// key. Returns an empty-but-keyed EntityMetadata when no row exists;
/// errors only on actual database failures.
[[nodiscard]] ErrorUtils::Result<EntityMetadata> load(QSqlDatabase &db, const QString &entityType,
                                                      const QString &entityIdentity,
                                                      const QString &collectionUuid);

/// The newest row for a collection regardless of entity type — what the
/// details pane's collection summary asks for ("tell me about this
/// collection, whichever scrape produced it"). Collection-typed rows win
/// over platform-typed rows of the same age so a franchise description
/// outranks console boilerplate. Returns an empty EntityMetadata (with
/// collectionUuid populated) when the collection has no rows.
[[nodiscard]] ErrorUtils::Result<EntityMetadata> loadForCollection(QSqlDatabase &db,
                                                                   const QString &collectionUuid);

/// Inserts or updates the row for the metadata's key triple. Always
/// refreshes `updated_at` to the current UTC ISO timestamp.
[[nodiscard]] ErrorUtils::Result<bool> save(QSqlDatabase &db, const EntityMetadata &metadata);

/// Removes the row for the exact key triple. Succeeds when no row exists.
[[nodiscard]] ErrorUtils::Result<bool> remove(QSqlDatabase &db, const QString &entityType,
                                              const QString &entityIdentity,
                                              const QString &collectionUuid);

} // namespace EntityMetadataStore

#endif // ENTITYMETADATA_H
