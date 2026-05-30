#ifndef KARTEND_UTILS_APP_COLLECTION_LAUNCHER_SETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_LAUNCHER_SETTINGS_H

// Leaf struct peeled out of GeneralSettings (Kartend-q1w6). The global
// LauncherPreset registry (stored in the top-level [Launchers] INI array)
// plus the optional RetroArch config-path override used by the core picker.

#include <QList>
#include <QString>

#include "launcherpreset.h"

struct LauncherSettings {
  // Globally-registered, reusable launcher configurations referenced by
  // collection-level launcher entries via LauncherConfig::presetId.
  QList<LauncherPreset> launcherPresets;
  // Optional override pointing at a RetroArch install (retroarch.cfg file or
  // core directory) so the core picker can list installed libretro cores.
  // Empty means probe the standard per-OS retroarch.cfg locations.
  QString retroarchConfigPath;
};

#endif // KARTEND_UTILS_APP_COLLECTION_LAUNCHER_SETTINGS_H
