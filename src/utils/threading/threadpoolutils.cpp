#include "threadpoolutils.h"

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
  }
  // Either way: null the pointer so late access fails fast.
  pool = nullptr;
  return drained;
}

} // namespace ThreadPoolUtils
