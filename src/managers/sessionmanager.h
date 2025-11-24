#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QMutex>
#include <QJsonObject>

struct CollectionConfig;

class SessionManager : public QObject {
  Q_OBJECT

public:
  struct LastSelectedInfo {
    int index = -1;
    QString title;
  };

  explicit SessionManager(QObject *parent = nullptr);
  ~SessionManager() override;
  
  void initialize();
  void saveToDisk();
  
  // Session State
  void setLastSelected(const QString &collectionName, int index, const QString &title);
  int getLastSelectedIndex(const QString &collectionName) const;
  
  // Collection Counts
  void setGlobalItemCount(qint64 count);
  void setCollectionCounts(const CollectionConfig &collection,
                           const QList<CollectionConfig> &allCollections,
                           qint64 itemCount, qint64 recursiveCount);
  bool getCollectionCounts(const CollectionConfig &collection,
                           const QList<CollectionConfig> &allCollections,
                           qint64 &itemCount, qint64 &recursiveCount) const;
  
  void clearStaleCollections(const QList<CollectionConfig> &currentCollections);

private:
  static QString getCacheDirectory();
  void readCollectionsData(const QJsonObject &root);
  void readGlobalData(const QJsonObject &root);
  
  mutable QMutex m_mutex;
  qint64 globalItemCount = 0;
  QHash<QString, qint64> collectionNameRecursiveCountCache;
  QHash<QString, qint64> collectionNameItemCountCache;
  QHash<QString, LastSelectedInfo> lastSelectedByName;
};

#endif // SESSIONMANAGER_H
