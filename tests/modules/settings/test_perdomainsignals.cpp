// Tests for the per-domain *Changed signals emitted from
// SettingsManager::saveCollections after a clean on-disk write. Each
// signal fires only when the named sub-struct actually changed against
// the previously-saved snapshot, identified by (name, mediaDirectory)
// UUID. Verifies both the emit-on-diff path and the no-op-on-equal path
// so a noisy regression (every Save firing every signal) gets caught.

#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include "collectionutils.h"
#include "settingsmanager.h"
#include "settingsutils.h"

class TestPerDomainSignals : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();

  void unchangedSave_emitsNoPerDomainSignals();
  void gridLayoutChange_emitsOnlyGridLayoutSignal();
  void sidebarChange_emitsOnlySidebarSignal();
  void multipleDomainChanges_eachEmitsOnce();
  void addedCollection_doesNotFirePerDomainSignals();
  void reorderOnly_doesNotFirePerDomainSignals();
};

void TestPerDomainSignals::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
}

void TestPerDomainSignals::cleanupTestCase() {
  QStandardPaths::setTestModeEnabled(false);
}

void TestPerDomainSignals::init() {
  const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
  QVERIFY2(configRoot.contains(QStringLiteral("qttest")),
           "QStandardPaths test mode should reroute ConfigLocation under qttest");
  QDir(configRoot).removeRecursively();
}

namespace {

CollectionConfig makeCol(const QString &name, const QString &mediaDir) {
  CollectionConfig c;
  c.name = name;
  c.mediaDirectory = mediaDir;
  return c;
}

} // namespace

void TestPerDomainSignals::unchangedSave_emitsNoPerDomainSignals() {
  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections{makeCol("A", "/tmp/a"), makeCol("B", "/tmp/b")};
  mgr.saveCollections(collections); // seed the baseline

  QSignalSpy gridSpy(&mgr, &ISettingsManager::gridLayoutChanged);
  QSignalSpy sidebarSpy(&mgr, &ISettingsManager::sidebarAppearanceChanged);
  QSignalSpy bgSpy(&mgr, &ISettingsManager::collectionBackgroundChanged);

  // Save the same list again — no field changed, so no per-domain signal
  // should fire. (The coarse collectionsModified() does still fire — it
  // covers the lifecycle / add+remove case and is intentionally noisier.)
  mgr.saveCollections(collections);

  QCOMPARE(gridSpy.count(), 0);
  QCOMPARE(sidebarSpy.count(), 0);
  QCOMPARE(bgSpy.count(), 0);
}

void TestPerDomainSignals::gridLayoutChange_emitsOnlyGridLayoutSignal() {
  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections{makeCol("A", "/tmp/a")};
  mgr.saveCollections(collections); // seed

  QSignalSpy gridSpy(&mgr, &ISettingsManager::gridLayoutChanged);
  QSignalSpy sidebarSpy(&mgr, &ISettingsManager::sidebarAppearanceChanged);

  collections[0].gridLayout.gridWidth = 8;
  mgr.saveCollections(collections);

  QCOMPARE(gridSpy.count(), 1);
  QCOMPARE(gridSpy.first().at(0).toInt(), 0); // collectionIndex
  QCOMPARE(sidebarSpy.count(), 0);
}

void TestPerDomainSignals::sidebarChange_emitsOnlySidebarSignal() {
  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections{makeCol("A", "/tmp/a")};
  mgr.saveCollections(collections);

  QSignalSpy gridSpy(&mgr, &ISettingsManager::gridLayoutChanged);
  QSignalSpy sidebarSpy(&mgr, &ISettingsManager::sidebarAppearanceChanged);

  collections[0].sidebar.sidebarVisible = !collections[0].sidebar.sidebarVisible;
  mgr.saveCollections(collections);

  QCOMPARE(sidebarSpy.count(), 1);
  QCOMPARE(sidebarSpy.first().at(0).toInt(), 0);
  QCOMPARE(gridSpy.count(), 0);
}

void TestPerDomainSignals::multipleDomainChanges_eachEmitsOnce() {
  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections{makeCol("A", "/tmp/a")};
  mgr.saveCollections(collections);

  QSignalSpy gridSpy(&mgr, &ISettingsManager::gridLayoutChanged);
  QSignalSpy sidebarSpy(&mgr, &ISettingsManager::sidebarAppearanceChanged);
  QSignalSpy bgSpy(&mgr, &ISettingsManager::collectionBackgroundChanged);
  QSignalSpy archiveSpy(&mgr, &ISettingsManager::archiveOptionsChanged);

  // Mutate three independent leaf structs in a single save.
  collections[0].gridLayout.gridWidth = 6;
  collections[0].background.backgroundColor = QStringLiteral("#abcdef");
  collections[0].archive.extractArchives = true;
  mgr.saveCollections(collections);

  QCOMPARE(gridSpy.count(), 1);
  QCOMPARE(bgSpy.count(), 1);
  QCOMPARE(archiveSpy.count(), 1);
  QCOMPARE(sidebarSpy.count(), 0); // unchanged
}

void TestPerDomainSignals::addedCollection_doesNotFirePerDomainSignals() {
  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections{makeCol("A", "/tmp/a")};
  mgr.saveCollections(collections); // seed with only A

  QSignalSpy gridSpy(&mgr, &ISettingsManager::gridLayoutChanged);
  QSignalSpy sidebarSpy(&mgr, &ISettingsManager::sidebarAppearanceChanged);

  // Append B — a new collection. Per-domain signals describe mutations
  // of existing collections, so the new one should NOT trigger them
  // (the coarse collectionsModified covers add/remove lifecycle).
  collections.append(makeCol("B", "/tmp/b"));
  mgr.saveCollections(collections);

  QCOMPARE(gridSpy.count(), 0);
  QCOMPARE(sidebarSpy.count(), 0);
}

void TestPerDomainSignals::reorderOnly_doesNotFirePerDomainSignals() {
  SettingsManager mgr(nullptr, nullptr);
  QList<CollectionConfig> collections{makeCol("A", "/tmp/a"), makeCol("B", "/tmp/b"),
                                      makeCol("C", "/tmp/c")};
  mgr.saveCollections(collections);

  QSignalSpy gridSpy(&mgr, &ISettingsManager::gridLayoutChanged);
  QSignalSpy sidebarSpy(&mgr, &ISettingsManager::sidebarAppearanceChanged);

  // Reorder without changing any fields. Identity is by UUID
  // (name, mediaDirectory), so a reorder must NOT fire spurious diffs.
  std::reverse(collections.begin(), collections.end());
  mgr.saveCollections(collections);

  QCOMPARE(gridSpy.count(), 0);
  QCOMPARE(sidebarSpy.count(), 0);
}

QTEST_GUILESS_MAIN(TestPerDomainSignals)
#include "test_perdomainsignals.moc"
