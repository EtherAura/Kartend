#ifndef ARROWNAVIGATIONHANDLER_H
#define ARROWNAVIGATIONHANDLER_H

#include "setuputils.h"
#include <QObject>
#include <QPointer>
#include <QScrollArea>
#include <functional>

QT_BEGIN_NAMESPACE
class QStackedWidget;
class QWidget;
QT_END_NAMESPACE

class KeyboardManager;
class ScrollManager;
class AnimationManager;
class ViewportManager;
class SelectionManager;
class InteractionStateHolder;
struct CollectionConfig;
struct GeneralSettings;
struct ApplicationContext;
template <typename T> class QList;

/**
 * @brief Setup struct for ArrowNavigationHandler dependencies.
 */
struct ArrowNavigationHandlerSetup {
  const ApplicationContext *ctx = nullptr;

  // Manager dependencies (can be overridden or taken from ctx)
  KeyboardManager *keyboardManager = nullptr;
  ScrollManager *scrollManager = nullptr;
  AnimationManager *animationManager = nullptr;
  ViewportManager *viewportManager = nullptr;
  SelectionManager *selectionManager = nullptr;

  // UI elements
  QScrollArea *itemScrollArea = nullptr;
  QWidget *gridContainer = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QWidget *itemsPage = nullptr;

  // State references
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
  GeneralSettings *generalSettings = nullptr;

  // Getters with ctx fallback
  SETUP_GETTER_DECL(KeyboardManager *, KeyboardManager)
  SETUP_GETTER_DECL(ScrollManager *, ScrollManager)
  SETUP_GETTER_DECL(AnimationManager *, AnimationManager)
  SETUP_GETTER_DECL(ViewportManager *, ViewportManager)
  SETUP_GETTER_DECL(SelectionManager *, SelectionManager)
  SETUP_GETTER_DECL(QScrollArea *, ItemScrollArea)
  SETUP_GETTER_DECL(QWidget *, GridContainer)
  SETUP_GETTER_DECL(QStackedWidget *, StackedWidget)
  SETUP_GETTER_DECL(QWidget *, ItemsPage)
  SETUP_GETTER_DECL(QList<CollectionConfig> *, Collections)
  SETUP_GETTER_DECL(int *, CurrentCollectionIndex)
  SETUP_GETTER_DECL(GeneralSettings *, GeneralSettings)
  SETUP_GETTER_DECL_CTX_ONLY(InteractionStateHolder *, InteractionState)
};

/**
 * @brief Handles arrow key navigation and key hold repeat logic.
 *
 * Extracts arrow key handling from InteractionManager to reduce complexity.
 * Coordinates with KeyboardManager for state, SelectionManager for selection
 * updates, and ViewportManager for visibility adjustments.
 */
class ArrowNavigationHandler : public QObject {
  Q_OBJECT

public:
  explicit ArrowNavigationHandler(QObject *parent = nullptr);
  ~ArrowNavigationHandler() override;

  void setupReferences(const ArrowNavigationHandlerSetup &setup);

  // Callback setters for parent coordination
  using SelectionCallback = std::function<int()>;
  using GridWidthCallback = std::function<int()>;
  using OffscreenCheckCallback = std::function<bool(int, int)>;

  void setGetCurrentSelection(SelectionCallback callback) {
    m_getCurrentSelection = std::move(callback);
  }
  void setGetCurrentGridWidth(GridWidthCallback callback) {
    m_getCurrentGridWidth = std::move(callback);
  }
  void setIsItemOffscreen(OffscreenCheckCallback callback) {
    m_isItemOffscreen = std::move(callback);
  }

  // Main arrow key handler - called from InteractionManager
  void handleArrowKeyNavigation(int direction, bool vertical);

  // Key repeat step handler - called when KeyboardManager emits repeatStep
  void handleRepeatStep();

  // Stop repeat and finalize state
  void handleStopRepeat(bool suppressRecentering);

signals:
  // Request InteractionManager to update selection with full side effects
  void requestFullSelectionUpdate(int newIndex);

  // Request centering for recenter after stop
  void requestRecenter();

  // Request horizontal suppression
  void requestMinorHorizontalSuppress();

  // Request focus on items page
  void requestFocusItemsPage();

private:
  // Visibility helpers
  void performVisibilityForKeyMove(bool isNewRow, int newSelection);

  // Helpers
  [[nodiscard]] int getCurrentGridWidth() const;
  [[nodiscard]] int getCurrentSelection() const;
  [[nodiscard]] int getTotalItems() const;
  [[nodiscard]] bool isWrapEnabled() const;

  // Manager references
  KeyboardManager *m_keyboardManager = nullptr;
  ScrollManager *m_scrollManager = nullptr;
  AnimationManager *m_animationManager = nullptr;
  ViewportManager *m_viewportManager = nullptr;
  SelectionManager *m_selectionManager = nullptr;
  InteractionStateHolder *m_state = nullptr;
  QPointer<QScrollArea> m_itemScrollArea = nullptr;
  QWidget *m_gridContainer = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  QWidget *m_itemsPage = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  int *m_currentCollectionIndex = nullptr;
  GeneralSettings *m_generalSettings = nullptr;

  // Callbacks for parent data access
  SelectionCallback m_getCurrentSelection;
  GridWidthCallback m_getCurrentGridWidth;
  OffscreenCheckCallback m_isItemOffscreen;
};

#endif // ARROWNAVIGATIONHANDLER_H
