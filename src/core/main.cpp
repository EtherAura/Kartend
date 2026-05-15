// Application entry point that initializes Qt and displays the main window.
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QStringList>
#include <QSurfaceFormat>
#include <QThreadPool>
#include <QTimer>
#include <QTranslator>

#include <cstdlib>

#include "collectionutils.h"
#include "errorutils.h"
#include "kartdb.h"
#include "kartmanager.h"
#include "kartreader.h"
#include "kartwriter.h"
#include "mainwindow.h"
#include "settingsmanager.h"

#include <QSqlDatabase>

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

  // Install translators: Qt's own (dialog buttons, file pickers) plus the
  // app's .qm files. Both fall back silently to the source-text English when
  // no matching locale ships. The translator objects must outlive QApplication,
  // so they use static storage duration.
  static QTranslator qtTranslator;
  if (qtTranslator.load(QLocale(), QStringLiteral("qt"), QStringLiteral("_"),
                        QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
    QCoreApplication::installTranslator(&qtTranslator);
  }
  static QTranslator appTranslator;
  QStringList translationDirs;
  translationDirs << QStringLiteral(APP_BUILD_TRANSLATIONS_DIR);
  translationDirs << QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
                                               QStringLiteral(APP_TRANSLATIONS_SUBDIR),
                                               QStandardPaths::LocateDirectory);
  translationDirs << (QCoreApplication::applicationDirPath() + QStringLiteral("/../share/") +
                      QStringLiteral(APP_TRANSLATIONS_SUBDIR));
  translationDirs.removeDuplicates();
  for (const QString &translationDir : translationDirs) {
    if (appTranslator.load(QLocale(), QStringLiteral("kartend"), QStringLiteral("_"),
                           translationDir)) {
      QCoreApplication::installTranslator(&appTranslator);
      break;
    }
  }

  // parse CLI options. Use process so --help, --version, and
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

    QCommandLineOption importKartOption(
        QStringLiteral("import-kart"),
        QApplication::translate("main", "Import a .kart package headlessly and exit."),
        QApplication::translate("main", "path"));
    parser.addOption(importKartOption);

    QCommandLineOption toOption(
        QStringLiteral("to"),
        QApplication::translate("main", "Destination directory for --import-kart."),
        QApplication::translate("main", "dir"));
    parser.addOption(toOption);

    QCommandLineOption exportKartOption(
        QStringLiteral("export-kart"),
        QApplication::translate("main", "Export the named collection headlessly and exit."),
        QApplication::translate("main", "name"));
    parser.addOption(exportKartOption);

    QCommandLineOption exportOutOption(
        QStringLiteral("export-out"),
        QApplication::translate("main", "Output path for --export-kart."),
        QApplication::translate("main", "path"));
    parser.addOption(exportOutOption);

    QCommandLineOption onConflictOption(
        QStringLiteral("on-conflict"),
        QApplication::translate("main", "Conflict policy for --import-kart: skip|overwrite|merge."),
        QApplication::translate("main", "policy"), QStringLiteral("skip"));
    parser.addOption(onConflictOption);

    parser.process(app);
    if (parser.isSet(collectionOption)) {
      cliCollectionOverride = parser.value(collectionOption).trimmed();
    }

    if (parser.isSet(importKartOption)) {
      const QString src = parser.value(importKartOption);
      const QString dest =
          parser.isSet(toOption) ? parser.value(toOption) : (QDir::homePath() + "/imported-kart");
      kart::MergeChoice headlessChoice = kart::MergeChoice::Skip;
      if (parser.isSet(onConflictOption)) {
        const QString v = parser.value(onConflictOption).toLower();
        if (v == "overwrite") {
          headlessChoice = kart::MergeChoice::Overwrite;
        } else if (v == "merge") {
          headlessChoice = kart::MergeChoice::Merge;
        }
      }
      kart::KartManager km;
      auto res = km.importKartHeadless(src, dest, false, headlessChoice);
      if (res.isError()) {
        fprintf(stderr, "kart import failed: %s\n", qPrintable(res.error().message));
        if (!res.error().details.isEmpty()) {
          fprintf(stderr, "  details: %s\n", qPrintable(res.error().details));
        }
        return 2;
      }
      fprintf(stderr, "imported to %s\n", qPrintable(res.value()));
      return 0;
    }

    if (parser.isSet(exportKartOption)) {
      const QString collectionName = parser.value(exportKartOption);
      if (!parser.isSet(exportOutOption)) {
        fprintf(stderr, "kart export requires --export-out <path>\n");
        return 2;
      }
      const QString outPath = parser.value(exportOutOption);
      SettingsManager headlessSettings(nullptr, nullptr);
      QList<CollectionConfig> collections;
      headlessSettings.loadCollections(collections);
      int idx = -1;
      for (int i = 0; i < collections.size(); ++i) {
        if (collections.at(i).name == collectionName) {
          idx = i;
          break;
        }
      }
      if (idx < 0) {
        fprintf(stderr, "kart export: no collection named '%s'\n", qPrintable(collectionName));
        return 2;
      }
      const CollectionConfig &cfg = collections.at(idx);
      const QString collectionUuid =
          CollectionUtils::computeCollectionUuid(cfg.name, cfg.mediaDirectory);
      auto dbRes = kart::openMediaDbConnection(QStringLiteral("kart-cli-export"));
      QSqlDatabase *dbPtr = nullptr;
      QSqlDatabase db;
      if (dbRes.isOk()) {
        db = dbRes.value();
        dbPtr = &db;
      }
      auto prep = KartWriter::prepareFromCollection(cfg, collectionUuid, {}, dbPtr);
      if (dbPtr) {
        kart::closeMediaDbConnection(db);
      }
      if (prep.isError()) {
        fprintf(stderr, "kart export prep failed: %s\n", qPrintable(prep.error().message));
        return 2;
      }
      auto params = prep.value();
      params.uuid = collectionUuid;
      params.name = cfg.name;
      KartWriter::Writer writer;
      QObject::connect(&writer, &KartWriter::Writer::progress,
                       [](double f) { fprintf(stderr, "export progress: %.0f%%\n", f * 100.0); });
      auto res = writer.writeKart(outPath, params);
      if (res.isError()) {
        fprintf(stderr, "kart export failed: %s\n", qPrintable(res.error().message));
        return 2;
      }
      fprintf(stderr, "exported '%s' to %s\n", qPrintable(collectionName), qPrintable(outPath));
      return 0;
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
    // override the persisted startupCollection for this launch
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
