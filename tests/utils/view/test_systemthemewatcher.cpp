// Tests for SystemThemeWatcher — the kdeglobals watcher that lets Kartend
// notice a runtime desktop accent / colour-scheme change Qt doesn't deliver as
// a palette-change event (Kartend-a0d6c). This covers the testable half — the
// detection + debounced signal; the re-broadcast it triggers is GUI-/KDE-gated
// and is verified manually.
//
// QTEST_GUILESS_MAIN: a QCoreApplication is enough (QFileSystemWatcher + QTimer
// need no display). We point $XDG_CONFIG_HOME at a temp dir so the watcher
// watches a kdeglobals we control. QSignalSpy::wait pumps the event loop — no
// fixed sleeps (the project's banned flake pattern).

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

  // A runtime accent change rewrites kdeglobals — the watcher must surface it
  // (once, after its 250ms debounce). Qt's inotify backend registers the watch
  // from the constructor but only begins delivering events once the event loop
  // has spun, so a change written before the watcher's socket-notifier is armed
  // can be missed entirely. Rather than masking that with a fixed arming sleep,
  // retry the triggering write until the watcher proves the watch is live.
  // Each wait window must exceed the 250ms debounce (a rewrite restarts the
  // coalescing timer, so a shorter window could starve it forever); spy.wait
  // returns as soon as the signal lands, so the common case is one write plus
  // one debounce interval. 10 attempts keep the old 10s ceiling for a
  // saturated box (CPU-bound build + desktop-stream encode).
  for (int attempt = 0; attempt < 10 && spy.isEmpty(); ++attempt) {
    writeAccent("200,40,40");
    spy.wait(1000);
  }
  QVERIFY2(spy.count() >= 1, "watcher never reported the kdeglobals accent change");
#endif
}

QTEST_GUILESS_MAIN(TestSystemThemeWatcher)
#include "test_systemthemewatcher.moc"
