#ifndef KEYBOARDMANAGER_H
#define KEYBOARDMANAGER_H

#include "collectionutils.h"
#include "setuputils.h"
#include <QKeyEvent>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QWidget;
class QScrollArea;
class QLineEdit;
class QStackedWidget;
class ScrollManager;
class InteractionStateHolder;
struct ApplicationContext;

struct KeyboardManagerSetup {
  const ApplicationContext *ctx = nullptr;

  ScrollManager *scrollManager = nullptr;
  QWidget *gridContainer = nullptr;
  QWidget *itemsPage = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QLineEdit *searchBar = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;

  SETUP_GETTER_DECL(ScrollManager*, ScrollManager)
  SETUP_GETTER_DECL(QWidget*, GridContainer)
  SETUP_GETTER_DECL(QWidget*, ItemsPage)
  SETUP_GETTER_DECL(QScrollArea*, ItemScrollArea)
  SETUP_GETTER_DECL(QStackedWidget*, StackedWidget)
  SETUP_GETTER_DECL(QLineEdit*, SearchBar)
  SETUP_GETTER_DECL(QList<CollectionConfig>*, Collections)
  SETUP_GETTER_DECL(int*, CurrentCollectionIndex)
  SETUP_GETTER_DECL_CTX_ONLY(InteractionStateHolder*, InteractionState)
};

/**
 * @brief Handles keyboard input processing and key repeat logic.
 *
 * Manages arrow key navigation, key hold/repeat behavior, and keyboard
 * shortcuts. Coordinates with ScrollManager for selection updates and
 * InteractionManager for centering.
 */
class KeyboardManager : public QObject {
  Q_OBJECT

public:
  explicit KeyboardManager(QObject *parent = nullptr);
  ~KeyboardManager() override;

  void setupReferences(const KeyboardManagerSetup &setup);

  // Key event handling
  [[nodiscard]] bool handleKeyPress(QKeyEvent *event, bool searchBarFocused);
  [[nodiscard]] bool handleKeyRelease(QKeyEvent *event);

  // Key repeat management
  void beginHoldRepeat();
  void stopRepeat(bool suppressRecentering = false);
  [[nodiscard]] bool isRepeating() const { return m_repeating; }

  // Navigation state
  [[nodiscard]] bool isPhysicalKeyDown() const { return m_physicalKeyDown; }
  void setPhysicalKeyDown(bool down) { m_physicalKeyDown = down; }
  [[nodiscard]] int repeatDelta() const { return m_repeatDelta; }
  [[nodiscard]] bool repeatVertical() const { return m_repeatVertical; }
  [[nodiscard]] Qt::Key repeatKey() const { return m_repeatKey; }

  // Wrap navigation state
  [[nodiscard]] bool isWrapSequenceActive() const {
    return m_wrapSequenceActive;
  }
  void setWrapSequenceActive(bool active) { m_wrapSequenceActive = active; }

  // Continuous scroll state
  [[nodiscard]] bool isContinuousScrollActive() const {
    return m_continuousScrollActive;
  }
  void setContinuousScrollActive(bool active) {
    m_continuousScrollActive = active;
  }

  // Selection calculation helpers
  static int calculateNewSelection(int totalItems, int currentSelection,
                                   int direction, bool wrapEnabled,
                                   bool vertical, int gridWidth, bool &didWrap);
  static int calculateHorizontalSelection(int totalItems, int currentSelection,
                                          int direction, bool wrapEnabled,
                                          bool &didWrap);
  static int calculateVerticalSelection(int totalItems, int currentSelection,
                                        int direction, bool wrapEnabled,
                                        int gridWidth, bool &didWrap);
  static bool hasRowChanged(int gridWidth, int currentSelection,
                            int newSelection);

  // Key direction derivation
  static bool deriveDirectionForKey(int key, int gridWidth, int &direction,
                                    bool &vertical);

  // Prepare for key navigation step
  void prepareKeyNavigationState();

  // Finalize key repeat state
  void finalizeKeyRepeat(QKeyEvent *event, int direction, bool vertical);

signals:
  // Action requests for InteractionManager
  void requestSelectionMove(int direction, bool vertical);
  void requestAlphabeticNavigation(bool forward);
  void requestEnterAction();
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

  // Manager references
  InteractionStateHolder *m_state = nullptr;
  ScrollManager *m_scrollManager = nullptr;
  QWidget *m_gridContainer = nullptr;
  QWidget *m_itemsPage = nullptr;
  QPointer<QScrollArea> m_itemScrollArea = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  QLineEdit *m_searchBar = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  int *m_currentCollectionIndex = nullptr;
};

#endif // KEYBOARDMANAGER_H
