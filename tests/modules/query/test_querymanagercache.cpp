// Tests for QueryManager's count/range cache generation tokens (Kartend-z8i0c).
//
// querymanagercache.cpp keys its queryUuids and sorted-items caches on two
// digests: a matching hash means the cache is still valid (fast path / hit),
// a different hash means it is stale and must be rebuilt (miss / invalidate).
// A wrong digest surfaces as stale sidebar counts that are hard to catch by
// hand, so the hashing is extracted into the pure QueryCacheHash helpers and
// its contract is locked in here. (The temp-table population itself runs on
// QueryManager's worker thread; an end-to-end hit/miss integration test is
// tracked separately.)
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QTest>

#include "collectiontypes.h"
#include "querycachehash.h"

namespace {
QStringList uuids(std::initializer_list<const char *> items) {
  QStringList out;
  for (const char *s : items) out << QString::fromLatin1(s);
  return out;
}
} // namespace

class TestQueryManagerCache : public QObject {
  Q_OBJECT
private slots:
  void uuidListHash_isDeterministic();
  void uuidListHash_changesWithSize();
  void uuidListHash_changesWithFirstLastMiddle();
  void uuidListHash_emptyIsStableAndDistinct();
  void sortCacheHash_isDeterministic();
  void sortCacheHash_changesWithFilterSortAndUuids();
};

void TestQueryManagerCache::uuidListHash_isDeterministic() {
  // Same UUID set -> same token -> the cache stays valid (a hit).
  const QStringList list = uuids({"a", "b", "c"});
  QCOMPARE(QueryCacheHash::uuidListHash(list), QueryCacheHash::uuidListHash(list));
}

void TestQueryManagerCache::uuidListHash_changesWithSize() {
  // Adding a UUID must invalidate the token even when the existing entries
  // (and thus first/last/middle) are unchanged — size is folded in.
  QVERIFY(QueryCacheHash::uuidListHash(uuids({"a", "b", "c"})) !=
          QueryCacheHash::uuidListHash(uuids({"a", "b", "c", "d"})));
}

void TestQueryManagerCache::uuidListHash_changesWithFirstLastMiddle() {
  const QByteArray base = QueryCacheHash::uuidListHash(uuids({"a", "b", "c"}));
  // first / middle / last are the three positions the fast hash samples.
  QVERIFY(base != QueryCacheHash::uuidListHash(uuids({"x", "b", "c"}))); // first differs
  QVERIFY(base != QueryCacheHash::uuidListHash(uuids({"a", "x", "c"}))); // middle differs
  QVERIFY(base != QueryCacheHash::uuidListHash(uuids({"a", "b", "x"}))); // last differs
}

void TestQueryManagerCache::uuidListHash_emptyIsStableAndDistinct() {
  QCOMPARE(QueryCacheHash::uuidListHash({}), QueryCacheHash::uuidListHash({}));
  QVERIFY(QueryCacheHash::uuidListHash({}) != QueryCacheHash::uuidListHash(uuids({"a"})));
}

void TestQueryManagerCache::sortCacheHash_isDeterministic() {
  const QStringList list = uuids({"a", "b", "c"});
  QCOMPARE(
      QueryCacheHash::sortCacheHash(list, QStringLiteral("genre:rpg"), SortMode::NameAscending),
      QueryCacheHash::sortCacheHash(list, QStringLiteral("genre:rpg"), SortMode::NameAscending));
}

void TestQueryManagerCache::sortCacheHash_changesWithFilterSortAndUuids() {
  const QStringList list = uuids({"a", "b", "c"});
  const QByteArray base =
      QueryCacheHash::sortCacheHash(list, QStringLiteral("genre:rpg"), SortMode::NameAscending);

  // A different active filter must rebuild the sorted cache.
  QVERIFY(base != QueryCacheHash::sortCacheHash(list, QStringLiteral("genre:fps"),
                                                SortMode::NameAscending));
  // A different sort order must rebuild it (positions change).
  QVERIFY(base != QueryCacheHash::sortCacheHash(list, QStringLiteral("genre:rpg"),
                                                SortMode::DateDescending));
  // A different UUID set must rebuild it.
  QVERIFY(base != QueryCacheHash::sortCacheHash(uuids({"a", "b", "d"}), QStringLiteral("genre:rpg"),
                                                SortMode::NameAscending));
}

QTEST_MAIN(TestQueryManagerCache)
#include "test_querymanagercache.moc"
