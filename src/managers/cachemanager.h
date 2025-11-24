#ifndef CACHEMANAGER_H
#define CACHEMANAGER_H

#include <QHash>
#include <QMutex>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QPair>
#include <QList>
#include <QJsonObject>

class CacheManager {
public:
  CacheManager();
  void initialize();
  void saveToDisk();
  
  QPixmap getArtwork(const QString &artworkPath);
  void cacheArtwork(const QString &artworkPath, const QPixmap &pixmap);
  void clearCollectionCache(int collectionIndex);
  static qint64 getCacheSize();
  void releaseGuiResources();

private:
  static QString getCacheDirectory();
  static QString getArtworkCachePath(const QString &artworkPath);
  
  void readTimestamps(const QJsonObject &root);
  static void writeTimestamps(const QHash<QString, qint64> &timestampsCopy);
  static void flushDirtyArtwork(const QList<QPair<QString, QPixmap>> &dirtyList);

  mutable QMutex m_mutex;
  QHash<QString, QPixmap> artworkCache;
  QHash<QString, qint64> fileTimestamps;
  QSet<QString> dirtyArtwork;
  quint64 m_totalPixmapBytes = 0;
};

#endif // CACHEMANAGER_H
