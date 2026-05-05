/**
 * @file test_applicationmanager_lifecycle.h
 * @brief Kartend-crr: lifecycle tests for ApplicationManager.
 *
 * Covers construct/destruct without initialize() (defensive teardown), the
 * post-initialize manager wiring/parenting graph, and shutdown() safety with
 * an empty CollectionConfig list. These run in the integration-test binary
 * because ApplicationManager pulls in the full top-level manager closure
 * (CacheManager, SessionManager, ArtworkManager, DatabaseManager,
 * PlaylistManager, SettingsManager, ScrollManager, SidebarManager,
 * NavigationManager, InteractionManager).
 */

#ifndef KARTEND_TESTS_TEST_APPLICATIONMANAGER_LIFECYCLE_H
#define KARTEND_TESTS_TEST_APPLICATIONMANAGER_LIFECYCLE_H

#include <QObject>

class TestApplicationManagerLifecycle : public QObject {
  Q_OBJECT

private slots:
  void testBareConstructDestructIsSafe();
  void testGettersReturnNullBeforeInitialize();
  void testInitializeWiresAllManagers();
  void testManagersAreParentedToApplicationManager();
  void testShutdownAfterInitializeIsSafe();
  void testDestructAfterInitializeWithoutShutdownIsSafe();
};

#endif // KARTEND_TESTS_TEST_APPLICATIONMANAGER_LIFECYCLE_H
