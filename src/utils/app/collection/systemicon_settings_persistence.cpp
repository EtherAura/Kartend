#include "systemicon_settings_persistence.h"

#include <algorithm>

#include <QLoggingCategory>

#include "collection/enumstringhelpers.h"
#include "settingskeys.h"

namespace keys = kartend::settings::keys;

// Mirrors the "kartend.settingsmanager" category declared in
// modules/data/settings/settingsmanager.h — utils-layer can't include that
// header (layering); re-declaring with the same string routes through the
// same Qt logging filter (same reasoning as collectiontreesettings_persistence).
namespace {
Q_LOGGING_CATEGORY(lcSettingsManager, "kartend.settingsmanager")
} // namespace

namespace SystemIconSettingsPersistence {

void load(QSettings &settings, SystemIconSettings &icon, const QString &collectionName) {
  icon.enabled = settings.value(keys::kSystemIconEnabled, SystemIconSettings{}.enabled).toBool();
  // Free-form: this is a libretro system name, and the set of them grows with
  // every RetroArch release. Validating it against the installed packs here
  // would drop a perfectly good name just because the user is between
  // installs, or on a second machine with a sparser pack. Resolution is where
  // a name that matches nothing turns into "no glyph", and iconPath is where
  // it is checked for being a safe path component.
  icon.systemName = settings.value(keys::kSystemIconName).toString().trimmed();
  bool subjectFallback = false;
  icon.subject = CollectionUtils::stringToSystemIconSubject(
      settings.value(keys::kSystemIconSubject, QStringLiteral("controller")).toString(),
      &subjectFallback);
  if (subjectFallback) {
    qCWarning(lcSettingsManager).nospace()
        << "Collection '" << collectionName << "': unknown " << keys::kSystemIconSubject
        << " value — falling back to 'controller'. Fix the INI to silence.";
  }
  // Empty means "pick a pack to suit the subject" — the normal case, not an
  // error, so no warning for an absent key.
  icon.packOverride = settings.value(keys::kSystemIconPack).toString().trimmed();
  bool placementFallback = false;
  icon.placement = CollectionUtils::stringToSystemIconPlacement(
      settings.value(keys::kSystemIconPlacement, QStringLiteral("before-name")).toString(),
      &placementFallback);
  if (placementFallback) {
    qCWarning(lcSettingsManager).nospace()
        << "Collection '" << collectionName << "': unknown " << keys::kSystemIconPlacement
        << " value — falling back to 'before-name'. Fix the INI to silence.";
  }
  bool styleFallback = false;
  icon.style = CollectionUtils::stringToTreeIconStyle(
      settings.value(keys::kSystemIconStyle, QStringLiteral("normal")).toString(), &styleFallback);
  if (styleFallback) {
    qCWarning(lcSettingsManager).nospace()
        << "Collection '" << collectionName << "': unknown " << keys::kSystemIconStyle
        << " value — falling back to 'normal'. Fix the INI to silence.";
  }
  icon.systemAutoDetected =
      settings.value(keys::kSystemIconAutoDetected, SystemIconSettings{}.systemAutoDetected)
          .toBool();
  icon.useCollectionArtwork =
      settings
          .value(keys::kSystemIconUseCollectionArtwork, SystemIconSettings{}.useCollectionArtwork)
          .toBool();
  // Clamp rather than warn: a slightly-off size (hand-tweaked INI, a config
  // carried from a different DPI) has an obvious best interpretation, unlike
  // the subject enum above where a typo means intent is unknown.
  const int rawSize = settings.value(keys::kSystemIconSize, SystemIconSettings{}.iconSize).toInt();
  icon.iconSize =
      std::clamp(rawSize, SystemIconSettings::kMinIconSize, SystemIconSettings::kMaxIconSize);
}

void save(QSettings &settings, const SystemIconSettings &icon) {
  settings.setValue(keys::kSystemIconEnabled, icon.enabled);
  settings.setValue(keys::kSystemIconName, icon.systemName);
  settings.setValue(keys::kSystemIconSubject,
                    CollectionUtils::systemIconSubjectToString(icon.subject));
  settings.setValue(keys::kSystemIconPack, icon.packOverride);
  settings.setValue(keys::kSystemIconPlacement,
                    CollectionUtils::systemIconPlacementToString(icon.placement));
  settings.setValue(keys::kSystemIconStyle, CollectionUtils::treeIconStyleToString(icon.style));
  settings.setValue(keys::kSystemIconAutoDetected, icon.systemAutoDetected);
  settings.setValue(keys::kSystemIconUseCollectionArtwork, icon.useCollectionArtwork);
  settings.setValue(keys::kSystemIconSize, icon.iconSize);
}

} // namespace SystemIconSettingsPersistence
