#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QtTest/QtTest>

#include "timerutils.h"

// TimerUtils::BoundedWait (Kartend-87x93): the bounded wait behind the keychain
// calls. It must let the awaited operation finish in every state of the
// application, including the one where Qt refuses to run a nested event loop:
// after QCoreApplication::quit()/exit(), until the application's exec() has
// returned. Timers stand in for the operation; no GUI and no keychain needed.

namespace {

constexpr int kOperationMs = 20;  // how long the awaited operation takes
constexpr int kGenerousMs = 5000; // a deadline no passing test comes near
constexpr int kShortMs = 100;     // a deadline a test lets pass on purpose

struct Outcome {
  bool finished = false;
  qint64 elapsedMs = -1;
};

// Runs body inside QCoreApplication::exec() and lets exec() return afterwards.
// A test slot has no running event loop of its own (QTest calls it directly),
// so this is how a test puts the application into "quitting". exec() clears
// that state when it returns, so one test cannot leak it into the next.
template <typename Body> void insideApplicationExec(Body &&body) {
  // Zero delay: defer the body to the first iteration of the exec() below, so
  // that it runs inside the application's event loop.
  QTimer::singleShot(0, qApp, [&body]() {
    body();
    QCoreApplication::exit(0); // harmless when the body already asked to quit
  });
  QCoreApplication::exec();
}

} // namespace

class TestBoundedWait : public QObject {
  Q_OBJECT
private slots:
  void finishesBeforeTheDeadline_returnsTrue();
  void neverFinishes_returnsFalseAtTheDeadline();
  void finishedBeforeRun_returnsAtOnce();
  void afterQuit_theOperationStillFinishes();
  void afterQuit_theDeadlineStillHolds();
  void quitDuringTheWait_theOperationStillFinishes();
};

void TestBoundedWait::finishesBeforeTheDeadline_returnsTrue() {
  TimerUtils::BoundedWait wait;
  // The awaited operation: it completes after kOperationMs, as a keychain
  // reply would.
  QTimer::singleShot(kOperationMs, &wait, &TimerUtils::BoundedWait::finish);
  QElapsedTimer timer;
  timer.start();
  QVERIFY(wait.run(kGenerousMs));
  QVERIFY(wait.isFinished());
  QVERIFY2(timer.elapsed() < kGenerousMs, "the wait ends with the operation, not at the deadline");
}

void TestBoundedWait::neverFinishes_returnsFalseAtTheDeadline() {
  TimerUtils::BoundedWait wait;
  QElapsedTimer timer;
  timer.start();
  QVERIFY(!wait.run(kShortMs));
  QVERIFY(!wait.isFinished());
  // A coarse timer may fire a few percent early.
  QVERIFY2(timer.elapsed() >= kShortMs * 8 / 10, "the wait must last until the deadline");
}

void TestBoundedWait::finishedBeforeRun_returnsAtOnce() {
  TimerUtils::BoundedWait wait;
  wait.finish(); // an operation that completed synchronously
  QElapsedTimer timer;
  timer.start();
  QVERIFY(wait.run(kGenerousMs));
  QVERIFY2(timer.elapsed() < kGenerousMs, "nothing to wait for");
}

// The regression. This is the state of the code that continues after a modal
// dialog which a quit has just closed (the first-run wizard on SIGTERM or at
// logout): a wait built on QEventLoop::exec() alone returned false here after
// zero milliseconds, so every keychain write and delete "timed out".
void TestBoundedWait::afterQuit_theOperationStillFinishes() {
  Outcome outcome;
  insideApplicationExec([&outcome]() {
    QCoreApplication::exit(0); // from here on Qt refuses to run a new event loop
    TimerUtils::BoundedWait wait;
    // The awaited operation, started while the application is quitting.
    QTimer::singleShot(kOperationMs, &wait, &TimerUtils::BoundedWait::finish);
    QElapsedTimer timer;
    timer.start();
    outcome.finished = wait.run(kGenerousMs);
    outcome.elapsedMs = timer.elapsed();
  });
  QVERIFY2(outcome.finished, "a wait started while the application is quitting must still let "
                             "the operation finish");
  QVERIFY2(outcome.elapsedMs < kGenerousMs, "and it ends with the operation, not at the deadline");
}

void TestBoundedWait::afterQuit_theDeadlineStillHolds() {
  Outcome outcome;
  insideApplicationExec([&outcome]() {
    QCoreApplication::exit(0);
    TimerUtils::BoundedWait wait; // nothing ever finishes it
    QElapsedTimer timer;
    timer.start();
    outcome.finished = wait.run(kShortMs);
    outcome.elapsedMs = timer.elapsed();
  });
  QVERIFY(!outcome.finished);
  QVERIFY2(outcome.elapsedMs >= kShortMs * 8 / 10,
           "while quitting, an operation that never finishes is still given the whole deadline");
  QVERIFY2(outcome.elapsedMs < kGenerousMs, "and no longer than that");
}

// A quit that arrives while the wait is in progress (Ctrl-C during a keychain
// call) ends every event loop of the thread. The operation is still in
// flight, and the wait must see it through.
void TestBoundedWait::quitDuringTheWait_theOperationStillFinishes() {
  Outcome outcome;
  insideApplicationExec([&outcome]() {
    TimerUtils::BoundedWait wait;
    // The quit arrives first, kOperationMs into the wait...
    QTimer::singleShot(kOperationMs, qApp, []() { QCoreApplication::exit(0); });
    // ...and the operation completes after it.
    QTimer::singleShot(3 * kOperationMs, &wait, &TimerUtils::BoundedWait::finish);
    QElapsedTimer timer;
    timer.start();
    outcome.finished = wait.run(kGenerousMs);
    outcome.elapsedMs = timer.elapsed();
  });
  QVERIFY2(outcome.finished, "a quit during the wait must not abandon the operation");
  QVERIFY2(outcome.elapsedMs < kGenerousMs, "and it ends with the operation, not at the deadline");
}

QTEST_GUILESS_MAIN(TestBoundedWait)
#include "test_boundedwait.moc"
