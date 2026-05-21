#ifndef MARQUEECONTROLLER_H
#define MARQUEECONTROLLER_H

#include "collectionutils.h"
#include <functional>
#include <QList>
#include <QObject>

class MarqueeWindow;
struct ApplicationContext;

namespace TimerUtils {
class DebouncedTimer;
}

/**
 * @brief Setup struct for MarqueeController dependencies.
 *
 * Every field is borrowed — MarqueeController owns only the MarqueeWindow it
 * creates and the artwork-refresh debounce timer.
 */
struct MarqueeControllerSetup {
  const ApplicationContext *ctx = nullptr;
  const GeneralSettings *generalSettings = nullptr;
  const int *currentCollectionIndex = nullptr;
  const QList<CollectionConfig> *collections = nullptr;
  std::function<bool()> isShuttingDown;
};

/**
 * @brief Drives the secondary-monitor "marquee" / topper window.
 *
 * Extracted from MainWindow. Owns the MarqueeWindow (lazily created the first
 * time the marquee is enabled) and the trailing-edge debounce timer that
 * coalesces artwork refreshes during selection storms (wheel / arrow-key
 * holds).
 *
 * Borrows every dependency; holds no back-pointer into MainWindow. The
 * collection state and GeneralSettings are read-only borrows — the controller
 * never mutates them — and the ApplicationContext is used purely to reach the
 * InteractionManager for the currently-selected file path.
 */
class MarqueeController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(MarqueeController)
public:
  explicit MarqueeController(QObject *parent = nullptr);
  ~MarqueeController() override;

  void setupReferences(const MarqueeControllerSetup &setup);

  /// Sync the marquee window to the current GeneralSettings.marquee* fields.
  /// Creates the window on first enable, destroys it on disable, re-pins to a
  /// different screen on screen-name change, and pushes a fresh pixmap for the
  /// active mode. Idempotent — invoked at startup and after each settings Save.
  void applyMarqueeSettings();

  /// Push the artwork relevant to the active marquee mode to the marquee
  /// window. No-op when the marquee is disabled / not created.
  void updateMarqueeArtwork();

  /// Schedule a debounced artwork refresh — the selection-storm entry point.
  /// Falls back to a direct refresh if the debouncer isn't wired yet.
  void requestArtworkRefresh();

private:
  // Borrowed dependencies — never owned, never deleted through these.
  const ApplicationContext *m_ctx = nullptr;
  const GeneralSettings *m_generalSettings = nullptr;
  const int *m_currentCollectionIndex = nullptr;
  const QList<CollectionConfig> *m_collections = nullptr;
  std::function<bool()> m_isShuttingDown;

  // The marquee window lives on its chosen QScreen and is owned by this
  // controller so it disposes alongside the app rather than leaking on exit.
  MarqueeWindow *m_marqueeWindow = nullptr;
  // Coalesces marquee-artwork refreshes during selection storms. Trailing-edge
  // fire so a single click still feels instant. Owned by this controller.
  TimerUtils::DebouncedTimer *m_marqueeDebouncer = nullptr;
};

#endif // MARQUEECONTROLLER_H
