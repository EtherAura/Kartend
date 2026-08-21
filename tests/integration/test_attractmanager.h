/**
 * @file test_attractmanager.h
 * @brief Integration tests for AttractManager's public surface.
 *
 * The activation/suspend state machine and the isEnabled / isDrivingSelection
 * accessors are exercised on a bare AttractManager. Timers are owned by the
 * manager and constructed in its ctor (single-shot idle, precise scroll,
 * bounce-pause, advance-selection) so they're safe to interrogate before
 * setupReferences has wired the scroll-area / settings graph.
 *
 * The fixture-backed test verifies InteractionManager owns and exposes the
 * manager after live wiring. A regression that left the unique_ptr null
 * after setup would surface here.
 *
 * The actual auto-scroll / advance-selection signal emission requires the
 * QScrollArea + GeneralSettings + scrolling event loop and is exercised
 * indirectly by the live UI; here we just cover the coordinator-local
 * defensive paths.
 */

#ifndef KARTEND_TESTS_TEST_ATTRACTMANAGER_H
#define KARTEND_TESTS_TEST_ATTRACTMANAGER_H

#include <QObject>

class TestAttractManager : public QObject {
  Q_OBJECT

private slots:
  void testConstructionInitialDefaults();
  void testIsEnabledFollowsSettings();
  void testReloadSettingsWithoutSettingsIsSafe();
  void testOnActivityDetectedIsNoOpWhenInactive();
  void testSetSuspendedToggleIsIdempotent();
  void testSetSuspendedDoesNotStartAttract();
  void testFixtureExposesAttractManagerViaInteractionManager();
  void testWheelScrollCountsAsActivity();
};

#endif // KARTEND_TESTS_TEST_ATTRACTMANAGER_H
