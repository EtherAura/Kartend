#ifndef KARTEND_TESTS_WORKERSIGNALSPY_H
#define KARTEND_TESTS_WORKERSIGNALSPY_H

// Thread-safe stand-in for QSignalSpy when the emitting QObject lives on a
// *different* thread than the test (main) thread.
//
// Qt 6.4's QSignalSpy connects with Qt::DirectConnection and has no internal
// locking, so its appendArgs() mutates the spy's QList on the emitter's
// (worker) thread while the test thread reads size() inside wait(). That is a
// genuine cross-thread data race that ThreadSanitizer correctly flags.
// (QSignalSpy only became thread-safe in a later Qt release.)
//
// WorkerSignalSpy instead connects the signal with Qt::QueuedConnection to a
// plain QObject member that lives on the *constructing* thread. The captured
// arguments are therefore appended to m_records by the constructing thread's
// event loop — never cross-thread — so every access below (count(), size(),
// at(), takeFirst(), wait()) touches m_records only from the thread that
// created the spy. No locking is needed because there is no sharing.
//
// The class is intentionally Q_OBJECT-free: a plain QObject member plus a
// lambda slot give a queued connection without any moc wiring, so the header
// needs no entry in the AUTOMOC source lists. wait()'s event loop is woken
// directly by the capturing lambda (it quits m_activeLoop), so no extra
// signal is required either.
//
// API mirrors the subset of QSignalSpy the query tests use:
//   wait(ms)      - spin a local QEventLoop until one more emission arrives or
//                   the timeout elapses; returns true if an emission arrived.
//   count()/size()/isEmpty() - number of captured emissions.
//   at(i)         - the i-th emission's arguments as a QList<QVariant>.
//   takeFirst()   - pop and return the first emission's arguments.
//   isValid()     - true if the signal connection was established.

#include <QEventLoop>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QVariant>

#include <utility>

class WorkerSignalSpy {
public:
  // Connects `signal` (a pointer-to-member signal of `sender`) so that every
  // emission's arguments are captured on the thread that runs this
  // constructor. `sender` may live on any thread.
  template <typename Sender, typename... SignalArgs>
  WorkerSignalSpy(const Sender *sender, void (Sender::*signal)(SignalArgs...)) {
    m_connection = QObject::connect(
        sender, signal, &m_context,
        // The slot's parameter types match the signal exactly, so Qt's
        // queued-connection machinery can marshal each argument across the
        // thread boundary. std::decay_t strips the const-ref the signal
        // declares its parameters with so the lambda owns its own copies.
        [this](std::decay_t<SignalArgs>... args) {
          QList<QVariant> record;
          record.reserve(sizeof...(args));
          // Fold over the pack, appending each argument as a QVariant in
          // declaration order.
          (record.append(QVariant::fromValue(std::move(args))), ...);
          m_records.append(std::move(record));
          // Wake an in-progress wait() promptly instead of relying on the
          // timeout. Runs on the constructing thread (queued connection), the
          // same thread wait() blocks on, so touching m_activeLoop is safe.
          if (m_activeLoop) {
            m_activeLoop->quit();
          }
        },
        Qt::QueuedConnection);
  }

  ~WorkerSignalSpy() {
    if (m_connection) {
      QObject::disconnect(m_connection);
    }
  }

  WorkerSignalSpy(const WorkerSignalSpy &) = delete;
  WorkerSignalSpy &operator=(const WorkerSignalSpy &) = delete;

  [[nodiscard]] bool isValid() const { return static_cast<bool>(m_connection); }

  [[nodiscard]] int count() const { return static_cast<int>(m_records.size()); }
  [[nodiscard]] int size() const { return count(); }
  [[nodiscard]] bool isEmpty() const { return m_records.isEmpty(); }

  [[nodiscard]] const QList<QVariant> &at(int index) const { return m_records.at(index); }

  QList<QVariant> takeFirst() { return m_records.takeFirst(); }

  // Spins a local event loop (so queued emissions can be delivered to
  // m_context on this thread) until *one more* emission than is already
  // captured arrives, or `timeoutMs` elapses. Matches QSignalSpy::wait():
  // any emission still sitting unprocessed in the event queue is delivered
  // as the loop runs and counts. Returns true if a new emission was
  // captured.
  bool wait(int timeoutMs = 5000) {
    const int before = count();

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(timeoutMs);

    // The capturing lambda quits this loop on the next emission; the timer
    // quits it if none arrives in time.
    m_activeLoop = &loop;
    loop.exec();
    m_activeLoop = nullptr;

    return count() > before;
  }

private:
  // The connection context. Living on the constructing thread is what forces
  // the queued slot — and therefore every m_records mutation — onto that
  // thread. A bare QObject is enough; no Q_OBJECT / moc is involved.
  QObject m_context;

  // Captured emissions, each a QList<QVariant> of that emission's arguments.
  // Mutated only by the queued lambda above, read only by the accessors —
  // both on the constructing thread.
  QList<QList<QVariant>> m_records;

  // The QEventLoop currently inside wait(), if any. Set/cleared and read only
  // on the constructing thread.
  QEventLoop *m_activeLoop = nullptr;

  QMetaObject::Connection m_connection;
};

#endif // KARTEND_TESTS_WORKERSIGNALSPY_H
