#ifndef THREADPOOLUTILS_H
#define THREADPOOLUTILS_H

class QThreadPool;

namespace ThreadPoolUtils {

/**
 * @brief Bounded-wait teardown of a QThreadPool with abandon-on-timeout
 * fallback.
 *
 * Calls clear() to drop queued tasks, then waitForDone(drainBudgetMs) to give
 * any in-flight tasks a chance to notice cancellation cooperatively. On
 * success, deletes the pool. On timeout, intentionally leaks the pool so
 * shutdown is not blocked by a runaway task — the OS reaps memory at process
 * exit. Either way, `pool` is set to nullptr so accidental late access fails
 * fast (and shows up under sanitizers) rather than touching freed-or-leaked
 * memory.
 *
 * @param pool Reference-to-pointer; nulled on return regardless of outcome.
 * @param drainBudgetMs How long to wait for cooperative cancellation. 2000ms
 *                     is the project default.
 * @return true if the pool drained within budget and was deleted; false if
 *         the budget expired and the pool was intentionally leaked. Callers
 *         use the return value to log a context-specific warning.
 *
 * PRECONDITION: every task queued on `pool` must cooperatively check a
 * cancellation flag (typically a shared_ptr<std::atomic_bool> captured by
 * value) and return promptly when it is set. Tasks that capture the owning
 * object as a raw `this` pointer and outlive its destructor will use-after-
 * free on the abandon-on-timeout path. Callers are responsible for setting
 * the cancellation flag before invoking this helper.
 */
[[nodiscard]] bool shutdownWithBudget(QThreadPool *&pool, int drainBudgetMs);

} // namespace ThreadPoolUtils

#endif // THREADPOOLUTILS_H
