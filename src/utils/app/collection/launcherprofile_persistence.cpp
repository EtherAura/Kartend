#include "launcherprofile_persistence.h"

#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace LauncherProfilePersistence {

void load(QSettings &settings, LauncherProfile &profile, bool &needsRewrite,
          const QString &collectionName, const PathSanitizer &sanitize) {
  profile.launcherPath =
      sanitize(settings.value(keys::kLauncherPath).toString(), QStringLiteral("launcherPath"));
  profile.corePath =
      sanitize(settings.value(keys::kCorePath).toString(), QStringLiteral("corePath"));
  profile.launchParameters = settings.value(keys::kLaunchParameters).toString();
  profile.launcherName = settings.value(keys::kLauncherName).toString();
  // additional launchers are stored as a QSettings array under
  // "additionalLaunchers". When the key is absent (legacy configs), the
  // collection just has the primary launcher and the array stays empty.
  const int additionalCount = settings.beginReadArray(keys::kAdditionalLaunchers);
  profile.additionalLaunchers.clear();
  profile.additionalLaunchers.reserve(additionalCount);
  for (int i = 0; i < additionalCount; ++i) {
    settings.setArrayIndex(i);
    LauncherConfig launcher;
    launcher.name = settings.value(keys::kName).toString();
    const QString launcherFieldId = QStringLiteral("additionalLaunchers[%1].launcherPath").arg(i);
    const QString coreFieldId = QStringLiteral("additionalLaunchers[%1].corePath").arg(i);
    launcher.launcherPath =
        sanitize(settings.value(keys::kLauncherPath).toString(), launcherFieldId);
    launcher.corePath = sanitize(settings.value(keys::kCorePath).toString(), coreFieldId);
    launcher.launchParameters = settings.value(keys::kLaunchParameters).toString();
    launcher.presetId = settings.value(keys::kPresetId).toString();
    // Drop entries where both launcherPath and presetId are empty: they
    // can't launch anything, but used to pass validation and pollute the
    // launcher chooser with a non-functional row. Rewrite so the cleanup
    // sticks on disk instead of resurrecting on next load (Kartend-gjeu).
    if (launcher.launcherPath.isEmpty() && launcher.presetId.isEmpty()) {
      needsRewrite = true;
      continue;
    }
    profile.additionalLaunchers.append(launcher);
  }
  settings.endArray();
  // Clamp at the load boundary so the stored value is always in range and no
  // downstream consumer has to defend against an out-of-range index from a
  // hand-edited INI (Kartend-aep2e). launcherCount() >= 1, so the upper bound
  // is never negative.
  const int rawDefaultIndex = settings.value(keys::kDefaultLauncherIndex, 0).toInt();
  profile.defaultLauncherIndex = qBound(0, rawDefaultIndex, profile.launcherCount() - 1);
  Q_UNUSED(collectionName);
}

void save(QSettings &settings, const LauncherProfile &profile, const PathSanitizer &sanitize) {
  settings.setValue(keys::kLauncherPath,
                    sanitize(profile.launcherPath, QStringLiteral("launcherPath")));
  settings.setValue(keys::kCorePath, sanitize(profile.corePath, QStringLiteral("corePath")));
  settings.setValue(keys::kLaunchParameters, profile.launchParameters);
  settings.setValue(keys::kLauncherName, profile.launcherName);
  // Persist the additional-launcher list as a QSettings array.
  // beginWriteArray clears any existing entries with the same prefix, so
  // launchers removed via the dialog don't linger in the INI.
  settings.beginWriteArray(keys::kAdditionalLaunchers, profile.additionalLaunchers.size());
  for (int i = 0; i < profile.additionalLaunchers.size(); ++i) {
    settings.setArrayIndex(i);
    const LauncherConfig &launcher = profile.additionalLaunchers[i];
    settings.setValue(keys::kName, launcher.name);
    const QString launcherFieldId = QStringLiteral("additionalLaunchers[%1].launcherPath").arg(i);
    const QString coreFieldId = QStringLiteral("additionalLaunchers[%1].corePath").arg(i);
    settings.setValue(keys::kLauncherPath, sanitize(launcher.launcherPath, launcherFieldId));
    settings.setValue(keys::kCorePath, sanitize(launcher.corePath, coreFieldId));
    settings.setValue(keys::kLaunchParameters, launcher.launchParameters);
    settings.setValue(keys::kPresetId, launcher.presetId);
  }
  settings.endArray();
  settings.setValue(keys::kDefaultLauncherIndex, profile.defaultLauncherIndex);
}

} // namespace LauncherProfilePersistence
