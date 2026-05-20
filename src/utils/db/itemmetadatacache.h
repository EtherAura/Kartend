#ifndef ITEMMETADATACACHE_H
#define ITEMMETADATACACHE_H

#include <list>
#include <optional>
#include <utility>

#include <QHash>
#include <QList>
#include <QString>

#include "itemartwork.h"
#include "itemmetadata.h"
#include "usagestatsstore.h"

/// Per-item LRU cache for the three DB lookups the sidebar refresh fires
/// on every settled selection: ItemMetadata, the ItemArtwork rows, and
/// the per-item UsageStats. One Entry per (collectionUuid, path); each
/// slot is independently populated so a consumer that only asks for one
/// kind (e.g. cover flow asks for artwork only) doesn't force-load the
/// other two.
///
/// Not thread-safe. The DatabaseManager methods that consult it all run
/// on the main thread (see DatabaseManager class doc). Every public method
/// asserts QThread::currentThread() == qApp->thread() in debug builds so
/// the rule is enforced instead of just documented.
class ItemMetadataCache {
public:
  static constexpr int DEFAULT_CAPACITY = 256;

  explicit ItemMetadataCache(int capacity = DEFAULT_CAPACITY);

  [[nodiscard]] std::optional<ItemMetadataStore::ItemMetadata>
  tryGetMetadata(const QString &collectionUuid, const QString &path) const;
  void putMetadata(const QString &collectionUuid, const QString &path,
                   ItemMetadataStore::ItemMetadata metadata);

  [[nodiscard]] std::optional<QList<ItemArtworkStore::ItemArtwork>>
  tryGetArtwork(const QString &collectionUuid, const QString &path) const;
  void putArtwork(const QString &collectionUuid, const QString &path,
                  QList<ItemArtworkStore::ItemArtwork> artwork);

  [[nodiscard]] std::optional<UsageStatsStore::ItemUsageStats>
  tryGetUsageStats(const QString &collectionUuid, const QString &path) const;
  void putUsageStats(const QString &collectionUuid, const QString &path,
                     UsageStatsStore::ItemUsageStats usage);

  /// Drops all three slots for (uuid, path). Called after any save that
  /// could change any of them (saveItemMetadata, saveItemArtwork,
  /// removeItemArtwork).
  void invalidateItem(const QString &collectionUuid, const QString &path);

  /// Drops only the usage-stats slot. Cheaper invalidation for
  /// recordItemLaunch / recordItemPlaySession which can't change
  /// metadata or artwork rows.
  void invalidateUsage(const QString &collectionUuid, const QString &path);

  /// Drops every entry whose key starts with the given uuid. Used when a
  /// whole collection is rescanned / cleared / re-imported.
  void invalidateCollection(const QString &collectionUuid);

  void clear();

  [[nodiscard]] int size() const;
  [[nodiscard]] int capacity() const { return m_capacity; }

private:
  struct Entry {
    std::optional<ItemMetadataStore::ItemMetadata> metadata;
    std::optional<QList<ItemArtworkStore::ItemArtwork>> artwork;
    std::optional<UsageStatsStore::ItemUsageStats> usage;
  };

  using Pair = std::pair<QString, Entry>;
  using Iter = std::list<Pair>::iterator;

  [[nodiscard]] static QString makeKey(const QString &uuid, const QString &path);

  /// Returns an iterator to the entry for `key`, promoting it to MRU
  /// front. Returns `m_lru.end()` when the key isn't cached.
  [[nodiscard]] Iter touch(const QString &key) const;
  /// Returns an iterator to the entry for `key`, creating an empty one
  /// at MRU front if it doesn't exist. Always evicts the LRU back if
  /// over capacity AFTER the insert.
  [[nodiscard]] Iter touchOrCreate(const QString &key);

  int m_capacity;
  // Stable storage of (key, entry) pairs in MRU-front order. List
  // iterators stay valid across the rest of the API, which is the whole
  // reason we use std::list here rather than a QHash chain.
  mutable std::list<Pair> m_lru;
  // Direct index from key → list iterator so lookups are O(1) and the
  // MRU promotion only has to splice one node.
  mutable QHash<QString, Iter> m_index;
};

#endif // ITEMMETADATACACHE_H
