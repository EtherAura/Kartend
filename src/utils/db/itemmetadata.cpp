// Read/write access to the item_metadata table.
//
// The table stores extended per-item metadata plus columns
// reserved for sibling features (manual_path for, custom_fields
// for). All structured fields are optional and skipped from
// the sidebar when empty.
#include "itemmetadata.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "errorutils.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace ItemMetadataStore {

bool ItemMetadata::isEmpty() const {
  return title.isEmpty() && description.isEmpty() && genre.isEmpty() && developer.isEmpty() &&
         publisher.isEmpty() && releaseDate.isEmpty() && contentRating.isEmpty() &&
         players.isEmpty() && runtimeSeconds < 0 && tags.isEmpty() && customFields.isEmpty() &&
         notes.isEmpty() && sourceUrl.isEmpty() && rating < 0 && manualPath.isEmpty() &&
         launcherIndex < 0;
}

namespace {

constexpr const char *SELECT_SQL =
    "SELECT title, description, genre, developer, publisher, release_date, "
    "content_rating, players, runtime_seconds, tags, custom_fields, "
    "manual_path, launcher_index, source, updated_at, notes, rating, source_url "
    "FROM item_metadata WHERE collection_uuid = ? AND path = ?";

constexpr const char *UPSERT_SQL =
    "INSERT INTO item_metadata ("
    "collection_uuid, path, title, description, genre, developer, publisher, "
    "release_date, content_rating, players, runtime_seconds, tags, "
    "custom_fields, manual_path, launcher_index, source, updated_at, "
    "notes, rating, source_url"
    ") VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
    "ON CONFLICT(collection_uuid, path) DO UPDATE SET "
    "title=excluded.title, description=excluded.description, "
    "genre=excluded.genre, developer=excluded.developer, "
    "publisher=excluded.publisher, release_date=excluded.release_date, "
    "content_rating=excluded.content_rating, players=excluded.players, "
    "runtime_seconds=excluded.runtime_seconds, tags=excluded.tags, "
    "custom_fields=excluded.custom_fields, manual_path=excluded.manual_path, "
    "launcher_index=excluded.launcher_index, "
    "source=excluded.source, updated_at=excluded.updated_at, "
    "notes=excluded.notes, rating=excluded.rating, "
    "source_url=excluded.source_url";

constexpr const char *DELETE_SQL =
    "DELETE FROM item_metadata WHERE collection_uuid = ? AND path = ?";

QVariant nullableString(const QString &value) {
  return value.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(value);
}

QVariant nullableRuntime(int seconds) {
  return seconds < 0 ? QVariant(QMetaType(QMetaType::Int)) : QVariant(seconds);
}

// Same NULL-vs-int handling as nullableRuntime, but for the per-item launcher
// override: -1 means "no override" and serializes to NULL so
// the column round-trips cleanly via SELECT's isNull() check below.
QVariant nullableLauncherIndex(int index) {
  return index < 0 ? QVariant(QMetaType(QMetaType::Int)) : QVariant(index);
}

// Rating uses the same -1 == NULL contract as launcher_index. Stored 0-10 so
// the UI's half-star widget round-trips without a floating-point hop.
QVariant nullableRating(int rating) {
  return rating < 0 ? QVariant(QMetaType(QMetaType::Int)) : QVariant(rating);
}

} // namespace

ErrorUtils::Result<ItemMetadata> load(QSqlDatabase &db, const QString &collectionUuid,
                                      const QString &path) {
  ItemMetadata metadata;
  metadata.collectionUuid = collectionUuid;
  metadata.path = path;

  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "ItemMetadataStore::load");
  }

  QSqlQuery q(db);
  if (!q.prepare(SELECT_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare item_metadata select", "ItemMetadataStore::load")
        .withDetails(q.lastError().text());
  }
  q.addBindValue(collectionUuid);
  q.addBindValue(path);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to query item_metadata",
                               "ItemMetadataStore::load")
        .withDetails(q.lastError().text());
  }
  if (!q.next()) {
    return metadata; // No row -> empty metadata with keys preserved.
  }

  metadata.title = q.value(0).toString();
  metadata.description = q.value(1).toString();
  metadata.genre = q.value(2).toString();
  metadata.developer = q.value(3).toString();
  metadata.publisher = q.value(4).toString();
  metadata.releaseDate = q.value(5).toString();
  metadata.contentRating = q.value(6).toString();
  metadata.players = q.value(7).toString();
  const QVariant runtime = q.value(8);
  metadata.runtimeSeconds = runtime.isNull() ? -1 : runtime.toInt();
  metadata.tags = q.value(9).toString();
  metadata.customFields = q.value(10).toString();
  metadata.manualPath = q.value(11).toString();
  const QVariant launcherIdx = q.value(12);
  metadata.launcherIndex = launcherIdx.isNull() ? -1 : launcherIdx.toInt();
  metadata.source = q.value(13).toString();
  metadata.updatedAt = q.value(14).toString();
  metadata.notes = q.value(15).toString();
  const QVariant ratingVar = q.value(16);
  metadata.rating = ratingVar.isNull() ? -1 : ratingVar.toInt();
  metadata.sourceUrl = q.value(17).toString();
  return metadata;
}

ErrorUtils::Result<QHash<QString, ItemMetadata>>
loadBatch(QSqlDatabase &db, const QString &collectionUuid, const QStringList &paths) {
  QHash<QString, ItemMetadata> out;

  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "ItemMetadataStore::loadBatch");
  }
  if (paths.isEmpty()) return out;

  out.reserve(paths.size());

  // SQLite's compile-time SQLITE_LIMIT_VARIABLE_NUMBER caps the number of
  // bound parameters per statement. Historic default is 999; modern builds
  // raise it to 32766. Stay safely under the conservative ceiling and
  // reserve one slot for the collection_uuid binding.
  constexpr qsizetype kChunkSize = 900;

  for (qsizetype chunkStart = 0; chunkStart < paths.size(); chunkStart += kChunkSize) {
    const qsizetype chunkEnd = qMin(chunkStart + kChunkSize, paths.size());
    const qsizetype chunkLen = chunkEnd - chunkStart;

    QString sql = QStringLiteral(
        "SELECT path, title, description, genre, developer, publisher, release_date, "
        "content_rating, players, runtime_seconds, tags, custom_fields, "
        "manual_path, launcher_index, source, updated_at, notes, rating, source_url "
        "FROM item_metadata WHERE collection_uuid = ? AND path IN (");
    for (qsizetype i = 0; i < chunkLen; ++i) {
      if (i > 0) sql.append(QLatin1Char(','));
      sql.append(QLatin1Char('?'));
    }
    sql.append(QLatin1Char(')'));

    QSqlQuery q(db);
    if (!q.prepare(sql)) {
      return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                 "Failed to prepare item_metadata batch select",
                                 "ItemMetadataStore::loadBatch")
          .withDetails(q.lastError().text());
    }
    q.addBindValue(collectionUuid);
    for (qsizetype i = chunkStart; i < chunkEnd; ++i) {
      q.addBindValue(paths.at(i));
    }
    if (!q.exec()) {
      return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                 "Failed to query item_metadata batch",
                                 "ItemMetadataStore::loadBatch")
          .withDetails(q.lastError().text());
    }

    while (q.next()) {
      ItemMetadata md;
      md.collectionUuid = collectionUuid;
      md.path = q.value(0).toString();
      md.title = q.value(1).toString();
      md.description = q.value(2).toString();
      md.genre = q.value(3).toString();
      md.developer = q.value(4).toString();
      md.publisher = q.value(5).toString();
      md.releaseDate = q.value(6).toString();
      md.contentRating = q.value(7).toString();
      md.players = q.value(8).toString();
      const QVariant runtime = q.value(9);
      md.runtimeSeconds = runtime.isNull() ? -1 : runtime.toInt();
      md.tags = q.value(10).toString();
      md.customFields = q.value(11).toString();
      md.manualPath = q.value(12).toString();
      const QVariant launcherIdx = q.value(13);
      md.launcherIndex = launcherIdx.isNull() ? -1 : launcherIdx.toInt();
      md.source = q.value(14).toString();
      md.updatedAt = q.value(15).toString();
      md.notes = q.value(16).toString();
      const QVariant ratingVar = q.value(17);
      md.rating = ratingVar.isNull() ? -1 : ratingVar.toInt();
      md.sourceUrl = q.value(18).toString();
      out.insert(md.path, md);
    }
  }

  // Mirror single-load: paths without a row return an empty-but-keyed stub
  // so the caller can iterate paths uniformly without checking contains().
  for (const QString &p : paths) {
    if (!out.contains(p)) {
      ItemMetadata empty;
      empty.collectionUuid = collectionUuid;
      empty.path = p;
      out.insert(p, empty);
    }
  }

  return out;
}

ErrorUtils::Result<bool> save(QSqlDatabase &db, const ItemMetadata &metadata) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "ItemMetadataStore::save");
  }
  if (metadata.path.isEmpty()) {
    return ErrorContext::warning(ErrorCode::InvalidArgument,
                                 "Cannot save item metadata without a path",
                                 "ItemMetadataStore::save");
  }

  QSqlQuery q(db);
  if (!q.prepare(UPSERT_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare item_metadata upsert", "ItemMetadataStore::save")
        .withDetails(q.lastError().text());
  }

  const QString updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

  q.addBindValue(metadata.collectionUuid);
  q.addBindValue(metadata.path);
  q.addBindValue(nullableString(metadata.title));
  q.addBindValue(nullableString(metadata.description));
  q.addBindValue(nullableString(metadata.genre));
  q.addBindValue(nullableString(metadata.developer));
  q.addBindValue(nullableString(metadata.publisher));
  q.addBindValue(nullableString(metadata.releaseDate));
  q.addBindValue(nullableString(metadata.contentRating));
  q.addBindValue(nullableString(metadata.players));
  q.addBindValue(nullableRuntime(metadata.runtimeSeconds));
  q.addBindValue(nullableString(metadata.tags));
  q.addBindValue(nullableString(metadata.customFields));
  q.addBindValue(nullableString(metadata.manualPath));
  q.addBindValue(nullableLauncherIndex(metadata.launcherIndex));
  q.addBindValue(nullableString(metadata.source));
  q.addBindValue(updatedAt);
  q.addBindValue(nullableString(metadata.notes));
  q.addBindValue(nullableRating(metadata.rating));
  q.addBindValue(nullableString(metadata.sourceUrl));

  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to upsert item_metadata",
                               "ItemMetadataStore::save")
        .withDetails(q.lastError().text());
  }
  return true;
}

ErrorUtils::Result<bool> remove(QSqlDatabase &db, const QString &collectionUuid,
                                const QString &path) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "ItemMetadataStore::remove");
  }
  QSqlQuery q(db);
  if (!q.prepare(DELETE_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare item_metadata delete",
                               "ItemMetadataStore::remove")
        .withDetails(q.lastError().text());
  }
  q.addBindValue(collectionUuid);
  q.addBindValue(path);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to delete item_metadata",
                               "ItemMetadataStore::remove")
        .withDetails(q.lastError().text());
  }
  return true;
}

CustomFieldList parseCustomFields(const QString &json) {
  CustomFieldList result;
  const QString trimmed = json.trimmed();
  if (trimmed.isEmpty()) {
    return result;
  }
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return result;
  }
  const QJsonObject obj = doc.object();
  // QJsonObject iterates in alphabetical key order, which is exactly the
  // ordering we want to surface in the sidebar Details section.
  for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
    const QString key = it.key().trimmed();
    if (key.isEmpty()) {
      continue;
    }
    result.append({key, it.value().toString()});
  }
  return result;
}

const QStringList &manualExtensions() {
  // Common manual/document formats. Kept narrow on purpose: anything outside
  // this set is uncommon enough that the user can attach it explicitly via
  // the per-item override (Set manual file...).
  static const QStringList exts = {
      QStringLiteral("pdf"),  QStringLiteral("epub"), QStringLiteral("cbr"), QStringLiteral("cbz"),
      QStringLiteral("djvu"), QStringLiteral("txt"),  QStringLiteral("md"),  QStringLiteral("html"),
      QStringLiteral("htm"),  QStringLiteral("rtf"),  QStringLiteral("doc"), QStringLiteral("docx"),
      QStringLiteral("odt"),  QStringLiteral("png"),  QStringLiteral("jpg"), QStringLiteral("jpeg"),
  };
  return exts;
}

QString findManualForBaseName(const QString &baseName, const QString &manualDirectory) {
  if (baseName.isEmpty() || manualDirectory.isEmpty()) {
    return {};
  }
  QString expanded = manualDirectory;
  if (expanded.startsWith('~')) {
    expanded = QDir::homePath() + expanded.mid(1);
  }
  QDir dir(expanded);
  if (!dir.exists()) {
    return {};
  }
  for (const QString &ext : manualExtensions()) {
    const QString lower = dir.absoluteFilePath(baseName + "." + ext);
    if (QFile::exists(lower)) {
      return lower;
    }
    const QString upper = dir.absoluteFilePath(baseName + "." + ext.toUpper());
    if (QFile::exists(upper)) {
      return upper;
    }
  }
  return {};
}

QString resolveManualFile(const QString &overridePath, const QString &baseName,
                          const QString &manualDirectory) {
  QString trimmed = overridePath.trimmed();
  if (!trimmed.isEmpty()) {
    if (trimmed.startsWith('~')) {
      trimmed = QDir::homePath() + trimmed.mid(1);
    }
    if (QFile::exists(trimmed)) {
      return trimmed;
    }
    // Override is set but missing on disk — don't silently fall back to
    // auto-discovery, since that would mask a stale/typo'd override. The
    // caller treats an empty return as "no manual available".
    return {};
  }
  return findManualForBaseName(baseName, manualDirectory);
}

QStringList parseTags(const QString &json) {
  QStringList result;
  const QString trimmed = json.trimmed();
  if (trimmed.isEmpty()) {
    return result;
  }
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isArray()) {
    return result;
  }
  const QJsonArray arr = doc.array();
  // Case-insensitive de-duplication preserves the first-seen casing so the
  // user's original capitalization survives a save/reload round-trip.
  QSet<QString> seen;
  for (const auto &val : arr) {
    const QString tag = val.toString().trimmed();
    if (tag.isEmpty()) {
      continue;
    }
    const QString lower = tag.toLower();
    if (seen.contains(lower)) {
      continue;
    }
    seen.insert(lower);
    result.append(tag);
  }
  return result;
}

QString serializeTags(const QStringList &tags) {
  QJsonArray arr;
  QSet<QString> seen;
  for (const QString &tag : tags) {
    const QString trimmed = tag.trimmed();
    if (trimmed.isEmpty()) {
      continue;
    }
    const QString lower = trimmed.toLower();
    if (seen.contains(lower)) {
      continue;
    }
    seen.insert(lower);
    arr.append(trimmed);
  }
  if (arr.isEmpty()) {
    return {};
  }
  return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString serializeCustomFields(const CustomFieldList &fields) {
  QJsonObject obj;
  for (const auto &pair : fields) {
    const QString key = pair.first.trimmed();
    if (key.isEmpty()) {
      continue;
    }
    obj.insert(key, pair.second);
  }
  if (obj.isEmpty()) {
    return {};
  }
  return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

} // namespace ItemMetadataStore
