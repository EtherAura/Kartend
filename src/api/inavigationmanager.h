#ifndef INAVIGATIONMANAGER_H
#define INAVIGATIONMANAGER_H

#include <QString>

class NavigationStackManager;

/**
 * @brief Sibling-facing interface to collection navigation.
 *
 * Like IArtworkManager, this is a deliberate role interface
 * (interface-segregation): the slice sibling managers reach for through
 * ApplicationContext, not NavigationManager's full surface (its many
 * signal-driven slots and lifecycle wiring stay on the concrete class,
 * which the owner holds directly).
 *
 * Plain abstract class, not a QObject — NavigationManager derives QObject
 * directly; see its multiple-inheritance declaration. Add a method here
 * only when a sibling genuinely needs to call it via ctx.
 */
class INavigationManager {
public:
  virtual ~INavigationManager() = default;

  [[nodiscard]] virtual bool isInRootView() const = 0;
  [[nodiscard]] virtual NavigationStackManager *stackManager() const = 0;

  virtual bool showCollectionItems(int collectionIndex) = 0;
  virtual void safeReloadCollection(int collectionIndex) = 0;
  virtual void forceRescanCollection(int collectionIndex) = 0;
  virtual void loadRootView() = 0;
  virtual void onVirtualFolderEntered(const QString &folderPath) = 0;
  virtual void goBackFromVirtualFolder() = 0;

  virtual void applyBackgroundForCollection(int collectionIndex) = 0;
  virtual void applyPrimaryColorForCollection(int collectionIndex) = 0;

  virtual void filterItems(const QString &searchText) = 0;
  virtual void filterItemsCurrentAndSubcollections(const QString &searchText) = 0;
  virtual void filterItemsAllCollections(const QString &searchText) = 0;

  /// Kartend-8uoe1: drop any in-flight item-count request (bumps the request
  /// token so the stale-token guard discards the result) and forget the
  /// persisted query filter that produced it. Called by search-clear paths
  /// that restore a view directly instead of issuing a fresh count request —
  /// otherwise a count fired for the abandoned query lands after the restore
  /// and rebuilds the view as search results.
  virtual void invalidatePendingItemCount() = 0;

  // Kartend-plqg: maxAttempts + attemptDelayMs dropped — SelectionRestoreCoordinator
  // ignored them (Q_UNUSED on receipt) so the callers' kRestoreAttempts /
  // kRestoreIntervalMs constants were also dead.
  virtual void scheduleSelectionRestore(int desiredIndex, int finalEnsureDelayMs) = 0;
};

#endif // INAVIGATIONMANAGER_H
