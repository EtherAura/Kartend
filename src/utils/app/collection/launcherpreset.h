#ifndef KARTEND_UTILS_APP_COLLECTION_LAUNCHERPRESET_H
#define KARTEND_UTILS_APP_COLLECTION_LAUNCHERPRESET_H

// Leaf struct extracted from collectionutils.h (Kartend-0yz3). Carrier for a
// globally-registered, reusable launcher configuration. Kept in its own
// translation-unit-input so panels that only need the preset type (e.g. the
// Launchers settings panel) don't drag in CollectionConfig + UIConstants.
// collectionutils.h re-includes this header so existing callers compile
// unchanged.

#include <QString>

/// a globally-registered, reusable launcher configuration. A
/// LauncherConfig that carries a non-empty `presetId` matching a preset's
/// `id` inherits its name + path + core + parameters from the preset at
/// resolution time (LauncherUtils::resolvePreset). Renaming a preset is
/// safe because references key off the stable `id`, not the user-visible
/// name. Deleting a referenced preset leaves the inline fields as the
/// fallback — typically empty, so the launch surfaces a clear error.
struct LauncherPreset {
  QString id;
  QString name;
  QString launcherPath;
  QString corePath;
  QString launchParameters;

  bool operator==(const LauncherPreset &other) const {
    return id == other.id && name == other.name && launcherPath == other.launcherPath &&
           corePath == other.corePath && launchParameters == other.launchParameters;
  }
};

#endif
