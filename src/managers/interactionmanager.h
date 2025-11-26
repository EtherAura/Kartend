#ifndef INTERACTIONMANAGER_H
#define INTERACTIONMANAGER_H

#include "collectionutils.h"
#include "keyboardmanager.h"
#include "mousemanager.h"
#include "animationmanager.h"
#include "searchmanager.h"
#include "searchutils.h"
#include "selectionmanager.h"
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
#include <memory>

class QTimer;
class MainWindow;
class MediaItemWidget;
class DatabaseManager;
class NavigationManager;
class SettingsManager;
class SidebarManager;
class ScrollManager;
class SessionManager;
class ArtworkManager;
class metadataSidebar;

struct InteractionManagerSetup {
  ScrollManager *scrollManager = nullptr;
  SidebarManager *sidebarManager = nullptr;
  SettingsManager *settingsManager = nullptr;
  DatabaseManager *databaseManager = nullptr;
  NavigationManager *navigationManager = nullptr;
  SessionManager *sessionManager = nullptr;
  ArtworkManager *artworkManager = nullptr;
  metadataSidebar *sidebar = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QWidget *gridContainer = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QWidget *itemsPage = nullptr;
  QWidget *collectionPage = nullptr;
  QLineEdit *searchBar = nullptr;
  QPushButton *searchModeButton = nullptr;
  MainWindow *mainWindow = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
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
  MediaItemWidget *getSelectedMediaItem() const;
  void setSelectedMediaItem(MediaItemWidget *widget);
  QString selectedFilePath() const;
  void ensureItemVisible(int index, bool allowHorizontalScroll);
  void initializeSearchModeForCurrentCollection();
  void beginSelectionRestore(int targetIndex);
  void cancelPendingSelectionRestore();
  void stopRepeat(bool suppressRecentering = false);
  bool isRestoringSelection() const;
  int targetRestoreIndex() const;
  bool forceImmediateCenter() const;

  bool m_navigationInProgress = false;

private:
  // Selection state - kept for internal use during transition, synced with SelectionManager
  bool m_restoringSelection = false;
  int m_targetRestoreIndex = -1;
  bool m_forceImmediateCenter = false;
  QString m_selectedFilePath;
  int m_selectedItemIndex = -1;

  int m_selectionRestoreToken = 0;
  bool m_selectionRestorePending = false;

signals:
  void selectionChanged(int index);
  void searchModeChanged(SearchMode mode);

public slots:
  void onSearchDebounceTimeout();
  void saveCurrentSelection();
  void handleImmediateSearchTextChanged(const QString &text);

private slots:
  // KeyboardManager callbacks
  void handleArrowKeyNavigation(int direction, bool vertical);
  void onKeyboardRepeatStep();
  void onKeyboardStopRepeat(bool suppressRecentering);

  // MouseManager callbacks
  void onMouseHoldScrollStep(int direction, bool isHorizontal);

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
  int handleWidgetSelection(MediaItemWidget *widget, const QPoint &clickPos,
                            QMouseEvent *originalEvent);
  void handleSuccessfulSelection(int index);
  void centerItemVertically(int index, bool immediate);
  int computeVerticalCenterDuration(int distance, bool repeatActive) const;
  void restoreViewedCollectionAfterSearchClear();
  void forceReloadViewedCollection();
  auto computeSearchContext() const -> SearchContext;
  QVector<SearchMode> buildSearchModeCycle(const SearchContext &ctx) const;
  void ensureHorizontallyVisible(int index);
  bool handleSlashKey();
  bool handleEscapeKey();
  bool hasDirectItemsForIndex(int idx) const;
  bool allowAllFor(const CollectionConfig &cfg, int collIndex,
                   bool hasSubs) const;
  bool handleKeyReleaseEvent(QObject *obj, QEvent *event);

  int resolveDoubleClickIndexCandidate() const;
  QString derivePathFromIndex(int idx) const;
  int resolveOwnerForPath(const QString &path) const;
  int getFallbackCollectionIndex() const;

  // Search delegation (owned helper)
  std::unique_ptr<SearchManager> m_searchManager;

  // Selection delegation (owned helper)
  std::unique_ptr<SelectionManager> m_selectionManager;

  // Keyboard delegation (owned helper)
  std::unique_ptr<KeyboardManager> m_keyboardManager;

  // Animation delegation (owned helper)
  std::unique_ptr<AnimationManager> m_animationManager;

  // Mouse hold scrolling delegation (owned helper)
  std::unique_ptr<MouseManager> m_mouseManager;

  ScrollManager *m_scrollManager = nullptr;
  SidebarManager *m_sidebarManager = nullptr;
  SettingsManager *m_settingsManager = nullptr;
  DatabaseManager *m_databaseManager = nullptr;
  NavigationManager *m_navigationManager = nullptr;
  SessionManager *m_sessionManager = nullptr;
  ArtworkManager *m_artworkManager = nullptr;
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
  static constexpr int ARROW_KEY_THROTTLE_MS = 260;
  bool m_isShuttingDown = false;
  bool m_deferredCenterPending = false;
  // Consolidated repeat flag: true when keyboard repeat or mouse hold is active
  bool m_repeating = false;
  bool m_physicalKeyDown = false;
  int m_lastSelectedRow = -1;
  bool m_isWrappingNavigation = false;
  bool m_allowArtworkDuringSelection = false;
  bool m_selectionAtViewportEdge = false;

  static constexpr double CONTINUOUS_SCROLL_ROW_DURATION_MS = 1500.0;
  bool m_continuousScrollActive = false;
  bool m_instantPositioning = false;
  bool m_wrapSequenceActive = false;
  void applyImmediateViewportPositioningForSelection(int index);
  void scheduleScrollbarRecovery();
  void ensureVerticalScrollbarPolicy();
  QMetaObject::Connection m_scrollbarRecoveryConn;
  void processSingleClickSelection(int visualIndex, const QString &filePath,
                                   bool applyScrollAreaSuppression);

  // Click selection helpers (computational methods moved to SelectionManager)
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

  bool computeForceImmediate(bool immediate) const;
  int computeSmallThreshold(int currentRow) const;
  bool handleSmallMovementEarlyReturn(int distance, bool clickScroll, int index,
                                      int currentRow);
  bool handleImmediateCenterPath(QScrollBar *verticalScrollBar, int targetY,
                                 int index, int currentRow);
  void onVScrollAnimationFinished();

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
  void updateViewAndRowAfterVisibility(int index, int gridWidth);
  void startEnsureVisibleVAnim(QScrollBar *vScrollBar, int startVal, int endVal,
                               bool isRepeating);
  auto shouldExitEnsureItemVisible(int index) const -> bool;

  // Key navigation helpers still used by handleArrowKeyNavigation
  void applyImmediateCenterSuppression();
  void updateSelectionForKeyMove(int newSelection);
  void performVisibilityForKeyMove(bool isNewRow, int newSelection);
};

#endif