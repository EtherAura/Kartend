#include "threadpoolutils.h"

#include "errorutils.h"

#include <QThreadPool>

namespace ThreadPoolUtils {

bool shutdownWithBudget(QThreadPool *&pool, int drainBudgetMs) {
  if (!pool) {
    return true;
  }
  pool->clear();
  const bool drained = pool->waitForDone(drainBudgetMs);
  if (drained) {
    delete pool;
  } else {
    // Tasks still running after the budget — leak the pool rather than risk a
    // use-after-free when an in-flight task touches it. Surface the leak here so
    // it isn't silent and dependent on every caller logging (Kartend-7vrx).
    qCWarning(ErrorUtils::lcErrors())
        << "ThreadPoolUtils::shutdownWithBudget: pool did not drain within" << drainBudgetMs
        << "ms; leaking it with" << pool->activeThreadCount()
        << "active thread(s) to avoid a use-after-free";
  }
  // Either way: null the pointer so late access fails fast.
  pool = nullptr;
  return drained;
}

} // namespace ThreadPoolUtils
