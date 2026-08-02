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

#if defined(__linux__) && defined(__GLIBC__)
#include <malloc.h>
#endif

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "errordialog.h"
#include "errorpresentation.h"
#include "errorutils.h"
#include "kartdb.h"
#include "kartmanager.h"
#include "kartreader.h"
#include "kartwriter.h"
#include "mainwindow.h"
#include "mediabackendconfig.h"
#include "pathutils.h"
#include "settingsmanager.h"

#include <QSqlDatabase>

// extern "C" linkage is needed on Windows: SDL2's pkg-config injects
// -Dmain=SDL_main so SDL2main's WinMain can dispatch into our entry
// point. The renamed `SDL_main` symbol has to be unmangled C for that
// WinMain (which lives in a C translation unit inside libSDL2main) to
// resolve it. On non-Windows the rename macro isn't defined, and main
// has implicit C linkage anyway, so this is a no-op.
extern "C" auto main(int argc, char *argv[]) -> int {
#if defined(__linux__) && defined(__GLIBC__)
  // Kartend-44ypc: stop the allocator sitting on freed artwork buffers.
  //
  // A heaptrack run on a 53k-item collection measured a 623M peak heap
  // against a 1.28G RSS — roughly 650M held by glibc rather than live data.
  // Two defaults drive that, and both are worth overriding here:
  //
  //   M_MMAP_THRESHOLD is *dynamic* by default: glibc raises it (up to 32M)
  //   whenever it sees an mmap'd block freed. Decoded artwork is ~640K a
  //   piece, so after the first few frees the threshold climbs past that and
  //   every subsequent image is carved out of the main heap instead. Those
  //   blocks can only be returned to the OS when they happen to sit at the
  //   top of the heap, so a scroll session leaves the arena pockmarked.
  //   Setting it explicitly also pins it — glibc stops auto-tuning once the
  //   value is set by hand — so image-sized allocations keep going through
  //   mmap and are handed straight back on free.
  //
  //   M_ARENA_MAX defaults to 8 * nproc (192 on a 12900K). Every arena that
  //   any thread touches keeps its own free lists, and this app runs a wide
  //   pool. Capping the count trades a little allocator contention for a far
  //   smaller resident footprint.
  //
  // Both must be set before the first allocation-heavy thread starts, which
  // is why they sit at the very top of main().
  mallopt(M_MMAP_THRESHOLD, 128 * 1024);
  mallopt(M_ARENA_MAX, 4);
#endif

  // Kartend-0vnvo: keep Qt's FFmpeg plugin off the hardware-decode backends
  // that leak a DRM render-node fd + Mesa worker thread per setSource().
  // Must run before the first QMediaPlayer exists, hence before QApplication.
  MediaBackendConfig::applyDecodingHwDeviceTypePolicy();

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

    // Kartend-u8wf0: opt-in to importing a .kart that bundles its own launcher
    // executable. Headless import refuses self-bundled launchers by default
    // because there is no interactive confirmer to warn the user.
    QCommandLineOption allowUntrustedLauncherOption(
        QStringLiteral("allow-untrusted-launcher"),
        QApplication::translate(
            "main", "Permit --import-kart to register a launcher path that points inside the "
                    "extracted kart tree (a self-bundled executable). Off by default."));
    parser.addOption(allowUntrustedLauncherOption);

    parser.process(app);
    if (parser.isSet(collectionOption)) {
      cliCollectionOverride = parser.value(collectionOption).trimmed();
    }

    if (parser.isSet(importKartOption)) {
      // CLI-seam validation: same shell-metachar / NUL / blank-after-expansion
      // checks CliArgs::parseStartupArguments runs on its parse() path —
      // applied here on the production process() path so a bad --import-kart
      // / --to is rejected before KartManager touches the filesystem.
      auto srcResult = PathUtils::expandAndValidateCliPath(parser.value(importKartOption),
                                                           QStringLiteral("import-kart"));
      if (srcResult.isError()) {
        fprintf(stderr, "kart import: %s\n", qPrintable(srcResult.error().message));
        if (!srcResult.error().details.isEmpty()) {
          fprintf(stderr, "  details: %s\n", qPrintable(srcResult.error().details));
        }
        return 2;
      }
      const QString src = srcResult.value();
      const QString rawDest =
          parser.isSet(toOption) ? parser.value(toOption) : (QDir::homePath() + "/imported-kart");
      auto destResult = PathUtils::expandAndValidateCliPath(rawDest, QStringLiteral("to"));
      if (destResult.isError()) {
        fprintf(stderr, "kart import: %s\n", qPrintable(destResult.error().message));
        if (!destResult.error().details.isEmpty()) {
          fprintf(stderr, "  details: %s\n", qPrintable(destResult.error().details));
        }
        return 2;
      }
      const QString dest = destResult.value();
      kart::MergeChoice headlessChoice = kart::MergeChoice::Skip;
      if (parser.isSet(onConflictOption)) {
        const QString v = parser.value(onConflictOption).toLower();
        if (v == "overwrite") {
          headlessChoice = kart::MergeChoice::Overwrite;
        } else if (v == "merge") {
          headlessChoice = kart::MergeChoice::Merge;
        } else if (v != "skip") {
          // Kartend audit E-04: don't silently treat a typo'd policy as Skip.
          fprintf(stderr,
                  "kart import: unknown --on-conflict policy '%s' (expected skip, "
                  "overwrite, or merge)\n",
                  qPrintable(parser.value(onConflictOption)));
          return 2;
        }
      }
      kart::KartManager km;
      auto res = km.importKartHeadless(src, dest, false, headlessChoice,
                                       parser.isSet(allowUntrustedLauncherOption));
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
      // Kartend-928mu: route --export-out through the same expand/validate seam
      // as --import-kart / --to (it was previously consumed raw, so a ~ wasn't
      // expanded and no metachar/NUL check ran before QSaveFile saw it).
      auto outResult = PathUtils::expandAndValidateCliPath(parser.value(exportOutOption),
                                                           QStringLiteral("export-out"));
      if (outResult.isError()) {
        fprintf(stderr, "kart export: %s\n", qPrintable(outResult.error().message));
        if (!outResult.error().details.isEmpty()) {
          fprintf(stderr, "  details: %s\n", qPrintable(outResult.error().details));
        }
        return 2;
      }
      const QString outPath = outResult.value();
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
  // the normal teardown path (no _Exit) so sanitizers can observe full
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
    // Smoke-test exit timer: triggers graceful app quit after smokeMs so the
    // sanitizer CI job exercises full destructor sequencing (no _Exit).
    QTimer::singleShot(smokeMs, &app, &QCoreApplication::quit);
  }

  // Kartend-hx6l: install the ErrorDialog-backed showError / showCriticalError
  // override on the ErrorPresentation seam. The default impl in
  // src/utils/app/errorpresentation_default.cpp is a qWarning-only fallback
  // so kartend_data link sites don't drag kartend_ui into every test exe.
  // Production main upgrades the default to the real modal here, where
  // integration-test setups (which install their own no-op override before
  // constructing MainWindowFixture) can't accidentally be overwritten.
  ErrorPresentation::setShowErrorOverride(
      [](QWidget *parent, const ErrorUtils::ErrorContext &context) {
        ErrorDialog::showError(parent, context);
      });
  ErrorPresentation::setShowCriticalErrorOverride(
      [](QWidget *parent, const ErrorUtils::ErrorContext &context, bool allowContinue) {
        return ErrorDialog::showCriticalError(parent, context, allowContinue);
      });

  {
    MainWindow window;
    // override the persisted startupCollection for this launch
    // when --collection was supplied. setupInitialTimersWithCollections() reads
    // m_generalSettings.startupCollection from inside a QTimer::singleShot(0)
    // lambda that fires after exec() begins, so it's safe to mutate the field
    // here, after MainWindow construction loaded settings from disk.
    if (!cliCollectionOverride.isEmpty()) {
      window.m_generalSettings.startup.startupCollection = cliCollectionOverride;
    }
    window.show();
    window.showStartupSplash();
    (void)QApplication::exec();
  }
  // MainWindow is now destroyed - all our cleanup (saves, etc.) is done.

  // This is NOT waiting for saves: cache + session writes are synchronous on
  // the shutdown thread now (Kartend-x2by) — they used to be fire-and-forget
  // and this same 200ms sleep routinely dropped them on slow disks. What's
  // left on the global pool here is abandoned fire-and-forget artwork work:
  // path cataloging + silent loads dispatched via globalInstance()->start /
  // QtConcurrent::run (see artworkpathcatalog.cpp, artworksilentloading.cpp).
  // The bounded wait is a best-effort window for those to finish before exit,
  // not a durability guarantee (Kartend-snb2q).
  QThreadPool::globalInstance()->waitForDone(200);

  if (smokeTestMode) {
    // Smoke-test: return normally so ASan/UBSan run their exit checks and
    // verify destruction order. The price is Qt's slower thread-pool teardown.
    return 0;
  }

  // Force immediate exit to skip Qt's lengthy global thread pool cleanup.
  // Our important work is done; remaining threads are abandoned artwork loads.
  // std::_Exit (not std::quick_exit) because MinGW's libstdc++ and MSVC's
  // STL both omit quick_exit — the underlying Windows CRT has no C11
  // quick_exit. _Exit has the same effect for us: no atexit, no destructors,
  // OS reaps the process.
  std::_Exit(0);
}
