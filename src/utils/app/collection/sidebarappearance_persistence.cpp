#include "sidebarappearance_persistence.h"
#include "collection/helpers.h"
#include "collection/sidebarappearance.h"

#include <QLoggingCategory>

#include "helpers.h"
#include "settingskeys.h"
#include "uiconstants/detailspaneconstants.h"

namespace keys = kartend::settings::keys;

// Mirrors the "kartend.settingsmanager" category declared in
// modules/data/settings/settingsmanager.h. utils-layer can't include that
// header (layering); re-declaring with the same string routes through the
// same Qt logging filter, so QT_LOGGING_RULES="kartend.settingsmanager=true"
// still toggles both sites uniformly.
namespace {
Q_LOGGING_CATEGORY(lcSettingsManager, "kartend.settingsmanager")
}

namespace SidebarAppearancePersistence {

void load(QSettings &settings, SidebarAppearance &sidebar, const QString &collectionName,
          const PathSanitizer &sanitize) {
  sidebar.sidebarVisible = settings.value(keys::kSidebarVisible, false).toBool();
  sidebar.sidebarMode = (settings.value(keys::kSidebarMode, "overlay").toString() == "fixed")
                            ? DetailsPaneMode::Expand
                            : DetailsPaneMode::Overlay;
  const QString rawSidebarPosition = settings.value(keys::kSidebarPosition, "right").toString();
  bool sidebarPositionFallback = false;
  sidebar.sidebarPosition =
      CollectionUtils::stringToDetailsPanePosition(rawSidebarPosition, &sidebarPositionFallback);
  if (sidebarPositionFallback) {
    qCWarning(lcSettingsManager).nospace()
        << "Collection '" << collectionName << "': unknown " << keys::kSidebarPosition << " value '"
        << rawSidebarPosition << "' — falling back to 'right'. Fix the INI to silence.";
  }
  const QString rawSidebarBackgroundType =
      settings.value(keys::kSidebarBackgroundType, "color").toString();
  bool sidebarBackgroundTypeFallback = false;
  sidebar.sidebarBackgroundType = CollectionUtils::stringToDetailsPaneBackgroundType(
      rawSidebarBackgroundType, &sidebarBackgroundTypeFallback);
  if (sidebarBackgroundTypeFallback) {
    qCWarning(lcSettingsManager).nospace()
        << "Collection '" << collectionName << "': unknown " << keys::kSidebarBackgroundType
        << " value '" << rawSidebarBackgroundType
        << "' — falling back to 'color'. Fix the INI to silence.";
  }
  sidebar.sidebarBackgroundColor = settings.value(keys::kSidebarBackgroundColor).toString();
  sidebar.sidebarBackgroundImage =
      sanitize(settings.value(keys::kSidebarBackgroundImage).toString(),
               QStringLiteral("sidebarBackgroundImage"));
  sidebar.sidebarPattern = CollectionUtils::stringToDetailsPanePattern(
      settings.value(keys::kSidebarPattern, "crosshatch").toString());
  sidebar.sidebarPatternIntensity = settings.value(keys::kSidebarPatternIntensity, 50).toInt();
  sidebar.sidebarPatternColor = settings.value(keys::kSidebarPatternColor).toString();
  sidebar.sidebarTextColor = settings.value(keys::kSidebarTextColor).toString();
  sidebar.sidebarAccentColor = settings.value(keys::kSidebarAccentColor).toString();
  sidebar.sidebarHeaderBgColor = settings.value(keys::kSidebarHeaderBgColor).toString();
  sidebar.sidebarSectionBgColor = settings.value(keys::kSidebarSectionBgColor).toString();
  sidebar.sidebarHeaderBgOpacity = settings.value(keys::kSidebarHeaderBgOpacity, 200).toInt();
  sidebar.sidebarSectionBgOpacity = settings.value(keys::kSidebarSectionBgOpacity, 170).toInt();
  sidebar.sidebarWidth =
      settings.value(keys::kSidebarWidth, UIConstants::DetailsPane::FIXED_WIDTH).toInt();
  sidebar.sidebarHeight =
      settings.value(keys::kSidebarHeight, UIConstants::DetailsPane::FIXED_HEIGHT).toInt();
  sidebar.sidebarWidthLocked = settings.value(keys::kSidebarWidthLocked, true).toBool();
  const QString rawSidebarActiveTab = settings.value(keys::kSidebarActiveTab, "item").toString();
  bool sidebarActiveTabFallback = false;
  sidebar.sidebarActiveTab =
      CollectionUtils::stringToDetailsPaneTab(rawSidebarActiveTab, &sidebarActiveTabFallback);
  if (sidebarActiveTabFallback) {
    qCWarning(lcSettingsManager).nospace()
        << "Collection '" << collectionName << "': unknown " << keys::kSidebarActiveTab
        << " value '" << rawSidebarActiveTab
        << "' — falling back to 'item'. Fix the INI to silence.";
  }
  sidebar.sidebarFontFamily = settings.value(keys::kSidebarFontFamily).toString();
  sidebar.sidebarFontPointSize = settings.value(keys::kSidebarFontPointSize, 0).toInt();
}

void save(QSettings &settings, const SidebarAppearance &sidebar, const PathSanitizer &sanitize) {
  settings.setValue(keys::kSidebarVisible, sidebar.sidebarVisible);
  settings.setValue(keys::kSidebarMode,
                    (sidebar.sidebarMode == DetailsPaneMode::Expand) ? "fixed" : "overlay");
  settings.setValue(keys::kSidebarPosition,
                    CollectionUtils::detailsPanePositionToString(sidebar.sidebarPosition));
  settings.setValue(
      keys::kSidebarBackgroundType,
      CollectionUtils::detailsPaneBackgroundTypeToString(sidebar.sidebarBackgroundType));
  settings.setValue(keys::kSidebarBackgroundColor, sidebar.sidebarBackgroundColor);
  settings.setValue(
      keys::kSidebarBackgroundImage,
      sanitize(sidebar.sidebarBackgroundImage, QStringLiteral("sidebarBackgroundImage")));
  settings.setValue(keys::kSidebarPattern,
                    CollectionUtils::detailsPanePatternToString(sidebar.sidebarPattern));
  settings.setValue(keys::kSidebarPatternIntensity, sidebar.sidebarPatternIntensity);
  settings.setValue(keys::kSidebarPatternColor, sidebar.sidebarPatternColor);
  settings.setValue(keys::kSidebarTextColor, sidebar.sidebarTextColor);
  settings.setValue(keys::kSidebarAccentColor, sidebar.sidebarAccentColor);
  settings.setValue(keys::kSidebarHeaderBgColor, sidebar.sidebarHeaderBgColor);
  settings.setValue(keys::kSidebarSectionBgColor, sidebar.sidebarSectionBgColor);
  settings.setValue(keys::kSidebarHeaderBgOpacity, sidebar.sidebarHeaderBgOpacity);
  settings.setValue(keys::kSidebarSectionBgOpacity, sidebar.sidebarSectionBgOpacity);
  settings.setValue(keys::kSidebarWidth, sidebar.sidebarWidth);
  settings.setValue(keys::kSidebarHeight, sidebar.sidebarHeight);
  settings.setValue(keys::kSidebarWidthLocked, sidebar.sidebarWidthLocked);
  settings.setValue(keys::kSidebarActiveTab,
                    CollectionUtils::detailsPaneTabToString(sidebar.sidebarActiveTab));
  settings.setValue(keys::kSidebarFontFamily, sidebar.sidebarFontFamily);
  settings.setValue(keys::kSidebarFontPointSize, sidebar.sidebarFontPointSize);
}

} // namespace SidebarAppearancePersistence
