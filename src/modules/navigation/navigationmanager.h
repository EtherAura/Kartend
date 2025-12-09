#ifndef NAVIGATIONMANAGER_H
#define NAVIGATIONMANAGER_H

#include "applicationcontext.h"
#include "collectionutils.h"
#include "errorutils.h"
#include "setuputils.h"
#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QMenuBar;
class QScrollArea;
class QStackedWidget;
QT_END_NAMESPACE

class InteractionManager;
class InteractionStateHolder;
class SettingsManager;
class SidebarManager;
class ScrollManager;
class DatabaseManager;
class SessionManager;
class ArtworkManager;
class MetadataSidebar;
class SelectionRestoreManager;
class LoadingOverlay;
class NavigationStackManager;

/**
 * @brief Setup struct for NavigationManager dependencies.
 * 
 * Fields can be set individually, or common fields can be populated
 * from an ApplicationContext via the ctx pointer.
 */
struct NavigationManagerSetup {
  // Optional: shared context for common fields
  const ApplicationContext *ctx = nullptr;
  
  // Manager dependencies (can be overridden or taken from ctx)
  InteractionManager *interactionManager = nullptr;
  SettingsManager *settingsManager = nullptr;
  SidebarManager *sidebarManager = nullptr;
  ScrollManager *scrollManager = nullptr;
  DatabaseManager *databaseManager = nullptr;
  SessionManager *sessionManager = nullptr;
  ArtworkManager *artworkManager = nullptr;
  
  // UI elements (can be overridden or taken from ctx)
  MetadataSidebar *sidebar = nullptr;
  int *currentCollectionIndex = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  const CollectionHierarchyCache *hierarchyCache = nullptr;
  GeneralSettings *generalSettings = nullptr;
  QLineEdit *searchBar = nullptr;
  QWidget *itemsPage = nullptr;
  QWidget *itemsTopBar = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QMenuBar *menubar = nullptr;
  QLabel *loadingLabel = nullptr;
  LoadingOverlay *loadingOverlay = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QWidget *gridContainer = nullptr;
  
  // Callbacks (not in context)
  std::function<bool()> isShuttingDown;
  std::function<void()> refreshTitleCounts;
  
  // Manager accessors that check ctx fallback
  SETUP_GETTER_INLINE_SAME(InteractionManager*, InteractionManager, interactionManager)
  SETUP_GETTER_INLINE_SAME(SettingsManager*, SettingsManager, settingsManager)
  SETUP_GETTER_INLINE_SAME(SidebarManager*, SidebarManager, sidebarManager)
  SETUP_GETTER_INLINE_SAME(ScrollManager*, ScrollManager, scrollManager)
  SETUP_GETTER_INLINE_SAME(DatabaseManager*, DatabaseManager, databaseManager)
  SETUP_GETTER_INLINE_SAME(SessionManager*, SessionManager, sessionManager)
  SETUP_GETTER_INLINE_SAME(ArtworkManager*, ArtworkManager, artworkManager)

  // UI element accessors that check ctx fallback
  SETUP_GETTER_INLINE_SAME(QScrollArea*, ItemScrollArea, itemScrollArea)
  SETUP_GETTER_INLINE_SAME(QWidget*, GridContainer, gridContainer)
  SETUP_GETTER_INLINE_SAME(QWidget*, ItemsPage, itemsPage)
  SETUP_GETTER_INLINE_SAME(QWidget*, ItemsTopBar, itemsTopBar)
  SETUP_GETTER_INLINE_SAME(QStackedWidget*, StackedWidget, stackedWidget)
  SETUP_GETTER_INLINE_SAME(QMenuBar*, Menubar, menubar)
  SETUP_GETTER_INLINE_SAME(QLineEdit*, SearchBar, searchBar)
  SETUP_GETTER_INLINE_SAME(QLabel*, LoadingLabel, loadingLabel)
  SETUP_GETTER_INLINE_SAME(LoadingOverlay*, LoadingOverlay, loadingOverlay)
  SETUP_GETTER_INLINE_SAME(MetadataSidebar*, Sidebar, sidebar)
  SETUP_GETTER_INLINE_SAME(QList<CollectionConfig>*, Collections, collections)
  SETUP_GETTER_INLINE_SAME(int*, CurrentCollectionIndex, currentCollectionIndex)
  SETUP_GETTER_INLINE_SAME(const CollectionHierarchyCache*, HierarchyCache, hierarchyCache)
  SETUP_GETTER_INLINE_SAME(GeneralSettings*, GeneralSettings, generalSettings)
  SETUP_GETTER_INLINE_CTX_ONLY(InteractionStateHolder*, InteractionState, interactionState)
};

class NavigationManager : public QObject {
  Q_OBJECT
public:
  explicit NavigationManager(QObject *parent = nullptr);
  ~NavigationManager() override;
  [[nodiscard]] bool isNavigationInProgress() const;

  // Navigation stack manager for hierarchy traversal
  [[nodiscard]] NavigationStackManager *stackManager() const { return m_stackManager.get(); }

private:
  std::unique_ptr<NavigationStackManager> m_stackManager;

public:

  void restoreSelectionForCurrentCollection();
  void setupReferences(const NavigationManagerSetup &setup);

public slots:
  auto showCollectionItems(int collectionIndex) -> bool;
  void navigateWithSharedItems(int collectionIndex);
  void safeReloadCollection(int collectionIndex);
  void onCollectionSelected(int collectionIndex);
  void onSubcollectionEntered(int subcollectionIndex);
  void onVirtualFolderEntered(const QString &folderPath);
  void goBackFromVirtualFolder();
  void loadCurrentAndSubcollections();
  void loadAllCollectionsView();
  void goBackToCollections();
  void filterItems(const QString &searchText);
  auto scheduleSelectionRestore(int desiredIndex, int maxAttempts,
                                int attemptDelayMs,
                                int finalEnsureDelayMs) -> void;
  void onItemsLoaded(const QStringList &filePaths,
                     const QHash<QString, QString> &fileNames);
  void onItemCountLoaded(int count);
  void onItemsRangeLoaded(int offset, const QStringList &filePaths, const QHash<QString, QString> &fileNames);
  void fetchItemsRange(int offset, int limit);
  void onMediaLibraryError(const ErrorUtils::ErrorContext &error);
  void onViewportChanged();
  
  // Appearance methods - can be called from SettingsManager after dialog closes
  void applyBackgroundForCollection(int collectionIndex);
  void applyPrimaryColorForCollection(int collectionIndex);

private:
  // Helper methods for navigateWithSharedItems
  auto initializeNavigationState() -> void;
  [[nodiscard]] auto validateAndPrepareNavigation(int collectionIndex) -> bool;
  auto handleSubcollectionNavigation(int collectionIndex, int previousIndex)
      -> void;
  auto handleRegularNavigation(int collectionIndex) -> void;
  auto finalizeNavigation(int collectionIndex) -> void;

  [[nodiscard]] auto areItemsShared(int fromIndex, int toIndex) const -> bool;
  auto applyCollectionSettingsOnly(int collectionIndex) -> void;
  [[nodiscard]] auto getSubcollections(int parentIndex) const -> QList<int>;
  [[nodiscard]] auto getAllDescendantCollections(int parentIndex) const -> QList<int>;

  InteractionManager *m_interactionManager = nullptr;
  InteractionStateHolder *m_state = nullptr;
  SettingsManager *m_settingsManager = nullptr;
  SidebarManager *m_sidebarManager = nullptr;
  ScrollManager *m_scrollManager = nullptr;
  DatabaseManager *m_databaseManager = nullptr;
  SessionManager *m_sessionManager = nullptr;
  ArtworkManager *m_artworkManager = nullptr;
  MetadataSidebar *m_MetadataSidebar = nullptr;
  int *m_currentCollectionIndex = nullptr;
  const CollectionHierarchyCache *m_hierarchyCache = nullptr;
  GeneralSettings *m_generalSettings = nullptr;
  QLineEdit *m_searchBar = nullptr;
  QWidget *m_itemsPage = nullptr;
  QWidget *m_itemsTopBar = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  QMenuBar *m_menubar = nullptr;
  QLabel *m_loadingLabel = nullptr;
  LoadingOverlay *m_loadingOverlay = nullptr;
  QScrollArea *m_itemScrollArea = nullptr;
  QWidget *m_gridContainer = nullptr;
  std::function<bool()> m_isShuttingDown;
  std::function<void()> m_refreshTitleCounts;

  // Owned manager for selection restore logic
  std::unique_ptr<SelectionRestoreManager> m_selectionRestoreManager;

  bool m_virtualScrollConnected = false;
  [[nodiscard]] auto collectionHasDescendantWithMedia(int parentIndex) const -> bool;
  bool m_allCollectionsActive = false;
  auto setSuppressArrowCenter(QScrollArea *scrollArea, int settleMs) -> void;
  [[nodiscard]] auto getHasSubAndItems(int collectionIndex, bool &hasSub,
                         bool &hasItems) const -> bool;
  auto updateItemsPageTitle(int collectionIndex) -> void;
  // Helper methods for goBackToCollections refactoring
  auto performNavigationStackCleanup() -> void;
  auto handleNavigationStackPop() -> void;
  auto handleNavigationFallback() -> void;
  [[nodiscard]] auto findSubcollectionVisualIndex(int targetCollectionIndex,
                                    int previousIndex) const -> int;
  auto scheduleNavigationReturn(int targetCollectionIndex,
                                int subcollectionVisualIndex) -> void;
  // Helper methods for onItemsLoaded refactoring
  [[nodiscard]] auto validateItemsLoadedContext() const -> bool;
  auto cleanupExistingNoItemsWidgets() -> void;
  [[nodiscard]] auto determineContentAvailability(const QStringList &filePaths,
                                    const QList<int> &subcollections) const
      -> bool;
  auto handleEmptyContent() -> void;
  [[nodiscard]] auto setupCollectionContext(const QStringList &filePaths,
                              const QHash<QString, QString> &fileNames) const
      -> CollectionContext;
  [[nodiscard]] auto calculateSelectionIndex(int totalItems) const -> int;
  [[nodiscard]] auto computeCollectionDepth(int collectionIndex) const -> int;
  auto schedulePostLoadOperations() -> void;
  // Helper methods for showCollectionItems refactoring
  [[nodiscard]] auto validateCollectionIndex(int collectionIndex) const -> bool;
  [[nodiscard]] auto handleSharedItemsNavigation(int collectionIndex) -> bool;
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