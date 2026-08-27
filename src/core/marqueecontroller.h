#ifndef MARQUEECONTROLLER_H
#define MARQUEECONTROLLER_H

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include <functional>
#include <QList>
#include <QObject>
#include <QString>

class MarqueeWindow;
class QImage;
class QScreen;
template <typename T> class QFutureWatcher;
#include "applicationcontext_fwd.h"

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

private slots:
  /// Re-pin the marquee when the monitor set changes mid-session
  /// (Kartend-599xq). Wired to QGuiApplication::screenRemoved / screenAdded in
  /// the constructor; the QScreen argument is unused because the answer is
  /// always "re-resolve from the configured name", whichever screen moved.
  ///
  /// A private SLOT rather than a plain private method so the test can drive
  /// it by name through the meta-object — QGuiApplication's screen signals
  /// cannot be emitted from outside Qt, and the alternative was widening the
  /// public API purely for the test.
  void handleScreenConfigurationChanged(QScreen *screen);

private:
  // Decode the marquee cover off the UI thread and push it when ready;
  // cancelMarqueeLoad supersedes any in-flight decode (Kartend-cq8yh).
  void startMarqueeLoad(const QString &path);
  void cancelMarqueeLoad();

  /// Set between a screenRemoved/screenAdded burst and its deferred re-pin, so
  /// a dock/undock that emits several signals costs one re-pin, not several.
  bool m_screenChangePending = false;

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

  // Last image path pushed to the window (image modes only). Lets a selection
  // storm that keeps resolving to the same cover skip redundant decodes;
  // cleared when we switch to video or tear the window down so a later
  // re-show of the same path still re-pushes (Kartend-cq8yh).
  QString m_lastMarqueePath;
  // In-flight off-thread cover decode, or nullptr. Superseded on each new
  // request so a fast scroll can't paint a stale cover.
  QFutureWatcher<QImage> *m_marqueeLoadWatcher = nullptr;
};

#endif // MARQUEECONTROLLER_H
