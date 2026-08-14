// Read/write access to the item_artwork table introduced in schema v6
// (sub-issue (a)). Mirrors the ItemMetadataStore pattern: keyed
// rows survive item id renumbering across rescans, manual overrides win when
// set, standard types fall back to subdirectory auto-discovery.
#include "itemartwork.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "artworkutils.h"
#include "errorutils.h"
#include "extensionutils.h"
#include "pathutils.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace ItemArtworkStore {

namespace {

constexpr const char *SELECT_ONE_SQL =
    "SELECT manual_path, updated_at FROM item_artwork "
    "WHERE collection_uuid = ? AND path = ? AND artwork_type = ?";

constexpr const char *SELECT_ALL_SQL =
    "SELECT artwork_type, manual_path, updated_at FROM item_artwork "
    "WHERE collection_uuid = ? AND path = ? ORDER BY id";

constexpr const char *UPSERT_SQL =
    "INSERT INTO item_artwork ("
    "collection_uuid, path, artwork_type, manual_path, updated_at"
    ") VALUES (?,?,?,?,?) "
    "ON CONFLICT(collection_uuid, path, artwork_type) DO UPDATE SET "
    "manual_path=excluded.manual_path, updated_at=excluded.updated_at";

constexpr const char *DELETE_ONE_SQL =
    "DELETE FROM item_artwork "
    "WHERE collection_uuid = ? AND path = ? AND artwork_type = ?";

/// Bulk manual-cover query (Kartend-1js9j). The artwork_type IN (...) list is
/// filled from ArtworkUtils::coverSubdirPriority() at call time so the set of
/// cover types stays defined in exactly one place; gallery-only rows (logo,
/// the scraper's non-standard types) never reach the fold. ORDER BY path lets
/// the fold stream one item at a time.
QString selectManualCoversSql(int coverTypeCount) {
  QString placeholders;
  placeholders.reserve(coverTypeCount * 2);
  for (int i = 0; i < coverTypeCount; ++i) {
    if (i > 0) {
      placeholders += QLatin1Char(',');
    }
    placeholders += QLatin1Char('?');
  }
  return QStringLiteral("SELECT path, artwork_type, manual_path FROM item_artwork "
                        "WHERE manual_path IS NOT NULL AND manual_path != '' "
                        "AND artwork_type IN (") +
         placeholders + QStringLiteral(") ORDER BY path");
}

QVariant nullableString(const QString &value) {
  return value.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(value);
}

QString expandTilde(const QString &path) {
  // Delegate to the canonical expansion so the rules can't drift: only "~/"
  // and a bare "~" expand — a directory literally named "~backup" (or a
  // "~user" form) stays literal.
  return PathUtils::expandPathWithoutExistenceCheck(path);
}

} // namespace

const QStringList &standardTypes() {
  // Sidebar gallery display order: primary cover first, supplemental last.
  // "front" is the cross-provider scraped-cover id (SS box-2D, MB front,
  // TMDB poster, Open Library cover all normalise to it); "box" is kept
  // for libraries that already store hand-curated cover art under a
  // `/box/` subfolder. Strings double as on-disk subdirectory names —
  // never reorder for cosmetics (the order is meaningful) and never
  // rename without a migration.
  static const QStringList types = {
      QString::fromLatin1(StandardTypes::Front),      QString::fromLatin1(StandardTypes::Box),
      QString::fromLatin1(StandardTypes::Screenshot), QString::fromLatin1(StandardTypes::Title),
      QString::fromLatin1(StandardTypes::Marquee),    QString::fromLatin1(StandardTypes::Fanart),
      QString::fromLatin1(StandardTypes::Logo),
  };
  return types;
}

bool isStandardType(const QString &artworkType) {
  return standardTypes().contains(artworkType);
}

QString standardTypeDisplayName(const QString &artworkType) {
  if (artworkType == QLatin1String(StandardTypes::Front)) {
    return QStringLiteral("Front Cover");
  }
  if (artworkType == QLatin1String(StandardTypes::Box)) {
    return QStringLiteral("Box");
  }
  if (artworkType == QLatin1String(StandardTypes::Screenshot)) {
    return QStringLiteral("Screenshot");
  }
  if (artworkType == QLatin1String(StandardTypes::Title)) {
    return QStringLiteral("Title Screen");
  }
  if (artworkType == QLatin1String(StandardTypes::Marquee)) {
    return QStringLiteral("Marquee");
  }
  if (artworkType == QLatin1String(StandardTypes::Fanart)) {
    return QStringLiteral("Fan Art");
  }
  if (artworkType == QLatin1String(StandardTypes::Logo)) {
    return QStringLiteral("Logo");
  }
  return {};
}

ErrorUtils::Result<ItemArtwork> load(QSqlDatabase &db, const QString &collectionUuid,
                                     const QString &path, const QString &artworkType) {
  ItemArtwork artwork;
  artwork.collectionUuid = collectionUuid;
  artwork.path = path;
  artwork.artworkType = artworkType;

  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "ItemArtworkStore::load");
  }

  QSqlQuery q(db);
  if (!q.prepare(SELECT_ONE_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare item_artwork select", "ItemArtworkStore::load")
        .withDetails(q.lastError().text());
  }
  q.addBindValue(collectionUuid);
  q.addBindValue(path);
  q.addBindValue(artworkType);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to query item_artwork",
                               "ItemArtworkStore::load")
        .withDetails(q.lastError().text());
  }
  if (!q.next()) {
    return artwork;
  }
  artwork.manualPath = q.value(0).toString();
  artwork.updatedAt = q.value(1).toString();
  return artwork;
}

ErrorUtils::Result<QList<ItemArtwork>>
loadAllForItem(QSqlDatabase &db, const QString &collectionUuid, const QString &path) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "ItemArtworkStore::loadAllForItem");
  }

  QSqlQuery q(db);
  if (!q.prepare(SELECT_ALL_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare item_artwork select-all",
                               "ItemArtworkStore::loadAllForItem")
        .withDetails(q.lastError().text());
  }
  q.addBindValue(collectionUuid);
  q.addBindValue(path);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to query item_artwork by item",
                               "ItemArtworkStore::loadAllForItem")
        .withDetails(q.lastError().text());
  }

  QList<ItemArtwork> rows;
  while (q.next()) {
    ItemArtwork artwork;
    artwork.collectionUuid = collectionUuid;
    artwork.path = path;
    artwork.artworkType = q.value(0).toString();
    artwork.manualPath = q.value(1).toString();
    artwork.updatedAt = q.value(2).toString();
    rows.append(artwork);
  }
  return rows;
}

ErrorUtils::Result<bool> save(QSqlDatabase &db, const ItemArtwork &artwork) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "ItemArtworkStore::save");
  }
  if (artwork.path.isEmpty()) {
    return ErrorContext::warning(ErrorCode::InvalidArgument,
                                 "Cannot save item artwork without a path",
                                 "ItemArtworkStore::save");
  }
  if (artwork.artworkType.isEmpty()) {
    return ErrorContext::warning(ErrorCode::InvalidArgument,
                                 "Cannot save item artwork without an artwork_type",
                                 "ItemArtworkStore::save");
  }

  QSqlQuery q(db);
  if (!q.prepare(UPSERT_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare item_artwork upsert", "ItemArtworkStore::save")
        .withDetails(q.lastError().text());
  }

  const QString updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  q.addBindValue(artwork.collectionUuid);
  q.addBindValue(artwork.path);
  q.addBindValue(artwork.artworkType);
  q.addBindValue(nullableString(artwork.manualPath));
  q.addBindValue(updatedAt);

  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to upsert item_artwork",
                               "ItemArtworkStore::save")
        .withDetails(q.lastError().text());
  }
  return true;
}

ErrorUtils::Result<bool> remove(QSqlDatabase &db, const QString &collectionUuid,
                                const QString &path, const QString &artworkType) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "ItemArtworkStore::remove");
  }
  QSqlQuery q(db);
  if (!q.prepare(DELETE_ONE_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare item_artwork delete", "ItemArtworkStore::remove")
        .withDetails(q.lastError().text());
  }
  q.addBindValue(collectionUuid);
  q.addBindValue(path);
  q.addBindValue(artworkType);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to delete item_artwork",
                               "ItemArtworkStore::remove")
        .withDetails(q.lastError().text());
  }
  return true;
}

QString findStandardArtwork(const QString &baseName, const QString &artworkDirectory,
                            const QString &artworkType) {
  if (baseName.isEmpty() || artworkDirectory.isEmpty() || !isStandardType(artworkType)) {
    return {};
  }
  const QString artworkRoot = expandTilde(artworkDirectory);
  if (artworkRoot.isEmpty()) {
    // Whitespace-only input trims to empty; QDir("") would mean the current
    // directory, which must not be silently probed for artwork.
    return {};
  }
  QDir typeDir(QDir(artworkRoot).absoluteFilePath(artworkType));
  if (!typeDir.exists()) {
    return {};
  }
  for (const QString &ext : ExtensionUtils::imageBaseExtensions()) {
    const QString lower = typeDir.absoluteFilePath(baseName + "." + ext);
    if (QFile::exists(lower)) {
      return lower;
    }
    const QString upper = typeDir.absoluteFilePath(baseName + "." + ext.toUpper());
    if (QFile::exists(upper)) {
      return upper;
    }
  }
  return {};
}

QString resolveArtworkPath(const QString &overridePath, const QString &baseName,
                           const QString &artworkDirectory, const QString &artworkType) {
  const QString trimmed = overridePath.trimmed();
  if (!trimmed.isEmpty()) {
    const QString expanded = expandTilde(trimmed);
    if (QFile::exists(expanded)) {
      return expanded;
    }
    return {};
  }
  return findStandardArtwork(baseName, artworkDirectory, artworkType);
}

QString resolveCoverPath(const QHash<QString, QString> &manualByType,
                         const QString &autoDiscovered) {
  // The empty map is the overwhelmingly common case (no hand-linked artwork on
  // this item at all), and it must cost nothing beyond the branch — this runs
  // once per staged row on every scan.
  if (!manualByType.isEmpty()) {
    // ArtworkUtils owns the definition of "a type that can supply a cover", so
    // adding a cover subdir there automatically makes a manual link on it
    // count. Types outside the list (logo, custom types) are gallery-only.
    for (const QString &type : ArtworkUtils::coverSubdirPriority()) {
      const QString link = manualByType.value(type).trimmed();
      if (link.isEmpty()) {
        continue;
      }
      const QString expanded = expandTilde(link);
      // Same stat resolveArtworkPath applies. A link whose target was deleted
      // is skipped, not returned: the fallback below then reports whatever
      // auto-discovery still finds, which is what the item actually renders.
      if (QFile::exists(expanded)) {
        return expanded;
      }
    }
  }
  return autoDiscovered;
}

ErrorUtils::Result<QHash<QString, QString>> loadManualCoverPaths(QSqlDatabase &db) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "ItemArtworkStore::loadManualCoverPaths");
  }

  const QStringList &coverTypes = ArtworkUtils::coverSubdirPriority();
  QSqlQuery q(db);
  if (!q.prepare(selectManualCoversSql(static_cast<int>(coverTypes.size())))) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare item_artwork manual-cover select",
                               "ItemArtworkStore::loadManualCoverPaths")
        .withDetails(q.lastError().text());
  }
  for (const QString &type : coverTypes) {
    q.addBindValue(type);
  }
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to query item_artwork manual covers",
                               "ItemArtworkStore::loadManualCoverPaths")
        .withDetails(q.lastError().text());
  }

  QHash<QString, QString> covers;
  QString currentPath;
  QHash<QString, QString> manualByType;
  // One item's links are contiguous thanks to ORDER BY path, so the fold only
  // ever holds the group it is inside.
  const auto flush = [&]() {
    if (currentPath.isEmpty()) {
      return;
    }
    const QString cover = resolveCoverPath(manualByType, QString());
    if (!cover.isEmpty()) {
      covers.insert(currentPath, cover);
    }
    manualByType.clear();
  };
  while (q.next()) {
    const QString path = q.value(0).toString();
    if (path != currentPath) {
      flush();
      currentPath = path;
    }
    manualByType.insert(q.value(1).toString(), q.value(2).toString());
  }
  flush();
  return covers;
}

} // namespace ItemArtworkStore
