#include "input_settings_persistence.h"

#include "settingskeys.h"
#include "uiconstants/scroll.h"

namespace keys = kartend::settings::keys;

namespace InputSettingsPersistence {

void load(QSettings &settings, InputSettings &opts) {
  // Load default true, though the struct default is false: a fresh install
  // remembers selection until the user opts out.
  opts.rememberSelection = settings.value(keys::kRememberSelection, true).toBool();
  opts.wrapNavigation = settings.value(keys::kWrapNavigation, false).toBool();
  opts.selectItemOnHover = settings.value(keys::kSelectItemOnHover, false).toBool();
  opts.keyboardRepeatIntervalMs = settings.value(keys::kKeyboardRepeatIntervalMs, 260).toInt();
  opts.keyboardRepeatDelayMs = settings.value(keys::kKeyboardRepeatDelayMs, 260).toInt();
  opts.clickHoldDelayMs = settings.value(keys::kClickHoldDelayMs, 500).toInt();
  opts.clickHoldRepeatIntervalMs = settings.value(keys::kClickHoldRepeatIntervalMs, 320).toInt();
  opts.listKeyboardRepeatIntervalMs =
      settings.value(keys::kListKeyboardRepeatIntervalMs, 50).toInt();
  opts.listClickHoldRepeatIntervalMs =
      settings.value(keys::kListClickHoldRepeatIntervalMs, 80).toInt();
  opts.mouseWheelRows = settings.value(keys::kMouseWheelRows, 1).toInt();
  opts.scrollAnimationDurationMs = settings.value(keys::kScrollAnimationDurationMs, 1500).toInt();
  opts.scrollVelocityMultiplier =
      settings
          .value(keys::kScrollVelocityMultiplier, UIConstants::Scroll::DEFAULT_VELOCITY_MULTIPLIER)
          .toDouble();
  // Bounds keep the multiplier from stalling or saturating the animation
  // pipeline.
  opts.scrollVelocityMultiplier =
      qBound(UIConstants::Scroll::MIN_VELOCITY_MULTIPLIER, opts.scrollVelocityMultiplier,
             UIConstants::Scroll::MAX_VELOCITY_MULTIPLIER);
  // Read raw; the caller coerces unrecognized values back to Shift.
  opts.artworkCycleModifier =
      settings.value(keys::kArtworkCycleModifier, static_cast<int>(Qt::ShiftModifier)).toInt();
}

void save(QSettings &settings, const InputSettings &opts) {
  settings.setValue(keys::kRememberSelection, opts.rememberSelection);
  settings.setValue(keys::kWrapNavigation, opts.wrapNavigation);
  settings.setValue(keys::kSelectItemOnHover, opts.selectItemOnHover);
  settings.setValue(keys::kKeyboardRepeatIntervalMs, opts.keyboardRepeatIntervalMs);
  settings.setValue(keys::kKeyboardRepeatDelayMs, opts.keyboardRepeatDelayMs);
  settings.setValue(keys::kClickHoldDelayMs, opts.clickHoldDelayMs);
  settings.setValue(keys::kClickHoldRepeatIntervalMs, opts.clickHoldRepeatIntervalMs);
  settings.setValue(keys::kListKeyboardRepeatIntervalMs, opts.listKeyboardRepeatIntervalMs);
  settings.setValue(keys::kListClickHoldRepeatIntervalMs, opts.listClickHoldRepeatIntervalMs);
  settings.setValue(keys::kMouseWheelRows, opts.mouseWheelRows);
  settings.setValue(keys::kScrollAnimationDurationMs, opts.scrollAnimationDurationMs);
  settings.setValue(keys::kScrollVelocityMultiplier, opts.scrollVelocityMultiplier);
  settings.setValue(keys::kArtworkCycleModifier, opts.artworkCycleModifier);
}

} // namespace InputSettingsPersistence
