#ifndef KARTEND_UTILS_APP_COLLECTION_APPEARANCE_SETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_APPEARANCE_SETTINGS_H

// Leaf struct peeled out of GeneralSettings (Kartend-q1w6). Global text
// appearance: title tint, the application-wide UI font, and the runtime
// text-zoom percent (bound to Ctrl+= / Ctrl+- / Ctrl+0).

#include <QString>

struct AppearanceSettings {
  int titleTintSaturation = 180; // Title text saturation (0-255)
  int titleTintLightness = 60;   // Title text lightness (0-255)
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
};

#endif // KARTEND_UTILS_APP_COLLECTION_APPEARANCE_SETTINGS_H
