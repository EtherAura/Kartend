#include "collectionbackground_persistence.h"
#include "collection/collectionbackground.h"
#include "collection/helpers.h"

#include <QLoggingCategory>

#include "helpers.h"
#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace {
Q_LOGGING_CATEGORY(lcSettingsManager, "kartend.settingsmanager")
} // namespace

namespace CollectionBackgroundPersistence {

void load(QSettings &settings, CollectionBackground &bg, const QString &collectionName,
          const PathSanitizer &sanitize) {
  const QString bgType = settings.value(keys::kBackgroundType, "color").toString().toLower();
  if (bgType == "image") {
    bg.backgroundType = BackgroundType::Image;
  } else if (bgType == "video") {
    bg.backgroundType = BackgroundType::Video;
  } else {
    bg.backgroundType = BackgroundType::Color;
  }
  bg.backgroundColor = settings.value(keys::kBackgroundColor).toString();
  bg.backgroundImage = sanitize(settings.value(keys::kBackgroundImage).toString(),
                                QStringLiteral("backgroundImage"));
  bg.backgroundVideo = sanitize(settings.value(keys::kBackgroundVideo).toString(),
                                QStringLiteral("backgroundVideo"));
  bg.primaryColor = settings.value(keys::kPrimaryColor).toString();
  bg.tileColor = settings.value(keys::kTileColor).toString();
  bg.selectionColor = settings.value(keys::kSelectionColor).toString();
  bg.headerLogoImage = sanitize(settings.value(keys::kHeaderLogoImage).toString(),
                                QStringLiteral("headerLogoImage"));
  const QString rawHeaderLogoPosition =
      settings.value(keys::kHeaderLogoPosition, "topcenter").toString();
  bool headerLogoPositionFallback = false;
  bg.headerLogoPosition = CollectionUtils::stringToHeaderLogoPosition(rawHeaderLogoPosition,
                                                                      &headerLogoPositionFallback);
  if (headerLogoPositionFallback) {
    qCWarning(lcSettingsManager).nospace()
        << "Collection '" << collectionName << "': unknown " << keys::kHeaderLogoPosition
        << " value '" << rawHeaderLogoPosition
        << "' — falling back to 'topcenter'. Fix the INI to silence.";
  }
  bg.vignetteEnabled = settings.value(keys::kVignetteEnabled, false).toBool();
  bg.vignetteIntensity = settings.value(keys::kVignetteIntensity, 60).toInt();
  bg.wallpaperParallax = settings.value(keys::kWallpaperParallax, false).toBool();
  bg.parallaxStrength = settings.value(keys::kParallaxStrength, 30).toInt();
  bg.toolbarBackdropBlur = settings.value(keys::kToolbarBackdropBlur, false).toBool();
  bg.backdropBlurRadius = settings.value(keys::kBackdropBlurRadius, 12).toInt();
}

void save(QSettings &settings, const CollectionBackground &bg, const PathSanitizer &sanitize) {
  QString bgTypeStr = QStringLiteral("color");
  if (bg.backgroundType == BackgroundType::Image) {
    bgTypeStr = QStringLiteral("image");
  } else if (bg.backgroundType == BackgroundType::Video) {
    bgTypeStr = QStringLiteral("video");
  }
  settings.setValue(keys::kBackgroundType, bgTypeStr);
  settings.setValue(keys::kBackgroundColor, bg.backgroundColor);
  settings.setValue(keys::kBackgroundImage,
                    sanitize(bg.backgroundImage, QStringLiteral("backgroundImage")));
  settings.setValue(keys::kBackgroundVideo,
                    sanitize(bg.backgroundVideo, QStringLiteral("backgroundVideo")));
  settings.setValue(keys::kPrimaryColor, bg.primaryColor);
  settings.setValue(keys::kTileColor, bg.tileColor);
  settings.setValue(keys::kSelectionColor, bg.selectionColor);
  settings.setValue(keys::kHeaderLogoImage,
                    sanitize(bg.headerLogoImage, QStringLiteral("headerLogoImage")));
  settings.setValue(keys::kHeaderLogoPosition,
                    CollectionUtils::headerLogoPositionToString(bg.headerLogoPosition));
  settings.setValue(keys::kVignetteEnabled, bg.vignetteEnabled);
  settings.setValue(keys::kVignetteIntensity, bg.vignetteIntensity);
  settings.setValue(keys::kWallpaperParallax, bg.wallpaperParallax);
  settings.setValue(keys::kParallaxStrength, bg.parallaxStrength);
  settings.setValue(keys::kToolbarBackdropBlur, bg.toolbarBackdropBlur);
  settings.setValue(keys::kBackdropBlurRadius, bg.backdropBlurRadius);
}

} // namespace CollectionBackgroundPersistence
