// DatabaseManager: per-item store facades — metadata, artwork, usage stats,
// and launch history — plus the queued worker-write primitive they share.
// Split from databasemanager.cpp — see that TU's header comment for the
// responsibility map.
//
// Kartend-vin5o — threading boundary (deliberate, documented):
// The load*() facades below run their QSqlQuery SYNCHRONOUSLY on the GUI-thread
// m_db connection (const_cast<QSqlDatabase&>(m_db)), while the corresponding
// writes are dispatched to the worker thread via queueWorkerWrite. This is
// intentional and safe, not an oversight:
//   - These are small, LRU-cache-fronted point reads (single (uuid,path) row),
//     so the GUI-thread cost is negligible on a cache miss and zero on a hit.
//   - SQLite runs in WAL mode, where a reader never blocks on a concurrent
//     writer (a background scan on the worker connection), so the GUI thread
//     does not stall on scan write-locks.
//   - Each thread uses its OWN QSqlDatabase connection (per-instance connection
//     names), so there is no cross-thread QSqlDatabase sharing / data race.
// The only consequence is read-after-write staleness for a just-queued write,
// which the code explicitly accepts (see the queueWorkerWrite note below).
// If GUI stalls ever surface here, the heavier AGGREGATE reads
// (loadAggregateUsageStats / loadTopPlayedItems) are the ones to move onto the
// worker behind a queued request/result signal like the existing fetch paths.
#include "databasemanager.h"

#include "dbtxn.h"

#include <functional>
#include <utility>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
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

void DatabaseManager::queueWorkerWrite(std::function<bool(QSqlDatabase &)> op,
                                       std::function<void()> onCompleted, const QString &context) {
  if (!m_worker || !op) {
    return;
  }
  // Queued onto the worker's thread: the closure runs against the worker's
  // connection (m_db there), never the GUI-thread connection. Ordering is
  // preserved (single worker thread, FIFO event queue), so successive
  // increment-style writes for the same item still apply in order — EXCEPT
  // when one of them hits lock contention and is requeued (Kartend-rctcv),
  // which lets a later write overtake it. Harmless for these callers: the
  // usage write is an increment (order-independent) and the history append is
  // ordered by its own stamped time, not by insertion order.
  //
  // The completion hop targets qApp (alive while any loop runs) and checks
  // the QPointer ON THE GUI THREAD — resolving a QPointer on the worker
  // would be the check-then-deref race fixed in scrollmanagerdata
  // (Kartend-t4e00); the worker only copies the pointer value here.
  QPointer<DatabaseManager> self(this);
  QMetaObject::invokeMethod(
      m_worker,
      [w = m_worker, op = std::move(op), onCompleted = std::move(onCompleted), self, context]() {
        // Kartend-rctcv: hand the hop to runWrite instead of firing it once
        // runWrite returns. A contended write now settles on a LATER event-loop
        // turn, so invalidating here would re-open the Kartend-4tprf hole from
        // the other side — the cache would be invalidated, re-populated by the
        // details-pane read, and only then would the write land.
        std::function<void()> onSettled;
        if (onCompleted) {
          onSettled = [self, onCompleted]() {
            QMetaObject::invokeMethod(
                qApp,
                [self, onCompleted]() {
                  if (self) {
                    onCompleted();
                  }
                },
                Qt::QueuedConnection);
          };
        }
        w->runWrite(op, context, std::move(onSettled));
      },
      Qt::QueuedConnection);
}

void DatabaseManager::recordItemLaunch(const QString &collectionUuid, const QString &path) {
  // Fires on every launch — potentially while a background scan holds the WAL
  // write lock — so run it on the worker connection rather than blocking the UI
  // (Kartend-fkvs). Usage is read back only by the sidebar/stats display
  // (cache-mediated, cosmetic), so the brief eventual-consistency window is
  // acceptable.
  //
  // Kartend-4tprf: invalidate BOTH eagerly and in the post-write completion
  // hop. Eager-only invalidation had a hole: the details pane refreshes right
  // after launch, its read re-populated the LRU with the PRE-launch row, and
  // nothing invalidated again after the write landed — so the stale count
  // stuck for the session. Same for the smart-playlist scope key: a re-open
  // between invalidate and write-completion cached a stale scope.
  // Kartend-neb85: stamp the launch HERE, on the queueing thread, and carry it
  // into the closure. The worker may not reach this write for up to ~38s under
  // lock contention (Kartend-rctcv's deferred requeue), so letting the store
  // stamp at execution time would record when the SQL ran — and a deferred
  // launch would then read as NEWER than one that actually followed it.
  const QDateTime launchedAt = QDateTime::currentDateTimeUtc();
  queueWorkerWrite(
      [collectionUuid, path, launchedAt](QSqlDatabase &db) -> bool {
        auto result = UsageStatsStore::recordLaunch(db, collectionUuid, path, launchedAt);
        if (result.isError()) {
          // Transient lock contention (a scan transaction on the scan-worker
          // connection) -> let runWrite retry instead of dropping the launch
          // (Kartend-cbtml).
          if (KartendDb::isLockContentionError(result.error())) {
            return false;
          }
          ErrorUtils::logError(result.error());
        }
        return true;
      },
      [this, collectionUuid, path]() {
        m_metadataCache.invalidateUsage(collectionUuid, path);
        emit requestInvalidateUsageSensitiveCaches();
      },
      QStringLiteral("DatabaseManager::recordItemLaunch"));
  m_metadataCache.invalidateUsage(collectionUuid, path);
  // play_count / last_played changed -> smart playlists like "Recently
  // launched" / "Most played" must re-evaluate (Kartend-s9jw), and a
  // played:/unplayed-filtered sorted-items cache must rebuild rather than
  // serve the pre-launch result set for the rest of the session.
  emit requestInvalidateUsageSensitiveCaches();
}

void DatabaseManager::recordItemPlaySession(const QString &collectionUuid, const QString &path,
                                            qint64 seconds) {
  // See recordItemLaunch for the eager + completion-hop invalidation pair
  // (Kartend-4tprf).
  queueWorkerWrite(
      [collectionUuid, path, seconds](QSqlDatabase &db) -> bool {
        auto result = UsageStatsStore::recordPlaySession(db, collectionUuid, path, seconds);
        if (result.isError()) {
          if (KartendDb::isLockContentionError(result.error())) {
            return false; // retried by runWrite (Kartend-cbtml)
          }
          ErrorUtils::logError(result.error());
        }
        return true;
      },
      [this, collectionUuid, path]() { m_metadataCache.invalidateUsage(collectionUuid, path); },
      QStringLiteral("DatabaseManager::recordItemPlaySession"));
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
  emit requestInvalidateUsageSensitiveCaches();
  return true;
}

// ─── HistoryStore facades ─────────────────────────────────────────────────────

void DatabaseManager::recordHistoryEntry(const QString &collectionUuid, const QString &path,
                                         const QString &name, int maxEntries) {
  // Also fires on launch (same hook as recordItemLaunch). The history log is
  // read only by the history dialog, so route the append + trim onto the worker
  // connection instead of blocking the launch on the UI thread (Kartend-fkvs).
  // Kartend-neb85: stamped on the queueing thread — see recordItemLaunch. Both
  // launch writes take their stamp the same way, so a single launch's usage row
  // and history row agree even when only one of them is deferred.
  const QDateTime launchedAt = QDateTime::currentDateTimeUtc();
  queueWorkerWrite(
      [collectionUuid, path, name, maxEntries, launchedAt](QSqlDatabase &db) -> bool {
        // Append + trim in one transaction so a crash between them can't leave
        // the history table over the cap (Kartend-5rcf). If the transaction
        // can't start, fall back to the prior autocommit behaviour rather than
        // dropping the write — the guard is inert then (no rollback on scope
        // exit), exactly the old inTransaction-gated handling.
        KartendDb::DbTransaction txn(db, "DatabaseManager::recordHistoryEntry");
        auto result = HistoryStore::recordLaunch(db, collectionUuid, path, name, launchedAt);
        if (result.isError()) {
          // Guard dtor rolls back iff the BEGIN succeeded; on transient lock
          // contention runWrite re-runs the whole append+trim (Kartend-cbtml).
          if (KartendDb::isLockContentionError(result.error())) {
            return false;
          }
          ErrorUtils::logError(result.error());
          return true;
        }
        if (maxEntries > 0) {
          auto trim = HistoryStore::trimToMaxEntries(db, maxEntries);
          if (trim.isError()) {
            if (KartendDb::isLockContentionError(trim.error())) {
              return false;
            }
            ErrorUtils::logError(trim.error());
            return true;
          }
        }
        if (txn.active()) {
          if (!txn.commitOrReport("Failed to commit history append+trim",
                                  ErrorUtils::Severity::Error)) {
            // A locked COMMIT is the most likely failure inside a scan-commit
            // window — re-running the rolled-back append+trim is safe.
            return false;
          }
        }
        return true;
      },
      {}, QStringLiteral("DatabaseManager::recordHistoryEntry"));
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
