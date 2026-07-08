// DatabaseManager: path resolution, collection-level maintenance (clear /
// migrate / purge orphans / cached-count refresh / last-scanned), and
// per-collection item-path / item-state enumeration. Split from
// databasemanager.cpp — see that TU's header comment for the responsibility map.
#include "databasemanager.h"

#include "dbtxn.h"

#include <algorithm>
#include <stdexcept>

#include <QCryptographicHash>
#include <QDateTime>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "applicationcontext.h"
#include "batchsizes.h"
#include "cachedcountsservice.h"
#include "collection/collectioncontext.h"
#include "collection/hierarchyhelpers.h"
#include "collection/typehelpers.h"
#include "collection/validationhelpers.h"
#include "errorutils.h"
#include "isessionmanager.h"
#include "pathutils.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

// ─── Path resolution (delegated to FileMapCache) ──────────────────────────────

int DatabaseManager::getCollectionIndexForFile(const QString &filePath) const {
  return m_fileMapCache.collectionIndexForFile(filePath);
}

QString DatabaseManager::findArtworkDirectoryForFile(const QString &filePath) const {
  return m_fileMapCache.artworkDirForFile(filePath);
}

QString DatabaseManager::resolveFilePath(const QString &rawEntry,
                                         const CollectionContext &context) const {
  return m_fileMapCache.resolveFilePath(rawEntry, context);
}

QString DatabaseManager::resolveRelativeFilePath(const QString &rawFileName,
                                                 const QHash<QString, QString> &fileNames) const {
  return m_fileMapCache.resolveRelativeFilePath(rawFileName, fileNames);
}

// ─── Counts ───────────────────────────────────────────────────────────────────

qint64 DatabaseManager::countCollectionByUuid(const QString &collectionUuid) const {
  if (!m_db.isOpen()) {
    return 0;
  }
  QSqlQuery countQuery(m_db);
  countQuery.prepare("SELECT COUNT(DISTINCT path) FROM items WHERE collection_uuid=?");
  countQuery.addBindValue(collectionUuid);
  if (!countQuery.exec() || !countQuery.next()) {
    return 0;
  }
  return countQuery.value(0).toLongLong();
}

qint64
DatabaseManager::countCollectionRecursive(int collectionIndex,
                                          const QList<CollectionConfig> &allCollections) const {
  if (!CollectionUtils::isValidIndex(collectionIndex, &allCollections)) {
    return 0;
  }
  const QString expandedMediaDir = PathUtils::validateAndExpandPath(
      allCollections[collectionIndex].mediaDirectory, allCollections[collectionIndex].name);
  const QString uuid = CollectionUtils::computeCollectionUuid(allCollections[collectionIndex].name,
                                                              expandedMediaDir);
  qint64 total = countCollectionByUuid(uuid);
  const QList<int> descendants =
      CollectionUtils::collectDescendantIndices(collectionIndex, allCollections);
  for (int descendantIndex : descendants) {
    const QString descExpandedMediaDir = PathUtils::validateAndExpandPath(
        allCollections[descendantIndex].mediaDirectory, allCollections[descendantIndex].name);
    const QString descendantUuid = CollectionUtils::computeCollectionUuid(
        allCollections[descendantIndex].name, descExpandedMediaDir);
    total += countCollectionByUuid(descendantUuid);
  }
  return total;
}

void DatabaseManager::clearCollectionFromDatabaseByUuid(const QString &collectionUuid) {
  if (!m_db.isOpen()) {
    return;
  }
  // Kartend-h62cc: bounded IMMEDIATE retry on lock contention, the main-thread
  // sibling of the QueryManagerInternal::clearCollectionFromDatabaseByUuid
  // ladder (which sleeps between attempts — acceptable on a worker, not here).
  // In WAL mode this deferred transaction can fail INSTANTLY with
  // SQLITE_BUSY_SNAPSHOT — the 30s busy_timeout never engages — when a worker
  // commit (a scan, or the one-shot FTS rebuild) lands between this
  // transaction's first read and its write-lock upgrade. A fresh attempt takes
  // a new snapshot, so retrying immediately resolves it without blocking the
  // main thread; a writer actually HOLDING the lock engages the busy handler
  // instead and never reaches this path within the timeout.
  constexpr int MAX_RETRIES = 3;
  for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
    KartendDb::DbTransaction txn(m_db, "DatabaseManager::clearCollectionFromDatabaseByUuid");
    if (!txn.activeOrReport("Failed to start database transaction")) {
      return;
    }
    try {
      QSqlQuery query(m_db);
      query.prepare("DELETE FROM items WHERE collection_uuid = ?");
      query.addBindValue(collectionUuid);
      if (!query.exec()) {
        throw std::runtime_error(query.lastError().text().toStdString());
      }
      QSqlQuery delc(m_db);
      delc.prepare("DELETE FROM collections WHERE uuid = ?");
      delc.addBindValue(collectionUuid);
      if (!delc.exec()) {
        throw std::runtime_error(delc.lastError().text().toStdString());
      }
      if (!txn.commit()) {
        throw std::runtime_error(m_db.lastError().text().toStdString());
      }
      m_metadataCache.invalidateCollection(collectionUuid);
      return; // success
    } catch (const std::exception &e) {
      // guard dtor rolls back at iteration end, before the next attempt
      const QString errorText = ErrorUtils::exceptionMessage(e);
      const bool isLockError = errorText.contains("locked", Qt::CaseInsensitive);
      if (!isLockError || attempt == MAX_RETRIES - 1) {
        // Non-lock error or final attempt — genuine terminal failure.
        auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                          "Failed to clear collection from database",
                                          "DatabaseManager::clearCollectionFromDatabaseByUuid")
                       .withDetails(errorText);
        ErrorUtils::logError(err);
        return;
      }
      // Lock contention — retry with a fresh snapshot.
    }
  }
}

void DatabaseManager::migrateCollectionUuid(const QString &oldUuid, const QString &newUuid) {
  if (!m_db.isOpen() || oldUuid.isEmpty() || newUuid.isEmpty() || oldUuid == newUuid) {
    return;
  }
  KartendDb::DbTransaction txn(m_db, "DatabaseManager::migrateCollectionUuid");
  if (!txn.activeOrReport("Failed to start database transaction")) {
    return;
  }
  try {
    auto run = [&](const QString &sql, const QVariantList &binds) {
      QSqlQuery q(m_db);
      q.prepare(sql);
      for (const QVariant &b : binds) {
        q.addBindValue(b);
      }
      if (!q.exec()) {
        throw std::runtime_error(q.lastError().text().toStdString());
      }
    };

    // The old UPDATE OR REPLACE only re-keyed `items` + `collections`. On a
    // (new_uuid, path) conflict REPLACE silently deleted the pre-existing target
    // row — losing its usage columns and orphaning its item_metadata /
    // item_artwork / launch_history (keyed independently by collection_uuid).
    // It also never re-keyed the per-item child tables or playlist references at
    // all, so a rename stranded curation, artwork overrides, play history, and
    // playlist entries under the old uuid (Kartend-ndyf). Re-key every table
    // explicitly and resolve conflicts deliberately, all inside the transaction.

    // items: a rescan may have created fresh stubs under newUuid before this
    // ran. Merge their usage onto the old (real-history) row — MAX so a
    // post-rename launch on the stub isn't lost — then drop the stub so the
    // re-key can't hit uniq_items_uuid_path.
    run("UPDATE items SET "
        "play_count = MAX(play_count, COALESCE((SELECT n.play_count FROM items n "
        "WHERE n.collection_uuid=? AND n.path=items.path),0)), "
        "total_play_seconds = MAX(total_play_seconds, COALESCE((SELECT n.total_play_seconds "
        "FROM items n WHERE n.collection_uuid=? AND n.path=items.path),0)), "
        "last_played = MAX(COALESCE(last_played,''), COALESCE((SELECT n.last_played FROM items n "
        "WHERE n.collection_uuid=? AND n.path=items.path),'')), "
        "rating = MAX(rating, COALESCE((SELECT n.rating FROM items n "
        "WHERE n.collection_uuid=? AND n.path=items.path),0)), "
        "artwork_path = COALESCE(NULLIF(items.artwork_path,''), (SELECT n.artwork_path FROM items "
        "n "
        "WHERE n.collection_uuid=? AND n.path=items.path AND n.artwork_path IS NOT NULL "
        "AND n.artwork_path!='')) "
        "WHERE collection_uuid=? AND EXISTS (SELECT 1 FROM items n "
        "WHERE n.collection_uuid=? AND n.path=items.path)",
        {newUuid, newUuid, newUuid, newUuid, newUuid, oldUuid, newUuid});
    run("DELETE FROM items WHERE collection_uuid=? AND EXISTS (SELECT 1 FROM items o "
        "WHERE o.collection_uuid=? AND o.path=items.path)",
        {newUuid, oldUuid});
    run("UPDATE items SET collection_uuid=? WHERE collection_uuid=?", {newUuid, oldUuid});

    // item_metadata (UNIQUE collection_uuid,path) + item_artwork (UNIQUE
    // collection_uuid,path,artwork_type): the old rows hold the user's curation
    // / artwork overrides; any newUuid rows are post-rename stubs. Drop the
    // conflicting stubs (old wins), then re-key.
    run("DELETE FROM item_metadata WHERE collection_uuid=? AND EXISTS (SELECT 1 FROM item_metadata "
        "o "
        "WHERE o.collection_uuid=? AND o.path=item_metadata.path)",
        {newUuid, oldUuid});
    run("UPDATE item_metadata SET collection_uuid=? WHERE collection_uuid=?", {newUuid, oldUuid});
    run("DELETE FROM item_artwork WHERE collection_uuid=? AND EXISTS (SELECT 1 FROM item_artwork o "
        "WHERE o.collection_uuid=? AND o.path=item_artwork.path "
        "AND o.artwork_type=item_artwork.artwork_type)",
        {newUuid, oldUuid});
    run("UPDATE item_artwork SET collection_uuid=? WHERE collection_uuid=?", {newUuid, oldUuid});

    // launch_history is append-only (no unique key) — just re-label every row.
    run("UPDATE launch_history SET collection_uuid=? WHERE collection_uuid=?", {newUuid, oldUuid});

    // Playlist references into this collection: nesting parent + per-item source
    // refs. No unique constraint is at risk, so a plain re-key suffices.
    run("UPDATE playlists SET parent_collection_uuid=? WHERE parent_collection_uuid=?",
        {newUuid, oldUuid});
    run("UPDATE playlist_items SET source_collection_uuid=? WHERE source_collection_uuid=?",
        {newUuid, oldUuid});

    // DAT-audit profiles link to collections by the same uuid (soft link,
    // '' = none — no unique constraint, so a plain re-key suffices). Omitting
    // this orphaned every linked profile on rename/move (Kartend-m6qsb.1);
    // purgeOrphanCollectionData still deliberately leaves profiles alone, so
    // the deliberate-rename path here is their only repair.
    run("UPDATE dat_audit_profile SET collection_uuid=? WHERE collection_uuid=?",
        {newUuid, oldUuid});

    // collections: drop any stale stub already sitting at newUuid, then re-key
    // the real row (replacing the old OR REPLACE, which would silently delete).
    run("DELETE FROM collections WHERE uuid=?", {newUuid});
    run("UPDATE collections SET uuid=? WHERE uuid=?", {newUuid, oldUuid});

    if (!txn.commit()) {
      throw std::runtime_error(m_db.lastError().text().toStdString());
    }
    m_metadataCache.invalidateCollection(oldUuid);
    m_metadataCache.invalidateCollection(newUuid);
  } catch (const std::exception &e) {
    ErrorUtils::logError(ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                                "Failed to migrate collection uuid",
                                                "DatabaseManager::migrateCollectionUuid")
                             .withDetails(ErrorUtils::exceptionMessage(e)));
  }
}

QList<IDatabaseManager::ItemPathRow>
DatabaseManager::loadAllItemPathsForCollection(const QString &collectionUuid) const {
  QList<IDatabaseManager::ItemPathRow> out;
  if (!m_db.isOpen() || collectionUuid.isEmpty()) {
    return out;
  }
  QSqlQuery q(const_cast<QSqlDatabase &>(m_db));
  if (!q.prepare("SELECT path, artwork_path FROM items WHERE collection_uuid = ? "
                 "ORDER BY name COLLATE NOCASE ASC")) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to prepare item-path enumeration query",
                                             "DatabaseManager::loadAllItemPathsForCollection")
                             .withDetails(q.lastError().text()));
    return out;
  }
  q.addBindValue(collectionUuid);
  if (!q.exec()) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to enumerate item paths",
                                             "DatabaseManager::loadAllItemPathsForCollection")
                             .withDetails(q.lastError().text()));
    return out;
  }
  while (q.next()) {
    IDatabaseManager::ItemPathRow row;
    row.path = q.value(0).toString();
    row.artworkPath = q.value(1).toString();
    out.append(row);
  }
  return out;
}

namespace {

/// Meta-table key for the purge gate (Kartend-dbqt5): SHA-1 of the sorted
/// live-uuid set the last successful purge ran against. When the set is
/// unchanged since then, the orphan scan is provably a no-op and is skipped.
const QString kOrphanPurgeHashKey = QStringLiteral("orphan_purge_uuid_set_hash");

QString readMetaValueOn(QSqlDatabase &db, const QString &key) {
  QSqlQuery q(db);
  q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT "
                        "NOT NULL DEFAULT '')"));
  q.prepare(QStringLiteral("SELECT value FROM meta WHERE key = ?"));
  q.addBindValue(key);
  if (q.exec() && q.next()) {
    return q.value(0).toString();
  }
  return {};
}

/// Body of the orphan purge, run on the WORKER connection via
/// queueWorkerWrite (Kartend-dbqt5 — this used to run synchronously on the
/// GUI-thread connection right after first paint, stalling the UI for
/// multiple seconds on large libraries).
void purgeOrphanRowsOnConnection(QSqlDatabase &db, const QSet<QString> &liveUuids,
                                 const QString &uuidSetHash) {
  using ErrorUtils::ErrorCode;
  using ErrorUtils::ErrorContext;

  if (!db.isOpen() || liveUuids.isEmpty()) {
    return;
  }

  // Gate (Kartend-dbqt5): skip the multi-second table scans when the live
  // collection set hasn't changed since the last successful purge — the
  // overwhelming majority of launches.
  if (readMetaValueOn(db, kOrphanPurgeHashKey) == uuidSetHash) {
    return;
  }

  // Kartend-yyudl: purge every table keyed by a collection uuid, not just
  // items + collections — child rows (scraped metadata, artwork overrides,
  // launch history, playlist entries) otherwise leak forever once their
  // collection is removed. This is deliberate clean-delete semantics; the
  // re-key path for deliberate renames is migrateCollectionUuid above.
  // file_hash_cache is excluded: it is keyed by absolute path only (no
  // uuid column) and self-validates via size+mtime.
  const QList<QPair<QString, QString>> targets = {
      {QStringLiteral("items"), QStringLiteral("collection_uuid")},
      {QStringLiteral("collections"), QStringLiteral("uuid")},
      {QStringLiteral("item_metadata"), QStringLiteral("collection_uuid")},
      {QStringLiteral("item_artwork"), QStringLiteral("collection_uuid")},
      {QStringLiteral("launch_history"), QStringLiteral("collection_uuid")},
      {QStringLiteral("playlist_items"), QStringLiteral("source_collection_uuid")}};

  // The inline NOT IN (?, …) form binds one variable per live uuid. With
  // hundreds of collections (each contributing up to two uuids) that count
  // approaches SQLite's 999-variable cap (KartendDb::BatchSizes::
  // SqliteVariableLimit), at which point the DELETE fails with "too many SQL
  // variables" and orphan rows are never purged. Past a batch's worth of
  // uuids, switch to the temp-table form: feed a purge-owned temp table in
  // bounded batches, then DELETE … NOT IN (SELECT uuid FROM …), which
  // carries no per-uuid binds in the DELETE itself. (Deliberately NOT the
  // worker's query_uuids table — its contents back cached query scopes.)
  const bool useTempTable = liveUuids.size() > KartendDb::BatchSizes::UuidListBatch;

  KartendDb::DbTransaction txn(db, "DatabaseManager::purgeOrphanCollectionData");
  if (!txn.activeOrReport("Failed to start database transaction")) {
    return;
  }
  try {
    if (useTempTable) {
      QSqlQuery ddl(db);
      if (!ddl.exec(QStringLiteral(
              "CREATE TEMP TABLE IF NOT EXISTS purge_live_uuids (uuid TEXT PRIMARY KEY)"))) {
        throw std::runtime_error(ddl.lastError().text().toStdString());
      }
      // Clear any rows left from a previous purge on this connection.
      if (!ddl.exec(QStringLiteral("DELETE FROM purge_live_uuids"))) {
        throw std::runtime_error(ddl.lastError().text().toStdString());
      }
      const QStringList uuidList(liveUuids.cbegin(), liveUuids.cend());
      constexpr int kBatch = KartendDb::BatchSizes::UuidListBatch;
      for (qsizetype start = 0; start < uuidList.size(); start += kBatch) {
        const qsizetype end = qMin(start + kBatch, uuidList.size());
        QStringList placeholders;
        placeholders.reserve(end - start);
        for (qsizetype i = start; i < end; ++i) {
          placeholders << QStringLiteral("(?)");
        }
        QSqlQuery ins(db);
        ins.prepare(QStringLiteral("INSERT OR IGNORE INTO purge_live_uuids (uuid) VALUES ") +
                    placeholders.join(QStringLiteral(", ")));
        for (qsizetype i = start; i < end; ++i) {
          ins.addBindValue(uuidList.at(i));
        }
        if (!ins.exec()) {
          throw std::runtime_error(ins.lastError().text().toStdString());
        }
      }
      for (const auto &target : targets) {
        QSqlQuery del(db);
        del.prepare(
            QStringLiteral("DELETE FROM %1 WHERE %2 NOT IN (SELECT uuid FROM purge_live_uuids)")
                .arg(target.first, target.second));
        if (!del.exec()) {
          throw std::runtime_error(del.lastError().text().toStdString());
        }
      }
    } else {
      // Small uuid counts stay well under the bind cap — a bound inline IN
      // clause is fine (no temp-table overhead).
      QStringList placeholders;
      placeholders.reserve(liveUuids.size());
      for (int i = 0; i < liveUuids.size(); ++i) {
        placeholders << QStringLiteral("?");
      }
      const QString inClause =
          QStringLiteral("(") + placeholders.join(QStringLiteral(", ")) + QStringLiteral(")");
      for (const auto &target : targets) {
        QSqlQuery del(db);
        del.prepare(QStringLiteral("DELETE FROM %1 WHERE %2 NOT IN %3")
                        .arg(target.first, target.second, inClause));
        for (const QString &uuid : liveUuids) {
          del.addBindValue(uuid);
        }
        if (!del.exec()) {
          throw std::runtime_error(del.lastError().text().toStdString());
        }
      }
    }
    // Stamp the gate INSIDE the purge transaction so a crash between purge
    // and stamp can't record a purge that never committed (the
    // watermark-outside-txn failure mode flagged on the FTS backfill,
    // Kartend-4i5e4).
    {
      QSqlQuery stamp(db);
      stamp.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key, value) VALUES(?, ?)"));
      stamp.addBindValue(kOrphanPurgeHashKey);
      stamp.addBindValue(uuidSetHash);
      if (!stamp.exec()) {
        throw std::runtime_error(stamp.lastError().text().toStdString());
      }
    }
    if (!txn.commit()) {
      throw std::runtime_error(db.lastError().text().toStdString());
    }
  } catch (const std::exception &e) {
    ErrorUtils::logError(ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                                "Failed to purge orphan collection data",
                                                "DatabaseManager::purgeOrphanCollectionData")
                             .withDetails(ErrorUtils::exceptionMessage(e)));
  }
}

} // namespace

void DatabaseManager::purgeOrphanCollectionData(const QList<CollectionConfig> &liveCollections) {
  // Derive the uuid set to keep. A collection's uuid is
  // hash(name, mediaDir); different scan paths feed either the raw or
  // the path-variable-expanded media dir, so keep BOTH forms — that
  // way a collection scanned under either is never purged as an
  // orphan. (For a plain media path the two forms are identical.)
  QSet<QString> liveUuids;
  liveUuids.reserve(liveCollections.size() * 2);
  for (const CollectionConfig &c : liveCollections) {
    liveUuids.insert(CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory));
    const QString expanded = PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
    if (expanded != c.mediaDirectory) {
      liveUuids.insert(CollectionUtils::computeCollectionUuid(c.name, expanded));
    }
  }

  // An empty live set means "no collections" — but that is also the
  // transient state during early startup, so refuse to nuke every
  // row on it. A genuinely empty library has nothing to purge anyway.
  if (liveUuids.isEmpty()) {
    return;
  }

  // Stable fingerprint of the live set for the skip-gate (sorted so QSet
  // iteration order can't produce spurious mismatches).
  QStringList sorted(liveUuids.cbegin(), liveUuids.cend());
  std::sort(sorted.begin(), sorted.end());
  const QString uuidSetHash = QString::fromLatin1(
      QCryptographicHash::hash(sorted.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha1)
          .toHex());

  // Kartend-dbqt5: run the purge on the worker connection — it iterates the
  // items table and historically froze the GUI for multiple seconds right
  // after first paint. FIFO ordering with other worker writes is preserved.
  queueWorkerWrite(
      [liveUuids, uuidSetHash](QSqlDatabase &db) -> bool {
        // Always "settled": the purge logs its own failures and re-arms via
        // the uuid-set-hash gate on the next startup, so a lock-contended run
        // self-heals without the runWrite retry ladder (Kartend-cbtml).
        purgeOrphanRowsOnConnection(db, liveUuids, uuidSetHash);
        return true;
      },
      {}, QStringLiteral("DatabaseManager::purgeOrphanCollectionData"));
}

void DatabaseManager::updateCachedCounts(const QList<CollectionConfig> &allCollections) {
  ISessionManager *session = m_ctx ? m_ctx->sessionManager() : nullptr;
  if (!m_db.isOpen() || !session || !m_cachedCounts) {
    return;
  }
  session->clearStaleCollections(allCollections);

  const int collectionCount = allCollections.size();
  QStringList uuids;
  uuids.reserve(collectionCount);
  for (int i = 0; i < collectionCount; ++i) {
    const QString expandedMediaDir =
        PathUtils::validateAndExpandPath(allCollections[i].mediaDirectory, allCollections[i].name);
    const QString uuid =
        CollectionUtils::computeCollectionUuid(allCollections[i].name, expandedMediaDir);
    uuids.append(uuid);
    if (expandedMediaDir.trimmed().isEmpty()) {
      clearCollectionFromDatabaseByUuid(uuid);
    }
  }
  m_cachedCounts->requestUpdate(allCollections, uuids);
}

// ─── Last-scanned ─────────────────────────────────────────────────────────────

QDateTime DatabaseManager::loadCollectionLastScanned(const QString &collectionUuid) const {
  if (collectionUuid.isEmpty() || !m_db.isValid() || !m_db.isOpen()) {
    return {};
  }
  QSqlQuery query(m_db);
  query.prepare(QStringLiteral("SELECT last_scanned FROM collections WHERE uuid = ?"));
  query.bindValue(0, collectionUuid);
  if (!query.exec() || !query.next()) {
    return {};
  }
  return QDateTime::fromString(query.value(0).toString(), Qt::ISODate);
}
