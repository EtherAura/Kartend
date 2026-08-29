/**
 * @file test_toolbarcontroller.h
 * @brief Integration tests for ToolbarController (src/core/toolbarcontroller.cpp).
 *
 * The widget-only surfaces (layout picker, search-mode action, home button,
 * per-button visibility flags) run against standalone QToolButton/QLineEdit
 * instances with no MainWindow. The surfaces that read MainWindow's public
 * collection/settings state (collection-warning badge, filter popup) run
 * against a MockedMainWindowFixture whose m_collections /
 * m_generalSettings / currentCollectionIndex the tests mutate directly.
 */

#ifndef KARTEND_TESTS_TEST_TOOLBARCONTROLLER_H
#define KARTEND_TESTS_TEST_TOOLBARCONTROLLER_H

#include <QObject>

class TestToolbarController : public QObject {
  Q_OBJECT

private slots:
  void setupViewModeButton_syncChecksExactlyActiveEntry_data();
  void setupViewModeButton_syncChecksExactlyActiveEntry();
  void viewModePopup_aboutToShowSyncsFromActiveCollection();
  void viewModePopup_aboutToShowLeavesStateAloneWithoutACollection();
  void setupSearchModeAction_installsLeadingSearchAction();
  void refreshHomeButton_followsStartupSettings();
  void applyToolbarCustomization_togglesFilterAndSearchVisibility();

  void refreshCollectionWarningBadge_hiddenWhenLauncherPathsClean();
  void refreshCollectionWarningBadge_missingLauncherShowsIssueTooltip();
  void refreshCollectionWarningBadge_placeholderPathExpandsBeforeJudging();
  void refreshCollectionWarningBadge_brokenPresetBackedEntryShowsBadge();

  void refreshFilterToolbar_buildsTypeRadiosAndTitleEntries();
  void refreshFilterToolbar_staleTypeFilterFallsBackToAllTypes();
  void refreshFilterToolbar_listsSavedFiltersFromTheRegistry();
  void titleFilterToggle_burstCoalescesIntoOneDebouncedSave();
};

#endif // KARTEND_TESTS_TEST_TOOLBARCONTROLLER_H
