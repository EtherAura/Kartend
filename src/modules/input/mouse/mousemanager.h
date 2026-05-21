#ifndef MOUSEMANAGER_H
#define MOUSEMANAGER_H

#include "imousemanager.h"
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

class IScrollManager;
class SelectionManager;
class ItemWidget;
class InteractionStateHolder;
struct CollectionConfig;
struct GeneralSettings;
struct ApplicationContext;

/// Setup struct for MouseManager dependencies
struct MouseManagerSetup {
  const ApplicationContext *ctx = nullptr;

  QScrollArea *itemScrollArea = nullptr;
  QWidget *gridContainer = nullptr;
  const QVector<CollectionConfig> *collections = nullptr;
  const int *currentCollectionIndex = nullptr;
  GeneralSettings *generalSettings = nullptr;

  SETUP_GETTER_DECL(QScrollArea *, ItemScrollArea)
  SETUP_GETTER_DECL(QWidget *, GridContainer)
  SETUP_GETTER_DECL(const QVector<CollectionConfig> *, Collections)
  SETUP_GETTER_DECL(const int *, CurrentCollectionIndex)
  SETUP_GETTER_DECL(GeneralSettings *, GeneralSettings)
};

/// Manages mouse hold scrolling behavior for click-and-hold navigation.
/// Handles both vertical (row-by-row) and horizontal (item-by-item) scrolling.
/// Also manages click hold timer and left mouse button tracking.
//
// QObject must be the first base; IMouseManager is a plain (non-QObject)
// role interface — single-QObject-base multiple inheritance.
class MouseManager : public QObject, public IMouseManager {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(MouseManager)
public:
  explicit MouseManager(QObject *parent = nullptr);
  ~MouseManager() override;

  void setupReferences(const MouseManagerSetup &setup);

  // --- State Queries ---
  [[nodiscard]] bool isMouseHoldScrolling() const override { return m_mouseHoldScrolling; }
  [[nodiscard]] bool isHorizontalMode() const { return m_mouseHoldHorizontal; }
  [[nodiscard]] int holdDirection() const { return m_mouseHoldDirection; }
  [[nodiscard]] int horizontalDirection() const { return m_mouseHoldHorizontalDirection; }
  [[nodiscard]] bool isLeftMouseDown() const { return m_leftMouseDown; }
  [[nodiscard]] bool isWheelScrolling() const { return m_wheelScrolling; }

  // --- Wheel Scrolling State ---
  void setWheelScrolling(bool scrolling) override;

  // --- Wheel Computation (static) ---
  /// Computes wheel scroll steps from angleDelta or pixelDelta
  [[nodiscard]] static int computeWheelSteps(const QWheelEvent *wheelEvent);

  // --- Left Mouse Button Tracking ---
  void setLeftMouseDown(bool down) override;

  // --- Click Hold Timer ---
  /// Starts the click hold timer that triggers hold scrolling after delay
  void startClickHoldTimer(const QPoint &clickPos, int selectedItemIndex, int gridWidth,
                           int totalItems);

  /// Stops the click hold timer if active
  void stopClickHoldTimer() override;

  /// Returns true if click hold timer is active
  [[nodiscard]] bool isClickHoldTimerActive() const;

  // --- Click Hold Horizontal Candidate ---
  /// Updates horizontal hold candidate based on click selection change
  void updateClickHoldHorizontalCandidate(int previousSelection, int targetSelection,
                                          int gridWidth);

  /// Clears the horizontal hold candidate state
  void clearHorizontalCandidate() override;

  // --- Hold Scrolling Control ---
  /// Starts mouse hold scrolling from a click position
  void startMouseHoldScrolling(const QPoint &clickPos, int selectedItemIndex, int gridWidth,
                               int totalItems);

  /// Attempts to start horizontal click hold mode
  /// Returns true if horizontal hold was started
  [[nodiscard]] bool tryStartHorizontalClickHold(int totalItems, int selectedItemIndex);

  /// Stops mouse hold scrolling
  void stopMouseHoldScrolling() override;

  // --- Widget Finding Utilities (static) ---
  /// Finds the best widget at the given click position and returns its visual
  /// index Returns {widget, visualIndex} pair; visualIndex is -1 if not found
  static std::pair<ItemWidget *, int> findBestWidgetForClick(const QPoint &clickPos,
                                                             IScrollManager *scrollManager,
                                                             QWidget *gridContainer);

  /// Finds the closest widget to the click position from candidates
  static ItemWidget *findClosestWidget(const QVector<ItemWidget *> &candidates,
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

  // Kartend-8jym: requestScrollAreaProperty + requestSetProperty signals
  // removed — never emitted anywhere in src/ and their connect handlers in
  // InteractionManager::wireMouseManagerSignals were the dynamic-Qt-property
  // antipattern Kartend-0zhz wanted gone. The state they were nominally for
  // is owned by InteractionStateHolder now.

private slots:
  void onMouseHoldScrollStep();
  void onClickHoldTimerTimeout();

private:
  /// Computes vertical scroll direction based on selected item position
  [[nodiscard]] int computeVerticalDirection(int selectedItemIndex, int gridWidth) const;

  // ctx is the single source of truth for sibling managers + state.
  const ApplicationContext *m_ctx = nullptr;
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
