// EntityMetadataStore — see entitymetadata.h. SQL conventions mirror
// itemmetadata.cpp: prepared statements, NULL for absent values so empty
// payloads round-trip to NULL, upsert on the table's UNIQUE key.
#include "entitymetadata.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace EntityMetadataStore {

namespace {

QVariant nullableString(const QString &value) {
  return value.isEmpty() ? QVariant(QMetaType::fromType<QString>()) : QVariant(value);
}

constexpr auto UPSERT_SQL = "INSERT INTO entity_metadata ("
                            " entity_type, entity_identity, collection_uuid,"
                            " title, description, art_path, custom_fields, source, updated_at"
                            ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
                            "ON CONFLICT(entity_type, entity_identity, collection_uuid) "
                            "DO UPDATE SET"
                            " title = excluded.title,"
                            " description = excluded.description,"
                            " art_path = excluded.art_path,"
                            " custom_fields = excluded.custom_fields,"
                            " source = excluded.source,"
                            " updated_at = excluded.updated_at";

constexpr auto SELECT_COLUMNS = " entity_type, entity_identity, collection_uuid,"
                                " title, description, art_path, custom_fields, source, updated_at ";

EntityMetadata fromQuery(QSqlQuery &q) {
  EntityMetadata m;
  m.entityType = q.value(0).toString();
  m.entityIdentity = q.value(1).toString();
  m.collectionUuid = q.value(2).toString();
  m.title = q.value(3).toString();
  m.description = q.value(4).toString();
  m.artPath = q.value(5).toString();
  m.customFields = q.value(6).toString();
  m.source = q.value(7).toString();
  m.updatedAt = q.value(8).toString();
  return m;
}

} // namespace

bool EntityMetadata::isEmpty() const {
  return title.isEmpty() && description.isEmpty() && artPath.isEmpty() && customFields.isEmpty();
}

ErrorUtils::Result<EntityMetadata> load(QSqlDatabase &db, const QString &entityType,
                                        const QString &entityIdentity,
                                        const QString &collectionUuid) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "EntityMetadataStore::load");
  }
  QSqlQuery q(db);
  if (!q.prepare(QStringLiteral("SELECT") + QLatin1String(SELECT_COLUMNS) +
                 QStringLiteral("FROM entity_metadata WHERE entity_type = ? AND "
                                "entity_identity = ? AND collection_uuid = ?"))) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare entity_metadata select",
                               "EntityMetadataStore::load")
        .withDetails(q.lastError().text());
  }
  q.addBindValue(entityType);
  q.addBindValue(entityIdentity);
  q.addBindValue(collectionUuid);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "entity_metadata select failed",
                               "EntityMetadataStore::load")
        .withDetails(q.lastError().text());
  }
  if (!q.next()) {
    EntityMetadata empty;
    empty.entityType = entityType;
    empty.entityIdentity = entityIdentity;
    empty.collectionUuid = collectionUuid;
    return empty;
  }
  return fromQuery(q);
}

ErrorUtils::Result<EntityMetadata> loadForCollection(QSqlDatabase &db,
                                                     const QString &collectionUuid) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "EntityMetadataStore::loadForCollection");
  }
  QSqlQuery q(db);
  // Collection-typed rows outrank platform rows of any age (a franchise /
  // collection description beats console boilerplate for the card that
  // describes the collection); within a type the newest scrape wins.
  if (!q.prepare(QStringLiteral("SELECT") + QLatin1String(SELECT_COLUMNS) +
                 QStringLiteral("FROM entity_metadata WHERE collection_uuid = ? "
                                "ORDER BY (entity_type = 'collection') DESC, updated_at DESC "
                                "LIMIT 1"))) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare entity_metadata collection select",
                               "EntityMetadataStore::loadForCollection")
        .withDetails(q.lastError().text());
  }
  q.addBindValue(collectionUuid);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "entity_metadata collection select failed",
                               "EntityMetadataStore::loadForCollection")
        .withDetails(q.lastError().text());
  }
  if (!q.next()) {
    EntityMetadata empty;
    empty.collectionUuid = collectionUuid;
    return empty;
  }
  return fromQuery(q);
}

ErrorUtils::Result<bool> save(QSqlDatabase &db, const EntityMetadata &metadata) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "EntityMetadataStore::save");
  }
  if (metadata.entityType.isEmpty() || metadata.entityIdentity.isEmpty()) {
    return ErrorContext::warning(ErrorCode::InvalidArgument,
                                 "Cannot save entity metadata without a type and identity",
                                 "EntityMetadataStore::save");
  }

  QSqlQuery q(db);
  if (!q.prepare(QLatin1String(UPSERT_SQL))) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare entity_metadata upsert",
                               "EntityMetadataStore::save")
        .withDetails(q.lastError().text());
  }

  q.addBindValue(metadata.entityType);
  q.addBindValue(metadata.entityIdentity);
  // The UNIQUE key column is NOT NULL DEFAULT '' — bind the empty string
  // rather than NULL so upserts key consistently for rows with no owning
  // collection (a shared platform row, should one ever be written that way).
  q.addBindValue(metadata.collectionUuid);
  q.addBindValue(nullableString(metadata.title));
  q.addBindValue(nullableString(metadata.description));
  q.addBindValue(nullableString(metadata.artPath));
  q.addBindValue(nullableString(metadata.customFields));
  q.addBindValue(nullableString(metadata.source));
  q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "entity_metadata upsert failed",
                               "EntityMetadataStore::save")
        .withDetails(q.lastError().text());
  }
  return true;
}

ErrorUtils::Result<bool> remove(QSqlDatabase &db, const QString &entityType,
                                const QString &entityIdentity, const QString &collectionUuid) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "EntityMetadataStore::remove");
  }
  QSqlQuery q(db);
  if (!q.prepare(QStringLiteral("DELETE FROM entity_metadata WHERE entity_type = ? AND "
                                "entity_identity = ? AND collection_uuid = ?"))) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare entity_metadata delete",
                               "EntityMetadataStore::remove")
        .withDetails(q.lastError().text());
  }
  q.addBindValue(entityType);
  q.addBindValue(entityIdentity);
  q.addBindValue(collectionUuid);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "entity_metadata delete failed",
                               "EntityMetadataStore::remove")
        .withDetails(q.lastError().text());
  }
  return true;
}

} // namespace EntityMetadataStore
