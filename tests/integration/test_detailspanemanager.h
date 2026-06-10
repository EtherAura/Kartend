/**
 * @file test_detailspanemanager.h
 * @brief Integration tests for DetailsPaneManager's public coordinator surface.
 *
 * The state machine (m_sidebarVisible, m_externallyHidden, m_overlayActive,
 * m_currentCollectionIndex) and the defensive early-returns in
 * toggleSidebar / updateSidebarLayout / applySidebarStateForCollection /
 * refreshCollectionSummary can be exercised on a bare DetailsPaneManager
 * because every layout-touching method short-circuits when the
 * IDetailsPane / mainLayout references are null. The fixture-backed test
 * confirms the live wiring routes through ApplicationManager.
 *
 * Metadata refresh (updateSidebarMetadata / refreshSidebarMetadataImmediate)
 * needs the full DatabaseManager + ArtworkManager + ItemMetadata graph;
 * those paths are covered by test_eventmanager_detailspane.cpp and
 * test_detailspane_coverflow.cpp via the real fixture. Here we lock the
 * coordinator-local state contracts.
 */

#ifndef KARTEND_TESTS_TEST_DETAILSPANEMANAGER_H
#define KARTEND_TESTS_TEST_DETAILSPANEMANAGER_H

#include <QObject>

class TestDetailsPaneManager : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void testConstructionInitialDefaults();
  void testToggleSidebarIsNoOpWithoutDetailsPane();
  void testSetExternallyHiddenTogglesAndEmits();
  void testSetExternallyHiddenIdempotentDoesNotEmit();
  void testSetOverlayActiveDoesNotCrash();
  void testApplySidebarStateForCollectionInvalidIndexIsNoOp();
  void testRefreshCollectionSummaryEarlyReturnsWithoutDetailsPane();
  void testSidebarWidgetReturnsNullBeforeSetup();

  void testFixtureExposesDetailsPaneManagerViaApplicationManager();
};

#endif // KARTEND_TESTS_TEST_DETAILSPANEMANAGER_H
