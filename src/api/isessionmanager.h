#ifndef ISESSIONMANAGER_H
#define ISESSIONMANAGER_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

struct CollectionConfig;

/**
 * @brief Abstract interface to the session-persistence layer.
 *
 * Exposes the full main-thread API of SessionManager so callers can be
 * wired against either the real (disk-backed) implementation or a test
 * double. The owning struct types (`LastSelectedInfo`, `CachedViewport`)
 * live here so the data contract travels with the interface;
 * `SessionManager` re-exports them as member aliases for source
 * compatibility.
 *
 * Unlike IDatabaseManager / ISettingsManager this is a plain abstract
 * class, NOT a QObject — SessionManager exposes no signals or slots, so
 * there is nothing to moc and no reason to drag QObject into the
 * interface. The concrete SessionManager still derives QObject directly
 * for parenting; see its multiple-inheritance declaration.
 *
 * The static shutdown helper `saveSessionBytesToDiskForShutdown` is
 * intentionally not part of this interface — it is instance-free and
 * stays on the concrete class.
 */
class ISessionManager {
public:
  virtual ~ISessionManager() = default;

  struct LastSelectedInfo {
    int index = -1;
    QString title;
  };

  /// Cached visible items for instant startup.
  struct CachedViewport {
    int startIndex = 0;                   // First visible item index
    int totalItems = 0;                   // Total item count when cached
    QStringList filePaths;                // Cached file paths
    QHash<QString, QString> fileNames;    // Path -> display name
    QHash<QString, QString> artworkPaths; // Path -> artwork file path (resolved)
    [[nodiscard]] bool isValid() const { return totalItems > 0 && !filePaths.isEmpty(); }
  };

  virtual void initialize() = 0;
  /// Persist the session synchronously. IMPORTANT: a NO-OP once the app is
  /// closing (QApplication::closingDown()). At/after shutdown, call
  /// saveToDiskForShutdown() instead — calling this one then silently drops the
  /// save (Kartend-ke02).
  virtual void saveToDisk() = 0;
  /// Shutdown counterpart to saveToDisk(): persists synchronously even while the
  /// app is closing. For the off-thread two-phase close, snapshot on the GUI
  /// thread with snapshotSessionJsonBytesForShutdown() and write the bytes via
  /// saveSessionBytesToDiskForShutdown() instead.
  virtual void saveToDiskForShutdown() = 0;

  [[nodiscard]] virtual QByteArray snapshotSessionJsonBytesForShutdown() const = 0;

  virtual void setLastSelected(const QString &collectionName, int index, const QString &title) = 0;
  [[nodiscard]] virtual int getLastSelectedIndex(const QString &collectionName) const = 0;
  [[nodiscard]] virtual qint64 getGlobalItemCount() const = 0;

  virtual void setGlobalItemCount(qint64 count) = 0;
  virtual void setCollectionCounts(const CollectionConfig &collection,
                                   const QList<CollectionConfig> &allCollections, qint64 itemCount,
                                   qint64 recursiveCount) = 0;
  [[nodiscard]] virtual bool getCollectionCounts(const CollectionConfig &collection,
                                                 const QList<CollectionConfig> &allCollections,
                                                 qint64 &itemCount,
                                                 qint64 &recursiveCount) const = 0;

  virtual void setCachedViewport(const QString &collectionKey, int startIndex, int totalItems,
                                 const QStringList &filePaths,
                                 const QHash<QString, QString> &fileNames,
                                 const QHash<QString, QString> &artworkPaths) = 0;
  [[nodiscard]] virtual CachedViewport getCachedViewport(const QString &collectionKey) const = 0;

  virtual void clearStaleCollections(const QList<CollectionConfig> &currentCollections) = 0;
};

#endif // ISESSIONMANAGER_H
