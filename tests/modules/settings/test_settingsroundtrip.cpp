// Tests for strict round-trip preservation of unknown per-collection INI
// keys. A newer build that writes a key (a future feature flag, an
// experimental setting) followed by an older build that loads + saves the
// same file must not silently drop the unknown key. Coverage protects
// against a version-skew data-loss class.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

#include "collection/collectionconfig.h"
#include "settingsmanager.h"
#include "settingsutils.h"

class TestSettingsRoundtrip : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();

  void unknownPerCollectionKey_preservedThroughLoadSave();
  void knownKey_overwritesPreservedDuplicate();
  void legacyBlocklistedKey_droppedNotPreserved();

private:
  static void writeConfigIni(const QString &iniContents);
  static QString readConfigIni();
};

void TestSettingsRoundtrip::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
}

void TestSettingsRoundtrip::cleanupTestCase() {
  QStandardPaths::setTestModeEnabled(false);
}

void TestSettingsRoundtrip::init() {
  const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
  // The path must be a test sandbox before we removeRecursively() it: Qt
  // test-mode's "qttest" marker (Linux/Windows) or, on macOS where Qt 6.8 test
  // mode doesn't reroute ConfigLocation (Kartend-zfwvr), under the
  // CFFIXED_USER_HOME home main() pinned.
  const QByteArray sandboxHome = qgetenv("CFFIXED_USER_HOME");
  QVERIFY2(
      configRoot.contains(QStringLiteral("qttest")) ||
          (!sandboxHome.isEmpty() && configRoot.startsWith(QString::fromLocal8Bit(sandboxHome))),
      "QStandardPaths config dir is not a test sandbox");
  QDir(configRoot).removeRecursively();
}

void TestSettingsRoundtrip::writeConfigIni(const QString &iniContents) {
  const QString dest = SettingsUtils::getConfigPath();
  QFile::remove(dest);
  QDir().mkpath(QFileInfo(dest).absolutePath());
  QFile f(dest);
  QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Text),
           qPrintable(QStringLiteral("Failed to open %1 for write").arg(dest)));
  QTextStream out(&f);
  out << iniContents;
  f.close();
}

QString TestSettingsRoundtrip::readConfigIni() {
  QFile f(SettingsUtils::getConfigPath());
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  return QString::fromUtf8(f.readAll());
}

void TestSettingsRoundtrip::unknownPerCollectionKey_preservedThroughLoadSave() {
  // Seed a collection whose section carries a key this build doesn't
  // recognise. Imagine a future Kartend build added it and an older build
  // (this one) is now loading + saving the same file.
  writeConfigIni(QStringLiteral("[TestCol]\n"
                                "name=TestCol\n"
                                "mediaDirectory=/tmp/media\n"
                                "futureFeatureEnabled=true\n"
                                "experimentalScrollMode=trampoline\n"));

  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections;
  mgr.loadCollections(collections);
  QCOMPARE(collections.size(), 1);
  QCOMPARE(collections[0].name, QStringLiteral("TestCol"));
  QVERIFY(collections[0].preservedKeys.contains(QStringLiteral("futureFeatureEnabled")));
  QVERIFY(collections[0].preservedKeys.contains(QStringLiteral("experimentalScrollMode")));

  // Save without modifying the unknown keys; they must survive.
  mgr.saveCollections(collections);

  const QString rewritten = readConfigIni();
  QVERIFY2(
      rewritten.contains(QStringLiteral("futureFeatureEnabled=true")),
      qPrintable(QStringLiteral("Unknown bool key was dropped on round-trip:\n%1").arg(rewritten)));
  QVERIFY2(rewritten.contains(QStringLiteral("experimentalScrollMode=trampoline")),
           qPrintable(
               QStringLiteral("Unknown string key was dropped on round-trip:\n%1").arg(rewritten)));
}

void TestSettingsRoundtrip::knownKey_overwritesPreservedDuplicate() {
  // The capture-then-replay strategy relies on known-field setValue()
  // overwriting the duplicate from preservedKeys. If that ordering ever
  // regresses, modifying a known field via the dialog would silently get
  // rolled back to the on-disk value.
  writeConfigIni(QStringLiteral("[TestCol]\n"
                                "name=TestCol\n"
                                "mediaDirectory=/tmp/old\n"));

  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections;
  mgr.loadCollections(collections);
  QCOMPARE(collections.size(), 1);

  collections[0].mediaDirectory = QStringLiteral("/tmp/new");
  mgr.saveCollections(collections);

  const QString rewritten = readConfigIni();
  QVERIFY2(rewritten.contains(QStringLiteral("mediaDirectory=/tmp/new")),
           qPrintable(QStringLiteral("Known-field write did NOT overwrite preserved duplicate:\n%1")
                          .arg(rewritten)));
  QVERIFY2(!rewritten.contains(QStringLiteral("mediaDirectory=/tmp/old")),
           qPrintable(
               QStringLiteral("Stale value lingered after known-field write:\n%1").arg(rewritten)));
}

void TestSettingsRoundtrip::legacyBlocklistedKey_droppedNotPreserved() {
  // videoDirectory and manualDirectory were intentionally collapsed into
  // the {artwork}/video and {artwork}/manual sublayouts. The strict
  // preservation logic must NOT undo that deliberate removal — the
  // blocklist exists for exactly this case.
  writeConfigIni(QStringLiteral("[TestCol]\n"
                                "name=TestCol\n"
                                "videoDirectory=/tmp/legacy-videos\n"
                                "manualDirectory=/tmp/legacy-manuals\n"));

  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections;
  mgr.loadCollections(collections);
  QCOMPARE(collections.size(), 1);
  QVERIFY(!collections[0].preservedKeys.contains(QStringLiteral("videoDirectory")));
  QVERIFY(!collections[0].preservedKeys.contains(QStringLiteral("manualDirectory")));

  mgr.saveCollections(collections);

  const QString rewritten = readConfigIni();
  QVERIFY2(
      !rewritten.contains(QStringLiteral("videoDirectory=")),
      qPrintable(
          QStringLiteral("Blocklisted videoDirectory leaked on round-trip:\n%1").arg(rewritten)));
  QVERIFY2(
      !rewritten.contains(QStringLiteral("manualDirectory=")),
      qPrintable(
          QStringLiteral("Blocklisted manualDirectory leaked on round-trip:\n%1").arg(rewritten)));
}

// Custom main (see test_settingsmigration): pin the config sandbox via
// CFFIXED_USER_HOME before QCoreApplication / any QStandardPaths query, since
// macOS Qt 6.8 test mode doesn't reroute ConfigLocation (Kartend-zfwvr).
int main(int argc, char *argv[]) {
  QTemporaryDir sandbox;
  qputenv("CFFIXED_USER_HOME", QFile::encodeName(sandbox.path()));
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication app(argc, argv);
  TestSettingsRoundtrip tc;
  return QTest::qExec(&tc, argc, argv);
}
#include "test_settingsroundtrip.moc"
