#ifndef VIEWPORTMANAGER_H
#define VIEWPORTMANAGER_H

#include "collectionutils.h"
#include "setuputils.h"
#include <QObject>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QScrollArea;
class QScrollBar;
QT_END_NAMESPACE

class AnimationManager;
class ScrollManager;
class SelectionManager;
class ArtworkManager;
class InteractionStateHolder;
struct ApplicationContext;

struct ViewportManagerSetup {
  const ApplicationContext *ctx = nullptr;
  const GeneralSettings *generalSettings = nullptr;

  QScrollArea *itemScrollArea = nullptr;
  ScrollManager *scrollManager = nullptr;
  SelectionManager *selectionManager = nullptr;
  AnimationManager *animationManager = nullptr;
  ArtworkManager *artworkManager = nullptr;
  InteractionStateHolder *interactionState = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
  const bool *isShuttingDown = nullptr;

  SETUP_GETTER_DECL(QScrollArea*, ItemScrollArea)
  SETUP_GETTER_DECL(ScrollManager*, ScrollManager)
  SETUP_GETTER_DECL(SelectionManager*, SelectionManager)
  SETUP_GETTER_DECL(AnimationManager*, AnimationManager)
  SETUP_GETTER_DECL(ArtworkManager*, ArtworkManager)
  SETUP_GETTER_DECL(InteractionStateHolder*, InteractionState)
  SETUP_GETTER_DECL(QList<CollectionConfig>*, Collections)
  SETUP_GETTER_DECL(int*, CurrentCollectionIndex)
  SETUP_GETTER_DECL(const bool*, IsShuttingDown)
  SETUP_GETTER_DECL(const GeneralSettings*, GeneralSettings)
};

/**
 * @brief Manages viewport positioning, centering, and visibility for items.
 *
 * Handles vertical/horizontal centering of items, ensuring items are visible,
 * and viewport positioning logic. Works with AnimationManager for smooth
 * scrolling and SelectionManager for row tracking.
 */
class ViewportManager : public QObject {
  Q_OBJECT

public:
  explicit ViewportManager(QObject *parent = nullptr);
  ~ViewportManager() override;

  void setupReferences(const ViewportManagerSetup &setup);

  // --- Primary Centering Operations ---
  void centerItemVertically(int index, bool immediate);
  void ensureItemVisible(int index, bool allowHorizontalScroll);
  void ensureHorizontallyVisible(int index);
  void applyImmediateViewportPositioningForSelection(int targetIndex);

  // --- State Accessors/Mutators ---
  void setForceImmediateCenter(bool force) { m_forceImmediateCenter = force; }
  [[nodiscard]] bool forceImmediateCenter() const {
    return m_forceImmediateCenter;
  }

  void setIsWrappingNavigation(bool wrapping) {
    m_isWrappingNavigation = wrapping;
  }
  [[nodiscard]] bool isWrappingNavigation() const {
    return m_isWrappingNavigation;
  }

  void setRestoringSelection(bool restoring);
  [[nodiscard]] bool isRestoringSelection() const;

  void setTargetRestoreIndex(int index);
  [[nodiscard]] int targetRestoreIndex() const;

  void setInstantPositioning(bool instant) { m_instantPositioning = instant; }
  [[nodiscard]] bool instantPositioning() const { return m_instantPositioning; }

  void setWrapSequenceActive(bool active) { m_wrapSequenceActive = active; }
  [[nodiscard]] bool wrapSequenceActive() const { return m_wrapSequenceActive; }

  void setContinuousScrollActive(bool active) {
    m_continuousScrollActive = active;
  }
  [[nodiscard]] bool continuousScrollActive() const {
    return m_continuousScrollActive;
  }

  void setRepeating(bool repeating) { m_repeating = repeating; }
  [[nodiscard]] bool isRepeating() const { return m_repeating; }

  void setPhysicalKeyDown(bool down) { m_physicalKeyDown = down; }
  [[nodiscard]] bool physicalKeyDown() const { return m_physicalKeyDown; }

  void setLastSelectedRow(int row) { m_lastSelectedRow = row; }
  [[nodiscard]] int lastSelectedRow() const { return m_lastSelectedRow; }

  // --- Suppression and State Management ---
  void applyImmediateCenterSuppression();
  void clearArrowCenterSuppressionWhenDue();
  void clearArtworkSuppressionViewportUpdateIfNeeded();
  void ensureVerticalScrollbarPolicy();

  // --- Animation Callbacks ---
  void onVScrollAnimationFinished();

signals:
  void requestSelectionUpdate(int index);
  void requestVirtualViewUpdate();

private:
  // --- Centering Helpers ---
  [[nodiscard]] bool computeForceImmediate(bool immediate) const;
  [[nodiscard]] int computeSmallThreshold(int currentRow) const;
  [[nodiscard]] bool shouldDeferCenterNow(bool immediate, int index) const;
  [[nodiscard]] bool shouldEarlyReturnUserScroll(bool forceImmediate) const;
  [[nodiscard]] bool
  handlePendingInitialCenterIfNeeded(QScrollBar *verticalScrollBar, int index,
                                     int targetYUnbounded, bool immediate);
  bool handleSmallMovementEarlyReturn(int distance, bool clickScroll, int index,
                                      int currentRow);
  void adjustForForceClickZeroDistance(QScrollBar *verticalScrollBar,
                                       int targetY, int &curY, int &distance,
                                       int &duration, bool forceClickAnim);
  [[nodiscard]] bool maybeHandleImmediateCenter(
      bool distanceSmall, bool useSmooth, bool forceImmediate,
      bool forceClickAnim, QScrollBar *verticalScrollBar, int targetY,
      int index, int currentRow);
  bool handleImmediateCenterPath(QScrollBar *verticalScrollBar, int targetY,
                                 int index, int currentRow);
  void finalizeImmediateCenteringState(int index, int currentRow);

  // --- Visibility Helpers ---
  [[nodiscard]] bool shouldExitEnsureItemVisible(int index) const;
  [[nodiscard]] bool handleImmediateCenterForEnsureVisible(int index);
  void updateViewAndRowAfterVisibility(int index, int gridWidth);
  void startEnsureVisibleVAnim(QScrollBar *vScrollBar, int startVal, int endVal,
                               bool isRepeating);

  // --- Scroll State Helpers ---
  void setProgrammaticScrollGuarded(bool enable);
  void setScrollValueAndUpdateSelection(QScrollBar *verticalScrollBar,
                                        int targetY, int index);
  [[nodiscard]] int computeVerticalCenterDuration(int distance,
                                                  bool repeatActive) const;
  [[nodiscard]] int getCurrentGridWidth() const;

  // State flags
  bool m_forceImmediateCenter = false;
  bool m_isWrappingNavigation = false;
  bool m_instantPositioning = false;
  bool m_wrapSequenceActive = false;
  bool m_continuousScrollActive = false;
  bool m_repeating = false;
  bool m_physicalKeyDown = false;
  int m_lastSelectedRow = -1;
  bool m_deferredCenterPending = false;

  // Manager references
  const GeneralSettings *m_generalSettings = nullptr;
  QPointer<QScrollArea> m_itemScrollArea = nullptr;
  ScrollManager *m_scrollManager = nullptr;
  SelectionManager *m_selectionManager = nullptr;
  AnimationManager *m_animationManager = nullptr;
  ArtworkManager *m_artworkManager = nullptr;
  InteractionStateHolder *m_state = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  int *m_currentCollectionIndex = nullptr;
  const bool *m_isShuttingDown = nullptr;
};

#endif // VIEWPORTMANAGER_H
