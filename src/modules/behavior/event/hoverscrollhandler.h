#ifndef HOVERSCROLLHANDLER_H
#define HOVERSCROLLHANDLER_H

#include "applicationcontext.h"
#include "collectionutils.h"
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QTimer>

QT_BEGIN_NAMESPACE
class QEvent;
class QScrollArea;
class QStackedWidget;
class QWidget;
QT_END_NAMESPACE

class ArtworkManager;
class IDetailsPaneManager;
class InteractionStateHolder;
class ItemWidget;
class ScrollManager;
class SelectionManager;
class ViewportManager;

/**
 * @brief Owns the hover-to-select state machine extracted from EventManager.
 *
 * When @c GeneralSettings::selectItemOnHover is enabled, hovering over an
 * ItemWidget selects it immediately, and after a stability dwell the
 * viewport recenters so the hovered row stays in view. Once recentered, a
 * follow-up poll re-arms the cycle if the cursor lands on a freshly-exposed
 * widget — that gives continuous scroll without requiring mouse motion.
 *
 * The handler keeps all of that state local: pending widget pointer,
 * stability anchor position, dwell timer. EventManager just forwards the
 * Enter / MouseMove events.
 */
class HoverScrollHandler : public QObject {
  Q_OBJECT
public:
  explicit HoverScrollHandler(QObject *parent = nullptr);
  ~HoverScrollHandler() override;

  struct Setup {
    const ApplicationContext *ctx = nullptr;
    QScrollArea *itemScrollArea = nullptr;
    QWidget *gridContainer = nullptr;
    QStackedWidget *stackedWidget = nullptr;
    QWidget *itemsPage = nullptr;
    QList<CollectionConfig> *collections = nullptr;
    int *currentCollectionIndex = nullptr;
    GeneralSettings *generalSettings = nullptr;
  };
  void setupReferences(const Setup &setup);

  /// Process a hover event (Enter / MouseMove). @p isRestoringSelection comes
  /// from the EventManager-level selection-restore guard. Always returns
  /// false: the event filter chain should not consume hover events because
  /// other widgets may want to react too.
  bool handleEvent(QObject *obj, QEvent *event, bool isRestoringSelection);

  /// Public reset hook. Called when the EventManager / InteractionManager
  /// wants to abandon any in-flight dwell (e.g. on collection navigation).
  void clearPendingScroll();

private slots:
  void commitPendingScroll();

private:
  void pollCursorForContinue();

  // ctx is the single source of truth for sibling managers + state.
  const ApplicationContext *m_ctx = nullptr;
  [[nodiscard]] ScrollManager *scrollMgr() const {
    return m_ctx ? m_ctx->scrollManager() : nullptr;
  }
  [[nodiscard]] SelectionManager *selectionMgr() const {
    return m_ctx ? m_ctx->selectionManager() : nullptr;
  }
  [[nodiscard]] ViewportManager *viewportMgr() const {
    return m_ctx ? m_ctx->viewportManager() : nullptr;
  }
  [[nodiscard]] IDetailsPaneManager *detailsPaneMgr() const {
    return m_ctx ? m_ctx->detailsPaneManager() : nullptr;
  }
  [[nodiscard]] InteractionStateHolder *state() const {
    return m_ctx ? m_ctx->interactionState() : nullptr;
  }

  QPointer<QScrollArea> m_itemScrollArea = nullptr;
  QWidget *m_gridContainer = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  QWidget *m_itemsPage = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  int *m_currentCollectionIndex = nullptr;
  GeneralSettings *m_generalSettings = nullptr;

  // Pending dwell state — null widget pointer means "no dwell active".
  QPointer<ItemWidget> m_pendingWidget;
  QPoint m_pendingGlobalPos;
  int m_pendingIndex = -1;
  QTimer m_dwellTimer;
};

#endif // HOVERSCROLLHANDLER_H
