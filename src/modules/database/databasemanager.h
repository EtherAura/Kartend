#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSqlDatabase>
#include <QStringList>

#include "collectionutils.h"

class SessionManager;

/**
 * @brief Coordinates database operations via a dedicated worker thread.
 * 
 * Threading Model:
 * - Main thread only: All public methods (loadItems, fetchItemCount, etc.)
 * - Worker thread: QueryManager executes actual SQL queries
 * - Communication: Signals/slots with Qt::QueuedConnection for thread safety
 * 
 * Thread-safe operations:
 * - resolveFilePath(), resolveRelativeFilePath() - read-only, no shared mutable state
 * - Signal emissions are queued to main thread automatically
 * 
 * NOT thread-safe (main thread only):
 * - initDatabase(), loadAllCollections(), loadItems()
 * - All setup and configuration methods
 */
class DatabaseManager : public QObject {
  Q_OBJECT
public:
  explicit DatabaseManager(SessionManager *sessionManager, QObject *parent = nullptr);
  ~DatabaseManager() override;

  void initDatabase();
  void loadAllCollections(const QList<CollectionConfig> &allCollections);
  void loadItems(const CollectionContext &context);
  void loadItemsWithSubcollections(const CollectionContext &context,
                              const QList<CollectionConfig> &allCollections);
  void updateCachedCounts(const QList<CollectionConfig> &allCollections);
  
  void fetchItemCount(const CollectionContext &context, const QList<CollectionConfig> &allCollections, const QString &filter = QString());
  void fetchItemsRange(const CollectionContext &context, const QList<CollectionConfig> &allCollections, int offset, int limit, const QString &filter = QString());

  [[nodiscard]] int getCollectionIndexForFile(const QString &filePath) const;
  [[nodiscard]] QString findArtworkDirectoryForFile(const QString &filePath) const;
  
  // File path resolution for relative paths in showAllSubcollectionItems mode
  [[nodiscard]] QString resolveFilePath(const QString &rawEntry,
                                         const CollectionContext &context) const;
  [[nodiscard]] QString resolveRelativeFilePath(const QString &rawFileName,
                                                 const QHash<QString, QString> &fileNames) const;
  [[nodiscard]] qint64
  countCollectionRecursive(int collectionIndex,
                           const QList<CollectionConfig> &allCollections);

signals:
  void itemsLoaded(const QStringList &filePaths,
                   const QHash<QString, QString> &fileNames);
  void itemCountLoaded(int count);
  void itemsRangeLoaded(int offset, const QStringList &filePaths, const QHash<QString, QString> &fileNames);
  void errorOccurred(const QString &message);

  // Internal signals to trigger worker
  void requestLoadAllCollections(const QList<CollectionConfig> &allCollections);
  void requestLoadItems(const CollectionContext &context);
  void requestLoadItemsWithSubcollections(const CollectionContext &context,
                                          const QList<CollectionConfig> &allCollections);
  void requestUpdateCachedCounts(const QList<CollectionConfig> &allCollections);
  void requestFetchItemCount(const CollectionContext &context, const QList<CollectionConfig> &allCollections, const QString &filter);
  void requestFetchItemsRange(const CollectionContext &context, const QList<CollectionConfig> &allCollections, int offset, int limit, const QString &filter);

private slots:
  void onWorkerItemsLoaded(const QStringList &filePaths,
                           const QHash<QString, QString> &fileNames,
                           const QHash<QString, QString> &fileToArtworkDir,
                           const QHash<QString, QString> &fileToMediaDir,
                           const QHash<QString, int> &fileToCollectionIndex);
  void onWorkerItemCountLoaded(int count);
  void onWorkerItemsRangeLoaded(int offset, const QStringList &filePaths, const QHash<QString, QString> &fileNames);

private:
  class QueryManager* m_worker;
  SessionManager *m_sessionManager;
  class QThread* m_workerThread;

  bool needsRescan(int collectionIndex, const CollectionConfig &collection);
  static QStringList scanMediaDirectory(const CollectionConfig &collection,
                                        QHash<QString, QDateTime> &timestamps);
  QStringList loadItemsFromDatabaseByUuid(const QString &collectionUuid);
  QStringList loadOrScanCollection(int collectionIndex,
                                   const CollectionConfig &collection,
                                   QHash<QString, QDateTime> &timestamps);
  void saveItemsToDatabase(int collectionIndex, const QStringList &filePaths,
                           const QHash<QString, QDateTime> &timestamps,
                           const CollectionConfig &collection);
  static int getCharacterSortPriority(const QString &text);
  static void sortFiles(QStringList &allFilePaths);
  [[nodiscard]] qint64 countCollectionByUuid(const QString &collectionUuid);
  [[nodiscard]] qint64 countGlobal(const QList<CollectionConfig> &allCollections);
  void clearCollectionFromDatabaseByUuid(const QString &collectionUuid);

  QSqlDatabase m_db;
  QString m_connectionName;

  mutable QMutex m_dataMutex; // Protects m_fileToArtworkDir, m_fileToCollectionIndex, m_fileToMediaDir
  QHash<QString, QString> m_fileToArtworkDir;
  QHash<QString, int> m_fileToCollectionIndex;
  QHash<QString, QString> m_fileToMediaDir;  // Maps file paths to their media directories

  static void appendFileMapsAndListCanonical(
      int collectionIndex, const CollectionConfig &expandedCollection,
      const QString &mappingArtworkDir, const QStringList &filePaths,
      QStringList &allFilePaths, QHash<QString, QString> &allFileNames,
      QHash<QString, QString> &fileToArtworkDir,
      QHash<QString, QString> &fileToMediaDir,
      QHash<QString, int> &fileToCollectionIndex, bool dedup);
};

#endif