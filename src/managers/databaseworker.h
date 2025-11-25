#ifndef DATABASEWORKER_H
#define DATABASEWORKER_H

#include <QObject>
#include <QSqlDatabase>
#include <QStringList>
#include <QHash>
#include <QDateTime>
#include "collectionconfig.h"

class SessionManager;

class DatabaseWorker : public QObject {
  Q_OBJECT
public:
  explicit DatabaseWorker(SessionManager *sessionManager, QObject *parent = nullptr);
  ~DatabaseWorker() override;

public slots:
  void initDatabase();
  void loadAllCollections(const QList<CollectionConfig> &allCollections);
  void loadItems(const CollectionContext &context);
  void loadItemsWithSubcollections(const CollectionContext &context,
                                   const QList<CollectionConfig> &allCollections);
  void updateCachedCounts(const QList<CollectionConfig> &allCollections);
  void fetchItemCount(const CollectionContext &context, const QList<CollectionConfig> &allCollections, const QString &filter);
  void fetchItemsRange(const CollectionContext &context, const QList<CollectionConfig> &allCollections, int offset, int limit, const QString &filter);

signals:
  void itemsLoaded(const QStringList &filePaths,
                   const QHash<QString, QString> &fileNames,
                   const QHash<QString, QString> &fileToArtworkDir,
                   const QHash<QString, int> &fileToCollectionIndex);
  void itemCountLoaded(int count);
  void itemsRangeLoaded(int offset, const QStringList &filePaths, const QHash<QString, QString> &fileNames);
  void errorOccurred(const QString &message);
  void countsUpdated();

private:
  SessionManager *m_sessionManager;
  QSqlDatabase m_db;
  QString m_connectionName;

  void ensureCollectionScanned(int collectionIndex, const CollectionConfig &collection);
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
  
  qint64 countCollectionByUuid(const QString &collectionUuid);
  qint64 countGlobal(const QList<CollectionConfig> &allCollections);
  qint64 countCollectionRecursive(int collectionIndex,
                                  const QList<CollectionConfig> &allCollections);
  void clearCollectionFromDatabaseByUuid(const QString &collectionUuid);
  static QString computeCollectionUuid(const QString &name, const QString &mediaDir);

  static void appendFileMapsAndListCanonical(
      int collectionIndex, const CollectionConfig &expandedCollection,
      const QString &mappingArtworkDir, const QStringList &filePaths,
      QStringList &allFilePaths, QHash<QString, QString> &allFileNames,
      QHash<QString, QString> &fileToArtworkDir,
      QHash<QString, QString> &fileToMediaDir,
      QHash<QString, int> &fileToCollectionIndex, bool dedup);
  
  static void sortFiles(QStringList &allFilePaths);
  static int getCharacterSortPriority(const QString &text);
};

#endif // DATABASEWORKER_H
