// Tests for SystemThemeWatcher — the kdeglobals watcher that lets Kartend
// notice a runtime desktop accent / colour-scheme change Qt doesn't deliver as
// a palette-change event (Kartend-a0d6c). This covers the testable half — the
// detection + debounced signal; the re-broadcast it triggers is GUI-/KDE-gated
// and is verified manually.
//
// QTEST_GUILESS_MAIN: a QCoreApplication is enough (QFileSystemWatcher + QTimer
// need no display). We point $XDG_CONFIG_HOME at a temp dir so the watcher
// watches a kdeglobals we control. QTRY_* pumps the event loop — no fixed
// sleeps (the project's banned flake pattern).

#include "systemthemewatcher.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

class TestSystemThemeWatcher : public QObject {
  Q_OBJECT
private slots:
  void emitsThemeChangedWhenKdeglobalsChanges();
};

void TestSystemThemeWatcher::emitsThemeChangedWhenKdeglobalsChanges() {
#ifndef Q_OS_LINUX
  QSKIP("SystemThemeWatcher watches kdeglobals only on Linux");
#else
  QTemporaryDir cfgDir;
  QVERIFY(cfgDir.isValid());
  qputenv("XDG_CONFIG_HOME", cfgDir.path().toUtf8());
  // The watcher resolves kdeglobals via GenericConfigLocation; confirm our env
  // override took so a miss fails loudly instead of silently watching the real
  // ~/.config.
  QCOMPARE(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation), cfgDir.path());

  const QString kglobals = cfgDir.filePath(QStringLiteral("kdeglobals"));
  const auto writeAccent = [&](const QByteArray &rgb) {
    QFile f(kglobals);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("[General]\nAccentColor=" + rgb + "\n");
    f.close();
  };
  writeAccent("10,20,30");

  SystemThemeWatcher watcher;
  QSignalSpy spy(&watcher, &SystemThemeWatcher::themeChanged);

  // Qt's inotify backend registers the watch from the constructor but only
  // begins delivering events once the event loop has spun at least once. Pump
  // it briefly so the watch is genuinely live before the triggering write —
  // otherwise a cold/loaded first run can write the change before the watcher's
  // socket-notifier is armed and miss it entirely.
  QTest::qWait(300);

  // A runtime accent change rewrites kdeglobals — the watcher must surface it
  // (once, after its 250ms debounce). QTRY returns as soon as the signal lands,
  // so the generous ceiling only matters on a saturated box where inotify
  // delivery stalls (CPU-bound build + desktop-stream encode); it does not slow
  // the common case.
  writeAccent("200,40,40");
  QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 10000);
#endif
}

QTEST_GUILESS_MAIN(TestSystemThemeWatcher)
#include "test_systemthemewatcher.moc"
