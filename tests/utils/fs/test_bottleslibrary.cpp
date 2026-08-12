// BottlesLibrary against staged bottle.yml files in the shape PyYAML's
// SafeDumper writes them (Kartend-4cff2). The parser is a deliberate subset,
// so what it REFUSES matters as much as what it reads: an unrecognised shape
// must drop that entry rather than half-read it.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "bottleslibrary.h"

class TestBottlesLibrary : public QObject {
  Q_OBJECT

private slots:
  void parsesProgramsFromDumpedConfig();
  void handlesQuotedValuesAndEmptyProgramMap();
  void refusesShapesItDoesNotUnderstand();
  void listsProgramsAcrossBottles();
  void launchArgumentsMirrorBottlesOwnInvocation();

private:
  QTemporaryDir m_dir;
  int m_counter = 0;

  static void writeFile(const QString &filePath, const QByteArray &content) {
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(content), qint64(content.size()));
  }

  /// Writes `<dataDir>/bottles/<name>/bottle.yml` and returns the data dir.
  QString stageBottle(const QString &dataDir, const QString &name, const QByteArray &yaml) {
    writeFile(dataDir + QStringLiteral("/bottles/") + name + QStringLiteral("/bottle.yml"), yaml);
    return dataDir;
  }

  QString newDataDir() { return m_dir.filePath(QStringLiteral("data%1").arg(m_counter++)); }
};

void TestBottlesLibrary::parsesProgramsFromDumpedConfig() {
  // Keys sorted, four-space indent, plain scalars — SafeDumper's block style.
  // The Windows path keeps its backslashes and its drive colon unquoted, which
  // is exactly why the key/value split looks for ": " and not ":".
  const QByteArray yaml = QByteArrayLiteral(
      "Arch: win64\n"
      "Environment: Gaming\n"
      "External_Programs:\n"
      "    0a1b2c3d-0000-4000-8000-000000000001:\n"
      "        arguments: ''\n"
      "        auto_discovered: false\n"
      "        executable: TheGame.exe\n"
      "        folder: /home/u/.local/share/bottles/bottles/Gaming/drive_c/Games/TheGame\n"
      "        icon: com.usebottles.bottles-program\n"
      "        id: 0a1b2c3d-0000-4000-8000-000000000001\n"
      "        name: The Game\n"
      "        path: C:\\Games\\TheGame\\TheGame.exe\n"
      "    0a1b2c3d-0000-4000-8000-000000000002:\n"
      "        executable: Other.exe\n"
      "        name: Another Game\n"
      "        path: C:\\Games\\Other.exe\n"
      "Name: Gaming\n"
      "Parameters:\n"
      "    dxvk: true\n"
      "    sync: wine\n"
      "Runner: sys-wine\n");
  const QString dataDir = stageBottle(newDataDir(), QStringLiteral("Gaming"), yaml);

  const BottlesLibrary::Bottle bottle =
      BottlesLibrary::parseConfig(dataDir + QStringLiteral("/bottles/Gaming/bottle.yml"));
  QCOMPARE(bottle.name, QStringLiteral("Gaming"));
  QCOMPARE(bottle.programs.size(), 2);
  // Sorted by program name.
  QCOMPARE(bottle.programs.at(0).name, QStringLiteral("Another Game"));
  QCOMPARE(bottle.programs.at(1).name, QStringLiteral("The Game"));
  QCOMPARE(bottle.programs.at(1).executable, QStringLiteral("TheGame.exe"));
  QCOMPARE(bottle.programs.at(1).bottle, QStringLiteral("Gaming"));
  // A themed icon name is not a file, so it must not become an artwork path.
  QVERIFY(bottle.programs.at(1).iconPath.isEmpty());
}

void TestBottlesLibrary::handlesQuotedValuesAndEmptyProgramMap() {
  const QString iconFile = m_dir.filePath(QStringLiteral("icons/game.png"));
  writeFile(iconFile, "x");
  const QByteArray yaml = QByteArrayLiteral("External_Programs:\n"
                                            "    id-1:\n"
                                            "        executable: Quoted.exe\n"
                                            "        icon: ") +
                          iconFile.toUtf8() +
                          QByteArrayLiteral("\n"
                                            "        name: 'Tom''s Game: Remastered'\n"
                                            "    id-2:\n"
                                            "        name: \"Double \\\"Quoted\\\"\"\n"
                                            "Name: 'A Bottle: Named Oddly'\n");
  const QString dataDir = stageBottle(newDataDir(), QStringLiteral("Odd"), yaml);

  const BottlesLibrary::Bottle bottle =
      BottlesLibrary::parseConfig(dataDir + QStringLiteral("/bottles/Odd/bottle.yml"));
  QCOMPARE(bottle.name, QStringLiteral("A Bottle: Named Oddly"));
  QCOMPARE(bottle.programs.size(), 2);
  QCOMPARE(bottle.programs.at(0).name, QStringLiteral("Double \"Quoted\""));
  QCOMPARE(bottle.programs.at(1).name, QStringLiteral("Tom's Game: Remastered"));
  // An icon that IS a real file on disk is usable artwork.
  QCOMPARE(bottle.programs.at(1).iconPath, iconFile);

  // A bottle with no programs at all: `{}` is how the dumper writes it.
  const QString emptyDir = stageBottle(newDataDir(), QStringLiteral("Empty"),
                                       QByteArrayLiteral("External_Programs: {}\nName: Empty\n"));
  const BottlesLibrary::Bottle empty =
      BottlesLibrary::parseConfig(emptyDir + QStringLiteral("/bottles/Empty/bottle.yml"));
  QCOMPARE(empty.name, QStringLiteral("Empty"));
  QVERIFY(empty.programs.isEmpty());
}

void TestBottlesLibrary::refusesShapesItDoesNotUnderstand() {
  // A tab means this file did not come from the dumper — refuse it whole
  // rather than guess at the nesting.
  const QString tabbed = stageBottle(newDataDir(), QStringLiteral("Tabbed"),
                                     QByteArrayLiteral("Name: Tabbed\nExternal_Programs:\n"
                                                       "\tid-1:\n\t\tname: Nope\n"));
  QVERIFY(BottlesLibrary::parseConfig(tabbed + QStringLiteral("/bottles/Tabbed/bottle.yml"))
              .name.isEmpty());

  // A block scalar in a field is a shape the reader does not handle: that
  // ENTRY is dropped, and the rest of the bottle still reads.
  const QString block = stageBottle(newDataDir(), QStringLiteral("Block"),
                                    QByteArrayLiteral("External_Programs:\n"
                                                      "    id-1:\n"
                                                      "        name: |\n"
                                                      "            Multi\n"
                                                      "            Line\n"
                                                      "    id-2:\n"
                                                      "        name: Fine\n"
                                                      "Name: Block\n"));
  const BottlesLibrary::Bottle bottle =
      BottlesLibrary::parseConfig(block + QStringLiteral("/bottles/Block/bottle.yml"));
  QCOMPARE(bottle.programs.size(), 1);
  QCOMPARE(bottle.programs.at(0).name, QStringLiteral("Fine"));

  // A field that is itself a map (a program's per-launch environment) must not
  // leak its keys into the program: `environment` sorts before `name`, so a
  // nested "name" would otherwise be read first and win on any entry whose own
  // name is missing.
  const QString nested = stageBottle(newDataDir(), QStringLiteral("Nested"),
                                     QByteArrayLiteral("External_Programs:\n"
                                                       "    id-1:\n"
                                                       "        environment:\n"
                                                       "            name: NOT_A_PROGRAM\n"
                                                       "            executable: nope.exe\n"
                                                       "        executable: Real.exe\n"
                                                       "        name: Real Name\n"
                                                       "    id-2:\n"
                                                       "        environment:\n"
                                                       "            name: ALSO_NOT_A_PROGRAM\n"
                                                       "Name: Nested\n"));
  const BottlesLibrary::Bottle nestedBottle =
      BottlesLibrary::parseConfig(nested + QStringLiteral("/bottles/Nested/bottle.yml"));
  QCOMPARE(nestedBottle.programs.size(), 1);
  QCOMPARE(nestedBottle.programs.at(0).name, QStringLiteral("Real Name"));
  QCOMPARE(nestedBottle.programs.at(0).executable, QStringLiteral("Real.exe"));

  // No Name key: there is nothing to pass to `bottles-cli -b`, so the config
  // is unusable however many programs it lists.
  const QString unnamed = stageBottle(newDataDir(), QStringLiteral("Unnamed"),
                                      QByteArrayLiteral("External_Programs:\n"
                                                        "    id-1:\n"
                                                        "        name: Orphan\n"));
  QVERIFY(BottlesLibrary::parseConfig(unnamed + QStringLiteral("/bottles/Unnamed/bottle.yml"))
              .name.isEmpty());

  QVERIFY(
      BottlesLibrary::parseConfig(m_dir.filePath(QStringLiteral("no/such.yml"))).name.isEmpty());
}

void TestBottlesLibrary::listsProgramsAcrossBottles() {
  const QString dataDir = newDataDir();
  stageBottle(dataDir, QStringLiteral("Gaming"),
              QByteArrayLiteral("External_Programs:\n"
                                "    id-1:\n"
                                "        name: Zed Game\n"
                                "Name: Gaming\n"));
  stageBottle(dataDir, QStringLiteral("Testing"),
              QByteArrayLiteral("External_Programs:\n"
                                "    id-2:\n"
                                "        name: Alpha Game\n"
                                "Name: Testing\n"));
  // A bottle with no programs contributes nothing.
  stageBottle(dataDir, QStringLiteral("Bare"),
              QByteArrayLiteral("External_Programs: {}\nName: Bare\n"));

  QCOMPARE(BottlesLibrary::bottles(dataDir).size(), 2);
  const QList<BottlesLibrary::Program> programs = BottlesLibrary::installedPrograms(dataDir);
  QCOMPARE(programs.size(), 2);
  QCOMPARE(programs.at(0).name, QStringLiteral("Alpha Game"));
  QCOMPARE(programs.at(0).bottle, QStringLiteral("Testing"));
  QCOMPARE(programs.at(1).name, QStringLiteral("Zed Game"));

  QVERIFY(BottlesLibrary::installedPrograms(QString()).isEmpty());
}

void TestBottlesLibrary::launchArgumentsMirrorBottlesOwnInvocation() {
  BottlesLibrary::Program program;
  program.name = QStringLiteral("The Game");
  program.bottle = QStringLiteral("Gaming");
  // The template contributes `run -p <program>`; the stub carries the rest,
  // one argv slot per entry. The quoting in Bottles' own desktop entry is the
  // shell's problem, not ours — these go straight to QProcess.
  const QStringList expected = {QStringLiteral("-b"), QStringLiteral("Gaming"),
                                QStringLiteral("--")};
  QCOMPARE(BottlesLibrary::stubLaunchArguments(program), expected);
}

QTEST_MAIN(TestBottlesLibrary)
#include "test_bottleslibrary.moc"
