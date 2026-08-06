// FlatpakLibrary discovery against a staged exports tree: .desktop parsing
// (exact keys, locale variants ignored), Game-category filtering, NoDisplay,
// X-Flatpak app-id precedence, cross-root dedup, and icon lookup.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "flatpaklibrary.h"

class TestFlatpakLibrary : public QObject {
  Q_OBJECT

private slots:
  void parsesDesktopEntry();
  void ignoresOtherSectionsAndLocalisedNames();
  void installedGamesFiltersAndDedups();
  void iconLookupPrefersLargest();

private:
  QTemporaryDir m_dir;

  [[nodiscard]] QString systemRoot() const { return m_dir.filePath(QStringLiteral("system")); }
  [[nodiscard]] QString userRoot() const { return m_dir.filePath(QStringLiteral("user")); }

  static void writeFile(const QString &filePath, const QByteArray &content) {
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(content), qint64(content.size()));
  }

  void writeDesktop(const QString &root, const QString &fileName, const QByteArray &body) {
    writeFile(root + QStringLiteral("/applications/") + fileName, body);
  }
};

void TestFlatpakLibrary::parsesDesktopEntry() {
  const QString path = m_dir.filePath(QStringLiteral("one.desktop"));
  writeFile(path, QByteArrayLiteral("[Desktop Entry]\n"
                                    "Name=SuperTuxKart\n"
                                    "Icon=net.supertuxkart.SuperTuxKart\n"
                                    "Categories=Game;ArcadeGame;\n"
                                    "X-Flatpak=net.supertuxkart.SuperTuxKart\n"));
  const FlatpakLibrary::DesktopEntry entry = FlatpakLibrary::parseDesktopFile(path);
  QCOMPARE(entry.name, QStringLiteral("SuperTuxKart"));
  QCOMPARE(entry.flatpakId, QStringLiteral("net.supertuxkart.SuperTuxKart"));
  QVERIFY(entry.categories.contains(QStringLiteral("Game")));
  QVERIFY(!entry.noDisplay);
}

void TestFlatpakLibrary::ignoresOtherSectionsAndLocalisedNames() {
  const QString path = m_dir.filePath(QStringLiteral("two.desktop"));
  writeFile(path, QByteArrayLiteral("[Desktop Entry]\n"
                                    "Name=Game Name\n"
                                    "Name[de]=Spielname\n"
                                    "NoDisplay=true\n"
                                    "[Desktop Action foo]\n"
                                    "Name=Action Name\n"));
  const FlatpakLibrary::DesktopEntry entry = FlatpakLibrary::parseDesktopFile(path);
  // The unlocalised Name wins and the action section's Name never leaks in.
  QCOMPARE(entry.name, QStringLiteral("Game Name"));
  QVERIFY(entry.noDisplay);
}

void TestFlatpakLibrary::installedGamesFiltersAndDedups() {
  writeDesktop(systemRoot(), QStringLiteral("net.supertuxkart.SuperTuxKart.desktop"),
               QByteArrayLiteral("[Desktop Entry]\nName=SuperTuxKart\nCategories=Game;\n"
                                 "X-Flatpak=net.supertuxkart.SuperTuxKart\n"));
  // Not a game — filtered.
  writeDesktop(systemRoot(), QStringLiteral("org.gnome.Calculator.desktop"),
               QByteArrayLiteral("[Desktop Entry]\nName=Calculator\nCategories=Utility;\n"));
  // Game but NoDisplay — filtered.
  writeDesktop(systemRoot(), QStringLiteral("org.example.Hidden.desktop"),
               QByteArrayLiteral("[Desktop Entry]\nName=Hidden\nCategories=Game;\n"
                                 "NoDisplay=true\n"));
  // Gaming-adjacent tool (the ProtonUp-Qt shape): Game paired with a
  // tool-signal main category — filtered.
  writeDesktop(systemRoot(), QStringLiteral("net.davidotek.pupgui2.desktop"),
               QByteArrayLiteral("[Desktop Entry]\nName=ProtonUp-Qt\n"
                                 "Categories=Game;Utility;\n"
                                 "X-Flatpak=net.davidotek.pupgui2\n"));
  // Same app exported in the user root too — deduped, first root wins.
  writeDesktop(userRoot(), QStringLiteral("net.supertuxkart.SuperTuxKart.desktop"),
               QByteArrayLiteral("[Desktop Entry]\nName=SuperTuxKart User Copy\n"
                                 "Categories=Game;\nX-Flatpak=net.supertuxkart.SuperTuxKart\n"));
  // No X-Flatpak key — app id falls back to the desktop file's basename.
  writeDesktop(userRoot(), QStringLiteral("org.example.NoKey.desktop"),
               QByteArrayLiteral("[Desktop Entry]\nName=A Bare Game\nCategories=Game;\n"));

  const QList<FlatpakLibrary::App> apps =
      FlatpakLibrary::installedGames({systemRoot(), userRoot()});
  QCOMPARE(apps.size(), 2);
  QCOMPARE(apps.at(0).name, QStringLiteral("A Bare Game"));
  QCOMPARE(apps.at(0).appId, QStringLiteral("org.example.NoKey"));
  QCOMPARE(apps.at(1).name, QStringLiteral("SuperTuxKart"));
}

void TestFlatpakLibrary::iconLookupPrefersLargest() {
  const QString appId = QStringLiteral("net.supertuxkart.SuperTuxKart");
  writeFile(systemRoot() + QStringLiteral("/icons/hicolor/64x64/apps/") + appId +
                QStringLiteral(".png"),
            "x");
  writeFile(systemRoot() + QStringLiteral("/icons/hicolor/256x256/apps/") + appId +
                QStringLiteral(".png"),
            "x");
  const QString icon = FlatpakLibrary::findExportedIcon(systemRoot(), appId);
  QVERIFY(icon.contains(QStringLiteral("256x256")));
  QVERIFY(FlatpakLibrary::findExportedIcon(systemRoot(), QStringLiteral("org.none")).isEmpty());
}

QTEST_MAIN(TestFlatpakLibrary)
#include "test_flatpaklibrary.moc"
