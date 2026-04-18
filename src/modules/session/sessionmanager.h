#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QString>

struct CollectionConfig;

class SessionManager : public QObject {
  Q_OBJECT

public:
  struct LastSelectedInfo {
    int index = -1;
    QString title;
  };

  // Cached visible items for instant startup
  struct CachedViewport {
    int startIndex = 0;                // First visible item index
    int totalItems = 0;                // Total item count when cached
    QStringList filePaths;             // Cached file paths
    QHash<QString, QString> fileNames; // Path -> display name
    QHash<QString, QString>
        artworkPaths; // Path -> artwork file path (resolved)
    bool isValid() const { return totalItems > 0 && !filePaths.isEmpty(); }
  };

  explicit SessionManager(QObject *parent = nullptr);
  ~SessionManager() override;

  void initialize();
  void saveToDisk();
  void saveToDiskForShutdown();

  // Shutdown-safe persistence helpers
  // Snapshot state while SessionManager is alive; write snapshot asynchronously
  // without needing an instance pointer.
  [[nodiscard]] QByteArray snapshotSessionJsonBytesForShutdown() const;
  static void saveSessionBytesToDiskForShutdown(const QByteArray &data);

  // Session State
  void setLastSelected(const QString &collectionName, int index,
                       const QString &title);
  [[nodiscard]] int getLastSelectedIndex(const QString &collectionName) const;
  [[nodiscard]] qint64 getGlobalItemCount() const { return globalItemCount; }

  // Collection Counts
  void setGlobalItemCount(qint64 count);
  void setCollectionCounts(const CollectionConfig &collection,
                           const QList<CollectionConfig> &allCollections,
                           qint64 itemCount, qint64 recursiveCount);
  [[nodiscard]] bool
  getCollectionCounts(const CollectionConfig &collection,
                      const QList<CollectionConfig> &allCollections,
                      qint64 &itemCount, qint64 &recursiveCount) const;

  // Cached viewport for instant startup
  void setCachedViewport(const QString &collectionKey, int startIndex,
                         int totalItems, const QStringList &filePaths,
                         const QHash<QString, QString> &fileNames,
                         const QHash<QString, QString> &artworkPaths);
  [[nodiscard]] CachedViewport
  getCachedViewport(const QString &collectionKey) const;

  void clearStaleCollections(const QList<CollectionConfig> &currentCollections);

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
