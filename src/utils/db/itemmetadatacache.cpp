#include "itemmetadatacache.h"

#include <utility>

#include <QCoreApplication>
#include <QThread>

namespace {
// Unit separator (U+001F) — picks a delimiter that can't appear in a
// collection UUID (hex string) or a filesystem path, so concatenation
// can't collide with a real key.
constexpr QChar kKeyDelim = QChar(0x1F);

// std::list splicing + QHash mutation under MRU promotion are not safe to
// share between threads. Assert at every public entry that we're on the
// main (GUI) thread — debug-only, so release builds pay nothing.
inline void assertMainThread() {
  Q_ASSERT_X(QCoreApplication::instance() == nullptr ||
                 QThread::currentThread() == QCoreApplication::instance()->thread(),
             "ItemMetadataCache", "ItemMetadataCache must only be accessed from the main thread");
}
} // namespace

ItemMetadataCache::ItemMetadataCache(int capacity) : m_capacity(capacity > 0 ? capacity : 1) {}

QString ItemMetadataCache::makeKey(const QString &uuid, const QString &path) {
  QString key;
  key.reserve(uuid.size() + 1 + path.size());
  key.append(uuid);
  key.append(kKeyDelim);
  key.append(path);
  return key;
}

ItemMetadataCache::Iter ItemMetadataCache::touch(const QString &key) const {
  auto idxIt = m_index.find(key);
  if (idxIt == m_index.end()) {
    return m_lru.end();
  }
  Iter listIt = idxIt.value();
  if (listIt != m_lru.begin()) {
    m_lru.splice(m_lru.begin(), m_lru, listIt);
  }
  return listIt;
}

ItemMetadataCache::Iter ItemMetadataCache::touchOrCreate(const QString &key) {
  auto idxIt = m_index.find(key);
  if (idxIt != m_index.end()) {
    Iter listIt = idxIt.value();
    if (listIt != m_lru.begin()) {
      m_lru.splice(m_lru.begin(), m_lru, listIt);
    }
    return listIt;
  }
  m_lru.emplace_front(key, Entry{});
  Iter inserted = m_lru.begin();
  m_index.insert(key, inserted);
  if (static_cast<int>(m_lru.size()) > m_capacity) {
    const QString &victimKey = m_lru.back().first;
    m_index.remove(victimKey);
    m_lru.pop_back();
  }
  return inserted;
}

std::optional<ItemMetadataStore::ItemMetadata>
ItemMetadataCache::tryGetMetadata(const QString &collectionUuid, const QString &path) const {
  assertMainThread();
  Iter it = touch(makeKey(collectionUuid, path));
  if (it == m_lru.end() || !it->second.metadata.has_value()) {
    return std::nullopt;
  }
  return it->second.metadata;
}

void ItemMetadataCache::putMetadata(const QString &collectionUuid, const QString &path,
                                    ItemMetadataStore::ItemMetadata metadata) {
  assertMainThread();
  Iter it = touchOrCreate(makeKey(collectionUuid, path));
  it->second.metadata = std::move(metadata);
}

std::optional<QList<ItemArtworkStore::ItemArtwork>>
ItemMetadataCache::tryGetArtwork(const QString &collectionUuid, const QString &path) const {
  assertMainThread();
  Iter it = touch(makeKey(collectionUuid, path));
  if (it == m_lru.end() || !it->second.artwork.has_value()) {
    return std::nullopt;
  }
  return it->second.artwork;
}

void ItemMetadataCache::putArtwork(const QString &collectionUuid, const QString &path,
                                   QList<ItemArtworkStore::ItemArtwork> artwork) {
  assertMainThread();
  Iter it = touchOrCreate(makeKey(collectionUuid, path));
  it->second.artwork = std::move(artwork);
}

std::optional<UsageStatsStore::ItemUsageStats>
ItemMetadataCache::tryGetUsageStats(const QString &collectionUuid, const QString &path) const {
  assertMainThread();
  Iter it = touch(makeKey(collectionUuid, path));
  if (it == m_lru.end() || !it->second.usage.has_value()) {
    return std::nullopt;
  }
  return it->second.usage;
}

void ItemMetadataCache::putUsageStats(const QString &collectionUuid, const QString &path,
                                      UsageStatsStore::ItemUsageStats usage) {
  assertMainThread();
  Iter it = touchOrCreate(makeKey(collectionUuid, path));
  it->second.usage = std::move(usage);
}

void ItemMetadataCache::invalidateItem(const QString &collectionUuid, const QString &path) {
  assertMainThread();
  const QString key = makeKey(collectionUuid, path);
  auto idxIt = m_index.find(key);
  if (idxIt == m_index.end()) {
    return;
  }
  m_lru.erase(idxIt.value());
  m_index.erase(idxIt);
}

void ItemMetadataCache::invalidateUsage(const QString &collectionUuid, const QString &path) {
  assertMainThread();
  const QString key = makeKey(collectionUuid, path);
  auto idxIt = m_index.find(key);
  if (idxIt == m_index.end()) {
    return;
  }
  Iter listIt = idxIt.value();
  listIt->second.usage.reset();
  // If the entry is now empty, evict it entirely so the cache doesn't
  // hoard stub keys for items the user only ever launched.
  if (!listIt->second.metadata.has_value() && !listIt->second.artwork.has_value()) {
    m_lru.erase(listIt);
    m_index.erase(idxIt);
  }
}

void ItemMetadataCache::invalidateCollection(const QString &collectionUuid) {
  assertMainThread();
  const QString prefix = collectionUuid + kKeyDelim;
  for (auto it = m_lru.begin(); it != m_lru.end();) {
    if (it->first.startsWith(prefix)) {
      m_index.remove(it->first);
      it = m_lru.erase(it);
    } else {
      ++it;
    }
  }
}

void ItemMetadataCache::clear() {
  assertMainThread();
  m_lru.clear();
  m_index.clear();
}

int ItemMetadataCache::size() const {
  assertMainThread();
  return static_cast<int>(m_lru.size());
}
