#include "attracthelpers.h"

#include <QtGlobal>

namespace AttractHelpers {

int linearAdvanceIndex(int current, int total) {
  if (total <= 0) {
    return -1;
  }
  if (current < 0) {
    return 0;
  }
  return (current + 1) % total;
}

AdvanceRange randomAdvanceRange(int current, int total, int direction) {
  AdvanceRange r;
  if (total <= 0) {
    return r;
  }
  if (direction >= 0) {
    r.rangeStart = current + 1;
    r.rangeEnd = total;
    if (r.rangeStart >= r.rangeEnd) {
      r.rangeStart = 0;
      r.rangeEnd = qMax(0, current);
    }
  } else {
    r.rangeStart = 0;
    r.rangeEnd = qMax(0, current);
    if (r.rangeStart >= r.rangeEnd) {
      r.rangeStart = current + 1;
      r.rangeEnd = total;
    }
  }
  if (r.rangeStart < 0) {
    r.rangeStart = 0;
  }
  if (r.rangeEnd > total) {
    r.rangeEnd = total;
  }
  return r;
}

ScrollStep accumulateScrollDelta(double accumulator, double speedPxPerTick) {
  ScrollStep step;
  const double summed = accumulator + speedPxPerTick;
  step.delta = static_cast<int>(summed);
  step.newAccumulator = summed - step.delta;
  return step;
}

NextScroll nextScrollPosition(int current, int delta, int direction, int minValue, int maxValue) {
  NextScroll result;
  result.next = current + (delta * direction);
  if (result.next >= maxValue) {
    result.next = maxValue;
    result.hitMax = true;
  } else if (result.next <= minValue) {
    result.next = minValue;
    result.hitMin = true;
  }
  return result;
}

} // namespace AttractHelpers
