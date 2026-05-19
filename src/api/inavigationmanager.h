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

  virtual void scheduleSelectionRestore(int desiredIndex, int maxAttempts, int attemptDelayMs,
                                        int finalEnsureDelayMs) = 0;
};

#endif // INAVIGATIONMANAGER_H
