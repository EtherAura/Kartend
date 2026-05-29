#ifndef KARTEND_UTILS_APP_COLLECTION_LAUNCHERCONFIG_H
#define KARTEND_UTILS_APP_COLLECTION_LAUNCHERCONFIG_H

// Composite launcher cluster extracted from collectionutils.h
// (Kartend-0yz3 step 10 — first non-leaf step). Carries:
//   - LauncherConfig     — one launcher entry, optionally referencing a
//                          globally-registered LauncherPreset by id.
//   - LauncherProfile    — per-collection launcher container (primary
//                          launcher fields + additionalLaunchers list +
//                          chooser-default index + lookup helpers).
//   - LauncherUtils::usesLibretroCore / resolvePreset.
// Kept in its own translation-unit-input so settings / launch / chooser
// code paths can take the launcher cluster without dragging in
// CollectionConfig + UIConstants. resolvePreset's implementation lives in
// collection/launcherconfig.cpp (Kartend-7uia).

#include <QList>
#include <QString>
#include <QStringList>
#include <QtCore/Qt>

#include "launcherpreset.h"

/// one entry in a collection's launcher list. The legacy primary
/// launcher (CollectionConfig::launcher.launcherPath/corePath/launchParameters)
/// is always present as launcher index 0; entries in
/// CollectionConfig::launcher.additionalLaunchers occupy indices 1..N. `name`
/// is a user-visible label shown in the launcher chooser; empty falls back to
/// the executable basename.
///
/// when `presetId` matches a registered LauncherPreset id, the
/// preset's fields override the inline launcher fields at resolution time.
struct LauncherConfig {
  QString name;
  QString launcherPath;
  QString corePath;
  QString launchParameters;
  /// Optional reference to a globally-registered preset.
  /// When set and the preset exists, all other fields are inherited from
  /// the preset. Empty means "use the inline fields verbatim".
  QString presetId;

  bool operator==(const LauncherConfig &other) const {
    return name == other.name && launcherPath == other.launcherPath && corePath == other.corePath &&
           launchParameters == other.launchParameters && presetId == other.presetId;
  }
};

namespace LauncherUtils {

/// returns true when @p launcherPath points at a libretro
/// frontend (currently RetroArch). The substring check is the single source
/// of truth — callers elsewhere ask "does this launcher take a libretro
/// core?" instead of inspecting the path themselves, so the rest of the
/// codebase stays free of specific emulator names. The match runs against
/// the entire path so "/usr/bin/retroarch", "retroarch.exe", and bare
/// "retroarch" in $PATH all qualify.
[[nodiscard]] inline bool usesLibretroCore(const QString &launcherPath) {
  return launcherPath.contains(QStringLiteral("retroarch"), Qt::CaseInsensitive);
}

/// Returns a LauncherConfig with fields resolved against the preset list.
/// When `lc.presetId` matches a preset, that preset's name/path/core/params
/// replace the inline fields (the preset id stays attached for round-trip
/// persistence). When the preset is missing or `presetId` is empty, the
/// returned config equals `lc`.
[[nodiscard]] LauncherConfig resolvePreset(const LauncherConfig &lc,
                                           const QList<LauncherPreset> &presets);

} // namespace LauncherUtils

/// Per-collection launcher cluster. Owns the primary launcher's
/// path/core/parameters/name, the chooser dialog's pre-selected index, and
/// the extras list. Extracted from the CollectionConfig god-struct as the
/// second cluster split (Kartend-r4x5 follow-up: launcher). Members keep
/// their legacy names so existing kartend.cfg INI keys (`launcherPath`,
/// `corePath`, `launchParameters`, `launcherName`, `additionalLaunchers/*`,
/// `defaultLauncherIndex`) and kart-manifest JSON keys (`launcher_path`,
/// `core_path`, `launch_parameters`, `launcher_name`,
/// `additional_launchers`, `default_launcher_index`) round-trip unchanged.
struct LauncherProfile {
  QString launcherPath;
  QString corePath;
  QString launchParameters;
  /// User-visible name for the primary (legacy) launcher. Empty is fine — the
  /// chooser dialog falls back to the executable basename.
  QString launcherName;
  /// Extra launchers beyond the primary, indexed at 1..N in launcher views.
  QList<LauncherConfig> additionalLaunchers;
  /// Index into the unified launcher view (0 = primary, 1..N =
  /// additionalLaunchers[0..N-1]). The chooser dialog pre-selects this entry
  /// when launcherCount() > 1; for single-launcher collections it is
  /// effectively ignored.
  int defaultLauncherIndex = 0;

  // ─── Launcher list helpers ───────────────────────────────────
  /// Total number of launchers visible to the user: 1 primary + N additional.
  [[nodiscard]] int launcherCount() const { return 1 + additionalLaunchers.size(); }

  /// Returns the launcher at the unified index. Index 0 is the primary
  /// (synthesized from the legacy fields); index 1..N maps to
  /// additionalLaunchers[0..N-1]. Out-of-range indices return an empty config.
  [[nodiscard]] LauncherConfig launcherAt(int index) const {
    if (index == 0) {
      // Qualify with this-> so the compiler doesn't confuse the legacy fields
      // with the same-named members on LauncherConfig during aggregate init.
      // The trailing empty string is the presetId — the primary launcher
      // slot itself never references a preset.
      return LauncherConfig{this->launcherName, this->launcherPath, this->corePath,
                            this->launchParameters, QString{}};
    }
    const int additionalIndex = index - 1;
    if (additionalIndex < 0 || additionalIndex >= this->additionalLaunchers.size()) {
      return LauncherConfig{};
    }
    return this->additionalLaunchers[additionalIndex];
  }

  /// Friendly display name for a launcher entry. Falls back to the executable
  /// basename when the user-supplied name is empty, and to a numbered label
  /// when the launcher path itself is also empty.
  [[nodiscard]] QString launcherDisplayName(int index) const {
    LauncherConfig launcher = launcherAt(index);
    if (!launcher.name.trimmed().isEmpty()) {
      return launcher.name.trimmed();
    }
    if (!launcher.launcherPath.trimmed().isEmpty()) {
      QString basename = launcher.launcherPath.trimmed();
      int slash = basename.lastIndexOf(QLatin1Char('/'));
      if (slash >= 0) {
        basename = basename.mid(slash + 1);
      }
      return basename;
    }
    return QString::number(index + 1);
  }

  bool operator==(const LauncherProfile &other) const {
    return launcherPath == other.launcherPath && corePath == other.corePath &&
           launchParameters == other.launchParameters && launcherName == other.launcherName &&
           additionalLaunchers == other.additionalLaunchers &&
           defaultLauncherIndex == other.defaultLauncherIndex;
  }
  bool operator!=(const LauncherProfile &other) const { return !(*this == other); }
};

namespace LauncherUtils {

/// Walk the profile's primary + additionalLaunchers slots and return a
/// user-facing string per slot whose launcherPath fails PathUtils::
/// checkLauncherPath. Empty paths skip silently (they're "unconfigured", not
/// "broken"). Each issue carries a localized field label + the raw path + a
/// localized status description, matching the sidebar's `⚠ Launcher path`
/// rows so the items-toolbar warning badge can reuse the strings verbatim.
[[nodiscard]] QStringList launcherPathIssues(const LauncherProfile &profile);

} // namespace LauncherUtils

#endif
