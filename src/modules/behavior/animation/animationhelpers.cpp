// Pure helper definitions extracted from animationmanager.cpp.
// These four AnimationManager static methods have no dependency on
// ScrollManager / ArtworkManager / InteractionStateHolder, so isolating
// them in their own TU lets unit tests link them without dragging in the
// full coordinator-manager graph.

#include "animationmanager.h"
#include "gridutils.h"
#include "uiconstants.h"

#include <cmath>
#include <QtGlobal>

int AnimationManager::computeVerticalCenterDuration(int distance, int itemHeight,
                                                    int verticalSpacing, bool repeatActive,
                                                    int durationMs) {
  int stepSpan = qMax(1, itemHeight + verticalSpacing);
  double rows = static_cast<double>(distance) / static_cast<double>(stepSpan);
  rows = std::max(rows, 1.0);

  int baseDuration = durationMs;
  double raw = rows * static_cast<double>(baseDuration);

  int duration = static_cast<int>(std::round(raw));
  duration = qBound(baseDuration, duration, baseDuration);
  return duration;
}

int AnimationManager::computeTargetYForIndex(int index, int gridWidth, int itemHeight,
                                             int verticalSpacing, int viewportHeight,
                                             int scrollbarMax, int totalHeight, int logicalHeight,
                                             int headerOffset) {
  int itemY = GridUtils::computeItemY(index, gridWidth, itemHeight, verticalSpacing,
                                      UIConstants::Grid::MARGINS);
  itemY += headerOffset;
  int logicalTargetY = itemY + (itemHeight / 2) - (viewportHeight / 2);

  int targetY = logicalTargetY;
  if (totalHeight > 0 && logicalHeight > totalHeight && viewportHeight > 0) {
    int widgetMax = totalHeight - viewportHeight;
    int logicalMax = logicalHeight - viewportHeight;
    if (logicalMax > 0 && widgetMax > 0) {
      logicalTargetY = qBound(0, logicalTargetY, logicalMax);
      targetY = static_cast<int>(static_cast<double>(logicalTargetY) * widgetMax / logicalMax);
    }
  }

  return qBound(0, targetY, scrollbarMax);
}

int AnimationManager::computeHorizontalTargetX(int itemX, int collectionItemWidth, int curX,
                                               int viewportWidth, int margins, int scrollMax) {
  int itemLeft = itemX;
  int itemRight = itemX + collectionItemWidth;
  int visibleLeft = curX;
  int visibleRight = curX + viewportWidth;

  int targetX = curX;

  if (itemLeft < visibleLeft + margins) {
    targetX = itemLeft - margins;
  } else if (itemRight > visibleRight - margins) {
    targetX = itemRight - viewportWidth + margins;
  }

  return qBound(0, targetX, scrollMax);
}

int AnimationManager::computeDesiredYForVisibility(int itemY, int itemHeight, int curY,
                                                   int viewportHeight, int margins,
                                                   bool &needVertical) {
  int itemTop = itemY;
  int itemBottom = itemY + itemHeight;
  int visibleTop = curY;
  int visibleBottom = curY + viewportHeight;

  needVertical = false;
  int desiredY = curY;

  if (itemTop < visibleTop + margins) {
    desiredY = itemTop - margins;
    needVertical = true;
  } else if (itemBottom > visibleBottom - margins) {
    desiredY = itemBottom - viewportHeight + margins;
    needVertical = true;
  }

  return desiredY;
}
