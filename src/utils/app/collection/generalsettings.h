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

  // Equality over the *settings* only. Every member below is a settings
  // sub-struct with its own defaulted operator==, so adding a field to any of
  // them — or a whole new sub-struct here — is folded into the comparison
  // automatically. This is the single source of truth the settings dialog's
  // dirty-check relies on, replacing the old ~210-line hand-enumerated field
  // compare that silently went stale whenever a new deferred field was added
  // (Kartend-6oqat, e.g. the retroarchConfigPath miss Kartend-hvdow).
  //
  // lastSelectedItems is deliberately EXCLUDED: it's runtime selection state
  // (no INI key, resolved/cleared at runtime), so folding it in would let a
  // selection change make the settings dialog look dirty.
  friend bool operator==(const GeneralSettings &a, const GeneralSettings &b) {
    return a.input == b.input && a.keybindings == b.keybindings && a.gamepad == b.gamepad &&
           a.scraper == b.scraper && a.attract == b.attract && a.marquee == b.marquee &&
           a.splash == b.splash && a.runtimeDetection == b.runtimeDetection &&
           a.toolbar == b.toolbar && a.view == b.view && a.appearance == b.appearance &&
           a.startup == b.startup && a.media == b.media && a.history == b.history &&
           a.launchers == b.launchers;
  }
  friend bool operator!=(const GeneralSettings &a, const GeneralSettings &b) { return !(a == b); }

  // Returns a copy with the free-text path / title / label fields trimmed. The
  // settings dialog persists and dirty-compares the *normalized* form so a
  // whitespace-only edit to one of these fields neither marks the dialog dirty
  // nor round-trips spurious surrounding spaces — preserving the long-standing
  // whitespace-insensitivity these specific fields had under the old per-field
  // check, now that the dirty-check is a whole-struct compare (Kartend-6oqat).
  [[nodiscard]] GeneralSettings normalizedForSave() const {
    GeneralSettings copy = *this;
    copy.launchers.retroarchConfigPath = copy.launchers.retroarchConfigPath.trimmed();
    copy.splash.bootSplashTitle = copy.splash.bootSplashTitle.trimmed();
    copy.splash.bootSplashSubtitle = copy.splash.bootSplashSubtitle.trimmed();
    copy.splash.resumeFocusSplashTitle = copy.splash.resumeFocusSplashTitle.trimmed();
    copy.splash.resumeFocusSplashSubtitle = copy.splash.resumeFocusSplashSubtitle.trimmed();
    copy.startup.startupVideoPath = copy.startup.startupVideoPath.trimmed();
    copy.startup.homeViewLabel = copy.startup.homeViewLabel.trimmed();
    copy.startup.homeViewIcon = copy.startup.homeViewIcon.trimmed();
    copy.scraper.options.quarantineDefaultDir = copy.scraper.options.quarantineDefaultDir.trimmed();
    return copy;
  }
};

#endif // KARTEND_UTILS_APP_COLLECTION_GENERALSETTINGS_H
