#ifndef SCROLLMANAGER_H
#define SCROLLMANAGER_H

#include "collectionutils.h"
#include "gridutils.h"
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
class MediaItemWidget;
class DatabaseManager;
class QPropertyAnimation;
class ArtworkManager;

struct VirtualMetrics {
  int itemWidth = 0;
  int itemHeight = 0;
  int itemsPerRow = 0;
  int horizontalSpacing = 0;
  int verticalSpacing = 0;
  int margins = 0;
  int totalWidth = 0;
  int totalHeight = 0;
  int actualGridWidth = 0;
  int totalRows = 0;
  bool isClipped = false;
  int overflowAmount = 0;
};

struct ScrollManagerSetup {
  QWidget *gridContainer = nullptr;
  QScrollArea *mediaScrollArea = nullptr;
  ArtworkManager *artworkManager = nullptr;
  const QList<CollectionConfig> *collections = nullptr;
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
  int getEffectiveHorizontalSpacing() const;
  int getFirstVisibleRow() const;
  int getLastVisibleRow() const;
  void updateSelectionForIndex(int selectedIndex);
  void refreshSelectionOverlayState();
  void setForceSelectionOverlayVisible(bool force);
  QString getSubcollectionName(int subcollectionIndex) const;
  void setDatabaseManager(DatabaseManager *manager);
  void recenterVirtualContainer();
  HorizontalAlignment getCurrentAlignment() const;
  void applyFilter(const QString &searchText);
  void cleanupActiveWidgets();
  void clearFilter();
  int getFilteredIndex(int visualIndex) const;
  int getScrollbarWidth() const;
  bool willNeedVerticalScrollbar() const;
  int getTotalItems() const;
  void enforceScrollContentConstraints();
  void recreateLayout();
  void centerHorizontalScrollbar(int currentCollectionIndex,
                                 const QList<CollectionConfig> &collections);
  void handleLayoutChange();
  void notifyUserActivity();
  int getCurrentGridWidth() const;
  void updateContextForSubcollection(int subcollectionIndex);
  void applySubcollectionFilter(int subcollectionIndex);
  int getEffectiveViewportWidth() const;
  void recalculateContainerMetrics();
  void forceVirtualViewUpdate();
  void preCalculateLayout();
  const QHash<int, MediaItemWidget *> &getActiveWidgets() const {
    return m_activeWidgets;
  }
  const QStringList &getFilePaths() const { return m_filePaths; }
  const QHash<QString, QString> &getFileNames() const { return m_fileNames; }
  QString filePathForVisualIndex(int visualIndex) const;
  void primeLayoutFor(const CollectionConfig &config);

signals:
  void widgetClicked(MediaItemWidget *widget, const QString &filePath);
  void widgetDoubleClickedWithCollection(const QString &filePath,
                                         int collectionIndex);
  void subcollectionEntered(int subcollectionIndex);
  void requestItemsRange(int offset, int limit);
  void virtualScrollSetupComplete();
  void filterChanged(int visibleItems, int totalOriginal);

private slots:
  void onScrollChanged();
  void onThrottledUpdate();
  void onSubcollectionDoubleClicked(int subcollectionIndex);
  void onArrowKeyViewUpdate();

private:
  void createVirtualContainer();
  void positionVirtualContainer();
  void cleanupVirtualContainer();
  void calculateVirtualMetrics();
  void connectScrollEvents();
  void disconnectScrollEvents();
  void ensureWidgetForIndex(int visualIndex);
  QPoint getItemPosition(int index) const;
  void rebuildFilteredIndices();

  // Widget pool for recycling MediaItemWidgets
  static constexpr int MAX_POOL_SIZE = 50;
  QList<MediaItemWidget *> m_widgetPool;
  MediaItemWidget *acquireWidget();
  void releaseWidget(MediaItemWidget *widget);
  void clearWidgetPool();

  QWidget *m_gridContainer = nullptr;
  QScrollArea *m_mediaScrollArea = nullptr;
  ArtworkManager *m_artworkManager = nullptr;
  QWidget *m_virtualContainer = nullptr;
  QStringList m_filePaths;
  QHash<QString, QString> m_fileNames;
  QHash<int, MediaItemWidget *> m_activeWidgets;
  QString m_currentFilter;
  bool m_isFiltered = false;
  QList<int> m_filteredIndices;
  QList<int> m_subcollections;
  const QList<CollectionConfig> *m_collections = nullptr;
  CollectionContext m_context;
  VirtualMetrics m_metrics;
  QTimer *m_scrollTimer = nullptr;
  QTimer *m_arrowKeyViewUpdateTimer = nullptr;
  int m_totalItems = 0;
  qint64 m_lastScrollTime = 0;
  bool m_isMutating = false;
  QHash<QString, QString> m_filePathToDisplayName;
  DatabaseManager *m_databaseManager = nullptr;
  bool m_destroying = false;
  QMetaObject::Connection m_vScrollConn;
  QMetaObject::Connection m_hScrollConn;
  int m_lastSelectedIndex = -1;
  int m_lastSelectedRow = -1;
  int m_selectionDirection = 0;
  bool m_userScrollbarActive = false;
  QTimer *m_userScrollIdleTimer = nullptr;
  int m_committedSelectedIndex = -1;
  bool m_forceSelectionOverlayVisible = false;
  bool m_restartingSelectionAnim = false;  // Prevents finish callback interference
  QHash<QString, QString> m_rawToFullPath;
  QString resolveToFullPath(const QString &raw) const;
  QPointer<QWidget> m_selectionOverlay;
  QPointer<QPropertyAnimation> m_selectionOverlayAnim;

  // Helper methods to reduce cognitive complexity
  QSet<int> calculateNeededIndices() const;
  void removeUnneededWidgets(const QSet<int> &needed);
  void updateArtworkIfAllowed();

  void initializeSubcollections();
  void setupFilePathMappings();
  void processRelativeFilePaths();
  void setupEmptyVirtualScrolling();
  void setupNormalVirtualScrolling();

  void connectVerticalScrollEvents(QScrollBar *verticalScrollbar);
  void connectHorizontalScrollEvents(QScrollBar *horizontalScrollbar);

  void handleProgrammaticScroll();
  void handleUserScroll();
  void setupScrollSuppression();
  void finalizeScrollChanges();

  bool shouldSkipArrowKeyUpdate() const;
  bool isScrollAnimationRunning(QScrollBar *scrollBar) const;
  void stopScrollAnimation(QScrollBar *scrollBar);
  int calculateCenterScrollTarget(int selectedIndex, int viewportHeight) const;
  void setupAndStartCenterAnimation(QScrollBar *scrollBar, int current,
                                    int target);

  void calculateMovementDirection(int selectedIndex, int prevIndex,
                                  int itemsPerRow, bool &isHorizontalMove);
  void setupSelectionOverlay();
  void setupSelectionAnimation();
  QRect selectionOverlayRectForWidget(MediaItemWidget *widget) const;
  QRect selectionOverlayRectForIndex(int visualIndex) const;
  void applySelectionOverlayToWidget(MediaItemWidget *widget);
  bool shouldKeepSelectionOverlayVisible() const;
  void handleHorizontalMoveAnimation(int selectedIndex, int prevIndex);
  void handleDirectSelectionUpdate(int selectedIndex);
  void prewarmSurroundingWidgets(int selectedIndex);
  void scheduleArrowKeyUpdate(int selectedIndex);

  void setupContainerSizes(int availableWidth, int contentWidth, bool overflow);
  HorizontalAlignment getEffectiveAlignment() const;
  void calculateScrollbarOffsets(bool verticalBarHidden, int &leftOffset,
                                 int &rightOffset, int &centerOffset) const;
  int calculateContainerPosition(int availableWidth, int contentWidth,
                                 bool overflow, HorizontalAlignment align,
                                 int leftOffset, int rightOffset,
                                 int centerOffset) const;
  void configureHorizontalScrollbar(bool overflow);

  bool matchesSubcollectionFilter(int subcollectionIndex,
                                  const QString &needle) const;
  bool matchesMediaItemFilter(int mediaIndex, const QString &needle) const;
  QString getDisplayNameForMediaItem(const QString &rawEntry) const;

  MediaItemWidget *createSubcollectionWidget(int visualIndex, int actualIndex);
  MediaItemWidget *createMediaItemWidget(int visualIndex, int actualIndex);
  void resolveMediaItemPaths(const QString &rawFileName, QString &fullPath,
                             QString &displayName, int &collectionIndex);
  void setupMediaItemConnections(MediaItemWidget *itemWidget, int visualIndex,
                                 const QString &fullPath, int collectionIndex);
  void configureArtworkForWidget(MediaItemWidget *itemWidget,
                                 const QString &fullPath);

  QString resolveAbsoluteFilePath(const QString &rawFileName);
  QString resolveRelativeFilePath(const QString &rawFileName);
  void updateCollectionIndexFromDatabase(const QString &fullPath,
                                         int &collectionIndex);

  void determineTargetCollections(int subcollectionIndex,
                                  QSet<int> &targetCollections);
  bool itemBelongsToTargetCollections(const QString &entry,
                                      const QSet<int> &targetCollections);
  void rebuildFilteredView();
};

#endif