// DesktopEntryFile — the .desktop parsing shared by the Flatpak exports scan
// and the XDG menu scan (Kartend-4cff2): exact-key parsing, the game-vs-tool
// category rule, Exec program extraction, and icon lookup.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "desktopentry.h"

class TestDesktopEntry : public QObject {
  Q_OBJECT

private slots:
  void parsesKnownKeysOnly();
  void ignoresOtherSectionsAndLocalisedNames();
  void gameCategoryExcludesTools();
  void execProgramHandlesQuotingAndFieldCodes();
  void iconLookupPrefersLargestRaster();
  void shareRootsFollowXdgEnvironment();

private:
  QTemporaryDir m_dir;

  static void writeFile(const QString &filePath, const QByteArray &content) {
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(content), qint64(content.size()));
  }
};

void TestDesktopEntry::parsesKnownKeysOnly() {
  const QString path = m_dir.filePath(QStringLiteral("full.desktop"));
  writeFile(path, QByteArrayLiteral("[Desktop Entry]\n"
                                    "# a comment\n"
                                    "Type=Application\n"
                                    "Name=Super Game\n"
                                    "Icon=super-game\n"
                                    "Exec=super-game --fullscreen %U\n"
                                    "TryExec=/usr/bin/super-game\n"
                                    "Categories=Game;ArcadeGame;\n"
                                    "Hidden=false\n"));
  const DesktopEntryFile::Entry entry = DesktopEntryFile::parse(path);
  QCOMPARE(entry.type, QStringLiteral("Application"));
  QCOMPARE(entry.name, QStringLiteral("Super Game"));
  QCOMPARE(entry.icon, QStringLiteral("super-game"));
  QCOMPARE(entry.exec, QStringLiteral("super-game --fullscreen %U"));
  QCOMPARE(entry.tryExec, QStringLiteral("/usr/bin/super-game"));
  QCOMPARE(entry.categories.size(), 2);
  QVERIFY(!entry.hidden);
  QVERIFY(!entry.noDisplay);
}

void TestDesktopEntry::ignoresOtherSectionsAndLocalisedNames() {
  const QString path = m_dir.filePath(QStringLiteral("sections.desktop"));
  writeFile(path, QByteArrayLiteral("[Desktop Entry]\n"
                                    "Name=Game Name\n"
                                    "Name[de]=Spielname\n"
                                    "NoDisplay=TRUE\n"
                                    "[Desktop Action new]\n"
                                    "Name=Action Name\n"
                                    "Exec=should-not-win\n"));
  const DesktopEntryFile::Entry entry = DesktopEntryFile::parse(path);
  QCOMPARE(entry.name, QStringLiteral("Game Name"));
  QVERIFY(entry.exec.isEmpty());
  // NoDisplay is compared case-insensitively, as the spec's booleans are
  // written both ways in the wild.
  QVERIFY(entry.noDisplay);
}

void TestDesktopEntry::gameCategoryExcludesTools() {
  QVERIFY(DesktopEntryFile::isGame({QStringLiteral("Game")}));
  QVERIFY(DesktopEntryFile::isGame({QStringLiteral("Qt"), QStringLiteral("Game")}));
  QVERIFY(!DesktopEntryFile::isGame({QStringLiteral("Utility")}));
  // The ProtonUp-Qt shape: a gaming tool declares Game beside a tool category.
  QVERIFY(!DesktopEntryFile::isGame({QStringLiteral("Game"), QStringLiteral("Utility")}));
  QVERIFY(!DesktopEntryFile::isGame({QStringLiteral("Game"), QStringLiteral("Settings")}));
  QVERIFY(!DesktopEntryFile::isGame({}));
  // Emulator frontends (RetroArch ships exactly "Game;Emulator;") DO count.
  // Launching another frontend from Kartend is a supported workflow, so this
  // is a product decision pinned by a test, not an oversight in the tool list.
  QVERIFY(DesktopEntryFile::isGame({QStringLiteral("Game"), QStringLiteral("Emulator")}));
}

void TestDesktopEntry::execProgramHandlesQuotingAndFieldCodes() {
  QCOMPARE(DesktopEntryFile::execProgram(QStringLiteral("steam steam://rungameid/620")),
           QStringLiteral("steam"));
  QCOMPARE(DesktopEntryFile::execProgram(QStringLiteral("/usr/bin/game -f %U")),
           QStringLiteral("/usr/bin/game"));
  // A quoted program with a space is the spec's escape for such paths.
  QCOMPARE(DesktopEntryFile::execProgram(QStringLiteral("\"/opt/My Game/run\" %f")),
           QStringLiteral("/opt/My Game/run"));
  QVERIFY(DesktopEntryFile::execProgram(QString()).isEmpty());
  QVERIFY(DesktopEntryFile::execProgram(QStringLiteral("%U")).isEmpty());
}

void TestDesktopEntry::iconLookupPrefersLargestRaster() {
  const QString root = m_dir.filePath(QStringLiteral("share"));
  writeFile(root + QStringLiteral("/icons/hicolor/64x64/apps/thegame.png"), "x");
  writeFile(root + QStringLiteral("/icons/hicolor/256x256/apps/thegame.png"), "x");
  writeFile(root + QStringLiteral("/pixmaps/oldgame.png"), "x");
  // SVG only: not usable as artwork, so it must not be reported.
  writeFile(root + QStringLiteral("/icons/hicolor/scalable/apps/vector.svg"), "x");

  QVERIFY(DesktopEntryFile::findIcon(QStringLiteral("thegame"), {root})
              .contains(QStringLiteral("256x256")));
  QVERIFY(DesktopEntryFile::findIcon(QStringLiteral("oldgame"), {root})
              .endsWith(QStringLiteral("pixmaps/oldgame.png")));
  QVERIFY(DesktopEntryFile::findIcon(QStringLiteral("missing"), {root}).isEmpty());

  // Kartend-0tddh: scalable is reported, but only after every raster is
  // exhausted — a PNG is a straight copy for the importer while an SVG has to
  // be rasterised, so a real raster always wins when the theme has one.
  QVERIFY(DesktopEntryFile::findIcon(QStringLiteral("vector"), {root})
              .endsWith(QStringLiteral("scalable/apps/vector.svg")));
  writeFile(root + QStringLiteral("/icons/hicolor/scalable/apps/thegame.svg"), "x");
  QVERIFY(DesktopEntryFile::findIcon(QStringLiteral("thegame"), {root})
              .endsWith(QStringLiteral("256x256/apps/thegame.png")));

  // Small rasters are poor covers but beat no cover; plenty of older packages
  // ship nothing bigger.
  writeFile(root + QStringLiteral("/icons/hicolor/48x48/apps/smallonly.png"), "x");
  QVERIFY(DesktopEntryFile::findIcon(QStringLiteral("smallonly"), {root})
              .endsWith(QStringLiteral("48x48/apps/smallonly.png")));

  // An absolute Icon= value is taken as-is, but only when it exists.
  const QString absolute = root + QStringLiteral("/pixmaps/oldgame.png");
  QCOMPARE(DesktopEntryFile::findIcon(absolute, {}), absolute);
  QVERIFY(DesktopEntryFile::findIcon(m_dir.filePath(QStringLiteral("none.png")), {}).isEmpty());
}

void TestDesktopEntry::shareRootsFollowXdgEnvironment() {
#ifdef Q_OS_WIN
  // XDG_DATA_DIRS is colon-separated, which cannot express a Windows path —
  // "C:/x:C:/y" is ambiguous at the drive letter. defaultShareRoots() reports
  // no roots there by design, so there is nothing to assert. The parsing
  // cases above stay live on every platform; only this one is Unix-shaped.
  QSKIP("XDG data roots are a Unix concept; defaultShareRoots() returns {} on Windows");
#else
  const QString home = m_dir.filePath(QStringLiteral("datahome"));
  const QString system = m_dir.filePath(QStringLiteral("datasystem"));
  QDir().mkpath(home);
  QDir().mkpath(system);
  const QByteArray previousHome = qgetenv("XDG_DATA_HOME");
  const QByteArray previousDirs = qgetenv("XDG_DATA_DIRS");
  qputenv("XDG_DATA_HOME", home.toUtf8());
  // The duplicate and the non-existent entry are both dropped; order is kept.
  qputenv("XDG_DATA_DIRS", (system + QStringLiteral(":") + m_dir.filePath(QStringLiteral("gone")) +
                            QStringLiteral(":") + system)
                               .toUtf8());

  const QStringList roots = DesktopEntryFile::defaultShareRoots();
  QCOMPARE(roots.size(), 2);
  QCOMPARE(roots.at(0), home);
  QCOMPARE(roots.at(1), system);

  qputenv("XDG_DATA_HOME", previousHome);
  qputenv("XDG_DATA_DIRS", previousDirs);
#endif
}

QTEST_MAIN(TestDesktopEntry)
#include "test_desktopentry.moc"
