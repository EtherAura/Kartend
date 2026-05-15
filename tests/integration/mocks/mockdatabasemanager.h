#ifndef KARTEND_TESTS_MOCKDATABASEMANAGER_H
#define KARTEND_TESTS_MOCKDATABASEMANAGER_H

#include "idatabasemanager.h"

namespace KartendTest {

/**
 * In-memory IDatabaseManager double used by integration tests that don't
 * exercise the SQLite layer.
 *
 * Every method is a no-op (queries return empty results, mutations succeed
 * silently). Callers that need richer behaviour can subclass and override
 * specific methods; this base intentionally avoids signal emission so tests
 * stay deterministic — assertions about loaded items, scan progress, etc.
 * should opt in by emitting from overrides.
 */
class MockDatabaseManager : public IDatabaseManager {
  Q_OBJECT
public:
  using IDatabaseManager::IDatabaseManager;

  void initDatabase() override {}

  [[nodiscard]] QString connectionName() const override { return QStringLiteral("mock"); }

  void loadAllCollections(const QList<CollectionConfig> &) override {}
  void loadItems(const CollectionContext &, const QList<CollectionConfig> &) override {}
  void loadItemsWithSubcollections(const CollectionContext &,
                                   const QList<CollectionConfig> &) override {}
  void updateCachedCounts(const QList<CollectionConfig> &) override {}

  void fetchItemCount(const CollectionContext &, const QList<CollectionConfig> &, const QString &,
                      int) override {}
  void fetchItemsRange(const CollectionContext &, const QList<CollectionConfig> &, int, int,
                       const QString &) override {}

  void fetchVisualIndexForPath(const CollectionContext &, const QList<CollectionConfig> &,
                               const QString &) override {}

  void invalidateCollectionCache(const QString &) override {}

  [[nodiscard]] int getCollectionIndexForFile(const QString &) const override { return -1; }
  [[nodiscard]] QString findArtworkDirectoryForFile(const QString &) const override { return {}; }

  [[nodiscard]] QString resolveFilePath(const QString &rawEntry,
                                        const CollectionContext &) const override {
    return rawEntry;
  }
  [[nodiscard]] QString resolveRelativeFilePath(const QString &rawFileName,
                                                const QHash<QString, QString> &) const override {
    return rawFileName;
  }
  [[nodiscard]] qint64 countCollectionRecursive(int,
                                                const QList<CollectionConfig> &) const override {
    return 0;
  }

  [[nodiscard]] QDateTime loadCollectionLastScanned(const QString &) const override { return {}; }

  [[nodiscard]] ItemMetadataStore::ItemMetadata loadItemMetadata(const QString &,
                                                                 const QString &) const override {
    return {};
  }
  bool saveItemMetadata(const ItemMetadataStore::ItemMetadata &) override { return true; }

  [[nodiscard]] QList<ItemArtworkStore::ItemArtwork>
  loadItemArtwork(const QString &, const QString &) const override {
    return {};
  }
  bool saveItemArtwork(const ItemArtworkStore::ItemArtwork &) override { return true; }
  bool removeItemArtwork(const QString &, const QString &, const QString &) override {
    return true;
  }
  void invalidateMetadataCacheItem(const QString &, const QString &) override {}

  [[nodiscard]] UsageStatsStore::ItemUsageStats
  loadItemUsageStats(const QString &, const QString &) const override {
    return {};
  }
  void recordItemLaunch(const QString &, const QString &) override {}
  void recordItemPlaySession(const QString &, const QString &, qint64) override {}
  [[nodiscard]] UsageStatsStore::AggregateStats loadAggregateUsageStats() const override {
    return {};
  }
  [[nodiscard]] QList<UsageStatsStore::ItemUsageRow> loadTopPlayedItems(int) const override {
    return {};
  }
  [[nodiscard]] QList<UsageStatsStore::ItemUsageRow> loadRecentlyPlayedItems(int) const override {
    return {};
  }
  [[nodiscard]] QList<UsageStatsStore::ItemUsageRow> loadNeverPlayedItems(int) const override {
    return {};
  }
  [[nodiscard]] qint64 countItemsPlayedSince(const QString &) const override { return 0; }
  [[nodiscard]] QHash<QString, UsageStatsStore::CollectionUsage>
  loadUsageByCollection() const override {
    return {};
  }
  bool resetAllUsageStats() override { return true; }

  void recordHistoryEntry(const QString &, const QString &, const QString &, int) override {}
  [[nodiscard]] QList<HistoryStore::HistoryEntry> loadRecentHistory(int) const override {
    return {};
  }
  [[nodiscard]] qint64 historyEntryCount() const override { return 0; }
  bool clearHistory() override { return true; }

public slots:
  void cancelScan() override {}
};

} // namespace KartendTest

#endif // KARTEND_TESTS_MOCKDATABASEMANAGER_H
