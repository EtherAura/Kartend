// Application entry point that initializes Qt and displays the main window.
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QStringList>
#include <QSurfaceFormat>
#include <QThreadPool>
#include <QTimer>

#include <cstdlib>

#include "collectionutils.h"
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

  // Kartend-z3w: parse CLI options. Use process() so --help, --version, and
  // unknown-option errors are handled with the standard Qt behavior (print
  // to stderr/stdout and exit). The parser definition mirrors
  // CliArgs::parseStartupArguments(); kept inline here to retain process()
  // semantics for the real CLI while the helper stays unit-testable.
  QString cliCollectionOverride;
  {
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QApplication::translate("main", "Kartend - Qt6/KDE multimedia collection launcher."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption collectionOption(
        QStringList{QStringLiteral("c"), QStringLiteral("collection")},
        QApplication::translate("main",
                                "Open Kartend directly into the named collection, bypassing the "
                                "configured default. Falls back to the default if <name> is "
                                "unknown."),
        QApplication::translate("main", "name"));
    parser.addOption(collectionOption);

    parser.process(app);
    if (parser.isSet(collectionOption)) {
      cliCollectionOverride = parser.value(collectionOption).trimmed();
    }
  }

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
    // Kartend-z3w: override the persisted startupCollection for this launch
    // when --collection was supplied. setupInitialTimersWithCollections() reads
    // m_generalSettings.startupCollection from inside a QTimer::singleShot(0)
    // lambda that fires after exec() begins, so it's safe to mutate the field
    // here, after MainWindow construction loaded settings from disk.
    if (!cliCollectionOverride.isEmpty()) {
      window.m_generalSettings.startupCollection = cliCollectionOverride;
    }
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
