#ifndef MOUSEMANAGER_H
#define MOUSEMANAGER_H

#include <QObject>
#include <QPoint>
#include <QTimer>
#include <QVector>

QT_BEGIN_NAMESPACE
class QScrollArea;
class QScrollBar;
class QWidget;
QT_END_NAMESPACE

class ScrollManager;
class SelectionManager;
class MainWindow;
class MediaItemWidget;
struct CollectionConfig;

/// Setup struct for MouseManager dependencies
struct MouseManagerSetup {
  ScrollManager *scrollManager = nullptr;
  SelectionManager *selectionManager = nullptr;
  MainWindow *mainWindow = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QWidget *gridContainer = nullptr;
  const QVector<CollectionConfig> *collections = nullptr;
  const int *currentCollectionIndex = nullptr;
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
  bool tryStartHorizontalClickHold(int totalItems, int selectedItemIndex);

  /// Stops mouse hold scrolling
  void stopMouseHoldScrolling();

  // --- Widget Finding Utilities (static) ---
  /// Finds the best widget at the given click position
  static MediaItemWidget *findBestWidgetForClick(
      const QPoint &clickPos,
      ScrollManager *scrollManager,
      QWidget *gridContainer);

  /// Finds the closest widget to the click position from candidates
  static MediaItemWidget *findClosestWidget(
      const QVector<MediaItemWidget *> &candidates,
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
  int computeVerticalDirection(int selectedItemIndex, int gridWidth) const;

  // References (not owned)
  ScrollManager *m_scrollManager = nullptr;
  SelectionManager *m_selectionManager = nullptr;
  MainWindow *m_mainWindow = nullptr;
  QScrollArea *m_itemScrollArea = nullptr;
  QWidget *m_gridContainer = nullptr;
  const QVector<CollectionConfig> *m_collections = nullptr;
  const int *m_currentCollectionIndex = nullptr;

  // Left mouse button state
  bool m_leftMouseDown = false;

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
