#ifndef VIEWPORTMANAGER_H
#define VIEWPORTMANAGER_H

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "iviewportmanager.h"
#include "setuputils.h"
#include <QObject>
#include <QPointer>
#include <QScrollArea>

QT_BEGIN_NAMESPACE
class QScrollBar;
QT_END_NAMESPACE

class IAnimationManager;
class IGridLayoutScroll;
class ISelectionOverlayScroll;
class ISelectionManager;
class IArtworkManager;
class InteractionStateHolder;
#include "applicationcontext_fwd.h"

struct ViewportManagerSetup {
  const ApplicationContext *ctx = nullptr;
  const GeneralSettings *generalSettings = nullptr;

  QScrollArea *itemScrollArea = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  const int *currentCollectionIndex = nullptr;
  const bool *isShuttingDown = nullptr;

  SETUP_GETTER_DECL(QScrollArea *, ItemScrollArea)
  SETUP_GETTER_DECL(QList<CollectionConfig> *, Collections)
  SETUP_GETTER_DECL(const int *, CurrentCollectionIndex)
  SETUP_GETTER_DECL(const bool *, IsShuttingDown)
  SETUP_GETTER_DECL(const GeneralSettings *, GeneralSettings)
};

/**
 * @brief Manages viewport positioning, centering, and visibility for items.
 *
 * Handles vertical/horizontal centering of items, ensuring items are visible,
 * and viewport positioning logic. Works with AnimationManager for smooth
 * scrolling and SelectionManager for row tracking.
 */
// QObject must be the first base; IViewportManager is a plain (non-QObject)
// role interface — single-QObject-base multiple inheritance.
class ViewportManager : public QObject, public IViewportManager {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ViewportManager)

public:
  explicit ViewportManager(QObject *parent = nullptr);
  ~ViewportManager() override;

  void setupReferences(const ViewportManagerSetup &setup);

  // --- Primary Centering Operations ---
  void centerItemVertically(int index, bool immediate) override;
  void ensureItemVisible(int index, bool allowHorizontalScroll) override;
  void ensureHorizontallyVisible(int index) override;
  void applyImmediateViewportPositioningForSelection(int targetIndex) override;

  /// Get the scroll scale for very large collections (>16M pixels)
  [[nodiscard]] double getScrollScale() const override;

  /// Convert logical scroll position to widget scroll position
  /// Uses viewport-aware interpolation for precise endpoint mapping
  [[nodiscard]] int toWidgetScrollY(int logicalScrollY) const override;

  // --- State Accessors/Mutators ---
  void setForceImmediateCenter(bool force) override { m_forceImmediateCenter = force; }
  [[nodiscard]] bool forceImmediateCenter() const { return m_forceImmediateCenter; }

  void setIsWrappingNavigation(bool wrapping) override { m_isWrappingNavigation = wrapping; }
  [[nodiscard]] bool isWrappingNavigation() const override { return m_isWrappingNavigation; }

  void setRestoringSelection(bool restoring);
  [[nodiscard]] bool isRestoringSelection() const;

  void setTargetRestoreIndex(int index) override;
  [[nodiscard]] int targetRestoreIndex() const;

  void setInstantPositioning(bool instant) { m_instantPositioning = instant; }
  [[nodiscard]] bool instantPositioning() const { return m_instantPositioning; }

  void setWrapSequenceActive(bool active) override { m_wrapSequenceActive = active; }
  [[nodiscard]] bool wrapSequenceActive() const { return m_wrapSequenceActive; }

  void setContinuousScrollActive(bool active) override { m_continuousScrollActive = active; }
  [[nodiscard]] bool continuousScrollActive() const override { return m_continuousScrollActive; }

  void setRepeating(bool repeating) override { m_repeating = repeating; }
  [[nodiscard]] bool isRepeating() const { return m_repeating; }

  void setPhysicalKeyDown(bool down) override { m_physicalKeyDown = down; }
  [[nodiscard]] bool physicalKeyDown() const { return m_physicalKeyDown; }

  void setLastSelectedRow(int row) { m_lastSelectedRow = row; }
  [[nodiscard]] int lastSelectedRow() const { return m_lastSelectedRow; }

  // --- Suppression and State Management ---
  void applyImmediateCenterSuppression() override;
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
  [[nodiscard]] bool handlePendingInitialCenterIfNeeded(QScrollBar *verticalScrollBar, int index,
                                                        int targetYUnbounded, bool immediate);
  bool handleSmallMovementEarlyReturn(int distance, bool clickScroll, int index, int currentRow);
  void adjustForForceClickZeroDistance(QScrollBar *verticalScrollBar, int targetY, int &curY,
                                       int &distance, int &duration, bool forceClickAnim);
  [[nodiscard]] bool maybeHandleImmediateCenter(bool distanceSmall, bool useSmooth,
                                                bool forceImmediate, bool forceClickAnim,
                                                QScrollBar *verticalScrollBar, int targetY,
                                                int index, int currentRow);
  bool handleImmediateCenterPath(QScrollBar *verticalScrollBar, int targetY, int index,
                                 int currentRow);
  void finalizeImmediateCenteringState(int index, int currentRow);

  // --- Visibility Helpers ---
  [[nodiscard]] bool shouldExitEnsureItemVisible(int index) const;
  [[nodiscard]] bool handleImmediateCenterForEnsureVisible(int index);
  void updateViewAndRowAfterVisibility(int index, int gridWidth);
  void startEnsureVisibleVAnim(QScrollBar *vScrollBar, int startVal, int endVal, bool isRepeating);

  // --- Scroll State Helpers ---
  void setProgrammaticScrollGuarded(bool enable);
  void setScrollValueAndUpdateSelection(QScrollBar *verticalScrollBar, int targetY, int index);
  [[nodiscard]] int computeVerticalCenterDuration(int distance, bool repeatActive) const;
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

  // ctx is the single source of truth for sibling managers + state. Inline
  // accessors below are the canonical read path; never cache sibling-manager
  // pointers as direct fields.
  const ApplicationContext *m_ctx = nullptr;
  // Kartend-h1l8f: ViewportManager spans exactly two scroll roles — grid
  // metrics / view refresh and the selection overlay — so it takes the two
  // role accessors instead of the IScrollManager facade. Both alias the same
  // ScrollManager and are null together, so guarding on either is sound.
  // (Two deferred-lambda sites still capture QPointer<IScrollManager> via
  // ctx->scrollManager() directly: QPointer needs the QObject facade.)
  [[nodiscard]] IGridLayoutScroll *scrollGrid() const {
    return m_ctx ? m_ctx->scrollGrid() : nullptr;
  }
  [[nodiscard]] ISelectionOverlayScroll *scrollOverlay() const {
    return m_ctx ? m_ctx->scrollOverlay() : nullptr;
  }
  [[nodiscard]] ISelectionManager *selectionMgr() const {
    return m_ctx ? m_ctx->selectionManager() : nullptr;
  }
  [[nodiscard]] IAnimationManager *animMgr() const {
    return m_ctx ? m_ctx->animationManager() : nullptr;
  }
  [[nodiscard]] IArtworkManager *artworkMgr() const {
    return m_ctx ? m_ctx->artworkManager() : nullptr;
  }
  [[nodiscard]] InteractionStateHolder *state() const {
    return m_ctx ? m_ctx->interactionState() : nullptr;
  }

  const GeneralSettings *m_generalSettings = nullptr;
  QPointer<QScrollArea> m_itemScrollArea = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  const int *m_currentCollectionIndex = nullptr;
  const bool *m_isShuttingDown = nullptr;
  /// Canonical shutdown test (Kartend-kalh1, mirrors GamepadManager):
  /// dereferences the MainWindow-owned flag pointer and folds in the
  /// app-global closingDown. Use this — never truth-test m_isShuttingDown,
  /// which is non-null for the manager's whole wired life and would read
  /// permanently true.
  [[nodiscard]] bool shuttingDown() const;
};

#endif // VIEWPORTMANAGER_H
