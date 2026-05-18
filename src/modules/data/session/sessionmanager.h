#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QString>

#include "isessionmanager.h"

struct CollectionConfig;

// QObject must be the first base; ISessionManager is a plain (non-QObject)
// interface, so this is single-QObject-base multiple inheritance.
class SessionManager : public QObject, public ISessionManager {
  Q_OBJECT

public:
  // Data-contract types are owned by ISessionManager; re-exported here so
  // existing `SessionManager::LastSelectedInfo` / `::CachedViewport`
  // references keep resolving without a call-site change.
  using LastSelectedInfo = ISessionManager::LastSelectedInfo;
  using CachedViewport = ISessionManager::CachedViewport;

  explicit SessionManager(QObject *parent = nullptr);
  ~SessionManager() override;

  void initialize() override;
  void saveToDisk() override;
  void saveToDiskForShutdown() override;

  // Shutdown-safe persistence helpers
  // Snapshot state while SessionManager is alive; write snapshot asynchronously
  // without needing an instance pointer.
  [[nodiscard]] QByteArray snapshotSessionJsonBytesForShutdown() const override;
  static void saveSessionBytesToDiskForShutdown(const QByteArray &data);

  // Session State
  void setLastSelected(const QString &collectionName, int index, const QString &title) override;
  [[nodiscard]] int getLastSelectedIndex(const QString &collectionName) const override;
  [[nodiscard]] qint64 getGlobalItemCount() const override { return globalItemCount; }

  // Collection Counts
  void setGlobalItemCount(qint64 count) override;
  void setCollectionCounts(const CollectionConfig &collection,
                           const QList<CollectionConfig> &allCollections, qint64 itemCount,
                           qint64 recursiveCount) override;
  [[nodiscard]] bool getCollectionCounts(const CollectionConfig &collection,
                                         const QList<CollectionConfig> &allCollections,
                                         qint64 &itemCount,
                                         qint64 &recursiveCount) const override;

  // Cached viewport for instant startup
  void setCachedViewport(const QString &collectionKey, int startIndex, int totalItems,
                         const QStringList &filePaths, const QHash<QString, QString> &fileNames,
                         const QHash<QString, QString> &artworkPaths) override;
  [[nodiscard]] CachedViewport getCachedViewport(const QString &collectionKey) const override;

  void clearStaleCollections(const QList<CollectionConfig> &currentCollections) override;

private:
  static QString getCacheDirectory();
  void readCollectionsData(const QJsonObject &root);
  void readGlobalData(const QJsonObject &root);
  void readCachedViewports(const QJsonObject &root);

  // Builds JSON object from current session state (caller must hold m_mutex)
  [[nodiscard]] QJsonObject buildSessionJson() const;

  // Atomically writes data to file using Qt's QSaveFile pattern.
  static bool atomicWriteFile(const QString &filePath, const QByteArray &data);

  mutable QMutex m_mutex;
  qint64 globalItemCount = 0;
  QHash<QString, qint64> collectionNameRecursiveCountCache;
  QHash<QString, qint64> collectionNameItemCountCache;
  QHash<QString, LastSelectedInfo> lastSelectedByName;
  QHash<QString, CachedViewport> cachedViewports;
};

#endif // SESSIONMANAGER_H
