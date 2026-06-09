// Tests for RetroArchUtils — retroarch.cfg parsing, core-directory
// resolution, and core enumeration. The standard-location probe in
// defaultConfigPaths() is OS-specific and filesystem-dependent, so the
// tests drive the override paths instead, which exercise the same
// parse + discovery code.

#include "retroarchutils.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

class TestRetroArchUtils : public QObject {
  Q_OBJECT
private slots:
  void parsesCoreDirectoryFromConfig();
  void expandsTildeInLibretroDirectory();
  void resolvesRelativeLibretroDirectoryAgainstConfigDir();
  void configDefaultSentinelReturnsEmpty();
  void configWithUnsafeLibretroDirectoryReturnsEmpty();
  void configWithoutKeyReturnsEmpty();
  void resolveCoreDirectory_dirOverrideUsedAsIs();
  void resolveCoreDirectory_cfgOverrideIsParsed();
  void resolveCoreDirectory_missingOverrideFallsThrough();
  void discoverCores_listsAndNamesLibretroFiles();
  void discoverCores_emptyForMissingDirectory();

private:
  QTemporaryDir m_dir;
  static void writeFile(const QString &path, const QByteArray &bytes);
};

void TestRetroArchUtils::writeFile(const QString &path, const QByteArray &bytes) {
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(bytes);
  f.close();
}

void TestRetroArchUtils::parsesCoreDirectoryFromConfig() {
  const QString coreDir = m_dir.filePath(QStringLiteral("cores"));
  QVERIFY(QDir().mkpath(coreDir));
  const QString cfg = m_dir.filePath(QStringLiteral("retroarch.cfg"));
  writeFile(cfg, QStringLiteral("video_driver = \"gl\"\n"
                                "libretro_directory = \"%1\"\n"
                                "audio_enable = \"true\"\n")
                     .arg(coreDir)
                     .toUtf8());
  QCOMPARE(RetroArchUtils::coreDirectoryFromConfig(cfg), QDir::cleanPath(coreDir));
}

void TestRetroArchUtils::expandsTildeInLibretroDirectory() {
  // retroarch.cfg routinely stores the core dir home-relative with a
  // leading `~`; it must expand to the real home directory.
  const QString cfg = m_dir.filePath(QStringLiteral("tilde.cfg"));
  writeFile(cfg, QByteArrayLiteral("libretro_directory = \"~/.config/retroarch/cores\"\n"));
  QCOMPARE(RetroArchUtils::coreDirectoryFromConfig(cfg),
           QDir(QDir::homePath()).filePath(QStringLiteral(".config/retroarch/cores")));
}

void TestRetroArchUtils::resolvesRelativeLibretroDirectoryAgainstConfigDir() {
  // A relative core dir resolves against the config file's directory.
  const QString cfg = m_dir.filePath(QStringLiteral("relative.cfg"));
  writeFile(cfg, QByteArrayLiteral("libretro_directory = \"cores\"\n"));
  QCOMPARE(RetroArchUtils::coreDirectoryFromConfig(cfg),
           QDir(QFileInfo(cfg).absolutePath()).filePath(QStringLiteral("cores")));
}

void TestRetroArchUtils::configDefaultSentinelReturnsEmpty() {
  // RetroArch writes "default" when the core dir hasn't been set —
  // that is not a real path.
  const QString cfg = m_dir.filePath(QStringLiteral("default.cfg"));
  writeFile(cfg, QByteArrayLiteral("libretro_directory = \"default\"\n"));
  QVERIFY(RetroArchUtils::coreDirectoryFromConfig(cfg).isEmpty());
}

void TestRetroArchUtils::configWithUnsafeLibretroDirectoryReturnsEmpty() {
  // Kartend-b2hi9: a libretro_directory carrying shell metachars (e.g. from a
  // third-party retroarch.cfg) is rejected by validatePathSecurity and treated
  // as unset, so it never drives core discovery / the -L launch argument.
  const QString cfg = m_dir.filePath(QStringLiteral("unsafe.cfg"));
  writeFile(cfg, QByteArrayLiteral("libretro_directory = \"/cores; rm -rf /\"\n"));
  QVERIFY(RetroArchUtils::coreDirectoryFromConfig(cfg).isEmpty());
}

void TestRetroArchUtils::configWithoutKeyReturnsEmpty() {
  const QString cfg = m_dir.filePath(QStringLiteral("nokey.cfg"));
  writeFile(cfg, QByteArrayLiteral("video_driver = \"gl\"\n"));
  QVERIFY(RetroArchUtils::coreDirectoryFromConfig(cfg).isEmpty());
}

void TestRetroArchUtils::resolveCoreDirectory_dirOverrideUsedAsIs() {
  const QString coreDir = m_dir.filePath(QStringLiteral("direct-cores"));
  QVERIFY(QDir().mkpath(coreDir));
  QCOMPARE(RetroArchUtils::resolveCoreDirectory(coreDir), QDir::cleanPath(coreDir));
}

void TestRetroArchUtils::resolveCoreDirectory_cfgOverrideIsParsed() {
  const QString coreDir = m_dir.filePath(QStringLiteral("cfg-cores"));
  QVERIFY(QDir().mkpath(coreDir));
  const QString cfg = m_dir.filePath(QStringLiteral("override.cfg"));
  writeFile(cfg, QStringLiteral("libretro_directory = \"%1\"\n").arg(coreDir).toUtf8());
  QCOMPARE(RetroArchUtils::resolveCoreDirectory(cfg), QDir::cleanPath(coreDir));
}

void TestRetroArchUtils::resolveCoreDirectory_missingOverrideFallsThrough() {
  // A non-existent override path is treated as "unset" — the result
  // is whatever the standard probe finds (often empty in CI), but it
  // must not echo the dead override back.
  const QString missing = m_dir.filePath(QStringLiteral("does-not-exist.cfg"));
  QVERIFY(RetroArchUtils::resolveCoreDirectory(missing) != missing);
}

void TestRetroArchUtils::discoverCores_listsAndNamesLibretroFiles() {
  const QString coreDir = m_dir.filePath(QStringLiteral("enum-cores"));
  QVERIFY(QDir().mkpath(coreDir));
  writeFile(QDir(coreDir).filePath(QStringLiteral("snes9x_libretro.so")), QByteArrayLiteral("x"));
  writeFile(QDir(coreDir).filePath(QStringLiteral("mgba_libretro.so")), QByteArrayLiteral("x"));
  // A non-core file must be ignored.
  writeFile(QDir(coreDir).filePath(QStringLiteral("readme.txt")), QByteArrayLiteral("x"));

  const auto cores = RetroArchUtils::discoverCores(coreDir);
  QCOMPARE(cores.size(), 2);
  // Sorted by display name; the `_libretro` tag + extension are stripped.
  QCOMPARE(cores.at(0).displayName, QStringLiteral("mgba"));
  QCOMPARE(cores.at(1).displayName, QStringLiteral("snes9x"));
  QCOMPARE(cores.at(1).path, QDir(coreDir).filePath(QStringLiteral("snes9x_libretro.so")));
}

void TestRetroArchUtils::discoverCores_emptyForMissingDirectory() {
  QVERIFY(RetroArchUtils::discoverCores(m_dir.filePath(QStringLiteral("absent"))).isEmpty());
  QVERIFY(RetroArchUtils::discoverCores(QString()).isEmpty());
}

QTEST_MAIN(TestRetroArchUtils)
#include "test_retroarchutils.moc"
