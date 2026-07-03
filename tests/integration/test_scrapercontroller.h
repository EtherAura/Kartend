/**
 * @file test_scrapercontroller.h
 * @brief Integration tests for ScraperController (src/core/scrapercontroller.cpp).
 *
 * The controller's dependency closure pulls the full ScrapeResultDialog +
 * ScraperService graph, so it runs inside the integration binary. The
 * controller itself is constructed standalone against closure contexts over
 * test-local state (no MainWindow needed). Covered: the per-collection
 * provider-builder seam (ScraperControllerInternal::makeProviderBuilder —
 * the MetadataProviderRegistry::claimLookupProvider path), the lazily
 * constructed / reused scraper dialog (including the QPointer cache
 * self-healing after an external dialog destruction), and every branch of
 * the startup resume prompt (no state / Keep / Discard / Resume /
 * auto-resume).
 */

#ifndef KARTEND_TESTS_TEST_SCRAPERCONTROLLER_H
#define KARTEND_TESTS_TEST_SCRAPERCONTROLLER_H

#include <QObject>

class TestScraperController : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void providerBuilder_resolvesProviderPerCollectionCategory();
  void providerBuilder_overrideIdWinsAndNonLookupOverrideFallsBack();
  void providerBuilder_invalidIndexOrNullListReturnsNull();

  void openScraperDialog_withoutDatabaseWarnsAndCreatesNoDialog();
  void openScraperDialog_createsThenReusesOneDialog();
  void openScraperDialog_recreatesAfterExternalDestruction();

  void promptResume_noPendingStateShowsNothing();
  void promptResume_keepForLaterLeavesStateOnDisk();
  void promptResume_discardRemovesStateWithoutOpeningDialog();
  void promptResume_resumeConsumesStateAndOpensDialog();
  void promptResume_autoResumeSkipsPromptAndOpensDialog();
};

#endif // KARTEND_TESTS_TEST_SCRAPERCONTROLLER_H
