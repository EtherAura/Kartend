// Application entry point that initializes Qt and displays the main window.
#include <QApplication>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QSurfaceFormat>
#include <QThreadPool>
#include <QTimer>

#include <cstdlib>

#include "mainwindow.h"

auto main(int argc, char *argv[]) -> int {
  // Enable Wayland-native features when running on Wayland
  // This improves integration with compositors like KWin, Sway, Hyprland
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  // Set desktop file name for proper app identification on Wayland
  // (enables taskbar grouping, app icons, etc.)
  QGuiApplication::setDesktopFileName(APP_ID);

  // Prefer fractional scaling with pixel-perfect rounding for crisp artwork
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
      Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif

  // Configure surface format for optimal rendering
  // - VSync: reduces tearing, especially beneficial on Wayland
  // - 2 buffers: standard double-buffering for smooth updates
  QSurfaceFormat format;
  format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
  format.setSwapInterval(1); // VSync enabled (0 to disable for lower latency)
  QSurfaceFormat::setDefaultFormat(format);

  QApplication app(argc, argv);
  QApplication::setApplicationName(APP_NAME);
  QApplication::setApplicationVersion(APP_VERSION);
  QApplication::setWindowIcon(QIcon(":/icon.svg"));

  // Ensure tooltips have solid backgrounds (fixes transparency on some themes)
  app.setStyleSheet(QStringLiteral("QToolTip { "
                                   "  background-color: palette(window); "
                                   "  color: palette(window-text); "
                                   "  border: 1px solid palette(mid); "
                                   "  padding: 4px; "
                                   "}"));

  // Optional runtime logging configuration.
  // If set, this overrides Qt's default filtering rules.
  // Example: KARTEND_LOG_RULES="kartend.*=true" ./kartend
  const QByteArray logRules = qgetenv("KARTEND_LOG_RULES");
  if (!logRules.isEmpty()) {
    QLoggingCategory::setFilterRules(QString::fromUtf8(logRules));
  }

  // Bridge legacy diagnostic env vars to logging-category rules so existing
  // KARTEND_PERF_TRACE=1 / KARTEND_SEARCH_DIAG=1 / KARTEND_SCAN_DIAG=1
  // invocations continue to work after diag prints were converted from
  // raw Qt logging macros to qCDebug(<category>).
  QStringList bridgedRules;
  if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) {
    bridgedRules << QStringLiteral("kartend.perftrace.debug=true");
  }
  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    bridgedRules << QStringLiteral("kartend.searchdiag.debug=true");
  }
  if (qEnvironmentVariableIsSet("KARTEND_SCAN_DIAG")) {
    bridgedRules << QStringLiteral("kartend.scanflow.debug=true");
  }
  if (!bridgedRules.isEmpty()) {
    // Append rather than replace, so KARTEND_LOG_RULES still wins if used.
    QLoggingCategory::setFilterRules(bridgedRules.join(QLatin1Char('\n')));
  }

  QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
    // Cleanup handled by MainWindow destructor
  });

  // Smoke-test hook: when KARTEND_SMOKE_TEST_EXIT_MS is set, schedule a
  // graceful quit after the given number of milliseconds and return through
  // the normal teardown path (no quick_exit) so sanitizers can observe full
  // destructor execution.
  const QByteArray smokeMsRaw = qgetenv("KARTEND_SMOKE_TEST_EXIT_MS");
  bool smokeTestMode = false;
  int smokeMs = 0;
  if (!smokeMsRaw.isEmpty()) {
    bool ok = false;
    smokeMs = smokeMsRaw.toInt(&ok);
    smokeTestMode = ok && smokeMs > 0;
  }
  if (smokeTestMode) {
    QTimer::singleShot(smokeMs, &app, &QCoreApplication::quit);
  }

  {
    MainWindow window;
    window.show();
    window.showStartupSplash();
    (void)QApplication::exec();
  }
  // MainWindow is now destroyed - all our cleanup (saves, etc.) is done.

  // Give the fire-and-forget save tasks a moment to complete on the global
  // pool. These are just small JSON writes, should be <100ms.
  QThreadPool::globalInstance()->waitForDone(200);

  if (smokeTestMode) {
    // Smoke-test: return normally so ASan/UBSan run their exit checks and
    // verify destruction order. The price is Qt's slower thread-pool teardown.
    return 0;
  }

  // Force immediate exit to skip Qt's lengthy global thread pool cleanup.
  // Our important work is done; remaining threads are abandoned artwork loads.
  std::quick_exit(0);
}
