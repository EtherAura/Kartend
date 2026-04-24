#ifndef EVENTMANAGER_H
#define EVENTMANAGER_H

#include "collectionutils.h"
#include "setuputils.h"
#include <QObject>
#include <QPointer>
#include <QScrollArea>

QT_BEGIN_NAMESPACE
class QEvent;
class QLineEdit;
class QMouseEvent;
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
class ArtworkManager;
class DatabaseManager;
class SidebarManager;
class InteractionStateHolder;
struct ApplicationContext;

/**
 * @brief Setup struct for EventManager dependencies.
 *
 * Follows the same pattern as other manager setup structs, with ctx fallback.
 */
struct EventManagerSetup {
  const ApplicationContext *ctx = nullptr;

  // Manager dependencies (can be overridden or taken from ctx)
  ScrollManager *scrollManager = nullptr;
  KeyboardManager *keyboardManager = nullptr;
  MouseManager *mouseManager = nullptr;
  AnimationManager *animationManager = nullptr;
  ViewportManager *viewportManager = nullptr;
  SelectionManager *selectionManager = nullptr;
  ArtworkManager *artworkManager = nullptr;
  DatabaseManager *databaseManager = nullptr;
  SidebarManager *sidebarManager = nullptr;

  // UI elements (can be overridden or taken from ctx)
  QScrollArea *itemScrollArea = nullptr;
  QWidget *gridContainer = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QWidget *itemsPage = nullptr;
  QWidget *itemsTopBar = nullptr;
  QLineEdit *searchBar = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
  GeneralSettings *generalSettings = nullptr;

  // Accessors with ctx fallback
  SETUP_GETTER_DECL(ScrollManager *, ScrollManager)
  [[nodiscard]] KeyboardManager *getKeyboardManager() const { return keyboardManager; }
  [[nodiscard]] MouseManager *getMouseManager() const { return mouseManager; }
  SETUP_GETTER_DECL(AnimationManager *, AnimationManager)
  SETUP_GETTER_DECL(ViewportManager *, ViewportManager)
  SETUP_GETTER_DECL(SelectionManager *, SelectionManager)
  SETUP_GETTER_DECL(ArtworkManager *, ArtworkManager)
  SETUP_GETTER_DECL(DatabaseManager *, DatabaseManager)
  SETUP_GETTER_DECL(SidebarManager *, SidebarManager)
  SETUP_GETTER_DECL(QScrollArea *, ItemScrollArea)
  SETUP_GETTER_DECL(QWidget *, GridContainer)
  SETUP_GETTER_DECL(QStackedWidget *, StackedWidget)
  SETUP_GETTER_DECL(QWidget *, ItemsPage)
  SETUP_GETTER_DECL(QWidget *, ItemsTopBar)
  SETUP_GETTER_DECL(QLineEdit *, SearchBar)
  SETUP_GETTER_DECL(QList<CollectionConfig> *, Collections)
  SETUP_GETTER_DECL(int *, CurrentCollectionIndex)
  SETUP_GETTER_DECL(GeneralSettings *, GeneralSettings)
  SETUP_GETTER_DECL_CTX_ONLY(InteractionStateHolder *, InteractionState)
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
  [[nodiscard]] bool handleWheelEvent(QObject *obj, QEvent *event);
  [[nodiscard]] bool handleKeyPressEvent(QObject *obj, QEvent *event);
  [[nodiscard]] bool handleKeyReleaseEvent(QObject *obj, QEvent *event);
  [[nodiscard]] bool handleMouseDoubleClick(QObject *obj, QEvent *event);
  [[nodiscard]] bool handleMousePress(QObject *obj, QEvent *event);

  // Helper methods
  [[nodiscard]] int getCurrentGridWidth() const;
  [[nodiscard]] QList<int> getSubcollections(int parentIndex) const;
  [[nodiscard]] bool applyWheelSelectionDelta(int wheelSteps);

  // Manager references
  ScrollManager *m_scrollManager = nullptr;
  KeyboardManager *m_keyboardManager = nullptr;
  MouseManager *m_mouseManager = nullptr;
  AnimationManager *m_animationManager = nullptr;
  ViewportManager *m_viewportManager = nullptr;
  SelectionManager *m_selectionManager = nullptr;
  ArtworkManager *m_artworkManager = nullptr;
  DatabaseManager *m_databaseManager = nullptr;
  SidebarManager *m_sidebarManager = nullptr;
  InteractionStateHolder *m_state = nullptr;
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

  // Reentrancy guard for wheel event handling
  bool m_processingWheelEvent = false;
};

#endif // EVENTMANAGER_H
