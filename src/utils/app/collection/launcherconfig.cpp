// Kartend-7uia: LauncherUtils::resolvePreset implementation moved here from
// collectionutils.cpp so the umbrella .cpp can retire. Sits next to its
// declaration in collection/launcherconfig.h.
#include "launcherconfig.h"
#include "launcherpreset.h"

#include <QList>

namespace LauncherUtils {

LauncherConfig resolvePreset(const LauncherConfig &lc, const QList<LauncherPreset> &presets) {
  if (lc.presetId.isEmpty()) {
    return lc;
  }
  for (const LauncherPreset &preset : presets) {
    if (preset.id == lc.presetId) {
      LauncherConfig resolved;
      resolved.name = preset.name;
      resolved.launcherPath = preset.launcherPath;
      resolved.corePath = preset.corePath;
      resolved.launchParameters = preset.launchParameters;
      resolved.presetId = lc.presetId;
      return resolved;
    }
  }
  return lc;
}

} // namespace LauncherUtils
