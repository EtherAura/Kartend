// Application entry point that initializes Qt and displays the main window.
#include <QApplication>
#include <QGuiApplication>
#include <QSurfaceFormat>

#include "artworkmanager.h"
#include "cachemanager.h"
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
  format.setSwapInterval(1);  // VSync enabled (0 to disable for lower latency)
  QSurfaceFormat::setDefaultFormat(format);

  QApplication app(argc, argv);
  QApplication::setApplicationName(APP_NAME);
  QApplication::setApplicationVersion(APP_VERSION);
  QApplication::setWindowIcon(QIcon(":/assets/icons/tag-events.png"));

  QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
    // Cleanup handled by MainWindow destructor
  });

  MainWindow window;
  window.show();

  int result = QApplication::exec();
  return result;
}