#ifndef KARTEND_UTILS_APP_COLLECTION_COLLECTIONBACKGROUND_H
#define KARTEND_UTILS_APP_COLLECTION_COLLECTIONBACKGROUND_H

// Leaf struct extracted from collectionutils.h (Kartend-0yz3 step 9).
// Carrier for the per-collection view-background / wallpaper cluster.
// Kept in its own translation-unit-input so the background-painting and
// wallpaper-parallax code paths can take a `const CollectionBackground &`
// without dragging in CollectionConfig + the rest of UIConstants.
// collectionutils.h re-includes this header so existing callers compile
// unchanged.

#include <QHash>
#include <QString>

#include "../collectiontypes.h"

/// Per-collection view background / wallpaper cluster. Extracted from
/// CollectionConfig (Kartend-r4x5 follow-up: view+background). Owns the
/// items-page background (color/image/video), the primary/tile/selection
/// palette, the header logo (image + position), the vignette overlay
/// (toggle + intensity), the wallpaper-parallax controls, and the toolbar
/// backdrop-blur knobs. Members keep their legacy names so existing INI +
/// kart-manifest keys round-trip unchanged; access as
/// `cfg.background.backgroundColor` etc.
struct CollectionBackground {
  BackgroundType backgroundType = BackgroundType::Color;
  QString backgroundColor; // Background color (hex like #1a1a2e)
  QString backgroundImage; // Background image path
  /// looping muted background video path. Active only when
  /// backgroundType == Video; empty disables the video and falls back to
  /// the system bg until the user picks a file. Sanitised on save like
  /// backgroundImage.
  QString backgroundVideo;
  QString primaryColor;   // Primary UI color for toolbar, menubar, search bar
  /// Which colour fills the TOOLBAR (user request 2026-08-18). Defaults to
  /// the desktop titlebar so the chrome matches the window decoration;
  /// `CollectionPrimary` restores the per-collection colour above.
  ToolbarColorSource toolbarColorSource = ToolbarColorSource::Titlebar;
  QString tileColor;      // Color for item tiles/placeholders (if blank, uses default)
  QString selectionColor; // Color for selection rectangle and glide overlay border

  /// optional header logo image painted at the top of the items
  /// viewport, distinct from `collectionIcon` (which renders on the
  /// collection-as-tile entry). Empty path disables the overlay. Sanitised
  /// like backgroundImage on save.
  QString headerLogoImage;
  HeaderLogoPosition headerLogoPosition = HeaderLogoPosition::TopCenter;

  /// edge-darkening vignette overlay on the items viewport.
  /// `vignetteIntensity` is the corner darkness percent (0 = no effect, 100
  /// = pitch black at the corners). 60 is a sensible "noticeable but
  /// subtle" default. Independent of background type — works equally well
  /// over color, image, and video bgs.
  bool vignetteEnabled = false;
  int vignetteIntensity = 60;

  /// per-collection wallpaper parallax. When enabled, the
  /// image background scrolls at `parallaxStrength` percent of the items
  /// scroll speed (0 = bg locked / no parallax movement; 100 = bg moves
  /// in lockstep with the content). Image bg only — video bgs paint via
  /// their own widget and are not affected by this toggle.
  bool wallpaperParallax = false;
  int parallaxStrength = 30;

  /// simulated backdrop blur on the items toolbar. When
  /// enabled with an image bg, the wallpaper is pre-blurred (cheap
  /// downscale-upscale) and painted as the toolbar background, mimicking
  /// macOS Vibrancy. `backdropBlurRadius` controls the blur intensity
  /// (higher = more blur). Video bgs are not blurred (per-frame Gaussian
  /// is too expensive without GPU shaders); the toggle is a no-op while a
  /// video bg is active.
  bool toolbarBackdropBlur = false;
  int backdropBlurRadius = 12;

  bool operator==(const CollectionBackground &other) const {
    return backgroundType == other.backgroundType && backgroundColor == other.backgroundColor &&
           backgroundImage == other.backgroundImage && backgroundVideo == other.backgroundVideo &&
           primaryColor == other.primaryColor && tileColor == other.tileColor &&
           selectionColor == other.selectionColor && headerLogoImage == other.headerLogoImage &&
           headerLogoPosition == other.headerLogoPosition &&
           vignetteEnabled == other.vignetteEnabled &&
           vignetteIntensity == other.vignetteIntensity &&
           wallpaperParallax == other.wallpaperParallax &&
           parallaxStrength == other.parallaxStrength &&
           toolbarBackdropBlur == other.toolbarBackdropBlur &&
           backdropBlurRadius == other.backdropBlurRadius;
  }
  bool operator!=(const CollectionBackground &other) const { return !(*this == other); }
};

// Fingerprint hash for the settings hot-reload diff baseline (Kartend-lc58a) —
// must hash exactly the fields operator== compares (see gridlayoutpreferences.h
// for the full rationale). Scoped enums are cast to int because qHash(Enum) only
// exists since Qt 6.5 and CI pins Qt 6.4.2.
inline size_t qHash(const CollectionBackground &key, size_t seed = 0) {
  return qHashMulti(
      seed, static_cast<int>(key.backgroundType), key.backgroundColor, key.backgroundImage,
      key.backgroundVideo, key.primaryColor, key.tileColor, key.selectionColor, key.headerLogoImage,
      static_cast<int>(key.headerLogoPosition), key.vignetteEnabled, key.vignetteIntensity,
      key.wallpaperParallax, key.parallaxStrength, key.toolbarBackdropBlur, key.backdropBlurRadius);
}

#endif
