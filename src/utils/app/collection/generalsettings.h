#ifndef KARTEND_UTILS_APP_COLLECTION_GENERALSETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_GENERALSETTINGS_H

// Application-wide settings composed from per-domain leaf structs (Kartend-q1w6,
// peeled out of the former 456-LOC god-struct). Each member is a small,
// independently-includable struct with its own *_settings.h header and
// *_settings_persistence.{h,cpp} I/O pair; SettingsManager delegates load/save
// to those namespaces. Field names live unchanged on the sub-structs so the
// QSettings keys round-trip; access moved from flat (settings.fooField) to
// nested (settings.section.fooField).
//
// Lives in its own translation-unit-input so settings dialog panels + the
// toolbar / scroll pipeline / attract mode / scraper service can take
// `const GeneralSettings &` without dragging in CollectionContext +
// CollectionHierarchyCache + the rest of the umbrella.

#include <QHash>

#include "appearance_settings.h"
#include "attract_settings.h"
#include "gamepad_settings.h"
#include "history_settings.h"
#include "input_settings.h"
#include "keybinding_settings.h"
#include "launcher_settings.h"
#include "marquee_settings.h"
#include "media_settings.h"
#include "runtime_detection_settings.h"
#include "scraper_settings.h"
#include "splash_settings.h"
#include "startup_settings.h"
#include "toolbar_settings.h"
#include "view_settings.h"

struct GeneralSettings {
  InputSettings input;                       // navigation + input timing
  KeybindingSettings keybindings;            // single-key keyboard bindings
  GamepadSettings gamepad;                   // symbolic gamepad bindings
  ScraperSettings scraper;                   // credentials + scrape options
  AttractSettings attract;                   // attract mode / autoscroll
  MarqueeSettings marquee;                   // secondary-monitor topper
  SplashSettings splash;                     // boot / resume splash screens
  RuntimeDetectionSettings runtimeDetection; // now-playing process tracking
  ToolbarSettings toolbar;                   // items-page top-bar customisation
  ViewSettings view;                         // sort, columns, chrome toggles
  AppearanceSettings appearance;             // title tint, UI font, text zoom
  StartupSettings startup;                   // startup collection / home view / intro
  MediaSettings media;                       // pixmap cache + preview tuning
  HistorySettings history;                   // launch-history toggle + cap
  LauncherSettings launchers;                // global launcher presets + RA path

  // Per-collection last-selected-item map. Stays a flat member: it isn't a
  // user-facing setting (no INI key) and is resolved/cleared at runtime.
  QHash<int, int> lastSelectedItems;

  GeneralSettings() = default;
};

#endif // KARTEND_UTILS_APP_COLLECTION_GENERALSETTINGS_H
