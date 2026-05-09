#ifndef ATTRACTMANAGER_H
#define ATTRACTMANAGER_H

#include "setuputils.h"
#include <QObject>
#include <QPointer>
#include <QScrollArea>
#include <QTimer>

QT_BEGIN_NAMESPACE
class QScrollBar;
QT_END_NAMESPACE

struct ApplicationContext;
struct GeneralSettings;
class ScrollManager;
class SelectionManager;

/**
 * @brief Setup struct for AttractManager dependencies.
 *
 * Follows the same pattern as other manager setup structs, with ctx fallback.
 */
struct AttractManagerSetup {
  const ApplicationContext *ctx = nullptr;

  QScrollArea *itemScrollArea = nullptr;
  ScrollManager *scrollManager = nullptr;
  SelectionManager *selectionManager = nullptr;
  const GeneralSettings *generalSettings = nullptr;
  const bool *isShuttingDown = nullptr;

  SETUP_GETTER_DECL(QScrollArea *, ItemScrollArea)
  SETUP_GETTER_DECL(ScrollManager *, ScrollManager)
  SETUP_GETTER_DECL(SelectionManager *, SelectionManager)
  SETUP_GETTER_DECL(const GeneralSettings *, GeneralSettings)
  SETUP_GETTER_DECL(const bool *, IsShuttingDown)
};

/**
 * @brief Manages idle-triggered autoscroll / attract-mode behaviour.
 *
 * When the user is idle for a configurable timeout, attract mode activates
 * and the viewport smoothly scrolls through the collection. Scrolling
 * reverses direction (bounces) at the top and bottom of the scroll range.
 * Any user activity immediately stops attract mode and returns control.
 *
 * Memory Ownership Model:
 * - Owns QTimers via Qt parent ownership (new QTimer(this))
 * - Does NOT own: m_itemScrollArea, m_scrollManager, m_generalSettings,
 *   m_isShuttingDown (borrowed references)
 */
class AttractManager : public QObject {
  Q_OBJECT

public:
  explicit AttractManager(QObject *parent = nullptr);
  ~AttractManager() override;

  void setupReferences(const AttractManagerSetup &setup);

  /// Returns true if attract mode is currently active (autoscrolling).
  [[nodiscard]] bool isActive() const { return m_attractActive; }

  /// Returns true if attract mode is enabled in settings.
  [[nodiscard]] bool isEnabled() const;

  /// True while AttractManager is itself driving a selection change (so the
  /// resulting selectionChanged signal must not be treated as user activity).
  [[nodiscard]] bool isDrivingSelection() const { return m_drivingSelection; }

public slots:
  /// Called when user activity is detected; resets the idle timer and
  /// stops attract mode if active.
  void onActivityDetected();

  /// Re-reads the enabled/timeout/speed settings (e.g. after settings change).
  void reloadSettings();

  /// suspend / resume attract mode externally. While suspended,
  /// the idle timer is halted, attract mode is stopped, and the manager
  /// refuses to start a new attract cycle. Resuming reseeds the idle timer
  /// from now so attract waits the full timeout before kicking back in.
  /// Used by MainWindow to halt attract while a launched application is
  /// running (its idle timer would otherwise fire under the launched app
  /// and start scrolling unseen).
  void setSuspended(bool suspended);

signals:
  /// Emitted when attract mode starts autoscrolling.
  void attractStarted();

  /// Emitted when attract mode stops (user activity or disabled).
  void attractStopped();

  /// Emitted when the selection-advance timer ticks. The receiver is expected
  /// to call selectItemByIndex() on @p index. Wrapped in setDrivingSelection
  /// so the resulting selectionChanged signal isn't treated as user activity.
  void requestSelectIndex(int index);

private slots:
  void onIdleTimeout();
  void onScrollTick();
  void onBouncePauseFinished();
  void onAdvanceSelectionTick();

private:
  void startAttract();
  void stopAttract();
  void resetIdleTimer();
  void startAdvanceSelectionTimerIfEnabled();

  // Manager references (borrowed, not owned)
  QPointer<QScrollArea> m_itemScrollArea = nullptr;
  ScrollManager *m_scrollManager = nullptr;
  SelectionManager *m_selectionManager = nullptr;
  const GeneralSettings *m_generalSettings = nullptr;
  const bool *m_isShuttingDown = nullptr;

  // Timers (owned via Qt parent)
  QTimer *m_idleTimer = nullptr;
  QTimer *m_scrollTimer = nullptr;
  QTimer *m_bouncePauseTimer = nullptr;
  QTimer *m_advanceSelectionTimer = nullptr;

  // State
  bool m_attractActive = false;
  int m_scrollDirection = 1; // 1 = down, -1 = up
  bool m_bouncePaused = false;
  bool m_drivingSelection = false;
  double m_scrollAccumulator = 0.0; // Fractional-pixel buffer for sub-px speeds
  // external suspend flag. While true, onIdleTimeout refuses
  // to start attract, and resetIdleTimer() refuses to arm the timer. Cleared
  // by setSuspended(false), which then arms a fresh idle countdown.
  bool m_suspended = false;
};

#endif // ATTRACTMANAGER_H
