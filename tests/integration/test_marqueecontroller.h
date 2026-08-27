/**
 * @file test_marqueecontroller.h
 * @brief Integration tests for MarqueeController (src/core/marqueecontroller.cpp).
 *
 * The controller borrows all state (settings / collection list / current
 * index) so it runs standalone against test-local structs — no MainWindow.
 * Covered: the marquee window lifecycle applyMarqueeSettings drives
 * (disabled = no window, enabled = one reused top-level MarqueeWindow,
 * disable tears it down) and the invalid-collection-index guard on the
 * artwork push. Video mode (Qt Multimedia) is deliberately untouched.
 */

#ifndef KARTEND_TESTS_TEST_MARQUEECONTROLLER_H
#define KARTEND_TESTS_TEST_MARQUEECONTROLLER_H

#include <QObject>

class TestMarqueeController : public QObject {
  Q_OBJECT

private slots:
  void applyMarqueeSettings_disabledCreatesNoWindow();
  void applyMarqueeSettings_enableReuseDisableLifecycle();
  void updateMarqueeArtwork_invalidCollectionIndexLeavesWindowAlone();
  void screenChanges_areWiredAtConstruction();
  void screenChange_repinsWithoutDuplicatingTheWindow();
  void screenChange_isInertWhileTheMarqueeIsDisabled();
};

#endif // KARTEND_TESTS_TEST_MARQUEECONTROLLER_H
