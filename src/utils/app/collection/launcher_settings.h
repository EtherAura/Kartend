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
  // Where launch-time archive extraction writes (Kartend-si0p5). Empty means
  // the default under QStandardPaths::CacheLocation.
  //
  // This deliberately does NOT default to TempLocation: on most Linux systems
  // /tmp is tmpfs, so extracting a DVD image there spends gigabytes of RAM.
  // A dual-layer DVD is 8.5 GB, which is a quarter of a 32 GiB tmpfs before
  // the emulator has loaded anything. Extraction is bounded by free space on
  // whichever volume this names, so pointing it at a large disk is the
  // supported way to launch big disc images.
  //
  // Extracted media does not accumulate (Kartend-2ygme, Kartend-ra8sf): an
  // extraction lives at most extractionRetentionHours past its last use, and
  // LaunchManager::sweepStaleExtractions enforces that. Only the two roots
  // Kartend creates under this directory are ever removed, never the directory
  // itself — it may well hold other things.
  QString extractionDirectory;
  // How long an extraction is kept after the program that used it exits
  // (Kartend-ra8sf). Extraction is expensive — a dual-layer DVD image is
  // several GB of disk work — so deleting on exit makes every relaunch pay it
  // again, while keeping forever is what filled the disk in the first place.
  //
  //   0  — delete as soon as the launched program exits
  //  >0  — keep for this many hours after last use, then sweep at startup
  //  <0  — never expire; the user reclaims the folder themselves
  //
  // The clock runs from LAST USE, not from extraction: relaunching a title
  // restarts its retention, so what you actually play stays warm and what you
  // tried once ages out.
  int extractionRetentionHours = 24;
  // Defaulted memberwise equality — keeps GeneralSettings::operator== and the
  // settings dirty-check field-complete automatically (Kartend-6oqat).
  bool operator==(const LauncherSettings &) const = default;
  // Trim the free-text path fields (Kartend audit S-07; folded by
  // GeneralSettings::normalizedForSave).
  void normalize() {
    retroarchConfigPath = retroarchConfigPath.trimmed();
    extractionDirectory = extractionDirectory.trimmed();
  }
};

#endif // KARTEND_UTILS_APP_COLLECTION_LAUNCHER_SETTINGS_H
