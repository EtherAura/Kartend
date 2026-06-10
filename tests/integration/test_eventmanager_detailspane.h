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
};

#endif // TEST_EVENTMANAGER_DETAILSPANE_H
