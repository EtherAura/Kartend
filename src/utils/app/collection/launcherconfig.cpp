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

QStringList launcherPathIssues(const LauncherProfile &profile) {
  QStringList issues;
  auto recordIssue = [&issues](const QString &fieldLabel, const QString &path) {
    if (path.isEmpty()) return;
    const PathUtils::PathStatus status = PathUtils::checkLauncherPath(path);
    if (status == PathUtils::PathStatus::OK || status == PathUtils::PathStatus::Empty) {
      return;
    }
    issues.append(QStringLiteral("%1 (%2): %3")
                      .arg(fieldLabel, path, PathUtils::pathStatusDescription(status)));
  };

  recordIssue(QCoreApplication::translate("LauncherUtils", "Launcher"), profile.launcherPath);
  for (int i = 0; i < profile.additionalLaunchers.size(); ++i) {
    const auto &al = profile.additionalLaunchers[i];
    const QString label =
        al.name.trimmed().isEmpty()
            ? QCoreApplication::translate("LauncherUtils", "Additional launcher %1").arg(i + 1)
            : QCoreApplication::translate("LauncherUtils", "Additional launcher %1 (%2)")
                  .arg(i + 1)
                  .arg(al.name);
    recordIssue(label, al.launcherPath);
  }
  return issues;
}

} // namespace LauncherUtils
