// DatabaseManager: per-item store facades — metadata, artwork, usage stats,
// and launch history — plus the queued worker-write primitive they share.
// Split from databasemanager.cpp — see that TU's header comment for the
// responsibility map.
#include "databasemanager.h"

#include <functional>
#include <utility>

#include <QMetaObject>
#include <QSqlError>

#include "errorutils.h"
#include "historystore.h"
#include "itemartwork.h"
#include "itemmetadata.h"
#include "querymanager.h"
#include "usagestatsstore.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

// ─── ItemMetadataStore facades ────────────────────────────────────────────────

ItemMetadataStore::ItemMetadata DatabaseManager::loadItemMetadata(const QString &collectionUuid,
                                                                  const QString &path) const {
  if (auto cached = m_metadataCache.tryGetMetadata(collectionUuid, path); cached) {
    return *cached;
  }
  auto result = ItemMetadataStore::load(const_cast<QSqlDatabase &>(m_db), collectionUuid, path);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    ItemMetadataStore::ItemMetadata empty;
    empty.collectionUuid = collectionUuid;
    empty.path = path;
    // Don't cache DB-failure stubs — a transient connection blip
    // shouldn't pin an empty row that masks the real data after
    // reconnection.
    return empty;
  }
  m_metadataCache.putMetadata(collectionUuid, path, result.value());
  return result.value();
}

QHash<QString, ItemMetadataStore::ItemMetadata>
DatabaseManager::loadItemMetadataBatch(const QString &collectionUuid,
                                       const QStringList &paths) const {
  QHash<QString, ItemMetadataStore::ItemMetadata> out;
  out.reserve(paths.size());

  // Walk cache first so a warm BatchScrapeRunner pre-flight doesn't issue
  // any SQL when the same paths were touched earlier in the session.
  QStringList missingPaths;
  missingPaths.reserve(paths.size());
  for (const QString &path : paths) {
    if (auto cached = m_metadataCache.tryGetMetadata(collectionUuid, path); cached) {
      out.insert(path, *cached);
    } else {
      missingPaths.append(path);
    }
  }

  if (missingPaths.isEmpty()) return out;

  auto result =
      ItemMetadataStore::loadBatch(const_cast<QSqlDatabase &>(m_db), collectionUuid, missingPaths);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    // Mirror single-load's graceful degradation: hand back empty stubs so
    // the caller can keep iterating without nullopt checks.
    for (const QString &p : missingPaths) {
      ItemMetadataStore::ItemMetadata empty;
      empty.collectionUuid = collectionUuid;
      empty.path = p;
      out.insert(p, empty);
    }
    return out;
  }

  // Merge in the freshly-loaded rows and back-populate the cache for every one,
  // exactly as loadItemMetadata does: it caches any non-error load — including a
  // manually-curated row whose `source` is empty, and a not-found empty stub
  // (both invalidated when the row later gains data). The old `!source.isEmpty()`
  // guard skipped both, re-querying them on every batch pre-flight (Kartend-l6mx).
  const auto &loaded = result.value();
  for (auto it = loaded.constBegin(); it != loaded.constEnd(); ++it) {
    out.insert(it.key(), it.value());
    m_metadataCache.putMetadata(collectionUuid, it.key(), it.value());
  }

  return out;
}

bool DatabaseManager::saveItemMetadata(const ItemMetadataStore::ItemMetadata &metadata) {
  auto result = ItemMetadataStore::save(m_db, metadata);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    emit errorOccurred(result.error());
    return false;
  }
  m_metadataCache.invalidateItem(metadata.collectionUuid, metadata.path);
  return true;
}

// ─── ItemArtworkStore facades ─────────────────────────────────────────────────

QList<ItemArtworkStore::ItemArtwork> DatabaseManager::loadItemArtwork(const QString &collectionUuid,
                                                                      const QString &path) const {
  if (auto cached = m_metadataCache.tryGetArtwork(collectionUuid, path); cached) {
    return *cached;
  }
  auto result =
      ItemArtworkStore::loadAllForItem(const_cast<QSqlDatabase &>(m_db), collectionUuid, path);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  m_metadataCache.putArtwork(collectionUuid, path, result.value());
  return result.value();
}

bool DatabaseManager::saveItemArtwork(const ItemArtworkStore::ItemArtwork &artwork) {
  auto result = ItemArtworkStore::save(m_db, artwork);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    emit errorOccurred(result.error());
    return false;
  }
  m_metadataCache.invalidateItem(artwork.collectionUuid, artwork.path);
  return true;
}

bool DatabaseManager::removeItemArtwork(const QString &collectionUuid, const QString &path,
                                        const QString &artworkType) {
  auto result = ItemArtworkStore::remove(m_db, collectionUuid, path, artworkType);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    emit errorOccurred(result.error());
    return false;
  }
  m_metadataCache.invalidateItem(collectionUuid, path);
  return true;
}

void DatabaseManager::invalidateMetadataCacheItem(const QString &collectionUuid,
                                                  const QString &path) {
  m_metadataCache.invalidateItem(collectionUuid, path);
}

// ─── UsageStatsStore facades ──────────────────────────────────────────────────

UsageStatsStore::ItemUsageStats DatabaseManager::loadItemUsageStats(const QString &collectionUuid,
                                                                    const QString &path) const {
  if (auto cached = m_metadataCache.tryGetUsageStats(collectionUuid, path); cached) {
    return *cached;
  }
  auto result =
      UsageStatsStore::loadForItem(const_cast<QSqlDatabase &>(m_db), collectionUuid, path);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    UsageStatsStore::ItemUsageStats empty;
    empty.collectionUuid = collectionUuid;
    empty.path = path;
    return empty;
  }
  m_metadataCache.putUsageStats(collectionUuid, path, result.value());
  return result.value();
}

void DatabaseManager::queueWorkerWrite(std::function<void(QSqlDatabase &)> op) {
  if (!m_worker || !op) {
    return;
  }
  // Queued onto the worker's thread: the closure runs against the worker's
  // connection (m_db there), never the GUI-thread connection. Ordering is
  // preserved (single worker thread, FIFO event queue), so successive
  // increment-style writes for the same item still apply in order.
  QMetaObject::invokeMethod(
      m_worker, [w = m_worker, op = std::move(op)]() { w->runWrite(op); }, Qt::QueuedConnection);
}

void DatabaseManager::recordItemLaunch(const QString &collectionUuid, const QString &path) {
  // Fires on every launch — potentially while a background scan holds the WAL
  // write lock — so run it on the worker connection rather than blocking the UI
  // (Kartend-fkvs). Usage is read back only by the sidebar/stats display
  // (cache-mediated, cosmetic), so the brief eventual-consistency window is
  // acceptable; invalidate so the next read refreshes once the write lands.
  queueWorkerWrite([collectionUuid, path](QSqlDatabase &db) {
    auto result = UsageStatsStore::recordLaunch(db, collectionUuid, path);
    if (result.isError()) {
      ErrorUtils::logError(result.error());
    }
  });
  m_metadataCache.invalidateUsage(collectionUuid, path);
  // play_count / last_played changed -> smart playlists like "Recently
  // launched" / "Most played" must re-evaluate (Kartend-s9jw).
  emit requestInvalidateSmartPlaylistScope();
}

void DatabaseManager::recordItemPlaySession(const QString &collectionUuid, const QString &path,
                                            qint64 seconds) {
  queueWorkerWrite([collectionUuid, path, seconds](QSqlDatabase &db) {
    auto result = UsageStatsStore::recordPlaySession(db, collectionUuid, path, seconds);
    if (result.isError()) {
      ErrorUtils::logError(result.error());
    }
  });
  m_metadataCache.invalidateUsage(collectionUuid, path);
}

UsageStatsStore::AggregateStats DatabaseManager::loadAggregateUsageStats() const {
  auto result = UsageStatsStore::loadAggregate(const_cast<QSqlDatabase &>(m_db));
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

QList<UsageStatsStore::ItemUsageRow> DatabaseManager::loadTopPlayedItems(int limit) const {
  auto result = UsageStatsStore::loadTopPlayed(const_cast<QSqlDatabase &>(m_db), limit);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

QList<UsageStatsStore::ItemUsageRow> DatabaseManager::loadRecentlyPlayedItems(int limit) const {
  auto result = UsageStatsStore::loadRecentlyPlayed(const_cast<QSqlDatabase &>(m_db), limit);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

QList<UsageStatsStore::ItemUsageRow> DatabaseManager::loadNeverPlayedItems(int limit) const {
  auto result = UsageStatsStore::loadNeverPlayed(const_cast<QSqlDatabase &>(m_db), limit);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

qint64 DatabaseManager::countItemsPlayedSince(const QString &isoCutoffUtc) const {
  auto result = UsageStatsStore::countPlayedSince(const_cast<QSqlDatabase &>(m_db), isoCutoffUtc);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return 0;
  }
  return result.value();
}

QHash<QString, UsageStatsStore::CollectionUsage> DatabaseManager::loadUsageByCollection() const {
  auto result = UsageStatsStore::loadCollectionBreakdown(const_cast<QSqlDatabase &>(m_db));
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

bool DatabaseManager::resetAllUsageStats() {
  auto result = UsageStatsStore::resetAll(m_db);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    emit errorOccurred(result.error());
    return false;
  }
  // The reset only touches the usage column, but enumerating every
  // cached entry to scrub one slot costs more than the simple clear.
  // resetAllUsageStats is a deliberate user action — flushing the
  // whole per-item cache here is fine; the next selection refills it.
  m_metadataCache.clear();
  // All play_count / last_played values just changed -> drop smart-playlist
  // scope so play-data-keyed playlists re-evaluate (Kartend-s9jw).
  emit requestInvalidateSmartPlaylistScope();
  return true;
}

// ─── HistoryStore facades ─────────────────────────────────────────────────────

void DatabaseManager::recordHistoryEntry(const QString &collectionUuid, const QString &path,
                                         const QString &name, int maxEntries) {
  // Also fires on launch (same hook as recordItemLaunch). The history log is
  // read only by the history dialog, so route the append + trim onto the worker
  // connection instead of blocking the launch on the UI thread (Kartend-fkvs).
  queueWorkerWrite([collectionUuid, path, name, maxEntries](QSqlDatabase &db) {
    // Append + trim in one transaction so a crash between them can't leave the
    // history table over the cap (Kartend-5rcf). If the transaction can't start,
    // fall back to the prior autocommit behaviour rather than dropping the write.
    const bool inTransaction = db.transaction();
    auto result = HistoryStore::recordLaunch(db, collectionUuid, path, name);
    if (result.isError()) {
      ErrorUtils::logError(result.error());
      if (inTransaction) {
        db.rollback();
      }
      return;
    }
    if (maxEntries > 0) {
      auto trim = HistoryStore::trimToMaxEntries(db, maxEntries);
      if (trim.isError()) {
        ErrorUtils::logError(trim.error());
        if (inTransaction) {
          db.rollback();
        }
        return;
      }
    }
    if (inTransaction && !db.commit()) {
      ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseTransactionFailed,
                                               "Failed to commit history append+trim",
                                               "DatabaseManager::recordHistoryEntry")
                               .withDetails(db.lastError().text()));
      db.rollback();
    }
  });
}

QList<HistoryStore::HistoryEntry> DatabaseManager::loadRecentHistory(int limit) const {
  auto result = HistoryStore::loadRecent(const_cast<QSqlDatabase &>(m_db), limit);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return {};
  }
  return result.value();
}

qint64 DatabaseManager::historyEntryCount() const {
  auto result = HistoryStore::count(const_cast<QSqlDatabase &>(m_db));
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    return 0;
  }
  return result.value();
}

bool DatabaseManager::clearHistory() {
  auto result = HistoryStore::clearAll(m_db);
  if (result.isError()) {
    ErrorUtils::logError(result.error());
    emit errorOccurred(result.error());
    return false;
  }
  return true;
}
