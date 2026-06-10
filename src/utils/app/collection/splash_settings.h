#ifndef KARTEND_UTILS_APP_COLLECTION_SPLASH_SETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_SPLASH_SETTINGS_H

// Leaf struct peeled out of GeneralSettings (Kartend-q1w6). Splash-screen
// toggles + text. Empty title/subtitle strings mean "use the built-in
// default" (app display name for boot, localized "Welcome back" for resume).
// Custom values are used verbatim — no %1 substitution.

#include <QString>

struct SplashSettings {
  bool bootSplashEnabled = true;
  bool resumeFocusSplashEnabled = true;
  QString bootSplashTitle;
  QString bootSplashSubtitle;
  QString resumeFocusSplashTitle;
  QString resumeFocusSplashSubtitle;
  // Defaulted memberwise equality — keeps GeneralSettings::operator== and the
  // settings dirty-check field-complete automatically (Kartend-6oqat).
  bool operator==(const SplashSettings &) const = default;
};

#endif // KARTEND_UTILS_APP_COLLECTION_SPLASH_SETTINGS_H
