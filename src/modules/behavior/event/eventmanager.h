#ifndef EVENTMANAGER_H
#define EVENTMANAGER_H

#include "collectionutils.h"
#include "setuputils.h"
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
class QWidget;
QT_END_NAMESPACE

class ItemWidget;
class ScrollManager;
class KeyboardManager;
class MouseManager;
class AnimationManager;
class ViewportManager;
class SelectionManager;
class IArtworkManager;
class IDatabaseManager;
class IDetailsPaneManager;
class InteractionStateHolder;
class HoverScrollHandler;
class WheelEventHandler;
struct ApplicationContext;

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
  int *currentCollectionIndex = nullptr;
  GeneralSettings *generalSettings = nullptr;

  SETUP_GETTER_DECL(QScrollArea *, ItemScrollArea)
  SETUP_GETTER_DECL(QWidget *, GridContainer)
  SETUP_GETTER_DECL(QStackedWidget *, StackedWidget)
  SETUP_GETTER_DECL(QWidget *, ItemsPage)
  SETUP_GETTER_DECL(QWidget *, ItemsTopBar)
  SETUP_GETTER_DECL(QLineEdit *, SearchBar)
  SETUP_GETTER_DECL(QList<CollectionConfig> *, Collections)
  SETUP_GETTER_DECL(int *, CurrentCollectionIndex)
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

public:
  explicit EventManager(QObject *parent = nullptr);
  ~EventManager() override;

  // Setup using struct pattern (preferred)
  void setupReferences(const EventManagerSetup &setup);

  // Event filter installation
  void installEventFilters();

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
  [[nodiscard]] bool handleKeyPressEvent(QObject *obj, QEvent *event);
  [[nodiscard]] bool handleKeyReleaseEvent(QObject *obj, QEvent *event);
  [[nodiscard]] bool handleMouseDoubleClick(QObject *obj, QEvent *event);
  [[nodiscard]] bool handleMousePress(QObject *obj, QEvent *event);

  // Helper methods
  [[nodiscard]] int getCurrentGridWidth() const;
  [[nodiscard]] QList<int> getSubcollections(int parentIndex) const;
  [[nodiscard]] ItemWidget *itemWidgetForObject(QObject *obj) const;
  [[nodiscard]] int visualIndexForWidget(ItemWidget *widget) const;

  // ctx is the single source of truth for sibling managers + state. Inline
  // accessors below are the canonical read path.
  const ApplicationContext *m_ctx = nullptr;
  [[nodiscard]] ScrollManager *scrollMgr() const {
    return m_ctx ? m_ctx->scrollManager() : nullptr;
  }
  [[nodiscard]] KeyboardManager *keyboardMgr() const {
    return m_ctx ? m_ctx->keyboardManager() : nullptr;
  }
  [[nodiscard]] MouseManager *mouseMgr() const { return m_ctx ? m_ctx->mouseManager() : nullptr; }
  [[nodiscard]] AnimationManager *animMgr() const {
    return m_ctx ? m_ctx->animationManager() : nullptr;
  }
  [[nodiscard]] ViewportManager *viewportMgr() const {
    return m_ctx ? m_ctx->viewportManager() : nullptr;
  }
  [[nodiscard]] SelectionManager *selectionMgr() const {
    return m_ctx ? m_ctx->selectionManager() : nullptr;
  }
  [[nodiscard]] IArtworkManager *artworkMgr() const {
    return m_ctx ? m_ctx->artworkManager() : nullptr;
  }
  [[nodiscard]] IDatabaseManager *databaseMgr() const {
    return m_ctx ? m_ctx->databaseManager() : nullptr;
  }
  [[nodiscard]] IDetailsPaneManager *detailsPaneMgr() const {
    return m_ctx ? m_ctx->detailsPaneManager() : nullptr;
  }
  [[nodiscard]] InteractionStateHolder *state() const {
    return m_ctx ? m_ctx->interactionState() : nullptr;
  }
  GeneralSettings *m_generalSettings = nullptr;

  // UI references
  QPointer<QScrollArea> m_itemScrollArea = nullptr;
  QWidget *m_gridContainer = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  QWidget *m_itemsPage = nullptr;
  QWidget *m_itemsTopBar = nullptr;
  QLineEdit *m_searchBar = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  int *m_currentCollectionIndex = nullptr;

  // Hover-to-select state machine. Owns its dwell timer + pending widget
  // pointer so EventManager only forwards Enter / MouseMove events into it.
  std::unique_ptr<HoverScrollHandler> m_hoverScroll;
  // Wheel-scroll state machine. Owns its reentrancy guard, animation
  // handoff, and selection-delta math so EventManager only forwards
  // QEvent::Wheel into it.
  std::unique_ptr<WheelEventHandler> m_wheelHandler;
};

#endif // EVENTMANAGER_H
