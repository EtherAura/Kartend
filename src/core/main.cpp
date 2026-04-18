// Application entry point that initializes Qt and displays the main window.
#include <QApplication>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QSurfaceFormat>
#include <QThreadPool>

#include <cstdlib>

#include "mainwindow.h"

auto main(int argc, char *argv[]) -> int {
  // Enable Wayland-native features when running on Wayland
  // This improves integration with compositors like KWin, Sway, Hyprland
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  // Set desktop file name for proper app identification on Wayland
  // (enables taskbar grouping, app icons, etc.)
  QGuiApplication::setDesktopFileName("kartend");

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

  QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
    // Cleanup handled by MainWindow destructor
  });

  {
    MainWindow window;
    window.show();
    (void)QApplication::exec();
  }
  // MainWindow is now destroyed - all our cleanup (saves, etc.) is done.

  // Give the fire-and-forget save tasks a moment to complete on the global
  // pool. These are just small JSON writes, should be <100ms.
  QThreadPool::globalInstance()->waitForDone(200);

  // Force immediate exit to skip Qt's lengthy global thread pool cleanup.
  // Our important work is done; remaining threads are abandoned artwork loads.
  std::quick_exit(0);
}