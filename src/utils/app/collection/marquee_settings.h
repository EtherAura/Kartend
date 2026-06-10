#ifndef KARTEND_UTILS_APP_COLLECTION_MARQUEE_SETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_MARQUEE_SETTINGS_H

// Leaf struct peeled out of GeneralSettings (Kartend-q1w6). Marquee /
// secondary-monitor display. Pins a frameless, always-on-top window to a
// marquee/topper screen mirroring the selected item's artwork (mode 0) or
// the current collection's icon (mode 1). When the named QScreen is absent,
// the runtime falls back to the primary screen and warns.

#include <QString>

struct MarqueeSettings {
  bool marqueeEnabled = false;
  QString marqueeScreenName; // QScreen::name() (e.g. "HDMI-A-1"); empty = primary screen
  int marqueeMode = 0;       // 0 = selected item's artwork; 1 = current collection's icon
  // Defaulted memberwise equality — keeps GeneralSettings::operator== and the
  // settings dirty-check field-complete automatically (Kartend-6oqat).
  bool operator==(const MarqueeSettings &) const = default;
};

#endif // KARTEND_UTILS_APP_COLLECTION_MARQUEE_SETTINGS_H
