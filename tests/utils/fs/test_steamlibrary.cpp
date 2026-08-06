// SteamLibrary discovery against a staged fake Steam install tree: VDF
// parsing, libraryfolders traversal (secondary drives), appmanifest reading,
// runtime-tool filtering, and both librarycache artwork layouts.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "steamlibrary.h"

class TestSteamLibrary : public QObject {
  Q_OBJECT

private slots:
  void vdfParsesStringsAndNesting();
  void vdfHandlesEscapesCommentsConditionals();
  void runtimeToolFilter();
  void libraryFoldersIncludesSecondaryDrives();
  void installedGamesAcrossLibraries();
  void artworkBothLayouts();

private:
  QTemporaryDir m_dir;

  [[nodiscard]] QString steamRoot() const { return m_dir.filePath(QStringLiteral("steam")); }
  [[nodiscard]] QString secondLibrary() const { return m_dir.filePath(QStringLiteral("drive2")); }

  static void writeFile(const QString &filePath, const QByteArray &content) {
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(content), qint64(content.size()));
  }

  static QByteArray appManifest(const char *appid, const char *name, const char *installdir) {
    return QByteArrayLiteral("\"AppState\"\n{\n\t\"appid\"\t\"") + appid +
           QByteArrayLiteral("\"\n\t\"name\"\t\"") + name +
           QByteArrayLiteral("\"\n\t\"installdir\"\t\"") + installdir +
           QByteArrayLiteral("\"\n\t\"StateFlags\"\t\"4\"\n}\n");
  }

  /// Stages the full two-library fixture used by several slots. Idempotent.
  void stageFixture() {
    writeFile(steamRoot() + QStringLiteral("/steamapps/appmanifest_220.acf"),
              appManifest("220", "Half-Life 2", "Half-Life 2"));
    writeFile(steamRoot() + QStringLiteral("/steamapps/appmanifest_1628350.acf"),
              appManifest("1628350", "Steam Linux Runtime 3.0 (sniper)", "SteamLinuxRuntime"));
    writeFile(steamRoot() + QStringLiteral("/steamapps/appmanifest_228980.acf"),
              appManifest("228980", "Steamworks Common Redistributables", "Steamworks Shared"));
    writeFile(secondLibrary() + QStringLiteral("/steamapps/appmanifest_400.acf"),
              appManifest("400", "Portal", "Portal"));
    const QByteArray folders = QByteArrayLiteral("\"libraryfolders\"\n{\n"
                                                 "\t\"0\"\n\t{\n\t\t\"path\"\t\"") +
                               steamRoot().toUtf8() +
                               QByteArrayLiteral("\"\n\t}\n"
                                                 "\t\"1\"\n\t{\n\t\t\"path\"\t\"") +
                               secondLibrary().toUtf8() +
                               QByteArrayLiteral("\"\n\t}\n"
                                                 "\t\"contentstatsid\"\t\"-42\"\n}\n");
    writeFile(steamRoot() + QStringLiteral("/steamapps/libraryfolders.vdf"), folders);
  }
};

void TestSteamLibrary::vdfParsesStringsAndNesting() {
  const QVariantMap doc = SteamLibrary::parseVdf(QStringLiteral(
      "\"root\"\n{\n\t\"key\"\t\"value\"\n\t\"nested\"\n\t{\n\t\t\"a\"\t\"1\"\n\t}\n}\n"));
  const QVariantMap root = doc.value("root").toMap();
  QCOMPARE(root.value("key").toString(), QStringLiteral("value"));
  QCOMPARE(root.value("nested").toMap().value("a").toString(), QStringLiteral("1"));
}

void TestSteamLibrary::vdfHandlesEscapesCommentsConditionals() {
  const QVariantMap doc = SteamLibrary::parseVdf(
      QStringLiteral("// leading comment\n"
                     "\"root\"\n{\n"
                     "\t\"quoted\"\t\"say \\\"hi\\\"\"\t// trailing comment\n"
                     "\t\"path\"\t\"C:\\\\Games\"\n"
                     "\t\"cond\"\t\"win-only\" [$WIN32]\n"
                     "\tbareKey\tbareValue\n"
                     "}\n"));
  const QVariantMap root = doc.value("root").toMap();
  QCOMPARE(root.value("quoted").toString(), QStringLiteral("say \"hi\""));
  QCOMPARE(root.value("path").toString(), QStringLiteral("C:\\Games"));
  QCOMPARE(root.value("cond").toString(), QStringLiteral("win-only"));
  QCOMPARE(root.value("bareKey").toString(), QStringLiteral("bareValue"));
}

void TestSteamLibrary::runtimeToolFilter() {
  QVERIFY(SteamLibrary::isRuntimeTool(QStringLiteral("Proton 9.0 (Beta)")));
  QVERIFY(SteamLibrary::isRuntimeTool(QStringLiteral("Steam Linux Runtime 3.0 (sniper)")));
  QVERIFY(SteamLibrary::isRuntimeTool(QStringLiteral("Steamworks Common Redistributables")));
  QVERIFY(!SteamLibrary::isRuntimeTool(QStringLiteral("Half-Life 2")));
  // "Proton" mid-name is a legitimate game title, not a tool.
  QVERIFY(!SteamLibrary::isRuntimeTool(QStringLiteral("The Proton Conspiracy")));
}

void TestSteamLibrary::libraryFoldersIncludesSecondaryDrives() {
  stageFixture();
  const QStringList roots = SteamLibrary::libraryFolders(steamRoot());
  QCOMPARE(roots.size(), 2);
  QVERIFY(roots.contains(steamRoot()));
  QVERIFY(roots.contains(secondLibrary()));
}

void TestSteamLibrary::installedGamesAcrossLibraries() {
  stageFixture();
  const QList<SteamLibrary::Game> games = SteamLibrary::installedGames(steamRoot());
  // Runtime tooling filtered; the second drive's game found; sorted by name.
  QCOMPARE(games.size(), 2);
  QCOMPARE(games.at(0).name, QStringLiteral("Half-Life 2"));
  QCOMPARE(games.at(0).appId, QStringLiteral("220"));
  QCOMPARE(games.at(1).name, QStringLiteral("Portal"));
  QCOMPARE(games.at(1).libraryRoot, secondLibrary());
}

void TestSteamLibrary::artworkBothLayouts() {
  // Post-2023 per-app subdirectory layout.
  writeFile(steamRoot() + QStringLiteral("/appcache/librarycache/220/library_600x900.jpg"), "x");
  writeFile(steamRoot() + QStringLiteral("/appcache/librarycache/220/logo.png"), "x");
  const SteamLibrary::Artwork newLayout = SteamLibrary::artworkFor(steamRoot(), "220");
  QVERIFY(newLayout.cover.endsWith(QStringLiteral("220/library_600x900.jpg")));
  QVERIFY(newLayout.logo.endsWith(QStringLiteral("220/logo.png")));
  QVERIFY(newLayout.hero.isEmpty());

  // Older flat naming, with only a header — used as the cover fallback.
  writeFile(steamRoot() + QStringLiteral("/appcache/librarycache/400_header.jpg"), "x");
  writeFile(steamRoot() + QStringLiteral("/appcache/librarycache/400_library_hero.jpg"), "x");
  const SteamLibrary::Artwork flatLayout = SteamLibrary::artworkFor(steamRoot(), "400");
  QVERIFY(flatLayout.cover.endsWith(QStringLiteral("400_header.jpg")));
  QVERIFY(flatLayout.hero.endsWith(QStringLiteral("400_library_hero.jpg")));

  const SteamLibrary::Artwork none = SteamLibrary::artworkFor(steamRoot(), "999");
  QVERIFY(none.cover.isEmpty());
}

QTEST_MAIN(TestSteamLibrary)
#include "test_steamlibrary.moc"
