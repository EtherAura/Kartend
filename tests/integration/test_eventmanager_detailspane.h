#ifndef TEST_EVENTMANAGER_DETAILSPANE_H
#define TEST_EVENTMANAGER_DETAILSPANE_H

#include <QObject>

// Integration tests for EventManager + DetailsPaneManager surface coverage
// Both managers were previously only verified to be non-null
// in test_mainwindow_smoke; these add focused behavioral checks.
class TestEventManagerDetailsPane : public QObject {
  Q_OBJECT
private slots:
  // EventManager wiring: lives on InteractionManager and survives MainWindow
  // construction in offscreen mode.
  void eventManager_isWiredOnInteractionManager();

  // DetailsPaneManager: toggleSidebar flips visibility + emits the change
  // signal; consecutive toggles round-trip the state.
  void detailsPaneManager_toggleSidebar_flipsVisibilityAndEmits();
  void detailsPaneManager_toggleSidebar_doubleToggleRestoresInitialState();

  // Kartend-6i10t user report: after a subcollection tile takes the
  // selection, an Item→Collection→Item tab round trip resurrected the
  // PREVIOUSLY selected item (the tab-switch re-push read a stale
  // m_currentItemFilePath). showSubcollectionSummary must drop the item
  // context so the overview survives the round trip.
  void detailsPaneManager_subcollectionSelection_survivesTabRoundTrip();

  // Kartend-6i10t user report: with an item selected in an aggregated view,
  // the Collection tab must show the item's OWNING (parent) collection, not
  // the viewed shell.
  void detailsPaneManager_collectionTab_showsSelectedItemsOwner();

  // DetailsPaneManager: setExternallyHidden suppresses visibility while
  // active and restores it when cleared, without mutating the persisted
  // m_sidebarVisible flag (verified by clearing the override and observing
  // the visibility flip back).
  void detailsPaneManager_externallyHidden_overridesVisibilityWhileSet();
  void detailsPaneManager_externallyHidden_redundantSetIsNoOp();

  // DetailsPaneManager: a deliberate toggle while externallyHidden is true
  // clears the override and ends with the sidebar visible (—
  // user intent outranks cover-flow's auto-hide).
  void detailsPaneManager_toggleClearsExternallyHidden();

  // EventManager: a wheel tick whose target lives in a SEPARATE top-level window
  // (a dialog such as the DAT Manager) must not be claimed as a main-window grid
  // scroll — it has to reach the dialog's own widgets.
  void eventManager_wheelOverForeignWindow_isNotClaimedByGridScroll();
};

#endif // TEST_EVENTMANAGER_DETAILSPANE_H
