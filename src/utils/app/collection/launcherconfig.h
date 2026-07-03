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

#include <QFileInfo>
#include <QHash>
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

// Fingerprint hash for the settings hot-reload diff baseline (Kartend-lc58a) —
// must hash exactly the fields operator== compares (see gridlayoutpreferences.h).
// Declared before LauncherProfile's qHash so the latter can fold the
// additionalLaunchers list in via qHashRange.
inline size_t qHash(const LauncherConfig &key, size_t seed = 0) {
  return qHashMulti(seed, key.name, key.launcherPath, key.corePath, key.launchParameters,
                    key.presetId);
}

namespace LauncherUtils {

/// returns true when @p launcherPath points at a libretro
/// frontend (currently RetroArch). The substring check is the single source
/// of truth — callers elsewhere ask "does this launcher take a libretro
/// core?" instead of inspecting the path themselves, so the rest of the
/// codebase stays free of specific emulator names. The match runs against
/// the entire path so "/usr/bin/retroarch", "retroarch.exe", and bare
/// "retroarch" in $PATH all qualify.
[[nodiscard]] inline bool usesLibretroCore(const QString &launcherPath) {
  // Kartend-r7att: match the executable BASENAME, not the whole path, so a
  // launcher merely living under a directory whose name contains "retroarch"
  // (e.g. /opt/retroarch-tools/wrapper, .../retroarch_videos/player) isn't
  // misclassified as libretro. Basename-contains (rather than startsWith) is
  // deliberate — it still matches plain "retroarch", "retroarch.exe", and the
  // flatpak "org.libretro.RetroArch" app-id form.
  return QFileInfo(launcherPath)
      .fileName()
      .contains(QStringLiteral("retroarch"), Qt::CaseInsensitive);
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

// Fingerprint hash for the settings hot-reload diff baseline (Kartend-lc58a) —
// must hash exactly the fields operator== compares (see gridlayoutpreferences.h).
// additionalLaunchers is folded in order-sensitively via qHashRange, which calls
// the LauncherConfig qHash declared above (there is no qHash overload for QList).
inline size_t qHash(const LauncherProfile &key, size_t seed = 0) {
  seed = qHashMulti(seed, key.launcherPath, key.corePath, key.launchParameters, key.launcherName,
                    key.defaultLauncherIndex);
  return qHashRange(key.additionalLaunchers.begin(), key.additionalLaunchers.end(), seed);
}

namespace LauncherUtils {

/// Expands the %collection% placeholder (case-insensitive) in a stored
/// launcher/core path and trims the result — the exact expansion
/// LaunchManager::buildLaunchCommand applies before executing. Shared so
/// pre-launch validation judges the same path the launch itself will run.
[[nodiscard]] inline QString expandCollectionPlaceholder(const QString &text,
                                                         const QString &collectionName) {
  QString out = text;
  out.replace(QStringLiteral("%collection%"), collectionName, Qt::CaseInsensitive);
  return out.trimmed();
}

/// Walk the profile's primary + additionalLaunchers slots and return a
/// user-facing string per slot whose launcherPath fails PathUtils::
/// checkLauncherPath. Empty paths skip silently (they're "unconfigured", not
/// "broken"). Each issue carries a localized field label + the raw path + a
/// localized status description, matching the sidebar's `⚠ Launcher path`
/// rows so the items-toolbar warning badge can reuse the strings verbatim.
[[nodiscard]] QStringList launcherPathIssues(const LauncherProfile &profile);

/// Launch-time overload: judges the paths a launch would actually execute.
/// Each slot is first resolved through resolvePreset — so a preset-backed
/// entry is checked against the preset's stored path instead of being
/// skipped as "unconfigured" — and %collection% is then expanded exactly
/// like buildLaunchCommand does, so a path routed through the placeholder
/// isn't misread as relative/unfindable. A slot whose presetId no longer
/// matches any preset AND whose inline fallback path is empty is flagged as
/// unresolvable naming the missing preset (it used to skip silently and
/// only fail later at buildLaunchCommand); a dangling presetId with a
/// usable inline path is judged on that fallback, since that is what would
/// launch. Labels and formatting match the raw overload above; the
/// reported path is the expanded one (what runs).
[[nodiscard]] QStringList launcherPathIssues(const LauncherProfile &profile,
                                             const QList<LauncherPreset> &presets,
                                             const QString &collectionName);

} // namespace LauncherUtils

#endif
