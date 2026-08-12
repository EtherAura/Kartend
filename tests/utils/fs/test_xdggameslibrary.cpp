// XdgGamesLibrary against a staged XDG data tree: which menu entries count as
// importable games, and — the point of the source — which ones belong to a
// launcher Kartend imports separately and must NOT be duplicated here
// (Kartend-4cff2).
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "xdggameslibrary.h"

class TestXdgGamesLibrary : public QObject {
  Q_OBJECT

private slots:
  /// Fresh roots per slot: every case stages entries into `applications/`, and
  /// a shared tree would let one slot's fixtures leak into the next one's
  /// counts.
  void init() { ++m_case; }

  void keepsGamesAndFiltersNonGames();
  void skipsEntriesOwnedByOtherSources();
  void skipsFlatpakExportRoots();
  void userEntryShadowsSystemEntry();
  void skipsEntriesWhoseTryExecIsGone();
  void findsIconForGame();

private:
  QTemporaryDir m_dir;
  int m_case = 0;

  [[nodiscard]] QString systemRoot() const {
    return m_dir.filePath(QStringLiteral("system%1").arg(m_case));
  }
  [[nodiscard]] QString userRoot() const {
    return m_dir.filePath(QStringLiteral("user%1").arg(m_case));
  }

  static void writeFile(const QString &filePath, const QByteArray &content) {
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(content), qint64(content.size()));
  }

  void writeDesktop(const QString &root, const QString &fileName, const QByteArray &body) {
    writeFile(root + QStringLiteral("/applications/") + fileName, body);
  }

  static QByteArray entry(const char *name, const char *exec, const char *categories,
                          const QByteArray &extra = {}) {
    return QByteArrayLiteral("[Desktop Entry]\nType=Application\nName=") + name +
           QByteArrayLiteral("\nExec=") + exec + QByteArrayLiteral("\nCategories=") + categories +
           QByteArrayLiteral("\n") + extra;
  }
};

void TestXdgGamesLibrary::keepsGamesAndFiltersNonGames() {
  writeDesktop(systemRoot(), QStringLiteral("kpat.desktop"),
               entry("KPatience", "kpat", "Qt;KDE;Game;CardGame;"));
  writeDesktop(systemRoot(), QStringLiteral("calc.desktop"),
               entry("Calculator", "calc", "Utility;"));
  writeDesktop(systemRoot(), QStringLiteral("hidden.desktop"),
               entry("Hidden Game", "hidden", "Game;", QByteArrayLiteral("NoDisplay=true\n")));
  writeDesktop(systemRoot(), QStringLiteral("deleted.desktop"),
               entry("Deleted Game", "deleted", "Game;", QByteArrayLiteral("Hidden=true\n")));
  writeDesktop(systemRoot(), QStringLiteral("tool.desktop"),
               entry("ProtonUp-Qt", "pupgui2", "Game;Utility;"));
  // RetroArch's real entry, verbatim. An emulator frontend IS importable —
  // launching one from Kartend is a supported workflow — so this one comes
  // through alongside the actual game.
  writeDesktop(systemRoot(), QStringLiteral("com.libretro.RetroArch.desktop"),
               entry("RetroArch", "retroarch %F", "Game;Emulator;"));
  // A Game-category entry with no Exec cannot be launched.
  writeFile(
      systemRoot() + QStringLiteral("/applications/noexec.desktop"),
      QByteArrayLiteral("[Desktop Entry]\nType=Application\nName=No Exec\nCategories=Game;\n"));
  writeDesktop(systemRoot(), QStringLiteral("link.desktop"),
               entry("A Link", "xdg-open https://example.invalid", "Game;",
                     QByteArrayLiteral("Type=Link\n")));

  const QList<XdgGamesLibrary::Game> games = XdgGamesLibrary::installedGames({systemRoot()});
  QCOMPARE(games.size(), 2);
  QCOMPARE(games.at(0).name, QStringLiteral("KPatience"));
  QVERIFY(games.at(0).desktopFile.endsWith(QStringLiteral("kpat.desktop")));
  QCOMPARE(games.at(1).name, QStringLiteral("RetroArch"));
}

void TestXdgGamesLibrary::skipsEntriesOwnedByOtherSources() {
  // The exact shape Steam writes into ~/.local/share/applications.
  writeDesktop(userRoot(), QStringLiteral("Portal 2.desktop"),
               entry("Portal 2", "steam steam://rungameid/620", "Game;"));
  writeDesktop(userRoot(), QStringLiteral("lutris-game.desktop"),
               entry("A Lutris Game", "lutris lutris:rungame/a-game", "Game;"));
  writeDesktop(userRoot(), QStringLiteral("heroic-game.desktop"),
               entry("An Epic Game", "heroic --no-gui heroic://launch/legendary/Quail", "Game;"));
  writeDesktop(userRoot(), QStringLiteral("bottled.desktop"),
               entry("A Bottled Game", "bottles-cli run -p Game -b Bottle --", "Game;"));
  writeDesktop(userRoot(), QStringLiteral("itch-game.desktop"),
               entry("An itch Game", "itch --prefer-launch itch://caves/abc/launch", "Game;"));
  writeDesktop(userRoot(), QStringLiteral("flatpak-run.desktop"),
               entry("A Flatpak Game", "flatpak run org.example.Game", "Game;"));
  // An entry carrying X-Flatpak belongs to the Flatpak source however it runs.
  writeDesktop(userRoot(), QStringLiteral("exported.desktop"),
               entry("An Exported Game", "/usr/bin/game", "Game;",
                     QByteArrayLiteral("X-Flatpak=org.example.Exported\n")));
  // …and a native game sitting in the same directory still comes through.
  writeDesktop(userRoot(), QStringLiteral("native.desktop"),
               entry("A Native Game", "thegame", "Game;"));

  const QList<XdgGamesLibrary::Game> games = XdgGamesLibrary::installedGames({userRoot()});
  QCOMPARE(games.size(), 1);
  QCOMPARE(games.at(0).name, QStringLiteral("A Native Game"));

  QVERIFY(XdgGamesLibrary::isForeignLauncher(QStringLiteral("steam steam://rungameid/620")));
  QVERIFY(XdgGamesLibrary::isForeignLauncher(QStringLiteral("/usr/bin/lutris")));
  QVERIFY(!XdgGamesLibrary::isForeignLauncher(QStringLiteral("supertuxkart")));
  // "steamlink" is its own program, not the Steam client.
  QVERIFY(!XdgGamesLibrary::isForeignLauncher(QStringLiteral("steamlink")));
}

void TestXdgGamesLibrary::skipsFlatpakExportRoots() {
  const QString exportRoot =
      m_dir.filePath(QStringLiteral("home/.local/share/flatpak/exports/share"));
  writeFile(exportRoot + QStringLiteral("/applications/org.example.Stk.desktop"),
            entry("SuperTuxKart", "/usr/bin/supertuxkart", "Game;"));
  QVERIFY(XdgGamesLibrary::isFlatpakExportRoot(exportRoot));
  QVERIFY(XdgGamesLibrary::isFlatpakExportRoot(QStringLiteral("/var/lib/flatpak/exports/share")));
  QVERIFY(!XdgGamesLibrary::isFlatpakExportRoot(QStringLiteral("/usr/share")));
  QVERIFY(XdgGamesLibrary::installedGames({exportRoot}).isEmpty());
}

void TestXdgGamesLibrary::userEntryShadowsSystemEntry() {
  writeDesktop(systemRoot(), QStringLiteral("thegame.desktop"),
               entry("The Game (system copy)", "thegame", "Game;"));
  writeDesktop(userRoot(), QStringLiteral("thegame.desktop"),
               entry("The Game", "thegame --tweaked", "Game;"));
  // Roots are probed in order, so the user root is passed first exactly as
  // DesktopEntryFile::defaultShareRoots() returns it.
  const QList<XdgGamesLibrary::Game> games =
      XdgGamesLibrary::installedGames({userRoot(), systemRoot()});
  QCOMPARE(games.size(), 1);
  QCOMPARE(games.at(0).name, QStringLiteral("The Game"));

  // A user-level entry that hides the game hides it outright: falling through
  // to the system copy would resurrect what the user's own root overrides.
  writeDesktop(userRoot(), QStringLiteral("thegame.desktop"),
               entry("The Game", "thegame", "Game;", QByteArrayLiteral("Hidden=true\n")));
  QVERIFY(XdgGamesLibrary::installedGames({userRoot(), systemRoot()}).isEmpty());
}

void TestXdgGamesLibrary::skipsEntriesWhoseTryExecIsGone() {
  const QString present = m_dir.filePath(QStringLiteral("bin/realgame"));
  writeFile(present, "#!/bin/sh\n");
  QFile::setPermissions(present, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

  writeDesktop(systemRoot(), QStringLiteral("stale.desktop"),
               entry("Uninstalled Game", "stalegame", "Game;",
                     QByteArrayLiteral("TryExec=/nonexistent/stalegame\n")));
  writeDesktop(systemRoot(), QStringLiteral("real.desktop"),
               entry("Real Game", "realgame", "Game;",
                     (QStringLiteral("TryExec=") + present + QStringLiteral("\n")).toUtf8()));

  const QList<XdgGamesLibrary::Game> games = XdgGamesLibrary::installedGames({systemRoot()});
  QCOMPARE(games.size(), 1);
  QCOMPARE(games.at(0).name, QStringLiteral("Real Game"));
}

void TestXdgGamesLibrary::findsIconForGame() {
  writeDesktop(systemRoot(), QStringLiteral("icongame.desktop"),
               entry("Icon Game", "icongame", "Game;", QByteArrayLiteral("Icon=icongame\n")));
  writeFile(systemRoot() + QStringLiteral("/icons/hicolor/128x128/apps/icongame.png"), "x");

  const QList<XdgGamesLibrary::Game> games = XdgGamesLibrary::installedGames({systemRoot()});
  QCOMPARE(games.size(), 1);
  QVERIFY(games.at(0).iconPath.endsWith(QStringLiteral("128x128/apps/icongame.png")));
}

QTEST_MAIN(TestXdgGamesLibrary)
#include "test_xdggameslibrary.moc"
