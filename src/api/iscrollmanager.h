#ifndef ISCROLLMANAGER_H
#define ISCROLLMANAGER_H

#include "collection/collectioncontext.h"
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class ItemWidget;
struct GridMetrics;

/**
 * @brief Sibling-facing interface to the virtual-scrolling / grid layer.
 *
 * Role interface (interface-segregation): the slice sibling managers reach
 * for through ApplicationContext, not ScrollManager's full surface (its
 * helper-manager wiring, private layout internals and the bulk of its
 * signal/slot set stay on the concrete class, which the owner — MainWindow —
 * holds directly).
 *
 * Unlike INavigationManager / IDetailsPaneManager (plain abstract classes),
 * this interface is a QObject — the same shape as IDatabaseManager. A
 * sibling (SelectionRestoreCoordinator) connects to ScrollManager's
 * virtualScrollSetupComplete signal through ctx, so that one signal is
 * promoted to the interface; QObject + Q_OBJECT is required to declare it.
 * ScrollManager keeps its remaining signals/slots on the concrete class and
 * derives this interface directly (single QObject base).
 *
 * setArtworkPreviewGallery() is intentionally absent: its parameter type
 * (ArtworkPreviewOverlay::GalleryEntry) is a nested type from a ui/ header
 * and cannot reach this neutral api/ header. Its sole caller obtains the
 * concrete ScrollManager for that one call.
 */
class IScrollManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(IScrollManager)
public:
  using QObject::QObject;
  ~IScrollManager() override = default;

  virtual void setupVirtualScrolling(int totalCount, const CollectionContext &context) = 0;
  virtual void updateMediaItemCount(int mediaItemCount) = 0;
  virtual void receiveItemsRange(int offset, const QStringList &filePaths,
                                 const QHash<QString, QString> &fileNames,
                                 const QHash<QString, QString> &fileToArtworkDir) = 0;
  virtual void cleanup() = 0;
  virtual void updateGridWidth(int newGridWidth) = 0;
  virtual void updateHorizontalGridHeight(int newHorizontalGridHeight) = 0;
  virtual void setSidebarShrinkingActive(bool active) = 0;
  [[nodiscard]] virtual bool sidebarShrinkingActive() const = 0;
  virtual void updateViewType(ViewType viewType) = 0;
  virtual void updateVirtualView() = 0;
  [[nodiscard]] virtual int getEffectiveHorizontalSpacing() const = 0;
  [[nodiscard]] virtual QString getSubcollectionName(int subcollectionIndex) const = 0;
  virtual void updateSelectionForIndex(int selectedIndex) = 0;
  virtual void refreshSelectionOverlayState() = 0;
  virtual void setForceSelectionOverlayVisible(bool force) = 0;
  virtual void applyFilter(const QString &searchText) = 0;
  virtual void cleanupActiveWidgets() = 0;
  virtual void clearFilter() = 0;
  virtual void showSearchLoadingOverlay() = 0;
  virtual void hideSearchLoadingOverlay() = 0;
  virtual void savePreSearchState() = 0;
  virtual void restorePreSearchState() = 0;
  [[nodiscard]] virtual bool hasPreSearchState() const = 0;
  [[nodiscard]] virtual int getFilteredIndex(int visualIndex) const = 0;
  [[nodiscard]] virtual int getTotalItems() const = 0;
  [[nodiscard]] virtual const GridMetrics &getMetrics() const = 0;
  virtual void recreateLayout() = 0;

  virtual void setPendingSelectionRestoreByPath(const QString &filePath) = 0;
  [[nodiscard]] virtual bool hasPendingSelectionRestoreByPath() const = 0;

  [[nodiscard]] virtual bool isArtworkPreviewVisible() const = 0;
  virtual bool hideArtworkPreview() = 0;
  virtual bool showMediaPreview(const QString &filePath, const QString &artworkDir,
                                const QString &videoDir) = 0;

  virtual void centerHorizontalScrollbar(int currentCollectionIndex,
                                         const QList<CollectionConfig> &collections) = 0;
  virtual void recenterVirtualContainer() = 0;
  virtual void handleLayoutChange() = 0;
  [[nodiscard]] virtual int getCurrentGridWidth() const = 0;
  virtual void updateContextForSubcollection(int subcollectionIndex) = 0;
  virtual void applySubcollectionFilter(int subcollectionIndex) = 0;
  virtual void recalculateContainerMetrics() = 0;
  virtual void forceVirtualViewUpdate() = 0;
  virtual void preCalculateLayout() = 0;
  [[nodiscard]] virtual const QHash<int, ItemWidget *> &getActiveWidgets() const = 0;
  virtual void injectCachedItems(int startIndex, const QStringList &filePaths,
                                 const QHash<QString, QString> &fileNames,
                                 const QHash<QString, QString> &artworkPaths = {}) = 0;
  [[nodiscard]] virtual bool
  getCurrentViewportForCache(int &startIndex, int &totalItems, QStringList &filePaths,
                             QHash<QString, QString> &fileNames,
                             QHash<QString, QString> &artworkPaths) const = 0;
  [[nodiscard]] virtual const QStringList &getFilePaths() const = 0;
  [[nodiscard]] virtual const QHash<QString, QString> &getFileNames() const = 0;
  [[nodiscard]] virtual int getSubcollectionCount() const = 0;
  [[nodiscard]] virtual int getVirtualFolderCount() const = 0;
  [[nodiscard]] virtual QString filePathForVisualIndex(int visualIndex) const = 0;
  [[nodiscard]] virtual QString virtualFolderPathForVisualIndex(int visualIndex) const = 0;
  virtual void primeLayoutFor(const CollectionConfig &config) = 0;
  virtual void setInitialScrollIndex(int index) = 0;

  [[nodiscard]] virtual int subcollectionIndexFromActual(int actualIndex) const = 0;

signals:
  void virtualScrollSetupComplete();
};

#endif // ISCROLLMANAGER_H
