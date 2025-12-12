/**
 * @file test_sessionmanager.cpp
 * @brief Unit tests for SessionManager session state persistence
 *
 * Tests the session state management including last selected items,
 * collection counts, and JSON persistence.
 */

#include "sessionmanager.h"
#include "collectionutils.h"
#include <QTemporaryDir>
#include <QTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

class TestSessionManager : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // Last selected state tests
  void testSetLastSelected_basic();
  void testGetLastSelectedIndex_notFound();
  void testGetLastSelectedIndex_suffixMatch();
  void testSetLastSelected_updateExisting();
  
  // Global item count tests
  void testGlobalItemCount_initial();
  void testGlobalItemCount_set();
  
  // Collection counts tests
  void testSetCollectionCounts_basic();
  void testGetCollectionCounts_notFound();
  void testGetCollectionCounts_hierarchical();
  
  // Clear stale collections tests
  void testClearStaleCollections_removesInvalid();
  void testClearStaleCollections_keepsValid();

private:
  SessionManager *m_sessionManager;
  QTemporaryDir *m_tempDir;
  
  // Helper to create test collection configs
  CollectionConfig createTestCollection(const QString &name, 
                                        int parentIndex = -1,
                                        bool isSubcollection = false);
};

void TestSessionManager::initTestCase() {
  m_tempDir = new QTemporaryDir();
  QVERIFY(m_tempDir->isValid());
}

void TestSessionManager::cleanupTestCase() {
  delete m_tempDir;
  m_tempDir = nullptr;
}

void TestSessionManager::init() {
  m_sessionManager = new SessionManager();
  // Note: We don't call initialize() to start with clean state
}

void TestSessionManager::cleanup() {
  delete m_sessionManager;
  m_sessionManager = nullptr;
}

CollectionConfig TestSessionManager::createTestCollection(const QString &name,
                                                          int parentIndex,
                                                          bool isSubcollection) {
  CollectionConfig config;
  config.name = name;
  config.parentCollectionIndex = parentIndex;
  config.isSubcollection = isSubcollection;
  config.mediaDirectory = m_tempDir->path() + "/" + name;
  return config;
}

// ─────────────────────────────────────────────────────────────────────────────
// Last selected state tests
// ─────────────────────────────────────────────────────────────────────────────

void TestSessionManager::testSetLastSelected_basic() {
  m_sessionManager->setLastSelected("TestCollection", 5, "Test Title");
  
  int index = m_sessionManager->getLastSelectedIndex("TestCollection");
  QCOMPARE(index, 5);
}

void TestSessionManager::testGetLastSelectedIndex_notFound() {
  int index = m_sessionManager->getLastSelectedIndex("NonExistent");
  QCOMPARE(index, -1);
}

void TestSessionManager::testGetLastSelectedIndex_suffixMatch() {
  // Set with hierarchical name
  m_sessionManager->setLastSelected("Parent/Child", 10, "Child Item");
  
  // Should find by suffix match
  int index = m_sessionManager->getLastSelectedIndex("Child");
  QCOMPARE(index, 10);
}

void TestSessionManager::testSetLastSelected_updateExisting() {
  m_sessionManager->setLastSelected("Collection", 1, "First");
  m_sessionManager->setLastSelected("Collection", 2, "Second");
  
  int index = m_sessionManager->getLastSelectedIndex("Collection");
  QCOMPARE(index, 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Global item count tests
// ─────────────────────────────────────────────────────────────────────────────

void TestSessionManager::testGlobalItemCount_initial() {
  qint64 count = m_sessionManager->getGlobalItemCount();
  QCOMPARE(count, 0);
}

void TestSessionManager::testGlobalItemCount_set() {
  m_sessionManager->setGlobalItemCount(12345);
  qint64 count = m_sessionManager->getGlobalItemCount();
  QCOMPARE(count, 12345);
}

// ─────────────────────────────────────────────────────────────────────────────
// Collection counts tests
// ─────────────────────────────────────────────────────────────────────────────

void TestSessionManager::testSetCollectionCounts_basic() {
  CollectionConfig config = createTestCollection("Games");
  QList<CollectionConfig> allCollections = {config};
  
  m_sessionManager->setCollectionCounts(config, allCollections, 100, 200);
  
  qint64 itemCount = 0;
  qint64 recursiveCount = 0;
  bool found = m_sessionManager->getCollectionCounts(config, allCollections, 
                                                      itemCount, recursiveCount);
  QVERIFY(found);
  QCOMPARE(itemCount, 100);
  QCOMPARE(recursiveCount, 200);
}

void TestSessionManager::testGetCollectionCounts_notFound() {
  CollectionConfig config = createTestCollection("NonExistent");
  QList<CollectionConfig> allCollections = {config};
  
  qint64 itemCount = 0;
  qint64 recursiveCount = 0;
  bool found = m_sessionManager->getCollectionCounts(config, allCollections,
                                                      itemCount, recursiveCount);
  QVERIFY(!found);
}

void TestSessionManager::testGetCollectionCounts_hierarchical() {
  // Create parent and child collections
  CollectionConfig parent = createTestCollection("Emulators");
  CollectionConfig child = createTestCollection("SNES", 0, true);
  QList<CollectionConfig> allCollections = {parent, child};
  
  // Set counts for child (hierarchical name will be "Emulators/SNES")
  m_sessionManager->setCollectionCounts(child, allCollections, 50, 50);
  
  qint64 itemCount = 0;
  qint64 recursiveCount = 0;
  bool found = m_sessionManager->getCollectionCounts(child, allCollections,
                                                      itemCount, recursiveCount);
  QVERIFY(found);
  QCOMPARE(itemCount, 50);
}

// ─────────────────────────────────────────────────────────────────────────────
// Clear stale collections tests
// ─────────────────────────────────────────────────────────────────────────────

void TestSessionManager::testClearStaleCollections_removesInvalid() {
  // Set up initial state with collection that will become stale
  CollectionConfig oldConfig = createTestCollection("OldCollection");
  QList<CollectionConfig> oldCollections = {oldConfig};
  m_sessionManager->setCollectionCounts(oldConfig, oldCollections, 10, 10);
  
  // New collections don't include OldCollection
  CollectionConfig newConfig = createTestCollection("NewCollection");
  QList<CollectionConfig> newCollections = {newConfig};
  
  // Clear stale
  m_sessionManager->clearStaleCollections(newCollections);
  
  // Old collection counts should be gone
  qint64 itemCount = 0;
  qint64 recursiveCount = 0;
  bool found = m_sessionManager->getCollectionCounts(oldConfig, oldCollections,
                                                      itemCount, recursiveCount);
  QVERIFY(!found);
}

void TestSessionManager::testClearStaleCollections_keepsValid() {
  // Set up state with valid collection
  CollectionConfig config = createTestCollection("ValidCollection");
  QList<CollectionConfig> collections = {config};
  m_sessionManager->setCollectionCounts(config, collections, 25, 25);
  
  // Clear stale with same collections
  m_sessionManager->clearStaleCollections(collections);
  
  // Valid collection counts should remain
  qint64 itemCount = 0;
  qint64 recursiveCount = 0;
  bool found = m_sessionManager->getCollectionCounts(config, collections,
                                                      itemCount, recursiveCount);
  QVERIFY(found);
  QCOMPARE(itemCount, 25);
}

QTEST_MAIN(TestSessionManager)
#include "test_sessionmanager.moc"