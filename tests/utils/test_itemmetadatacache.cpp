// Tests for the per-item LRU cache that fronts DatabaseManager's
// loadItemMetadata / loadItemArtwork / loadItemUsageStats. The cache is
// pure in-memory state — these tests don't need a real database.
#include <QTest>

#include "itemmetadatacache.h"

class TestItemMetadataCache : public QObject {
  Q_OBJECT
private slots:
  void missOnEmpty();
  void roundTripPerSlot();
  void slotsAreIndependent();
  void invalidateItemClearsAllSlots();
  void invalidateUsageKeepsOtherSlots();
  void invalidateUsageEvictsStub();
  void invalidateCollectionDropsByPrefix();
  void evictionFollowsLruOrder();
  void mruPromotionOnRead();
  void clearDropsEverything();
};

namespace {

ItemMetadataStore::ItemMetadata makeMeta(const QString &uuid, const QString &path,
                                          const QString &title) {
  ItemMetadataStore::ItemMetadata m;
  m.collectionUuid = uuid;
  m.path = path;
  m.title = title;
  return m;
}

UsageStatsStore::ItemUsageStats makeUsage(const QString &uuid, const QString &path, int count) {
  UsageStatsStore::ItemUsageStats u;
  u.collectionUuid = uuid;
  u.path = path;
  u.playCount = count;
  return u;
}

ItemArtworkStore::ItemArtwork makeArtworkRow(const QString &uuid, const QString &path,
                                              const QString &type) {
  ItemArtworkStore::ItemArtwork a;
  a.collectionUuid = uuid;
  a.path = path;
  a.artworkType = type;
  return a;
}

} // namespace

void TestItemMetadataCache::missOnEmpty() {
  ItemMetadataCache cache(8);
  QVERIFY(!cache.tryGetMetadata("u1", "p1").has_value());
  QVERIFY(!cache.tryGetArtwork("u1", "p1").has_value());
  QVERIFY(!cache.tryGetUsageStats("u1", "p1").has_value());
  QCOMPARE(cache.size(), 0);
}

void TestItemMetadataCache::roundTripPerSlot() {
  ItemMetadataCache cache(8);
  cache.putMetadata("u1", "p1", makeMeta("u1", "p1", "Title"));
  cache.putArtwork("u1", "p1",
                   QList<ItemArtworkStore::ItemArtwork>{makeArtworkRow("u1", "p1", "front")});
  cache.putUsageStats("u1", "p1", makeUsage("u1", "p1", 5));

  auto meta = cache.tryGetMetadata("u1", "p1");
  QVERIFY(meta.has_value());
  QCOMPARE(meta->title, QStringLiteral("Title"));

  auto art = cache.tryGetArtwork("u1", "p1");
  QVERIFY(art.has_value());
  QCOMPARE(art->size(), 1);
  QCOMPARE(art->at(0).artworkType, QStringLiteral("front"));

  auto usage = cache.tryGetUsageStats("u1", "p1");
  QVERIFY(usage.has_value());
  QCOMPARE(usage->playCount, 5);

  // Single entry — all three slots share one (uuid, path) row.
  QCOMPARE(cache.size(), 1);
}

void TestItemMetadataCache::slotsAreIndependent() {
  ItemMetadataCache cache(8);
  // Only the metadata slot populated — other two must miss.
  cache.putMetadata("u1", "p1", makeMeta("u1", "p1", "OnlyMeta"));
  QVERIFY(cache.tryGetMetadata("u1", "p1").has_value());
  QVERIFY(!cache.tryGetArtwork("u1", "p1").has_value());
  QVERIFY(!cache.tryGetUsageStats("u1", "p1").has_value());
}

void TestItemMetadataCache::invalidateItemClearsAllSlots() {
  ItemMetadataCache cache(8);
  cache.putMetadata("u1", "p1", makeMeta("u1", "p1", "T"));
  cache.putArtwork("u1", "p1", {makeArtworkRow("u1", "p1", "front")});
  cache.putUsageStats("u1", "p1", makeUsage("u1", "p1", 1));

  cache.invalidateItem("u1", "p1");

  QVERIFY(!cache.tryGetMetadata("u1", "p1").has_value());
  QVERIFY(!cache.tryGetArtwork("u1", "p1").has_value());
  QVERIFY(!cache.tryGetUsageStats("u1", "p1").has_value());
  QCOMPARE(cache.size(), 0);
}

void TestItemMetadataCache::invalidateUsageKeepsOtherSlots() {
  ItemMetadataCache cache(8);
  cache.putMetadata("u1", "p1", makeMeta("u1", "p1", "T"));
  cache.putArtwork("u1", "p1", {makeArtworkRow("u1", "p1", "front")});
  cache.putUsageStats("u1", "p1", makeUsage("u1", "p1", 7));

  cache.invalidateUsage("u1", "p1");

  QVERIFY(cache.tryGetMetadata("u1", "p1").has_value());
  QVERIFY(cache.tryGetArtwork("u1", "p1").has_value());
  QVERIFY(!cache.tryGetUsageStats("u1", "p1").has_value());
}

void TestItemMetadataCache::invalidateUsageEvictsStub() {
  // When only the usage slot was populated, invalidating it leaves the
  // entry empty — the cache should reclaim that row instead of keeping
  // an empty bucket that pins a key.
  ItemMetadataCache cache(8);
  cache.putUsageStats("u1", "p1", makeUsage("u1", "p1", 1));
  QCOMPARE(cache.size(), 1);

  cache.invalidateUsage("u1", "p1");
  QCOMPARE(cache.size(), 0);
}

void TestItemMetadataCache::invalidateCollectionDropsByPrefix() {
  ItemMetadataCache cache(16);
  cache.putMetadata("col-A", "p1", makeMeta("col-A", "p1", "A1"));
  cache.putMetadata("col-A", "p2", makeMeta("col-A", "p2", "A2"));
  cache.putMetadata("col-B", "p1", makeMeta("col-B", "p1", "B1"));

  cache.invalidateCollection("col-A");

  QVERIFY(!cache.tryGetMetadata("col-A", "p1").has_value());
  QVERIFY(!cache.tryGetMetadata("col-A", "p2").has_value());
  QVERIFY(cache.tryGetMetadata("col-B", "p1").has_value());
  QCOMPARE(cache.size(), 1);
}

void TestItemMetadataCache::evictionFollowsLruOrder() {
  ItemMetadataCache cache(3);
  cache.putMetadata("u", "p1", makeMeta("u", "p1", "T1"));
  cache.putMetadata("u", "p2", makeMeta("u", "p2", "T2"));
  cache.putMetadata("u", "p3", makeMeta("u", "p3", "T3"));
  cache.putMetadata("u", "p4", makeMeta("u", "p4", "T4")); // evicts p1

  QCOMPARE(cache.size(), 3);
  QVERIFY(!cache.tryGetMetadata("u", "p1").has_value());
  QVERIFY(cache.tryGetMetadata("u", "p2").has_value());
  QVERIFY(cache.tryGetMetadata("u", "p3").has_value());
  QVERIFY(cache.tryGetMetadata("u", "p4").has_value());
}

void TestItemMetadataCache::mruPromotionOnRead() {
  ItemMetadataCache cache(3);
  cache.putMetadata("u", "p1", makeMeta("u", "p1", "T1"));
  cache.putMetadata("u", "p2", makeMeta("u", "p2", "T2"));
  cache.putMetadata("u", "p3", makeMeta("u", "p3", "T3"));
  // Touch p1 — should promote to MRU front, so the next eviction takes
  // p2 (now the LRU back) instead of p1.
  QVERIFY(cache.tryGetMetadata("u", "p1").has_value());
  cache.putMetadata("u", "p4", makeMeta("u", "p4", "T4"));

  QVERIFY(cache.tryGetMetadata("u", "p1").has_value()); // survived
  QVERIFY(!cache.tryGetMetadata("u", "p2").has_value()); // evicted
  QVERIFY(cache.tryGetMetadata("u", "p3").has_value());
  QVERIFY(cache.tryGetMetadata("u", "p4").has_value());
}

void TestItemMetadataCache::clearDropsEverything() {
  ItemMetadataCache cache(8);
  cache.putMetadata("u", "p1", makeMeta("u", "p1", "T"));
  cache.putArtwork("u", "p2", {makeArtworkRow("u", "p2", "front")});
  cache.putUsageStats("u", "p3", makeUsage("u", "p3", 1));
  QCOMPARE(cache.size(), 3);

  cache.clear();

  QCOMPARE(cache.size(), 0);
  QVERIFY(!cache.tryGetMetadata("u", "p1").has_value());
  QVERIFY(!cache.tryGetArtwork("u", "p2").has_value());
  QVERIFY(!cache.tryGetUsageStats("u", "p3").has_value());
}

QTEST_MAIN(TestItemMetadataCache)
#include "test_itemmetadatacache.moc"
