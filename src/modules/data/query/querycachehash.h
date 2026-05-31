#ifndef QUERYCACHEHASH_H
#define QUERYCACHEHASH_H

#include <QByteArray>
#include <QCryptographicHash>
#include <QString>
#include <QStringList>

#include "collectiontypes.h"

// Generation-token hashes for QueryManager's count/range cache (Kartend-z8i0c).
//
// QueryManager keys its queryUuids and sorted-items caches on these digests:
// when the recomputed hash matches the cached one the layer takes the fast
// path (cache hit / skip repopulation); when it differs the cache is treated
// as stale and rebuilt. A wrong hash surfaces as stale sidebar counts, so the
// hashing is extracted here as a pure function (no QueryManager / DB state)
// and unit-tested directly.
//
// Both are deliberately *fast* hashes — they fold in the list size plus the
// first / last / middle UUID rather than every element, because hashing 3000+
// UUIDs on every query was a measurable cost. The contract the cache relies on
// (and the tests lock in): identical inputs hash identically, and a change in
// size / first / last / middle UUID (or, for the sort hash, the filter or sort
// mode) changes the digest.
namespace QueryCacheHash {

[[nodiscard]] inline QByteArray uuidListHash(const QStringList &uuids) {
  QByteArray data;
  data.reserve(128);
  data.append(QByteArray::number(uuids.size()));
  if (!uuids.isEmpty()) {
    data.append(uuids.first().toUtf8());
    if (uuids.size() > 1) {
      data.append(uuids.last().toUtf8());
    }
    if (uuids.size() > 2) {
      data.append(uuids[uuids.size() / 2].toUtf8());
    }
  }
  return QCryptographicHash::hash(data, QCryptographicHash::Md5);
}

[[nodiscard]] inline QByteArray sortCacheHash(const QStringList &uuids, const QString &filter,
                                              SortMode sortMode) {
  QByteArray data;
  data.reserve(256);
  data.append(QByteArray::number(uuids.size()));
  if (!uuids.isEmpty()) {
    data.append(uuids.first().toUtf8());
    if (uuids.size() > 1) {
      data.append(uuids.last().toUtf8());
    }
    if (uuids.size() > 2) {
      data.append(uuids[uuids.size() / 2].toUtf8());
    }
  }
  data.append(filter.toUtf8());
  data.append(QByteArray::number(static_cast<int>(sortMode)));
  return QCryptographicHash::hash(data, QCryptographicHash::Md5);
}

} // namespace QueryCacheHash

#endif // QUERYCACHEHASH_H
