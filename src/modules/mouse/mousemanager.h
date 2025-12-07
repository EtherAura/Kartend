#ifndef MOUSEMANAGER_H
#define MOUSEMANAGER_H

#include "setuputils.h"
#include <QObject>
#include <QPoint>
#include <QTimer>
#include <QVector>

QT_BEGIN_NAMESPACE
class QScrollArea;
class QWheelEvent;
class QWidget;
QT_END_NAMESPACE

class ScrollManager;
class SelectionManager;
class ItemWidget;
class InteractionStateHolder;
struct CollectionConfig;
struct GeneralSettings;
struct ApplicationContext;

/// Setup struct for MouseManager dependencies
struct MouseManagerSetup {
  const ApplicationContext *ctx = nullptr;

  ScrollManager *scrollManager = nullptr;
  SelectionManager *selectionManager = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QWidget *gridContainer = nullptr;
  const QVector<CollectionConfig> *collections = nullptr;
  const int *currentCollectionIndex = nullptr;
  GeneralSettings *generalSettings = nullptr;

  SETUP_GETTER_DECL(ScrollManager*, ScrollManager)
  SETUP_GETTER_DECL(SelectionManager*, SelectionManager)
  SETUP_GETTER_DECL(QScrollArea*, ItemScrollArea)
  SETUP_GETTER_DECL(QWidget*, GridContainer)
  SETUP_GETTER_DECL(const QVector<CollectionConfig>*, Collections)
  SETUP_GETTER_DECL(const int*, CurrentCollectionIndex)
  SETUP_GETTER_DECL(GeneralSettings*, GeneralSettings)
  SETUP_GETTER_DECL_CTX_ONLY(InteractionStateHolder*, InteractionState)
};

/// Manages mouse hold scrolling behavior for click-and-hold navigation.
/// Handles both vertical (row-by-row) and horizontal (item-by-item) scrolling.
/// Also manages click hold timer and left mouse button tracking.
class MouseManager : public QObject {
  Q_OBJECT
public:
  explicit MouseManager(QObject *parent = nullptr);
  ~MouseManager() override;

  void setupReferences(const MouseManagerSetup &setup);

  // --- State Queries ---
  [[nodiscard]] bool isMouseHoldScrolling() const { return m_mouseHoldScrolling; }
  [[nodiscard]] bool isHorizontalMode() const { return m_mouseHoldHorizontal; }
  [[nodiscard]] int holdDirection() const { return m_mouseHoldDirection; }
  [[nodiscard]] int horizontalDirection() const { return m_mouseHoldHorizontalDirection; }
  [[nodiscard]] bool isLeftMouseDown() const { return m_leftMouseDown; }
  [[nodiscard]] bool isWheelScrolling() const { return m_wheelScrolling; }

  // --- Wheel Scrolling State ---
  void setWheelScrolling(bool scrolling);

  // --- Wheel Computation (static) ---
  /// Computes wheel scroll steps from angleDelta or pixelDelta
  [[nodiscard]] static int computeWheelSteps(const QWheelEvent *wheelEvent);

  // --- Left Mouse Button Tracking ---
  void setLeftMouseDown(bool down);

  // --- Click Hold Timer ---
  /// Starts the click hold timer that triggers hold scrolling after delay
  void startClickHoldTimer(const QPoint &clickPos, int selectedItemIndex,
                           int gridWidth, int totalItems);

  /// Stops the click hold timer if active
  void stopClickHoldTimer();

  /// Returns true if click hold timer is active
  [[nodiscard]] bool isClickHoldTimerActive() const;

  // --- Click Hold Horizontal Candidate ---
  /// Updates horizontal hold candidate based on click selection change
  void updateClickHoldHorizontalCandidate(int previousSelection,
                                          int targetSelection,
                                          int gridWidth);

  /// Clears the horizontal hold candidate state
  void clearHorizontalCandidate();

  // --- Hold Scrolling Control ---
  /// Starts mouse hold scrolling from a click position
  void startMouseHoldScrolling(const QPoint &clickPos, int selectedItemIndex,
                               int gridWidth, int totalItems);

  /// Attempts to start horizontal click hold mode
  /// Returns true if horizontal hold was started
  [[nodiscard]] bool tryStartHorizontalClickHold(int totalItems, int selectedItemIndex);

  /// Stops mouse hold scrolling
  void stopMouseHoldScrolling();

  // --- Widget Finding Utilities (static) ---
  /// Finds the best widget at the given click position
  static ItemWidget *findBestWidgetForClick(
      const QPoint &clickPos,
      ScrollManager *scrollManager,
      QWidget *gridContainer);

  /// Finds the closest widget to the click position from candidates
  static ItemWidget *findClosestWidget(
      const QVector<ItemWidget *> &candidates,
      const QPoint &clickPos);

signals:
  /// Emitted when a scroll step should advance selection
  /// direction: -1 for up/left, +1 for down/right
  /// isHorizontal: true for horizontal movement
  void scrollStepRequested(int direction, bool isHorizontal);

  /// Emitted when hold scrolling starts
  void holdScrollingStarted(bool isHorizontal);

  /// Emitted when hold scrolling stops
  void holdScrollingStopped();

  /// Request to update selection to a specific index during horizontal hold
  void requestSelectionUpdate(int index);

  /// Request to set scroll manager overlay visibility
  void requestOverlayVisibility(bool visible);

  /// Request to set properties on the scroll area
  void requestScrollAreaProperty(const char *name, bool value);

  /// Request to set a dynamic property
  void requestSetProperty(const char *name, const QVariant &value);

private slots:
  void onMouseHoldScrollStep();
  void onClickHoldTimerTimeout();

private:
  /// Computes vertical scroll direction based on selected item position
  [[nodiscard]] int computeVerticalDirection(int selectedItemIndex, int gridWidth) const;

  // References (not owned)
  ScrollManager *m_scrollManager = nullptr;
  SelectionManager *m_selectionManager = nullptr;
  InteractionStateHolder *m_state = nullptr;
  QScrollArea *m_itemScrollArea = nullptr;
  QWidget *m_gridContainer = nullptr;
  const QVector<CollectionConfig> *m_collections = nullptr;
  const int *m_currentCollectionIndex = nullptr;
  GeneralSettings *m_generalSettings = nullptr;

  // Left mouse button state
  bool m_leftMouseDown = false;

  // Wheel scrolling state
  bool m_wheelScrolling = false;

  // Click hold timer (initiates hold scrolling after delay)
  QTimer *m_clickHoldTimer = nullptr;
  QPoint m_clickHoldPos;
  int m_clickHoldSelectedIndex = -1;
  int m_clickHoldGridWidth = 0;
  int m_clickHoldTotalItems = 0;

  // Hold scrolling state
  QTimer *m_mouseHoldTimer = nullptr;
  bool m_mouseHoldScrolling = false;
  int m_mouseHoldDirection = 0;
  bool m_mouseHoldHorizontal = false;
  bool m_clickHoldHorizontalEligible = false;
  int m_mouseHoldHorizontalDirection = 0;
  int m_mouseHoldHorizontalStartIndex = -1;

  // Cached for step callbacks
  int m_cachedGridWidth = 0;
  int m_cachedSelectedIndex = -1;
};

#endif // MOUSEMANAGER_H
