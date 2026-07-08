#ifndef WHEELEVENTHANDLER_H
#define WHEELEVENTHANDLER_H

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include <QObject>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QEvent;
class QScrollArea;
class QScrollBar;
class QStackedWidget;
class QWidget;
QT_END_NAMESPACE

class IArtworkPreviewScroll;
class IGridLayoutScroll;
class IMouseWheelState;
class InteractionStateHolder;
class IScrollDataSource;
class ISelectionCore;
class ISelectionOverlayScroll;
class IViewportPositioner;
class IViewportScrollState;
class IWheelScrollAnimator;

/**
 * @brief Owns the wheel-scrolling state machine extracted from EventManager.
 *
 * Wheel events drive selection movement (rather than freeform scrolling), so
 * the handler has to coordinate three pieces:
 *  - delta accumulation (selection step + view-type-aware row math),
 *  - smooth-scroll animation handoff to AnimationManager, and
 *  - the bookkeeping flags ScrollManager / ViewportManager / state read to
 *    distinguish wheel-driven scrolls from arrow / hover / programmatic ones.
 *
 * EventManager forwards QEvent::Wheel here and otherwise stays out of the
 * way. The handler emits its own start/end signals; EventManager re-emits
 * them so external consumers (toolbar, attract mode) keep their existing
 * subscription to the EventManager surface.
 */
class WheelEventHandler : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(WheelEventHandler)
  // Test seam: grants the unit test private access to applySelectionDelta /
  // onAnimationFinished so the deterministic selection-math + cleanup paths can
  // be driven directly, matching the friend-access convention test_mousemanager
  // uses for MouseManager's private slots.
  friend class TestWheelEventHandler;

public:
  explicit WheelEventHandler(QObject *parent = nullptr);
  ~WheelEventHandler() override;

  struct Setup {
    const ApplicationContext *ctx = nullptr;
    QScrollArea *itemScrollArea = nullptr;
    QStackedWidget *stackedWidget = nullptr;
    QWidget *itemsPage = nullptr;
    QList<CollectionConfig> *collections = nullptr;
    const int *currentCollectionIndex = nullptr;
    GeneralSettings *generalSettings = nullptr;
  };
  void setupReferences(const Setup &setup);

  /// Forward QEvent::Wheel here. Returns true when the handler consumed the
  /// event (a sentinel state may swallow it without selection movement).
  bool handleEvent(QObject *obj, QEvent *event);

signals:
  /// Emitted when wheel-driven scrolling begins. EventManager re-emits this
  /// as wheelScrollStarted so external listeners stay decoupled.
  void scrollStarted();
  /// Emitted when the wheel-driven smooth-scroll animation finishes. Pairs
  /// with scrollStarted.
  void scrollEnded();

private:
  bool eventBelongsToSidebar() const;
  bool canProceed() const;
  int computeTargetScroll(int selectedIndex, const CollectionConfig &collection,
                          QScrollBar *axisScrollBar, bool horizontalView) const;
  void onAnimationFinished();
  bool applySelectionDelta(int wheelSteps);
  QList<int> getSubcollections(int parentIndex) const;

  // ctx is the single source of truth for sibling managers + state.
  const ApplicationContext *m_ctx = nullptr;
  // Kartend-dl0uz.2: every sibling reach goes through a role view — the
  // four scroll roles this handler spans (grid, overlay, preview, data) plus
  // the wheel-scoped input roles. Read through ctx on every call, never
  // cached.
  [[nodiscard]] IGridLayoutScroll *scrollGrid() const {
    return m_ctx ? m_ctx->scrollGrid() : nullptr;
  }
  [[nodiscard]] ISelectionOverlayScroll *scrollOverlay() const {
    return m_ctx ? m_ctx->scrollOverlay() : nullptr;
  }
  [[nodiscard]] IArtworkPreviewScroll *scrollPreview() const {
    return m_ctx ? m_ctx->scrollPreview() : nullptr;
  }
  [[nodiscard]] IScrollDataSource *scrollData() const {
    return m_ctx ? m_ctx->scrollData() : nullptr;
  }
  [[nodiscard]] ISelectionCore *selectionCore() const {
    return m_ctx ? m_ctx->selectionCore() : nullptr;
  }
  [[nodiscard]] IViewportScrollState *viewportScrollState() const {
    return m_ctx ? m_ctx->viewportScrollState() : nullptr;
  }
  [[nodiscard]] IViewportPositioner *viewportPositioner() const {
    return m_ctx ? m_ctx->viewportPositioner() : nullptr;
  }
  [[nodiscard]] IWheelScrollAnimator *wheelAnimator() const {
    return m_ctx ? m_ctx->wheelAnimator() : nullptr;
  }
  [[nodiscard]] IMouseWheelState *mouseWheelState() const {
    return m_ctx ? m_ctx->mouseWheelState() : nullptr;
  }
  [[nodiscard]] InteractionStateHolder *state() const {
    return m_ctx ? m_ctx->interactionState() : nullptr;
  }

  QPointer<QScrollArea> m_itemScrollArea = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  QWidget *m_itemsPage = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  const int *m_currentCollectionIndex = nullptr;
  GeneralSettings *m_generalSettings = nullptr;

  // Reentrancy guard: animation completion + signal processing can re-enter.
  bool m_processing = false;
};

#endif // WHEELEVENTHANDLER_H
