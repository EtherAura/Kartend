#ifndef KARTEND_UTILS_APP_COLLECTION_APPEARANCE_SETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_APPEARANCE_SETTINGS_H

// Leaf struct peeled out of GeneralSettings (Kartend-q1w6). Global text
// appearance: title tint, the application-wide UI font, and the runtime
// text-zoom percent (bound to Ctrl+= / Ctrl+- / Ctrl+0).

#include <QString>

struct AppearanceSettings {
  // Kartend-bbcu6: item titles use the palette's text colour by default, like
  // every other label in the app. Tinting them is opt-in.
  //
  // The tint used to be unconditional, and its saturation/lightness below are
  // ABSOLUTE rather than relative to the theme — so the old default (180/60)
  // rendered near background luminance on a dark scheme: 1.63:1 against Breeze
  // Dark, failing WCAG AA (4.5:1) and even the 3:1 large-text floor. The
  // palette colour cannot fail that way, because the theme already guarantees
  // it against its own background.
  bool titleTintEnabled = false;
  int titleTintSaturation = 180; // Title text saturation (0-255), tint only
  int titleTintLightness = 60;   // Title text lightness (0-255), tint only
  QString titleBaseColor;        // Base color for title text (empty = use highlight)
  // Global UI font applied via QApplication::setFont() after load. Empty
  // family / 0 point size means "use the platform default" so an upgrading
  // install keeps Qt's chosen font until the user picks one.
  QString globalUiFontFamily;
  int globalUiFontPointSize = 0;
  // Application-wide multiplier on every font size, stored as percent
  // (100 = unscaled). Clamped at load/save so a hand-edited value can't
  // render text at 0pt or blow it past 3x.
  int uiTextZoomPercent = 100;
  // Defaulted memberwise equality — keeps GeneralSettings::operator== and the
  // settings dirty-check field-complete automatically (Kartend-6oqat).
  bool operator==(const AppearanceSettings &) const = default;
};

#endif // KARTEND_UTILS_APP_COLLECTION_APPEARANCE_SETTINGS_H
