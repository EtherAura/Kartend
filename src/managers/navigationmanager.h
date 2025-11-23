#ifndef NAVIGATIONMANAGER_H
#define NAVIGATIONMANAGER_H

#include "collectionconfig.h"
#include <QHash>
#include <QList>
#include <QObject>
#include <QScrollArea>
#include <QStringList>
#include <functional>

class MainWindow;

class NavigationManager : public QObject {
  Q_OBJECT
public:
  explicit NavigationManager(MainWindow *mainWindow);
  ~NavigationManager() override;
  bool m_navigationInProgress = false;

  QList<int> m_navigationStack;
  int m_navigationDepth = 0;

  void restoreSelectionForCurrentCollection();
  void setCollections(QList<CollectionConfig> *collections);

public slots:
  auto showCollectionItems(int collectionIndex) -> bool;
  void navigateWithSharedItems(int collectionIndex);
  void safeReloadCollection(int collectionIndex);
  void onCollectionSelected(int collectionIndex);
  void onSubcollectionEntered(int subcollectionIndex);
  void loadCurrentAndSubcollections();
  void loadAllCollectionsView();
  void goBackToCollections();
  void filterItems(const QString &searchText);
  auto scheduleSelectionRestore(int desiredIndex, int maxAttempts,
                                int attemptDelayMs,
                                int finalEnsureDelayMs) const -> void;
  void onItemsLoaded(const QStringList &filePaths,
                     const QHash<QString, QString> &fileNames);
  void onMediaLibraryError(const QString &error);
  void onViewportChanged();

private:
  // Helper methods for navigateWithSharedItems
  auto initializeNavigationState() -> void;
  auto validateAndPrepareNavigation(int collectionIndex) -> bool;
  auto handleSubcollectionNavigation(int collectionIndex, int previousIndex)
      -> void;
  auto handleRegularNavigation(int collectionIndex) -> void;
  auto finalizeNavigation(int collectionIndex) -> void;

  auto areItemsShared(int fromIndex, int toIndex) const -> bool;
  auto applyCollectionSettingsOnly(int collectionIndex) -> void;
  auto getSubcollections(int parentIndex) const -> QList<int>;
  auto getAllDescendantCollections(int parentIndex) const -> QList<int>;

  MainWindow *m_mainWindow;
  bool m_virtualScrollConnected = false;
  auto collectionHasDescendantWithMedia(int parentIndex) const -> bool;
  bool m_allCollectionsActive = false;
  auto setSuppressArrowCenter(QScrollArea *scrollArea, int settleMs) -> void;
  auto getHasSubAndItems(int collectionIndex, bool &hasSub,
                         bool &hasItems) const -> bool;
  auto updateItemsPageTitle(int collectionIndex) -> void;
  // Helper methods for handleSubcollectionNavigation refactoring
  auto shouldRestoreSelection() const -> bool;
  auto getSelectionRestoreIndex(int collectionIndex) const -> int;
  auto createSelectionRestoreLambda(int collectionIndex, int selIdx,
                                    int token) const -> std::function<void()>;
  auto scheduleSelectionRestoreVerification(int collectionIndex, int selIdx,
                                            int token) -> void;
  // Helper methods for scheduleSelectionRestore refactoring
  auto validateSelectionRestoreContext() const -> bool;
  auto initializeSelectionRestoreToken() const -> int;
  auto createRestoreValidationLambda(int scheduledCollectionIndex,
                                     int token) const -> std::function<bool()>;
  auto executeSelectionRestore(int desiredIndex, int scheduledCollectionIndex,
                               int token) const -> void;
  // Helper methods for goBackToCollections refactoring
  auto performNavigationStackCleanup() -> void;
  auto handleNavigationStackPop() -> void;
  auto handleNavigationFallback() -> void;
  auto findSubcollectionVisualIndex(int targetCollectionIndex,
                                    int previousIndex) const -> int;
  auto scheduleNavigationReturn(int targetCollectionIndex,
                                int subcollectionVisualIndex) -> void;
  // Helper methods for onItemsLoaded refactoring
  auto validateItemsLoadedContext() const -> bool;
  auto cleanupExistingNoItemsWidgets() -> void;
  auto determineContentAvailability(const QStringList &filePaths,
                                    const QList<int> &subcollections) const
      -> bool;
  auto handleEmptyContent() -> void;
  auto setupCollectionContext(const QStringList &filePaths,
                              const QHash<QString, QString> &fileNames) const
      -> CollectionContext;
  auto calculateSelectionIndex(int totalItems) const -> int;
  auto computeCollectionDepth(int collectionIndex) const -> int;
  auto schedulePostLoadOperations() -> void;
  // Helper methods for showCollectionItems refactoring
  auto validateCollectionIndex(int collectionIndex) const -> bool;
  auto handleSharedItemsNavigation(int collectionIndex) -> bool;
  auto prepareNonSharedNavigation(int collectionIndex) -> void;
  auto loadCollectionData(int collectionIndex) -> void;
  QList<CollectionConfig> *m_collections = nullptr;
};

#endif