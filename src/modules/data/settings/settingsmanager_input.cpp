// Input-section settings I/O. Split from settingsmanager.cpp along leaf-struct
// boundaries; loadGeneralSettings/saveGeneralSettings (in settingsmanager.cpp)
// orchestrate these helpers. The load helper runs while the caller holds the
// [General] group open.
#include "settingsmanager.h"

#include <QSettings>

#include "collection/generalsettings.h"
#include "collection/input_settings_persistence.h"
#include "settingshelpers.h"
#include "uiconstants/scroll.h"

void SettingsManager::loadInputSection(QSettings &s, GeneralSettings &settings) {
  InputSettingsPersistence::load(s, settings.input);
  // coerceArtworkCycleModifier lives in the data layer (out of reach from the
  // utils-layer persistence TU): coerce the raw value back to a single allowed
  // modifier so a hand-edit can't disable the artwork-cycle gesture entirely.
  settings.input.artworkCycleModifier =
      SettingsHelpers::coerceArtworkCycleModifier(settings.input.artworkCycleModifier);
}

void SettingsManager::saveInputSection(QSettings &s, const GeneralSettings &settings) {
  m_generalSettings.input = settings.input;
  m_generalSettings.input.scrollVelocityMultiplier =
      qBound(UIConstants::Scroll::MIN_VELOCITY_MULTIPLIER,
             m_generalSettings.input.scrollVelocityMultiplier,
             UIConstants::Scroll::MAX_VELOCITY_MULTIPLIER);
  InputSettingsPersistence::save(s, m_generalSettings.input);
}
