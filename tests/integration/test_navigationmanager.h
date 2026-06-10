/**
 * @file test_navigationmanager.h
 * @brief: integration tests for NavigationManager's
 *        signal-driven coordination surface.
 *
 * Covers the slots that cannot be exercised in pure unit tests because
 * NavigationManager directly includes ui_mainwindow.h and references the
 * full MainWindow widget graph: onCollectionSelected, onSubcollectionEntered,
 * and the DatabaseManager error forwarding via onMediaLibraryError. The
 * helper logic (NavigationStackManager, navigationhelpers) is already
 * covered by tests/test_navigationstackmanager and tests/test_navigationhelpers.
 */

#ifndef KARTEND_TESTS_TEST_NAVIGATIONMANAGER_H
#define KARTEND_TESTS_TEST_NAVIGATIONMANAGER_H

#include <QObject>

class TestNavigationManager : public QObject {
  Q_OBJECT

private slots:
  void testOnCollectionSelectedClearsNavigationStack();
  void testOnSubcollectionEnteredIgnoresOutOfRangeIndex();
  void testOnSubcollectionEnteredUnwindsPushOnNavigationFailure();
  void testOnSubcollectionEnteredSkipsPushWhenNoCurrentCollection();
  void testOnMediaLibraryErrorRendersErrorWidget();
};

#endif // KARTEND_TESTS_TEST_NAVIGATIONMANAGER_H
