#ifndef SCROLLMANAGER_H
#define SCROLLMANAGER_H

#include "collectionutils.h"
#include "gridlayoutcalculator.h"
#include "setuputils.h"
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QTimer>

class QWidget;
class QScrollArea;
class QScrollBar;
class ItemWidget;
class DatabaseManager;
class QPropertyAnimation;
class ArtworkManager;
class WidgetPoolManager;
class FilterManager;
class SelectionOverlayManager;
class VirtualContainerManager;
class SelectionCoordinator;
class ScrollEventHandler;
class ItemWidgetFactory;
class InteractionStateHolder;
class ArrowKeyScrollHelper;

namespace TimerUtils {
class DebouncedTimer;
}

// Alias for backward compatibility - use GridMetrics from GridLayoutCalculator
using VirtualMetrics = GridMetrics;

struct ApplicationContext;

/**
 * @brief Setup struct for ScrollManager dependencies.
 */
struct ScrollManagerSetup {
  const ApplicationContext *ctx = nullptr;
  QWidget *gridContainer = nullptr;
  QScrollArea *mediaScrollArea = nullptr;
  ArtworkManager *artworkManager = nullptr;
  const QList<CollectionConfig> *collections = nullptr;
  const CollectionHierarchyCache *hierarchyCache = nullptr;
  
  SETUP_GETTER_DECL(QWidget*, GridContainer)
  SETUP_GETTER_DECL(QScrollArea*, MediaScrollArea)
  SETUP_GETTER_DECL(ArtworkManager*, ArtworkManager)
  SETUP_GETTER_DECL(const QList<CollectionConfig>*, Collections)
  SETUP_GETTER_DECL(const CollectionHierarchyCache*, HierarchyCache)
  SETUP_GETTER_DECL_CTX_ONLY(InteractionStateHolder*, InteractionState)
};

class ScrollManager : public QObject {
  Q_OBJECT
public:
  ScrollManager(QObject *parent = nullptr);
  ~ScrollManager() override;
  void setupReferences(const ScrollManagerSetup &setup);
  void setupVirtualScrolling(int totalCount, const CollectionContext &context);
  void receiveItemsRange(int offset, const QStringList &filePaths, const QHash<QString, QString> &fileNames);
  void cleanup();
  void updateGridWidth(int newGridWidth);
  void updateVirtualView();
  [[nodiscard]] int getEffectiveHorizontalSpacing() const;
  [[nodiscard]] int getFirstVisibleRow() const;
  [[nodiscard]] int getLastVisibleRow() const;
  void updateSelectionForIndex(int selectedIndex);
  void refreshSelectionOverlayState();
  void setForceSelectionOverlayVisible(bool force);
  [[nodiscard]] QString getSubcollectionName(int subcollectionIndex) const;
  void setDatabaseManager(DatabaseManager *manager);
  void recenterVirtualContainer();
  [[nodiscard]] HorizontalAlignment getCurrentAlignment() const;
  void applyFilter(const QString &searchText);
  void cleanupActiveWidgets();
  void clearFilter();
  [[nodiscard]] int getFilteredIndex(int visualIndex) const;
  [[nodiscard]] int getScrollbarWidth() const;
  [[nodiscard]] bool willNeedVerticalScrollbar() const;
  [[nodiscard]] int getTotalItems() const;
  void enforceScrollContentConstraints();
  void recreateLayout();
  void centerHorizontalScrollbar(int currentCollectionIndex,
                                 const QList<CollectionConfig> &collections);
  void handleLayoutChange();
  void notifyUserActivity();
  [[nodiscard]] int getCurrentGridWidth() const;
  void updateContextForSubcollection(int subcollectionIndex);
  void applySubcollectionFilter(int subcollectionIndex);
  [[nodiscard]] int getEffectiveViewportWidth() const;
  void recalculateContainerMetrics();
  void forceVirtualViewUpdate();
  void preCalculateLayout();
  [[nodiscard]] const QHash<int, ItemWidget *> &getActiveWidgets() const {
    return m_activeWidgets;
  }
  [[nodiscard]] const QStringList &getFilePaths() const { return m_filePaths; }
  [[nodiscard]] const QHash<QString, QString> &getFileNames() const { return m_fileNames; }
  [[nodiscard]] QString filePathForVisualIndex(int visualIndex) const;
  void primeLayoutFor(const CollectionConfig &config);

signals:
  void subcollectionEntered(int subcollectionIndex);
  void virtualFolderEntered(const QString &folderPath);
  void requestItemsRange(int offset, int limit);
  void virtualScrollSetupComplete();
  void filterChanged(int visibleItems, int totalOriginal);

private slots:
  void onScrollChanged();
  void onThrottledUpdate();
  void onSubcollectionDoubleClicked(int subcollectionIndex);
  void onVirtualFolderDoubleClicked(const QString &folderPath);
  void onArrowKeyViewUpdate();

private:
  void createVirtualContainer();
  void positionVirtualContainer();
  void cleanupVirtualContainer();
  void calculateVirtualMetrics();
  void connectScrollEvents();
  void disconnectScrollEvents();
  void ensureWidgetForIndex(int visualIndex);
  [[nodiscard]] QPoint getItemPosition(int index) const;

  // Widget pool manager for recycling ItemWidgets
  WidgetPoolManager *m_widgetPool = nullptr;
  ItemWidget *acquireWidget();
  void releaseWidget(ItemWidget *widget);

  // Filter manager for search and subcollection filtering
  FilterManager *m_filterManager = nullptr;

  // Selection overlay manager for glide animation
  SelectionOverlayManager *m_overlayManager = nullptr;

  // Virtual container manager for container lifecycle
  VirtualContainerManager *m_containerManager = nullptr;

  // Selection coordinator for selection state and movement analysis
  SelectionCoordinator *m_selectionCoordinator = nullptr;

  // Scroll event handler for scroll event wiring
  ScrollEventHandler *m_scrollEventHandler = nullptr;

  // Item widget factory for creating and configuring widgets
  ItemWidgetFactory *m_widgetFactory = nullptr;

  // Arrow key scroll helper for centering animation
  ArrowKeyScrollHelper *m_arrowKeyScrollHelper = nullptr;

public:
  [[nodiscard]] const WidgetPoolManager *getWidgetPool() const { return m_widgetPool; }
  [[nodiscard]] const FilterManager *getFilterManager() const { return m_filterManager; }
  [[nodiscard]] const SelectionOverlayManager *getOverlayManager() const { return m_overlayManager; }
  [[nodiscard]] const VirtualContainerManager *getContainerManager() const { return m_containerManager; }
  [[nodiscard]] const SelectionCoordinator *getSelectionCoordinator() const { return m_selectionCoordinator; }
  [[nodiscard]] const ScrollEventHandler *getScrollEventHandler() const { return m_scrollEventHandler; }

private:

  InteractionStateHolder *m_state = nullptr;
  QWidget *m_gridContainer = nullptr;
  QScrollArea *m_mediaScrollArea = nullptr;
  ArtworkManager *m_artworkManager = nullptr;
  const CollectionHierarchyCache *m_hierarchyCache = nullptr;
  QWidget *m_virtualContainer = nullptr;
  QStringList m_filePaths;
  QHash<QString, QString> m_fileNames;
  QHash<int, ItemWidget *> m_activeWidgets;
  QList<int> m_subcollections;
  QStringList m_virtualFolders;  // Subfolder paths for virtual folder navigation
  const QList<CollectionConfig> *m_collections = nullptr;
  CollectionContext m_context;
  VirtualMetrics m_metrics;
  QTimer *m_scrollTimer = nullptr;  // Throttle timer (not debounce)
  QTimer *m_arrowKeyViewUpdateTimer = nullptr;
  int m_totalItems = 0;
  qint64 m_lastScrollTime = 0;
  bool m_isMutating = false;
  QHash<QString, QString> m_filePathToDisplayName;
  DatabaseManager *m_databaseManager = nullptr;
  bool m_destroying = false;
  int m_lastSelectedIndex = -1;
  int m_lastSelectedRow = -1;
  int m_selectionDirection = 0;
  TimerUtils::DebouncedTimer *m_userScrollIdleTimer = nullptr;
  int m_committedSelectedIndex = -1;

  // Helper methods to reduce cognitive complexity
  [[nodiscard]] QSet<int> calculateNeededIndices() const;
  void removeUnneededWidgets(const QSet<int> &needed);
  void updateArtworkIfAllowed();

  void initializeSubcollections();
  void initializeVirtualFolders();
  void setupFilePathMappings();
  void setupEmptyVirtualScrolling();
  void setupNormalVirtualScrolling();

  void handleProgrammaticScroll();
  void handleUserScroll();
  void setupScrollSuppression();
  void finalizeScrollChanges();

  void calculateMovementDirection(int selectedIndex, int prevIndex,
                                  int itemsPerRow, bool &isHorizontalMove);
  [[nodiscard]] QRect selectionOverlayRectForIndex(int visualIndex) const;
  void handleHorizontalMoveAnimation(int selectedIndex, int prevIndex);
  void handleDirectSelectionUpdate(int selectedIndex);
  void prewarmSurroundingWidgets(int selectedIndex);
  void scheduleArrowKeyUpdate(int selectedIndex);

  void rebuildFilteredView();
};

#endif