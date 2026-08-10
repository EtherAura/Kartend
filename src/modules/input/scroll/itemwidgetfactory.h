#ifndef ITEMWIDGETFACTORY_H
#define ITEMWIDGETFACTORY_H

#include "collection/collectioncontext.h"
#include <functional>
#include <QObject>
#include <QSet>
#include <QString>

class ItemWidget;
class QWidget;
class IArtworkManager;
class IDatabaseManager;
class WidgetPoolManager;
#include "applicationcontext_fwd.h"

namespace ItemWidgetFactoryHelpers {

// Pure placeholder-artwork resolution policy extracted from
// ItemWidgetFactory::resolvePlaceholderArtworkForCollection so the
// precedence rules are unit-testable without a factory/widget graph
// (same shape as the ScrollHelpers extractions):
//
//   1. The collection's own placeholderArtwork (inherited up the parent
//      chain via CollectionUtils::resolvePlaceholderArtwork) wins.
//   2. Otherwise the active context's configured placeholder is the
//      fallback.
//   3. A non-empty result is expanded through
//      SettingsUtils::expandConfigVariables using the collection's name
//      when @p collectionIndex is valid, else @p contextCollectionName —
//      the expansion validates existence, so a dangling path resolves to
//      an empty string.
//
// The per-index memo lives in ItemWidgetFactory (cleared by the
// setCollections / setCollectionContext setters); this function is the
// memoized computation.
[[nodiscard]] QString resolvePlaceholderArtwork(const QList<CollectionConfig> *collections,
                                                int collectionIndex,
                                                const QString &contextPlaceholder,
                                                const QString &contextCollectionName);

// Pure precedence policy for a SUBCOLLECTION TILE's artwork (Kartend-kb2vx),
// extracted for the same reason as the placeholder policy above — it is a
// decision, not a widget operation, and deserves testing without a widget
// graph. Two sources, in order:
//
//   1. The CHILD's own `collectionIcon`, resolved through
//      CollectionUtils::resolvedCollectionIcon (trim + `~` / `%collection%`
//      expansion). An explicit per-collection choice, so it always wins.
//   2. Otherwise an image named after the child in the PARENT's artwork
//      directory (@p parentArtworkDirectory) — the convention that predates
//      the collectionIcon key, resolved with the same name matching per-item
//      artwork uses.
//
// Empty when neither resolves; the caller then falls back to placeholder
// artwork. Grid and List were doing (2) only, which is why setting
// collectionIcon on a subcollection appeared to do nothing there.
//
// All three consumers of the key (this, CoverFlowController::buildCard,
// MarqueeController) resolve through the same shared seam, so an icon written
// as `~/icons/films.png` renders in every layout or none (Kartend-dkh90).
//
// @p subcollectionName is the child's display name; empty input yields an
// empty result rather than probing the directory for "".
[[nodiscard]] QString resolveSubcollectionTileArtwork(const QList<CollectionConfig> *collections,
                                                      int subcollectionIndex,
                                                      const QString &subcollectionName,
                                                      const QString &parentArtworkDirectory);

} // namespace ItemWidgetFactoryHelpers

/**
 * @brief Factory for creating and configuring ItemWidget instances.
 *
 * Handles widget acquisition from pool, configuration based on collection
 * settings, and artwork loading setup. Separates widget lifecycle management
 * from scrolling logic.
 */
class ItemWidgetFactory : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ItemWidgetFactory)
public:
  explicit ItemWidgetFactory(QObject *parent = nullptr);
  ~ItemWidgetFactory() override = default;

  // Dependencies
  void setWidgetPool(WidgetPoolManager *pool) { m_widgetPool = pool; }
  // Kartend-davi: instead of caching IArtworkManager / IDatabaseManager
  // pointers as fields, the factory now reads them through the app
  // context. Caller wires the context once; ApplicationContext::managers
  // stays authoritative.
  void setApplicationContext(const ApplicationContext *ctx) { m_ctx = ctx; }
  void setParentWidget(QWidget *parent) { m_parentWidget = parent; }

  // Context for widget creation
  void setCollectionContext(const CollectionContext &context) {
    m_context = context;
    // Placeholder resolution reads the context (config fallback + collection
    // name for variable expansion) — drop the memo when the context changes.
    m_placeholderArtworkCache.clear();
  }
  void setMetrics(int itemWidth, int itemHeight);

  // Collections list for looking up collection names
  void setCollections(const QList<CollectionConfig> *collections) {
    m_collections = collections;
    m_placeholderArtworkCache.clear();
  }

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
  void clearPendingRangeRequests() {
    m_pendingRangeRequests.clear();
    m_emptyRangeAttempts.clear();
  }

  /**
   * @brief Clears a single pending range request.
   * @param startIndex The chunk start index that was requested.
   *
   * Called when the chunk fills with rows — also resets its empty-response
   * retry budget so a later transient emptiness gets the full allowance again.
   */
  void clearPendingRangeRequest(int startIndex) {
    m_pendingRangeRequests.remove(startIndex);
    m_emptyRangeAttempts.remove(startIndex);
  }

  /**
   * @brief Record that the chunk at @p startIndex came back with zero rows.
   *
   * A legitimately-empty chunk previously stayed pending forever, so prefetch /
   * createMediaWidget never re-requested it and its slots showed "Loading..."
   * until a collection switch (Kartend-ejsf). Allow a few re-requests — a
   * transient count/filter over-report resolves quickly — then stop (leave it
   * pending) so a persistently-empty chunk can't spin a tight request loop.
   */
  void onEmptyRangeResponse(int startIndex) {
    if (++m_emptyRangeAttempts[startIndex] < kMaxEmptyRangeAttempts) {
      m_pendingRangeRequests.remove(startIndex); // permit one more re-request
    }
  }

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

  [[nodiscard]] IArtworkManager *artworkMgr() const;
  [[nodiscard]] IDatabaseManager *dbMgr() const;

  WidgetPoolManager *m_widgetPool = nullptr;
  const ApplicationContext *m_ctx = nullptr;
  QWidget *m_parentWidget = nullptr;

  CollectionContext m_context;
  int m_itemWidth = 0;
  int m_itemHeight = 0;

  SubcollectionNameResolver m_subcollectionNameResolver;
  const QStringList *m_filePaths = nullptr;
  const QHash<QString, QString> *m_fileNames = nullptr;
  QHash<QString, QString> m_cachedArtworkPaths; // fullPath -> artworkPath from session cache
  // Memoized resolvePlaceholderArtworkForCollection results keyed by
  // collection index. The resolution walks the parent chain and expands
  // config variables, and ran once per widget materialization for a value
  // that only changes with the collections list or the active context —
  // both of which clear this cache (see the setters above).
  mutable QHash<int, QString> m_placeholderArtworkCache;
  QSet<int> m_pendingRangeRequests; // Tracks chunk start indices with pending
                                    // requests
  // Per-chunk count of consecutive empty (zero-row) responses. Bounds the
  // re-requests of a chunk that keeps coming back empty so it can't spin a
  // tight request loop; reset when the chunk fills or on a bulk clear
  // (Kartend-ejsf).
  QHash<int, int> m_emptyRangeAttempts;
  static constexpr int kMaxEmptyRangeAttempts = 3;
  int m_totalItemCount = 0;                               // Total items for adaptive chunk sizing
  const QList<CollectionConfig> *m_collections = nullptr; // Collection list for name lookup
  int m_collectionColumnWidth = 150;                      // Collection column width for list mode
  int m_artworkColumnWidth = 32;                          // Artwork column width for list mode

  [[nodiscard]] int computeChunkSize() const;
  void prefetchAdjacentChunks(int currentMediaIndex, int chunkSize);
};

#endif // ITEMWIDGETFACTORY_H
