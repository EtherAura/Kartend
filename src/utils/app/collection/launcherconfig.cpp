// Kartend-7uia: LauncherUtils::resolvePreset implementation moved here from
// collectionutils.cpp so the umbrella .cpp can retire. Sits next to its
// declaration in collection/launcherconfig.h.
#include "launcherconfig.h"
#include "launcherpreset.h"
#include "pathutils.h"

#include <QCoreApplication>
#include <QList>
#include <QStringList>

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

namespace {

// Shared per-slot formatting for both launcherPathIssues overloads: one
// user-facing "<label> (<path>): <status>" line per broken slot; empty paths
// skip silently ("unconfigured", not "broken").
void appendPathIssue(QStringList &issues, const QString &fieldLabel, const QString &path) {
  if (path.isEmpty()) return;
  const PathUtils::PathStatus status = PathUtils::checkLauncherPath(path);
  if (status == PathUtils::PathStatus::OK || status == PathUtils::PathStatus::Empty) {
    return;
  }
  issues.append(QStringLiteral("%1 (%2): %3")
                    .arg(fieldLabel, path, PathUtils::pathStatusDescription(status)));
}

// True when @p presets contains a preset with @p id. resolvePreset's return
// value cannot distinguish "matched" from "dangling id fell back to inline",
// so the dangling-preset check in the launch-time overload needs its own
// lookup.
bool presetExists(const QList<LauncherPreset> &presets, const QString &id) {
  for (const LauncherPreset &preset : presets) {
    if (preset.id == id) {
      return true;
    }
  }
  return false;
}

// Localized label for a unified launcher slot (0 = primary, 1..N =
// additional). Additional launchers are numbered from 1 — the chooser's
// user-visible index — with the entry's name in parens when present.
QString slotLabel(int unifiedIndex, const QString &name) {
  if (unifiedIndex == 0) {
    return QCoreApplication::translate("LauncherUtils", "Launcher");
  }
  if (name.trimmed().isEmpty()) {
    return QCoreApplication::translate("LauncherUtils", "Additional launcher %1").arg(unifiedIndex);
  }
  return QCoreApplication::translate("LauncherUtils", "Additional launcher %1 (%2)")
      .arg(unifiedIndex)
      .arg(name);
}

} // namespace

QStringList launcherPathIssues(const LauncherProfile &profile) {
  QStringList issues;
  for (int i = 0; i < profile.launcherCount(); ++i) {
    const LauncherConfig slot = profile.launcherAt(i);
    // The primary slot's label ignores the name (slotLabel(0) is "Launcher");
    // additional slots embed their user-supplied name like before.
    appendPathIssue(issues, slotLabel(i, slot.name), slot.launcherPath);
  }
  return issues;
}

QStringList launcherPathIssues(const LauncherProfile &profile, const QList<LauncherPreset> &presets,
                               const QString &collectionName) {
  QStringList issues;
  for (int i = 0; i < profile.launcherCount(); ++i) {
    // Judge what a launch of this slot would actually execute: preset fields
    // override inline ones (LaunchManager::launchItem resolves the same way),
    // then %collection% expands exactly like buildLaunchCommand.
    const LauncherConfig slot = profile.launcherAt(i);
    const LauncherConfig effective = resolvePreset(slot, presets);
    const QString effectivePath =
        expandCollectionPlaceholder(effective.launcherPath, collectionName);
    // A dangling presetId (its preset was deleted) resolves back to the
    // inline fields. A usable inline path still launches and is judged
    // below; an empty one used to skip silently as "unconfigured" and only
    // fail later at buildLaunchCommand with a generic "No launcher
    // configured" — flag it here, naming the missing preset, so the
    // pre-launch gate catches it.
    if (!slot.presetId.isEmpty() && effectivePath.isEmpty() &&
        !presetExists(presets, slot.presetId)) {
      issues.append(
          QCoreApplication::translate(
              "LauncherUtils", "%1: references a launcher preset that no longer exists (id \"%2\")")
              .arg(slotLabel(i, slot.name), slot.presetId));
      continue;
    }
    appendPathIssue(issues, slotLabel(i, effective.name), effectivePath);
  }
  return issues;
}

} // namespace LauncherUtils
