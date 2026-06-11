#ifndef IVIRTUALSCROLLLIFECYCLE_H
#define IVIRTUALSCROLLLIFECYCLE_H

#include "collection/collectioncontext.h"
#include <QHash>
#include <QString>
#include <QStringList>

/**
 * @brief Virtual-scroll lifecycle role of the scroll layer (Kartend-h1l8f).
 *
 * One of the six role interfaces IScrollManager unions: view setup/teardown
 * and the data-delivery path that feeds it (range receipt, cache injection,
 * viewport snapshot for session caching). Matches the VirtualScrollEngine /
 * ScrollDataStore side of the implementation split.
 *
 * Plain abstract class, not a QObject — ScrollManager derives QObject once,
 * through IScrollManager. Reached via ctx->scrollLifecycle().
 */
class IVirtualScrollLifecycle {
public:
  virtual ~IVirtualScrollLifecycle() = default;

  virtual void setupVirtualScrolling(int totalCount, const CollectionContext &context) = 0;
  /// Update only the media-file portion of the data model (keeps
  /// subcollections and virtual folders intact) and recalculate container
  /// metrics without tearing down the view.
  virtual void updateMediaItemCount(int mediaItemCount) = 0;
  virtual void receiveItemsRange(int offset, const QStringList &filePaths,
                                 const QHash<QString, QString> &fileNames,
                                 const QHash<QString, QString> &fileToArtworkDir) = 0;
  virtual void cleanup() = 0;
  /// Inject cached items for instant startup display (bypasses database
  /// fetch).
  virtual void injectCachedItems(int startIndex, const QStringList &filePaths,
                                 const QHash<QString, QString> &fileNames,
                                 const QHash<QString, QString> &artworkPaths = {}) = 0;
  /// Get current viewport data for session caching (returns true if valid
  /// data).
  [[nodiscard]] virtual bool
  getCurrentViewportForCache(int &startIndex, int &totalItems, QStringList &filePaths,
                             QHash<QString, QString> &fileNames,
                             QHash<QString, QString> &artworkPaths) const = 0;
  /// Initial scroll index for pre-positioning before widget creation.
  virtual void setInitialScrollIndex(int index) = 0;
};

#endif // IVIRTUALSCROLLLIFECYCLE_H
