#include "interactionhelpers.h"

#include <algorithm>

#include <QFileInfo>

namespace InteractionHelpers {

auto stepSizeForViewType(ViewType viewType, int gridWidth) -> int {
  if (viewType == ViewType::List || viewType == ViewType::CoverFlow) {
    return 1;
  }
  return gridWidth > 0 ? gridWidth : 1;
}

auto computeVerticalHoldStep(int currentIndex, int direction, int stepSize, int totalItems,
                             bool wrap) -> HoldScrollStep {
  HoldScrollStep result;
  if (totalItems <= 0 || stepSize <= 0) {
    return result;
  }

  const int safeCurrent = std::max(0, currentIndex);
  int next = safeCurrent + (direction * stepSize);

  if (wrap) {
    if (next < 0) {
      // Modulo for negatives: ((next % N) + N) % N maps any negative back into
      // [0, totalItems). This mirrors the production wrap rule used by
      // InteractionManager::onMouseHoldScrollStep before extraction.
      next = ((next % totalItems) + totalItems) % totalItems;
      result.didWrap = true;
    } else if (next >= totalItems) {
      next = next % totalItems;
      result.didWrap = true;
    }
  } else {
    next = std::max(next, 0);
    if (next >= totalItems) {
      next = totalItems - 1;
    }
  }

  result.nextIndex = next;
  return result;
}

auto resolveOwnerIndex(int dbIndex, int fallbackIndex, int collectionsSize) -> int {
  if (dbIndex >= 0 && dbIndex < collectionsSize) {
    return dbIndex;
  }
  if (fallbackIndex >= 0 && fallbackIndex < collectionsSize) {
    return fallbackIndex;
  }
  return -1;
}

auto classifyContextTarget(bool isSubcollection, bool isVirtualFolder, bool hasFilePath)
    -> ContextTargetFlags {
  ContextTargetFlags flags;
  flags.isMediaItem = !isSubcollection && !isVirtualFolder;
  flags.showLaunch = flags.isMediaItem && hasFilePath;
  flags.showOpen = isSubcollection || isVirtualFolder;
  return flags;
}

auto playlistContextFlags(bool insidePlaylist, bool isSmartPlaylist, bool isReservedPlaylist)
    -> PlaylistMenuFlags {
  PlaylistMenuFlags flags;
  if (!insidePlaylist) {
    return flags;
  }
  flags.showRemoveFromPlaylist = !isSmartPlaylist;
  flags.showEditSmartFilter = isSmartPlaylist;
  flags.showDeletePlaylist = !isReservedPlaylist;
  return flags;
}

auto pickLauncherIndex(int overrideIndex, int defaultLauncherIndex, int launcherCount) -> int {
  if (launcherCount <= 0) {
    return 0;
  }
  if (overrideIndex >= 0 && overrideIndex < launcherCount) {
    return overrideIndex;
  }
  return std::clamp(defaultLauncherIndex, 0, launcherCount - 1);
}

auto classifyExpandActivation(bool expandModeEnabled, int expandedItemIndex, int activationIndex,
                              bool overlayVisible) -> ExpandActivation {
  if (!expandModeEnabled) {
    return ExpandActivation::LaunchDirectly;
  }
  if (expandedItemIndex == activationIndex && activationIndex >= 0 && overlayVisible) {
    return ExpandActivation::CollapseThenLaunch;
  }
  return ExpandActivation::TryExpand;
}

auto displayTitleForFilePath(const QString &filePath) -> QString {
  if (filePath.isEmpty()) {
    return {};
  }
  return QFileInfo(filePath).completeBaseName().replace('_', ' ').simplified();
}

} // namespace InteractionHelpers
