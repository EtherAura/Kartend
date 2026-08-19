#ifndef EVENTMANAGER_H
#define EVENTMANAGER_H

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "setuputils.h"
#include <functional>
#include <memory>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QScrollArea>

QT_BEGIN_NAMESPACE
class QEvent;
class QLineEdit;
class QMouseEvent;
class QScrollBar;
class QStackedWidget;
class QTimer;
class QWidget;
QT_END_NAMESPACE

class ItemWidget;
class IScrollDataSource;
class IGridLayoutScroll;
class ISearchStateScroll;
class IArtworkPreviewScroll;
class IKeyEventSink;
class IMouseHoldControl;
class IViewportScrollState;
class ISelectionCore;
class IUserActivitySink;
class IFileCollectionLookup;
class InteractionStateHolder;
class HoverScrollHandler;
class WheelEventHandler;
#include "applicationcontext_fwd.h"

/**
 * @brief Setup struct for EventManager dependencies.
 *
 * Follows the same pattern as other manager setup structs, with ctx fallback.
 */
struct EventManagerSetup {
  const ApplicationContext *ctx = nullptr;

  // UI / collection-state references — sibling managers and InteractionStateHolder
  // are read directly from ctx at runtime, never duplicated here.
  QScrollArea *itemScrollArea = nullptr;
  QWidget *gridContainer = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QWidget *itemsPage = nullptr;
  QWidget *itemsTopBar = nullptr;
  QLineEdit *searchBar = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  const int *currentCollectionIndex = nullptr;
  GeneralSettings *generalSettings = nullptr;

  SETUP_GETTER_DECL(QScrollArea *, ItemScrollArea)
  SETUP_GETTER_DECL(QWidget *, GridContainer)
  SETUP_GETTER_DECL(QStackedWidget *, StackedWidget)
  SETUP_GETTER_DECL(QWidget *, ItemsPage)
  SETUP_GETTER_DECL(QWidget *, ItemsTopBar)
  SETUP_GETTER_DECL(QLineEdit *, SearchBar)
  SETUP_GETTER_DECL(QList<CollectionConfig> *, Collections)
  SETUP_GETTER_DECL(const int *, CurrentCollectionIndex)
  SETUP_GETTER_DECL(GeneralSettings *, GeneralSettings)
};

/**
 * @brief Manages event filtering and dispatching for user input events.
 *
 * Handles mouse, keyboard, and wheel events, delegating to specialized
 * managers (KeyboardManager, MouseManager, etc.) as appropriate.
 * Extracted from InteractionManager to separate event handling concerns.
 */
class EventManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(EventManager)
  // Test seam: grants the unit test private access to the mouse-dispatch
  // handlers (handleMousePress / handleMouseDoubleClick / handleWheelEvent) and
  // the itemWidgetForObject / visualIndexForWidget helpers so the
  // signal-emission + parent-chain logic can be driven directly, matching the
  // friend-access convention test_mousemanager uses.
  friend class TestEventManagerMouse;

public:
  explicit EventManager(QObject *parent = nullptr);
  ~EventManager() override;

  // Setup using struct pattern (preferred)
  void setupReferences(const EventManagerSetup &setup);

  /// Injects the "is a modal scrape dialog currently visible" predicate.
  /// EventManager gates item-grid input while a scrape dialog is up, but the
  /// dialog type lives in the UI layer — the owner (MainWindow) supplies this
  /// so the input module needs no UI-chrome include.
  void setModalScrapeDialogVisiblePredicate(std::function<bool()> predicate);

  /// Single definition of the "a modal UI is capturing input" gate: a Qt
  /// modal widget is active, or the (owner-injected) scrape-result dialog is
  /// visible. filterEvent() applies these predicates per event type for
  /// keyboard/mouse/wheel input; gamepad input is signal/timer driven and
  /// never crosses filterEvent, so InteractionManager's gamepad-driven slots
  /// consult this same gate instead of duplicating the predicates.
  [[nodiscard]] bool modalInputGateActive() const;

  // Main event filter entry point
  [[nodiscard]] bool filterEvent(QObject *obj, QEvent *event);

  // State accessors for coordination with InteractionManager
  [[nodiscard]] bool isRestoringSelection() const;

signals:
  // Event signals for InteractionManager to handle
  void widgetDoubleClicked(const QString &filePath, int collectionIndex);
  void widgetClicked(ItemWidget *widget, int visualIndex, const QPoint &clickPos,
                     QMouseEvent *event);
  void contextMenuRequested(ItemWidget *widget, int visualIndex, const QPoint &globalPos);
  /// Emitted when the user middle-clicks an item. Used to
  /// open a video-first media preview overlay without launching.
  void mediaPreviewRequested(ItemWidget *widget, int visualIndex);
  /// Emitted when the user middle-clicks while holding the artwork-cycle
  /// modifier configured in `GeneralSettings::artworkCycleModifier`
  /// Cycles the displayed artwork through the item's
  /// available types without changing selection or launching.
  void artworkTypeCycleRequested(ItemWidget *widget, int visualIndex);
  void clearSelectionRequested();
  void slashKeyPressed();
  void escapeKeyPressed();
  void activityDetected();
  void wheelScrollStarted();
  void wheelScrollEnded();
  void scrollbarClicked();
  void requestStopRepeat(bool suppressRecentering);

private:
  // Event handlers
  [[nodiscard]] bool handleActivityEvent(QEvent *event);
  [[nodiscard]] bool handleMouseButtonPress(QObject *obj, QEvent *event);
  [[nodiscard]] bool handleMouseButtonRelease(QObject *obj, QEvent *event);
  [[nodiscard]] bool handleHoverSelection(QObject *obj, QEvent *event);
  [[nodiscard]] bool handleWheelEvent(QObject *obj, QEvent *event);
  /// True while any fullscreen artwork overlay is up — the grid's or the
  /// details pane's. The app filter stands down for both keys and wheel
  /// so the overlay's own handlers run (field reports 2026-08-18).
  [[nodiscard]] bool artworkOverlayVisible() const;
  [[nodiscard]] bool handleKeyPressEvent(QObject *obj, QEvent *event);
  [[nodiscard]] bool handleKeyReleaseEvent(QObject *obj, QEvent *event);
  [[nodiscard]] bool handleMouseDoubleClick(QObject *obj, QEvent *event);
  [[nodiscard]] bool handleMousePress(QObject *obj, QEvent *event);

  // Helper methods
  [[nodiscard]] int getCurrentGridWidth() const;
  [[nodiscard]] QList<int> getSubcollections(int parentIndex) const;
  [[nodiscard]] ItemWidget *itemWidgetForObject(QObject *obj) const;
  [[nodiscard]] int visualIndexForWidget(ItemWidget *widget) const;
  [[nodiscard]] bool modalScrapeDialogVisible() const {
    return m_isModalScrapeDialogVisible && m_isModalScrapeDialogVisible();
  }

  // ctx is the single source of truth for sibling managers + state. Inline
  // accessors below are the canonical read path.
  const ApplicationContext *m_ctx = nullptr;
  // Kartend-d2q3l: split the IScrollManager facade into the specific scroll
  // roles this consumer uses (data, search, grid, preview). Read through ctx on
  // every call, never cached.
  [[nodiscard]] IScrollDataSource *scrollData() const {
    return m_ctx ? m_ctx->scrollData() : nullptr;
  }
  [[nodiscard]] IGridLayoutScroll *scrollGrid() const {
    return m_ctx ? m_ctx->scrollGrid() : nullptr;
  }
  [[nodiscard]] ISearchStateScroll *scrollSearch() const {
    return m_ctx ? m_ctx->scrollSearch() : nullptr;
  }
  [[nodiscard]] IArtworkPreviewScroll *scrollPreview() const {
    return m_ctx ? m_ctx->scrollPreview() : nullptr;
  }
  // Kartend-dl0uz.2: the remaining sibling reads go through role views too —
  // each accessor names exactly the slice this event filter drives.
  [[nodiscard]] IKeyEventSink *keyEventSink() const {
    return m_ctx ? m_ctx->keyEventSink() : nullptr;
  }
  [[nodiscard]] IMouseHoldControl *mouseHold() const {
    return m_ctx ? m_ctx->mouseHold() : nullptr;
  }
  [[nodiscard]] IViewportScrollState *viewportScrollState() const {
    return m_ctx ? m_ctx->viewportScrollState() : nullptr;
  }
  [[nodiscard]] ISelectionCore *selectionCore() const {
    return m_ctx ? m_ctx->selectionCore() : nullptr;
  }
  [[nodiscard]] IUserActivitySink *userActivity() const {
    return m_ctx ? m_ctx->userActivity() : nullptr;
  }
  [[nodiscard]] IFileCollectionLookup *fileCollectionLookup() const {
    return m_ctx ? m_ctx->fileCollectionLookup() : nullptr;
  }
  [[nodiscard]] InteractionStateHolder *state() const {
    return m_ctx ? m_ctx->interactionState() : nullptr;
  }
  GeneralSettings *m_generalSettings = nullptr;

  // Owner-supplied predicate: true while a modal scrape dialog is visible.
  // Defaults to "never visible" until the owner injects the real check.
  std::function<bool()> m_isModalScrapeDialogVisible;

  // UI references
  QPointer<QScrollArea> m_itemScrollArea = nullptr;
  QWidget *m_gridContainer = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  QWidget *m_itemsPage = nullptr;
  QWidget *m_itemsTopBar = nullptr;
  QLineEdit *m_searchBar = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  const int *m_currentCollectionIndex = nullptr;
  /// Restartable countdown that clears continuous-scroll state after the user
  /// stops interacting with the scrollbar. Coalesces rapid presses so an earlier
  /// timer can't clear the flag mid-drag (Kartend-otha). Lazily constructed.
  QTimer *m_continuousScrollClearTimer = nullptr;

  // Hover-to-select state machine. Owns its dwell timer + pending widget
  // pointer so EventManager only forwards Enter / MouseMove events into it.
  std::unique_ptr<HoverScrollHandler> m_hoverScroll;
  // Wheel-scroll state machine. Owns its reentrancy guard, animation
  // handoff, and selection-delta math so EventManager only forwards
  // QEvent::Wheel into it.
  std::unique_ptr<WheelEventHandler> m_wheelHandler;
};

#endif // EVENTMANAGER_H
