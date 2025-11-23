#ifndef INTERACTIONMANAGER_H
#define INTERACTIONMANAGER_H

#include "collectionconfig.h"
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QObject>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QVector>

class QTimer;
class MainWindow;
class MediaItemWidget;
class DatabaseManager;
class NavigationManager;
class SettingsManager;
class SidebarManager;
class ScrollManager;
class metadataSidebar;

enum class SearchMode {
  CurrentCollection,
  CurrentAndSubcollections,
  AllCollections
};

struct InteractionManagerSetup {
  ScrollManager *scrollManager = nullptr;
  SidebarManager *sidebarManager = nullptr;
  SettingsManager *settingsManager = nullptr;
  DatabaseManager *databaseManager = nullptr;
  NavigationManager *navigationManager = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QWidget *gridContainer = nullptr;
  metadataSidebar *sidebarWidget = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QWidget *itemsPage = nullptr;
  QWidget *collectionPage = nullptr;
  QLineEdit *searchBar = nullptr;
  QPushButton *searchModeButton = nullptr;
  MainWindow *mainWindow = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
};

struct SearchContext {
  bool hasSubs = false;
  bool realDirectItems = false;
  bool allowAll = false;
  bool isContainer = false;
};

class InteractionManager : public QObject {
  Q_OBJECT
public:
  explicit InteractionManager(QObject *parent = nullptr);
  ~InteractionManager() override;
  void setupReferences(const InteractionManagerSetup &setup);
  void handleWidgetClicked(MediaItemWidget *widget, const QString &filePath);
  void handleWidgetDoubleClickedWithCollection(const QString &filePath,
                                               int collectionIndex);
  bool handleItemKeyPress(QKeyEvent *event);
  void selectItemByIndex(int index, bool allowHorizontalScroll);
  void clearSelection();
  void clearSelectionAndFocus();
  int currentSelectedIndex() const;
  int getCurrentGridWidth() const;
  void toggleSearchMode();
  void updateSearchModeButton();
  void updateSearchBarPlaceholder();
  void launchItemWithCollection(const QString &filePath, int collectionIndex);
  bool isWheelScrolling() const;
  auto eventFilter(QObject *obj, QEvent *event) -> bool override;
  auto handleGlobalKeyPress(QKeyEvent *event) -> bool;
  MediaItemWidget *getSelectedMediaItem() const { return m_selectedMediaItem; }
  void setSelectedMediaItem(MediaItemWidget *widget) {
    m_selectedMediaItem = widget;
  }
  void ensureItemVisible(int index, bool allowHorizontalScroll);
  void initializeSearchModeForCurrentCollection();
  void beginSelectionRestore(int targetIndex);
  void cancelPendingSelectionRestore();
  void stopRepeat(bool suppressRecentering = false);

  bool m_restoringSelection = false;
  int m_targetRestoreIndex = -1;
  bool m_forceImmediateCenter = false;
  QString m_selectedFilePath;
  int m_selectedItemIndex = -1;
  bool m_navigationInProgress = false;

private:
  int m_selectionRestoreToken = 0;
  bool m_selectionRestorePending = false;

signals:
  void selectionChanged(int index);
  void searchModeChanged(SearchMode mode);

public slots:
  void onSearchDebounceTimeout();
  void saveCurrentSelection();
  void handleImmediateSearchTextChanged(const QString &text);
  void onSearchTextChanged(const QString &text);

private slots:
  void beginHoldRepeat();
  void onRepeatStep();
  void performDebouncedSearch();

private:
  // Event filter helper methods
  auto handleActivityEvent(QEvent *event) -> bool;
  auto handleMouseButtonPress(QObject *obj, QEvent *event) -> bool;
  auto handleMouseButtonRelease(QObject *obj, QEvent *event) -> bool;
  auto handleWheelEvent(QObject *obj, QEvent *event) -> bool;
  auto handleKeyPressEvent(QObject *obj, QEvent *event) -> bool;
  auto handleMouseDoubleClick(QObject *obj, QEvent *event) -> bool;
  auto handleMousePress(QObject *obj, QEvent *event) -> bool;

  QList<int> getSubcollections(int parentIndex) const;
  static QStringList parseParameters(const QString &paramString);
  void updateFilePathForSelection(int index, const QList<int> &subcollections);
  void trySelectWidget(int index, const QList<int> &subcollections,
                       int attempt);
  MediaItemWidget *findBestWidgetForClick(const QPoint &clickPos);
  static MediaItemWidget *
  findClosestWidget(const QVector<MediaItemWidget *> &candidates,
                    const QPoint &clickPos);
  int handleWidgetSelection(MediaItemWidget *widget, const QPoint &clickPos,
                            QMouseEvent *originalEvent);
  void updateClickHoldHorizontalCandidate(int previousSelection,
                                          int targetSelection);
  void handleSuccessfulSelection(int index);
  void centerItemVertically(int index, bool immediate);
  int computeVerticalCenterDuration(int distance, bool repeatActive) const;
  void restoreViewedCollectionAfterSearchClear();
  void forceReloadViewedCollection();
  auto computeSearchContext() const -> SearchContext;
  QVector<SearchMode> buildSearchModeCycle(const SearchContext &ctx) const;
  void ensureHorizontallyVisible(int index);
  void startMouseHoldScrolling(const QPoint &clickPos);
  void stopMouseHoldScrolling();
  void onMouseHoldScrollStep();
  bool tryStartHorizontalClickHold(int totalItems);
  bool handleSlashKey();
  bool handleEscapeKey();
  bool hasDirectItemsForIndex(int idx) const;
  bool allowAllFor(const CollectionConfig &cfg, int collIndex,
                   bool hasSubs) const;
  bool handleKeyReleaseEvent(QObject *obj, QEvent *event);
  static int computeWheelSteps(const QWheelEvent *wheelEvent);
  static void stopArrowKeyAnimationIfRunning(QScrollBar *scrollBar);

  int resolveDoubleClickIndexCandidate() const;
  QString derivePathFromIndex(int idx) const;
  int resolveOwnerForPath(const QString &path) const;
  int getFallbackCollectionIndex() const;

  ScrollManager *m_scrollManager = nullptr;
  SidebarManager *m_sidebarManager = nullptr;
  SettingsManager *m_settingsManager = nullptr;
  DatabaseManager *m_databaseManager = nullptr;
  NavigationManager *m_navigationManager = nullptr;
  QPointer<QScrollArea> m_itemScrollArea = nullptr;
  QWidget *m_gridContainer = nullptr;
  metadataSidebar *m_metadataSidebar = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  QWidget *m_itemsPage = nullptr;
  QWidget *m_collectionPage = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  int *m_currentCollectionIndex = nullptr;
  QLineEdit *m_searchBar = nullptr;
  QPushButton *m_searchModeButton = nullptr;
  MainWindow *m_mainWindow = nullptr;

  MediaItemWidget *m_selectedMediaItem = nullptr;
  SearchMode m_currentSearchMode = SearchMode::CurrentCollection;
  QString m_currentSearchText;
  bool m_wheelScrolling = false;
  QPropertyAnimation *m_vScrollAnim = nullptr;
  QPropertyAnimation *m_hScrollAnim = nullptr;
  static constexpr int ARROW_KEY_THROTTLE_MS = 260;
  bool m_isShuttingDown = false;
  bool m_deferredCenterPending = false;
  QTimer *m_repeatTimer = nullptr;
  QTimer *m_repeatStartTimer = nullptr;
  bool m_repeating = false;
  Qt::Key m_repeatKey = Qt::Key_unknown;
  int m_repeatDelta = 0;
  bool m_repeatVertical = false;
  bool m_physicalKeyDown = false;
  int m_lastSelectedRow = -1;
  bool m_isWrappingNavigation = false;
  bool m_allowArtworkDuringSelection = false;
  QTimer *m_searchDebounceTimer = nullptr;
  QMetaObject::Connection m_searchItemsLoadedConn;
  bool m_searchActive = false;
  int m_preSearchCollectionIndex = -1;
  SearchMode m_preSearchMode = SearchMode::CurrentCollection;
  int m_preSearchSelectedIndex = -1;
  int m_repeatInterval;
  bool m_selectionAtViewportEdge = false;

  static constexpr double CONTINUOUS_SCROLL_ROW_DURATION_MS = 1500.0;
  bool m_continuousScrollActive = false;
  bool m_leftMouseDown = false;
  QTimer *m_clickHoldTimer = nullptr;
  bool m_instantPositioning = false;
  bool m_wrapSequenceActive = false;
  QTimer *m_mouseHoldTimer = nullptr;
  bool m_mouseHoldScrolling = false;
  int m_mouseHoldDirection = 0;
  bool m_mouseHoldHorizontal = false;
  bool m_clickHoldHorizontalEligible = false;
  int m_mouseHoldHorizontalDirection = 0;
  int m_mouseHoldHorizontalStartIndex = -1;
  void applyImmediateViewportPositioningForSelection(int index);
  void scheduleScrollbarRecovery();
  void ensureVerticalScrollbarPolicy();
  QMetaObject::Connection m_scrollbarRecoveryConn;
  void processSingleClickSelection(int visualIndex, const QString &filePath,
                                   bool applyScrollAreaSuppression);

  // Extracted helpers to reduce complexity while preserving behavior
  auto shouldTreatAsNewRowForClick(int targetIndex, int gridWidth) const
      -> bool;
  static bool shouldAnimateHorizontalHop(int fromIndex, int toIndex,
                                         int gridWidth);
  void runHorizontalHopAnimation(int start, int target, qint64 nowMs);
  void handleNewRowClickSelection(int visualIndex, qint64 nowMs);
  void handleSameRowClickSelection(int visualIndex, bool skipCenter,
                                   qint64 nowMs);
  void applySelectionStateForIndex(int idx);
  void finalizeRestoreFlagsAndFocus();
  void scheduleSidebarMetadataUpdateIfVisible(int targetIndex,
                                              int initialDelayMs,
                                              int secondaryDelayMs);
  void scheduleSearchBarRefocusIfNeeded();
  void handleCollectionsSearchDebounce(bool stoppedTyping,
                                       const QString &newSearchText);
  void handleItemsSearchDebounce(bool startedTyping, bool stoppedTyping,
                                 const QString &newSearchText);
  void buildSearchDebounceState(bool &onCollections, bool &onItems,
                                bool &startedTyping, bool &stoppedTyping,
                                QString &newSearchText);
  auto handleStartedTypingForCurrentMode() -> bool;
  QString titleForIndexInColl(int coll, int idx) const;
  void persistSelectionForIndex(int coll, int idx);

  static int computeTargetYForIndex(int index, int gridWidth, int itemHeight,
                                    int verticalSpacing, int viewportHeight,
                                    int scrollbarMax);
  bool computeForceImmediate(bool immediate) const;
  int computeSmallThreshold(int currentRow) const;
  bool handleSmallMovementEarlyReturn(int distance, bool clickScroll, int index,
                                      int currentRow);
  bool handleImmediateCenterPath(QScrollBar *verticalScrollBar, int targetY,
                                 int index, int currentRow);
  void ensureVAnimCreated(QScrollBar *vScrollBar);
  void configureAndStartVerticalAnimation(QScrollBar *vScrollBar, int curY,
                                          int targetY, int duration,
                                          bool clickScroll, bool clickHoldAdv);
  void updateVirtualViewAndSelectionDuringVAnim(bool clickScroll,
                                                bool clickHoldAdv);
  void onVScrollAnimationFinished();

  // Repeat-step refactor helpers (behavior-preserving)
  static int calculateNewSelection(int totalItems, int currentSelection,
                                   int direction, bool wrapEnabled,
                                   bool vertical, int gridWidth, bool &didWrap);
  static int calculateHorizontalSelection(int totalItems, int currentSelection,
                                          int direction, bool wrapEnabled,
                                          bool &didWrap);
  static int calculateVerticalSelection(int totalItems, int currentSelection,
                                        int direction, bool wrapEnabled,
                                        int gridWidth, bool &didWrap);
  static bool hasRowChanged(int gridWidth, int currentSelection,
                            int newSelection);
  void setPendingSelectionIfNeeded(bool condition, int newSelection);
  void updateSelectionStateAfterMove(int newSelection);
  /// Applies wheel-scroll steps and returns true if wrapping occurred.
  bool applyWheelSelectionDelta(int wheelSteps);

  auto processEnterOrReturnKey(int totalItems) -> bool;
  auto handleEnterOnSubcollection(int currentSelection, const QList<int> &subs)
      -> bool;
  auto handleEnterOnItem(int currentSelection, int totalItems) -> bool;
  auto isItemOffscreen(int selection, int gridWidth) const -> bool;
  void applyMinorHorizontalSuppress();

  // Immediate centering helpers
  void stopActiveVerticalAnims(QScrollBar *verticalScrollBar);
  void setProgrammaticScrollGuarded(bool enable);
  void setScrollValueAndUpdateSelection(QScrollBar *verticalScrollBar,
                                        int targetY, int index);
  void clearArtworkSuppressionViewportUpdateIfNeeded();
  void clearArrowCenterSuppressionWhenDue();
  void finalizeImmediateCenteringState(int index, int currentRow);

  // Horizontal visibility helpers
  void initHorizontalAnimIfNeeded(QScrollBar *hScrollBar);
  void animateHorizontalHold(QScrollBar *hScrollBar, int startX, int targetX);
  void animateHorizontalSmooth(QScrollBar *hScrollBar, int startX, int targetX);

  // Selection helpers
  void persistSuppressedSelectionAndMaybeCenter(
      int index, const QList<int> &subcollections, bool skipCenter);

  // Additional helpers to reduce complexity while preserving behavior
  bool shouldDeferCenterNow(bool immediate, int index) const;
  bool shouldEarlyReturnUserScroll(bool forceImmediate) const;
  bool handlePendingInitialCenterIfNeeded(QScrollBar *verticalScrollBar,
                                          int index, int targetYUnbounded,
                                          bool immediate);
  void adjustForForceClickZeroDistance(QScrollBar *verticalScrollBar,
                                       int targetY, int &curY, int &distance,
                                       int &duration, bool forceClickAnim);
  bool handleImmediateCenterForEnsureVisible(int index);
  bool maybeHandleImmediateCenter(bool distanceSmall, bool useSmooth,
                                  bool forceImmediate, bool forceClickAnim,
                                  QScrollBar *verticalScrollBar, int targetY,
                                  int index, int currentRow);
  bool handleExistingVerticalAnimIfRunning(QScrollBar *verticalScrollBar,
                                           int targetY, bool clickScroll,
                                           bool clickHoldAdv, int &curY,
                                           int &distance);

  // ensureItemVisible helpers
  static int computeHorizontalTargetX(int itemX, int collectionItemWidth,
                                      int curX, int viewportWidth, int margins,
                                      int scrollMax);
  static int computeDesiredYForVisibility(int itemY, int itemHeight, int curY,
                                          int viewportHeight, int margins,
                                          bool &needV);
  void updateViewAndRowAfterVisibility(int index, int gridWidth);
  void startEnsureVisibleVAnim(QScrollBar *vScrollBar, int startVal, int endVal,
                               bool isRepeating);
  auto shouldExitEnsureItemVisible(int index) const -> bool;

  // key handling helpers
  static auto deriveDirectionForKey(int key, int gridWidth, int &direction,
                                    bool &vertical) -> bool;

  // Reduced-complexity helpers for key navigation
  auto guardForActiveNavigation(QKeyEvent *event) -> bool;
  void prepareKeyNavigationState();
  auto computeIsNewRow(int currentSelection, int newSelection,
                       int gridWidth) const -> bool;
  void applyImmediateCenterSuppression();
  void updateSelectionForKeyMove(int newSelection);
  void performVisibilityForKeyMove(bool isNewRow, int newSelection);
  void finalizeKeyRepeat(QKeyEvent *event, int direction, bool vertical);
};

#endif