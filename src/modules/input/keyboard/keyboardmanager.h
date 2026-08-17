#ifndef KEYBOARDMANAGER_H
#define KEYBOARDMANAGER_H

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "ikeyboardmanager.h"
#include "setuputils.h"
#include <QKeyEvent>
#include <QObject>
#include <QPointer>
#include <QScrollArea>
#include <QtCore/Qt>
#include <QTimer>

class QWidget;
class QLineEdit;
class QStackedWidget;
class ScrollManager;
class InteractionStateHolder;
#include "applicationcontext_fwd.h"

struct KeyboardManagerSetup {
  const ApplicationContext *ctx = nullptr;
  const GeneralSettings *generalSettings = nullptr;

  QWidget *gridContainer = nullptr;
  QWidget *itemsPage = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QLineEdit *searchBar = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  const int *currentCollectionIndex = nullptr;

  SETUP_GETTER_DECL(QWidget *, GridContainer)
  SETUP_GETTER_DECL(QWidget *, ItemsPage)
  SETUP_GETTER_DECL(QScrollArea *, ItemScrollArea)
  SETUP_GETTER_DECL(QStackedWidget *, StackedWidget)
  SETUP_GETTER_DECL(QLineEdit *, SearchBar)
  SETUP_GETTER_DECL(QList<CollectionConfig> *, Collections)
  SETUP_GETTER_DECL(const int *, CurrentCollectionIndex)
};

/**
 * @brief Handles keyboard input processing and key repeat logic.
 *
 * Manages arrow key navigation, key hold/repeat behavior, and keyboard
 * shortcuts. Coordinates with ScrollManager for selection updates and
 * InteractionManager for centering.
 */
// QObject must be the first base; IKeyboardManager is a plain (non-QObject)
// role interface — single-QObject-base multiple inheritance.
class KeyboardManager : public QObject, public IKeyboardManager {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(KeyboardManager)

public:
  explicit KeyboardManager(QObject *parent = nullptr);
  ~KeyboardManager() override;

  void setupReferences(const KeyboardManagerSetup &setup);

  // Key event handling
  [[nodiscard]] bool handleKeyPress(QKeyEvent *event, bool searchBarFocused) override;
  [[nodiscard]] bool handleKeyRelease(QKeyEvent *event) override;

  // Key repeat management
  void beginHoldRepeat();
  void stopRepeat(bool suppressRecentering = false) override;
  [[nodiscard]] bool isRepeating() const override { return m_repeating; }
  void finalizeKeyRepeatForKey(Qt::Key key, int direction, bool vertical) override;
  [[nodiscard]] bool consumePendingNavigationKey(Qt::Key &outKey) override;

  // Navigation state
  [[nodiscard]] bool isPhysicalKeyDown() const override { return m_physicalKeyDown; }
  void setPhysicalKeyDown(bool down) override { m_physicalKeyDown = down; }
  [[nodiscard]] int repeatDelta() const override { return m_repeatDelta; }
  [[nodiscard]] bool repeatVertical() const override { return m_repeatVertical; }
  [[nodiscard]] Qt::Key repeatKey() const { return m_repeatKey; }

  // Wrap navigation state
  [[nodiscard]] bool isWrapSequenceActive() const override { return m_wrapSequenceActive; }
  void setWrapSequenceActive(bool active) override { m_wrapSequenceActive = active; }

  // Continuous scroll state
  [[nodiscard]] bool isContinuousScrollActive() const override { return m_continuousScrollActive; }
  void setContinuousScrollActive(bool active) override { m_continuousScrollActive = active; }

  // Selection calculation helpers
  static int calculateNewSelection(int totalItems, int currentSelection, int direction,
                                   bool wrapEnabled, bool vertical, int gridWidth, bool &didWrap,
                                   bool horizontalLayout = false);
  static int calculateHorizontalSelection(int totalItems, int currentSelection, int direction,
                                          bool wrapEnabled, bool &didWrap);
  static int calculateVerticalSelection(int totalItems, int currentSelection, int direction,
                                        bool wrapEnabled, int gridWidth, bool &didWrap);
  /// column-major selection move for the Horizontal view mode.
  /// `direction` is ±1 for Up/Down (within-column step) or ±itemsPerCol for
  /// Left/Right (between-column step). Wrap behavior mirrors Grid mode but on
  /// the transposed axis: Up/Down wraps within the same column, Left/Right
  /// wraps to first/last column.
  static int calculateHorizontalLayoutSelection(int totalItems, int currentSelection, int direction,
                                                bool wrapEnabled, int itemsPerCol, bool downAxis,
                                                bool &didWrap);
  static bool hasRowChanged(int gridWidth, int currentSelection, int newSelection);

  // Key direction derivation
  static bool deriveDirectionForKey(int key, int gridWidth, int &direction, bool &vertical);

  // Prepare for key navigation step
  void prepareKeyNavigationState() override;

  // Finalize key repeat state
  void finalizeKeyRepeat(QKeyEvent *event, int direction, bool vertical) override;

signals:
  // Action requests for InteractionManager
  void requestSelectionMove(int direction, bool vertical);
  void requestAlphabeticNavigation(bool forward);
  void requestJumpToEdge(bool toEnd); // Home/End key navigation
  void requestEnterAction();
  /// request to open the item detail page for the current
  /// selection. Bound to GeneralSettings::keyItemDetails (default I).
  /// Suppressed while the search bar has focus so typed letters still
  /// reach the filter.
  void requestItemDetails();
  /// Jump to the synthetic Home view. Bound to GeneralSettings::keyHomeView
  /// when useHomeView is enabled and the binding is non-zero.
  void requestHomeView();
  /// Toggle the collection tree panel (Kartend-ob1c9). Bound to
  /// GeneralSettings::keyToggleCollectionTree when non-zero.
  void requestToggleCollectionTree();
  void requestSearchModeToggle();
  void requestSearchBarFocus();
  void requestScrollAnimationStop();
  void requestEscapeAction();
  void requestClearSearchBar();
  void requestFocusGrid();

  // Repeat step request - for InteractionManager to perform repeat navigation
  void repeatStepRequested();
  void stopRepeatRequested(bool suppressRecentering);

private slots:
  void onRepeatStep();
  void onRepeatStartTimeout();

private:
  void initTimers();
  void cleanupTimers();
  void configureRepeatProperties();
  void clearRepeatState();

  // Repeat lifecycle as a single state (Kartend-4su6t). The shared interaction
  // flags hold-repeat touches used to be set in beginHoldRepeat and cleared in a
  // *different* combination in stopRepeat, which let them drift (recurring
  // stuck-flag bugs: suppressed artwork, disabled centering). applyRepeatPhase()
  // is the ONE place that maps a phase onto every repeat-owned flag, so begin and
  // stop can no longer diverge. Behaviour is identical to the old pair; the
  // per-phase flag values are just single-sourced here.
  //
  // m_continuousScrollActive is only *asserted* here (repeat is one of several
  // continuous-scroll sources — wheel, mouse-hold, glide); its clear stays with
  // the animation/arrow handlers that own the glide tail, exactly as before.
  enum class RepeatPhase { Idle, VerticalRepeat, HorizontalRepeat };
  void applyRepeatPhase(RepeatPhase phase);

  // Repeat state
  QTimer *m_repeatTimer = nullptr;
  QTimer *m_repeatStartTimer = nullptr;
  bool m_repeating = false;
  Qt::Key m_repeatKey = Qt::Key_unknown;
  int m_repeatDelta = 0;
  bool m_repeatVertical = false;
  bool m_physicalKeyDown = false;
  int m_repeatInterval = 0;

  // Navigation state
  bool m_wrapSequenceActive = false;
  bool m_continuousScrollActive = false;
  bool m_isShuttingDown = false;

  // Pending navigation key: used to associate custom key bindings with the
  // repeat pipeline (ArrowNavigationHandler doesn't receive the QKeyEvent).
  bool m_hasPendingNavigationKey = false;
  Qt::Key m_pendingNavigationKey = Qt::Key_unknown;
  qint64 m_pendingNavigationKeyAtMs = 0;

  // ctx is the single source of truth for sibling managers (ScrollManager,
  // InteractionStateHolder) — never cache them as direct fields.
  const ApplicationContext *m_ctx = nullptr;
  const GeneralSettings *m_generalSettings = nullptr;
  QWidget *m_gridContainer = nullptr;
  QWidget *m_itemsPage = nullptr;
  QPointer<QScrollArea> m_itemScrollArea = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  QLineEdit *m_searchBar = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  const int *m_currentCollectionIndex = nullptr;
};

#endif // KEYBOARDMANAGER_H
