/**
 * @file test_applicationmanager_lifecycle.h
 * @brief: lifecycle tests for ApplicationManager.
 *
 * Covers construct/destruct without initialize() (defensive teardown), the
 * post-initialize manager wiring/parenting graph, and shutdown() safety with
 * an empty CollectionConfig list. These run in the integration-test binary
 * because ApplicationManager pulls in the full top-level manager closure
 * (CacheManager, SessionManager, ArtworkManager, DatabaseManager,
 * PlaylistManager, SettingsManager, ScrollManager, DetailsPaneManager,
 * NavigationManager, InteractionManager).
 */

#ifndef KARTEND_TESTS_TEST_APPLICATIONMANAGER_LIFECYCLE_H
#define KARTEND_TESTS_TEST_APPLICATIONMANAGER_LIFECYCLE_H

#include <QObject>

class TestApplicationManagerLifecycle : public QObject {
  Q_OBJECT

private slots:
  // Drains the QtConcurrent global thread pool after every test method so
  // each test gets fresh worker threads. Without this, persistent pool
  // workers retain TSan access history across test boundaries — see
  // test_main.cpp's drainGlobalThreadPool() and commit 70a674a. The
  // top-level drain between qExec scopes covers cross-suite isolation;
  // this one covers cross-method isolation within this suite, which fires
  // initialize()+shutdown() repeatedly and reliably surfaces a phantom
  // memmove-vs-memmove race on QArrayData buffers without it.
  void cleanup();

  void testBareConstructDestructIsSafe();
  void testGettersReturnNullBeforeInitialize();
  void testInitializeWiresAllManagers();
  void testManagersHaveNullQObjectParent();
  void testShutdownAfterInitializeIsSafe();
  void testDestructAfterInitializeWithoutShutdownIsSafe();
};

#endif // KARTEND_TESTS_TEST_APPLICATIONMANAGER_LIFECYCLE_H
