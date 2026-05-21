#ifndef ARROWNAVIGATIONHANDLER_H
#define ARROWNAVIGATIONHANDLER_H

#include "setuputils.h"
#include <functional>
#include <QObject>
#include <QPointer>
#include <QScrollArea>

QT_BEGIN_NAMESPACE
class QStackedWidget;
class QWidget;
QT_END_NAMESPACE

class IKeyboardManager;
class IScrollManager;
class IAnimationManager;
class IViewportManager;
class ISelectionManager;
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

  // Sibling managers come from ctx; only non-manager fields stay here.
  QScrollArea *itemScrollArea = nullptr;
  QWidget *gridContainer = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QWidget *itemsPage = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
  GeneralSettings *generalSettings = nullptr;

  SETUP_GETTER_DECL(QScrollArea *, ItemScrollArea)
  SETUP_GETTER_DECL(QWidget *, GridContainer)
  SETUP_GETTER_DECL(QStackedWidget *, StackedWidget)
  SETUP_GETTER_DECL(QWidget *, ItemsPage)
  SETUP_GETTER_DECL(QList<CollectionConfig> *, Collections)
  SETUP_GETTER_DECL(int *, CurrentCollectionIndex)
  SETUP_GETTER_DECL(GeneralSettings *, GeneralSettings)
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
  Q_DISABLE_COPY_MOVE(ArrowNavigationHandler)

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

  // ctx is the single source of truth for sibling managers + state.
  const ApplicationContext *m_ctx = nullptr;
  [[nodiscard]] IKeyboardManager *keyboardMgr() const {
    return m_ctx ? m_ctx->keyboardManager() : nullptr;
  }
  [[nodiscard]] IScrollManager *scrollMgr() const {
    return m_ctx ? m_ctx->scrollManager() : nullptr;
  }
  [[nodiscard]] IAnimationManager *animMgr() const {
    return m_ctx ? m_ctx->animationManager() : nullptr;
  }
  [[nodiscard]] IViewportManager *viewportMgr() const {
    return m_ctx ? m_ctx->viewportManager() : nullptr;
  }
  [[nodiscard]] ISelectionManager *selectionMgr() const {
    return m_ctx ? m_ctx->selectionManager() : nullptr;
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

  // Callbacks for parent data access
  SelectionCallback m_getCurrentSelection;
  GridWidthCallback m_getCurrentGridWidth;
  OffscreenCheckCallback m_isItemOffscreen;
};

#endif // ARROWNAVIGATIONHANDLER_H
