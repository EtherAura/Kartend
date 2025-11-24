#ifndef NAVIGATIONMANAGER_H
#define NAVIGATIONMANAGER_H

#include "collectionconfig.h"
#include <QHash>
#include <QList>
#include <QObject>
#include <QScrollArea>
#include <QStringList>
#include <functional>

class InteractionManager;
class SettingsManager;
class SidebarManager;
class ScrollManager;
class DatabaseManager;
class SessionManager;
class metadataSidebar;
class QLineEdit;
class QStackedWidget;
class QLabel;

struct NavigationManagerDependencies {
  InteractionManager *interactionManager = nullptr;
  SettingsManager *settingsManager = nullptr;
  SidebarManager *sidebarManager = nullptr;
  ScrollManager *scrollManager = nullptr;
  DatabaseManager *databaseManager = nullptr;
  SessionManager *sessionManager = nullptr;
  metadataSidebar *metadataSidebar = nullptr;
  int *currentCollectionIndex = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  GeneralSettings *generalSettings = nullptr;
  QLineEdit *searchBar = nullptr;
  QWidget *itemsPage = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QLabel *loadingLabel = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QWidget *gridContainer = nullptr;
  std::function<bool()> isShuttingDown;
  std::function<void()> refreshTitleCounts;
};

class NavigationManager : public QObject {
  Q_OBJECT
public:
  explicit NavigationManager(QObject *parent = nullptr);
  ~NavigationManager() override;
  bool m_navigationInProgress = false;

  QList<int> m_navigationStack;
  int m_navigationDepth = 0;

  void restoreSelectionForCurrentCollection();
  void setupReferences(const NavigationManagerDependencies &deps);

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
                                int finalEnsureDelayMs) -> void;
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

  InteractionManager *m_interactionManager = nullptr;
  SettingsManager *m_settingsManager = nullptr;
  SidebarManager *m_sidebarManager = nullptr;
  ScrollManager *m_scrollManager = nullptr;
  DatabaseManager *m_databaseManager = nullptr;
  SessionManager *m_sessionManager = nullptr;
  metadataSidebar *m_metadataSidebar = nullptr;
  int *m_currentCollectionIndex = nullptr;
  GeneralSettings *m_generalSettings = nullptr;
  QLineEdit *m_searchBar = nullptr;
  QWidget *m_itemsPage = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  QLabel *m_loadingLabel = nullptr;
  QScrollArea *m_itemScrollArea = nullptr;
  QWidget *m_gridContainer = nullptr;
  std::function<bool()> m_isShuttingDown;
  std::function<void()> m_refreshTitleCounts;

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
                                    int token) -> std::function<void()>;
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

  void persistCurrentSelection();
  void prepareForNonSharedNavigationHelper();
  void suspendItemsPageRendering();
  void resumeItemsPageRendering();
  void applyUiPoliciesForCollection(int collectionIndex);

  QList<CollectionConfig> *m_collections = nullptr;
};

#endif