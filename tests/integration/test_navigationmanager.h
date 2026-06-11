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

  // Root/Home view routing (Kartend-ood0m): loadRootView's synchronous state
  // transition and goBackToCollections' three dispatch branches (root-view
  // no-op, home-view escape, stack pop).
  void testLoadRootViewEntersRootState();
  void testLoadRootViewHonorsCustomHomeLabel();
  void testLoadRootViewWithNoCollectionsShowsEmptyState();
  void testGoBackIsNoOpInRootView();
  void testGoBackEscapesToHomeViewForRootCollection();
  void testGoBackPopsNavigationStack();

  // Virtual-folder navigation (Kartend-ood0m): the currentSubfolder state
  // machine driven by onVirtualFolderEntered / goBackFromVirtualFolder /
  // onBreadcrumbLinkClicked.
  void testVirtualFolderNavigationUpdatesSubfolderPath();
  void testVirtualFolderEnterIgnoredWithoutCurrentCollection();
  void testBreadcrumbLinksDriveSubfolderNavigation();
};

#endif // KARTEND_TESTS_TEST_NAVIGATIONMANAGER_H
