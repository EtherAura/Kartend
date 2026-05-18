#ifndef ITEMWIDGETFACTORY_H
#define ITEMWIDGETFACTORY_H

#include "collectionutils.h"
#include <functional>
#include <QObject>
#include <QSet>
#include <QString>

class ItemWidget;
class QWidget;
class IArtworkManager;
class IDatabaseManager;
class WidgetPoolManager;

/**
 * @brief Factory for creating and configuring ItemWidget instances.
 *
 * Handles widget acquisition from pool, configuration based on collection
 * settings, and artwork loading setup. Separates widget lifecycle management
 * from scrolling logic.
 */
class ItemWidgetFactory : public QObject {
  Q_OBJECT
public:
  explicit ItemWidgetFactory(QObject *parent = nullptr);
  ~ItemWidgetFactory() override = default;

  // Dependencies
  void setWidgetPool(WidgetPoolManager *pool) { m_widgetPool = pool; }
  void setArtworkManager(IArtworkManager *manager) { m_artworkManager = manager; }
  void setDatabaseManager(IDatabaseManager *manager) { m_databaseManager = manager; }
  void setParentWidget(QWidget *parent) { m_parentWidget = parent; }

  // Context for widget creation
  void setCollectionContext(const CollectionContext &context) { m_context = context; }
  void setMetrics(int itemWidth, int itemHeight);

  // Collections list for looking up collection names
  void setCollections(const QList<CollectionConfig> *collections) { m_collections = collections; }

  // Collection column width for list mode (synced from header drag-resize)
  void setCollectionColumnWidth(int width) { m_collectionColumnWidth = width; }

  // Artwork column width for list mode (synced from header drag-resize)
  void setArtworkColumnWidth(int width) { m_artworkColumnWidth = width; }

  // Subcollection name resolver callback
  using SubcollectionNameResolver = std::function<QString(int)>;
  void setSubcollectionNameResolver(SubcollectionNameResolver resolver) {
    m_subcollectionNameResolver = std::move(resolver);
  }

  // File data accessors (set by ScrollManager before creating widgets)
  void setFileData(const QStringList *filePaths, const QHash<QString, QString> *fileNames);

  // Total item count for adaptive chunk sizing
  void setTotalItemCount(int count) { m_totalItemCount = count; }

  // Cached artwork paths for instant startup (bypasses artwork directory
  // lookup)
  void setCachedArtworkPaths(const QHash<QString, QString> &artworkPaths);
  void clearCachedArtworkPaths() { m_cachedArtworkPaths.clear(); }

  /**
   * @brief Creates a subcollection widget.
   * @param subcollectionIndex The index of the subcollection.
   * @return Configured ItemWidget for the subcollection.
   */
  [[nodiscard]] ItemWidget *createSubcollectionWidget(int subcollectionIndex);

  /**
   * @brief Creates a media item widget.
   * @param mediaIndex Index into the file paths list (after subcollections).
   * @param collectionIndex Output: detected collection index for the item.
   * @return Configured ItemWidget for the media item, or nullptr if invalid.
   */
  [[nodiscard]] ItemWidget *createMediaWidget(int mediaIndex, int &collectionIndex);

  /**
   * @brief Creates a virtual folder widget for navigating subfolders.
   * @param folderPath The relative path of the folder.
   * @return Configured ItemWidget for the virtual folder.
   */
  [[nodiscard]] ItemWidget *createVirtualFolderWidget(const QString &folderPath);

  /**
   * @brief Creates a placeholder widget for items still loading.
   * @return ItemWidget showing "Loading..." state.
   */
  [[nodiscard]] ItemWidget *createPlaceholderWidget();

  /**
   * @brief Releases a widget back to the pool.
   * @param widget The widget to release.
   * @param visibleRows Current visible row count for pool sizing.
   * @param itemsPerRow Items per row for pool sizing.
   */
  void releaseWidget(ItemWidget *widget, int visibleRows, int itemsPerRow);

  /**
   * @brief Clears pending range requests, allowing new requests after data
   * arrives.
   */
  void clearPendingRangeRequests() { m_pendingRangeRequests.clear(); }

  /**
   * @brief Clears a single pending range request.
   * @param startIndex The chunk start index that was requested.
   */
  void clearPendingRangeRequest(int startIndex) { m_pendingRangeRequests.remove(startIndex); }

  /**
   * @brief Prefetch data for a specific range (used during scrollbar drag).
   * @param startIndex Start of the range to prefetch.
   * @param count Number of items to prefetch.
   */
  void prefetchRangeAt(int startIndex, int count);

  /**
   * @brief Re-configure artwork for a widget that may have missed initial
   * config.
   *
   * Called when directories weren't cached during initial widget creation
   * but are now available. Will find and add artwork path to pending queue.
   *
   * @param widget The widget to configure artwork for.
   * @param fullPath Full path to the media file.
   * @param forceDirectLookup If true, bypass cache and do direct filesystem
   * lookup.
   */
  void configureArtworkForWidget(ItemWidget *widget, const QString &fullPath,
                                 bool forceDirectLookup = false);

signals:
  /**
   * @brief Emitted when a subcollection widget is double-clicked.
   * @param subcollectionIndex The index of the subcollection.
   */
  void subcollectionDoubleClicked(int subcollectionIndex);

  /**
   * @brief Emitted when a virtual folder widget is double-clicked.
   * @param folderPath The relative path of the folder.
   */
  void virtualFolderDoubleClicked(const QString &folderPath);

  /**
   * @brief Emitted when items need to be loaded from the database.
   * @param startIndex Start of the range to load.
   * @param count Number of items to load.
   */
  void requestItemsRange(int startIndex, int count);

private:
  [[nodiscard]] ItemWidget *acquireWidget();
  void configureBaseWidget(ItemWidget *widget);
  void resolveMediaItemPaths(const QString &rawFileName, QString &fullPath, QString &displayName,
                             int &collectionIndex);
  void updateCollectionIndexFromDatabase(const QString &fullPath, int &collectionIndex);
  [[nodiscard]] QString resolvePlaceholderArtworkForCollection(int collectionIndex) const;
  void applyPlaceholderArtwork(ItemWidget *widget, const QString &placeholderArtwork) const;

  WidgetPoolManager *m_widgetPool = nullptr;
  IArtworkManager *m_artworkManager = nullptr;
  IDatabaseManager *m_databaseManager = nullptr;
  QWidget *m_parentWidget = nullptr;

  CollectionContext m_context;
  int m_itemWidth = 0;
  int m_itemHeight = 0;

  SubcollectionNameResolver m_subcollectionNameResolver;
  const QStringList *m_filePaths = nullptr;
  const QHash<QString, QString> *m_fileNames = nullptr;
  QHash<QString, QString> m_cachedArtworkPaths; // fullPath -> artworkPath from session cache
  QSet<int> m_pendingRangeRequests;             // Tracks chunk start indices with pending
                                                // requests
  int m_totalItemCount = 0;                     // Total items for adaptive chunk sizing
  const QList<CollectionConfig> *m_collections = nullptr; // Collection list for name lookup
  int m_collectionColumnWidth = 150;                      // Collection column width for list mode
  int m_artworkColumnWidth = 32;                          // Artwork column width for list mode

  [[nodiscard]] int computeChunkSize() const;
  void prefetchAdjacentChunks(int currentMediaIndex, int chunkSize);
};

#endif // ITEMWIDGETFACTORY_H
