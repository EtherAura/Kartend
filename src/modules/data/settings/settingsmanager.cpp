// Handles config file I/O, collection settings, and the settings dialog
// interface.
#include "settingsmanager.h"
#include "applicationcontext.h"
#include "collectionutils.h"
#include "configvalidation.h"
#include "errorutils.h"
#include "extensionutils.h"
#include "iartworkmanager.h"
#include "icachemanager.h"
#include "idetailspanemanager.h"
#include "imainwindow.h"
#include "inavigationmanager.h"
#include "isessionmanager.h"
#include "pathutils.h"
#include "scrapelogger.h"
#include "settingshelpers.h"
#include "settingsutils.h"
#include "timerutils.h"
#include "uiconstants.h"
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPointer>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTimer>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcSettingsManager, "kartend.settingsmanager")
#define debugLog(msg) qCDebug(lcSettingsManager) << msg

// Construct settings manager and initialize QSettings.
SettingsManager::SettingsManager(const ApplicationContext *ctx, QObject *parent)
    : ISettingsManager(parent), m_ctx(ctx) {
  QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  QDir configDir(configPath);
  if (!configDir.exists() && !configDir.mkpath(".")) {
    ErrorUtils::logError(ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::ConfigSaveFailed,
                                                           "Failed to create config directory",
                                                           "SettingsManager::SettingsManager")
                             .withDetails(QString("Path: %1").arg(configPath)));
  }
}

SettingsManager::~SettingsManager() = default;

// Loads general settings (selection indices now resolved from persistent cache
// separately)
void SettingsManager::loadGeneralSettings(GeneralSettings &settings) {
  QSettings s(SettingsUtils::getConfigPath(), SettingsUtils::getFormat());

  // Mirror saveCollections()/saveGeneralSettings() write-side path validation
  // on read so a hand-edited config can't sneak shell metacharacters or null
  // bytes into a path that's later passed to QProcess / QFile.
  auto sanitizeLoadedPath = [](const QString &value, const QString &fieldName) -> QString {
    if (value.isEmpty()) {
      return value;
    }
    auto security = PathUtils::validatePathSecurity(value);
    if (security.isError()) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::InvalidFilePath,
                                            QString("Refusing to load insecure %1").arg(fieldName),
                                            "SettingsManager::loadGeneralSettings")
              .withDetails(QString("Value: %1, Reason: %2").arg(value, security.error().message)));
      return QString();
    }
    return value;
  };

  s.beginGroup("General");
  settings.rememberSelection = s.value("rememberSelection", true).toBool();
  settings.wrapNavigation = s.value("wrapNavigation", false).toBool();
  settings.selectItemOnHover = s.value("selectItemOnHover", false).toBool();
  settings.pixmapCacheSizeMB = s.value("pixmapCacheSizeMB", 50).toInt();
  // Clamp to reasonable range: 10MB - 500MB
  settings.pixmapCacheSizeMB = qBound(10, settings.pixmapCacheSizeMB, 500);
  settings.videoThumbnailExtractionTimeoutMs =
      s.value("videoThumbnailExtractionTimeoutMs", 4000).toInt();
  // Clamp to keep slow-system tuning useful while preventing the queue from
  // stalling indefinitely on a misconfigured value.
  settings.videoThumbnailExtractionTimeoutMs =
      qBound(1000, settings.videoThumbnailExtractionTimeoutMs, 30000);
  // Load timing settings (direct ms/count values)
  settings.keyboardRepeatIntervalMs = s.value("keyboardRepeatIntervalMs", 260).toInt();
  settings.keyboardRepeatDelayMs = s.value("keyboardRepeatDelayMs", 260).toInt();
  settings.clickHoldDelayMs = s.value("clickHoldDelayMs", 500).toInt();
  settings.clickHoldRepeatIntervalMs = s.value("clickHoldRepeatIntervalMs", 320).toInt();
  settings.listKeyboardRepeatIntervalMs = s.value("listKeyboardRepeatIntervalMs", 50).toInt();
  settings.listClickHoldRepeatIntervalMs = s.value("listClickHoldRepeatIntervalMs", 80).toInt();
  settings.mouseWheelRows = s.value("mouseWheelRows", 1).toInt();
  settings.scrollAnimationDurationMs = s.value("scrollAnimationDurationMs", 1500).toInt();
  settings.scrollVelocityMultiplier = s.value("scrollVelocityMultiplier", 1.0).toDouble();
  // Clamp to a safe range: 0.25× - 5.0× so the multiplier can't stall or
  // saturate the animation pipeline.
  settings.scrollVelocityMultiplier = qBound(0.25, settings.scrollVelocityMultiplier, 5.0);
  // Load text appearance settings
  settings.titleTintSaturation = s.value("titleTintSaturation", 180).toInt();
  settings.titleTintLightness = s.value("titleTintLightness", 60).toInt();
  settings.titleBaseColor = s.value("titleBaseColor", QString()).toString();
  // opt-in title overlay on placeholder art
  settings.showTitleInPlaceholder = s.value("showTitleInPlaceholder", false).toBool();

  // global UI font. Empty family / 0 size = platform default.
  // No clamp on point size beyond Qt's own validation; the spinbox in the
  // settings dialog limits user input to a sane range.
  settings.globalUiFontFamily = s.value("globalUiFontFamily", QString()).toString();
  settings.globalUiFontPointSize = s.value("globalUiFontPointSize", 0).toInt();

  // runtime text zoom. Persisted as percent so a hand-edited
  // value reads obviously; clamped to [50, 300] to keep typography legible
  // and avoid absurdly tiny / huge widget sizes that the layout pipeline
  // wasn't designed for.
  settings.uiTextZoomPercent = qBound(50, s.value("uiTextZoomPercent", 100).toInt(), 300);

  // preview video volume (0-100). Clamped on read so a
  // hand-edited out-of-range value can't mute audio permanently or push
  // QAudioOutput into undefined territory.
  settings.previewVideoVolume = qBound(0, s.value("previewVideoVolume", 100).toInt(), 100);

  // startup video. Stored even when disabled so the user can
  // keep a path configured and toggle it off temporarily.
  settings.startupVideoEnabled = s.value("startupVideoEnabled", false).toBool();
  settings.startupVideoPath =
      sanitizeLoadedPath(s.value("startupVideoPath", QString()).toString(), "startupVideoPath");

  // Controls: keyboard bindings
  settings.keyNavLeft = s.value("keyNavLeft", static_cast<int>(Qt::Key_Left)).toInt();
  settings.keyNavRight = s.value("keyNavRight", static_cast<int>(Qt::Key_Right)).toInt();
  settings.keyNavUp = s.value("keyNavUp", static_cast<int>(Qt::Key_Up)).toInt();
  settings.keyNavDown = s.value("keyNavDown", static_cast<int>(Qt::Key_Down)).toInt();
  settings.keyConfirm = s.value("keyConfirm", static_cast<int>(Qt::Key_Return)).toInt();
  settings.keyBack = s.value("keyBack", static_cast<int>(Qt::Key_Escape)).toInt();
  settings.keySearch = s.value("keySearch", static_cast<int>(Qt::Key_Slash)).toInt();
  settings.keyAlphabeticBack =
      s.value("keyAlphabeticBack", static_cast<int>(Qt::Key_PageUp)).toInt();
  settings.keyAlphabeticForward =
      s.value("keyAlphabeticForward", static_cast<int>(Qt::Key_PageDown)).toInt();
  settings.keyJumpFirst = s.value("keyJumpFirst", static_cast<int>(Qt::Key_Home)).toInt();
  settings.keyJumpLast = s.value("keyJumpLast", static_cast<int>(Qt::Key_End)).toInt();
  // detail-page key (opens DetailPageOverlay).
  settings.keyItemDetails = s.value("keyItemDetails", static_cast<int>(Qt::Key_I)).toInt();
  settings.keyHomeView = s.value("keyHomeView", 0).toInt();

  // Controls: gamepad bindings
  settings.gamepadUseDpad = s.value("gamepadUseDpad", true).toBool();
  settings.gamepadUseLeftStick = s.value("gamepadUseLeftStick", true).toBool();
  settings.gamepadConfirmButton = s.value("gamepadConfirmButton", QString("A")).toString();
  settings.gamepadBackButton = s.value("gamepadBackButton", QString("B")).toString();
  settings.gamepadToggleSidebarButton =
      s.value("gamepadToggleSidebarButton", QString("Y")).toString();

  // artwork-cycle modifier. Coerce hand-edited junk back to Shift
  // so the gesture is always reachable; allow only the single-modifier flags
  // we expose in the settings UI.
  settings.artworkCycleModifier = SettingsHelpers::coerceArtworkCycleModifier(
      s.value("artworkCycleModifier", static_cast<int>(Qt::ShiftModifier)).toInt());

  // Sort preferences
  settings.sortMode = SettingsHelpers::coerceSortMode(
      s.value("sortMode", static_cast<int>(SortMode::NameAscending)).toInt());
  settings.excludeSubfoldersFromSort = s.value("excludeSubfoldersFromSort", false).toBool();
  // collection categorization filters. Defaults are "no filter"
  // so an upgrading user sees all subcollections as before until they pick a
  // type or toggle the hide button on the toolbar.
  settings.collectionTypeFilter = s.value("collectionTypeFilter", QString()).toString().trimmed();
  settings.hideSubcollectionTiles = s.value("hideSubcollectionTiles", false).toBool();
  settings.listCollectionColumnWidth = s.value("listCollectionColumnWidth", 150).toInt();
  settings.listArtworkColumnWidth = s.value("listArtworkColumnWidth", 32).toInt();
  settings.startupCollection = s.value("startupCollection", QString()).toString();
  settings.useHomeView = s.value("useHomeView", false).toBool();
  settings.homeViewLabel = s.value("homeViewLabel", QString()).toString();
  settings.homeViewIcon = s.value("homeViewIcon", QString()).toString();
  settings.retroarchConfigPath = s.value("retroarchConfigPath", QString()).toString();

  // Attract mode
  settings.attractModeEnabled = s.value("attractModeEnabled", false).toBool();
  settings.attractModeIdleTimeoutSec = qBound(
      UIConstants::Attract::MIN_IDLE_TIMEOUT_SEC,
      s.value("attractModeIdleTimeoutSec", UIConstants::Attract::DEFAULT_IDLE_TIMEOUT_SEC).toInt(),
      UIConstants::Attract::MAX_IDLE_TIMEOUT_SEC);
  settings.attractModeAutoScrollEnabled = s.value("attractModeAutoScrollEnabled", true).toBool();
  settings.attractModeScrollSpeed = qBound(
      UIConstants::Attract::MIN_SCROLL_SPEED_PX,
      s.value("attractModeScrollSpeed", UIConstants::Attract::DEFAULT_SCROLL_SPEED_PX).toDouble(),
      UIConstants::Attract::MAX_SCROLL_SPEED_PX);
  settings.attractModeAdvanceSelectionEnabled =
      s.value("attractModeAdvanceSelectionEnabled", false).toBool();
  settings.attractModeAdvanceSelectionIntervalSec =
      qBound(UIConstants::Attract::MIN_ADVANCE_INTERVAL_SEC,
             s.value("attractModeAdvanceSelectionIntervalSec",
                     UIConstants::Attract::DEFAULT_ADVANCE_INTERVAL_SEC)
                 .toInt(),
             UIConstants::Attract::MAX_ADVANCE_INTERVAL_SEC);
  settings.attractModeAdvanceSelectionRandom =
      s.value("attractModeAdvanceSelectionRandom", false).toBool();

  // Marquee / secondary monitor — opt-in. Mode is clamped to the known
  // range so a hand-edited INI with a bogus value doesn't propagate into
  // the runtime (defaults to 0 = item artwork).
  settings.marqueeEnabled = s.value("marqueeEnabled", false).toBool();
  settings.marqueeScreenName = s.value("marqueeScreenName").toString();
  settings.marqueeMode = qBound(0, s.value("marqueeMode", 0).toInt(), 2);

  // Splash screens
  settings.bootSplashEnabled = s.value("bootSplashEnabled", true).toBool();
  settings.resumeFocusSplashEnabled = s.value("resumeFocusSplashEnabled", true).toBool();
  settings.bootSplashTitle = s.value("bootSplashTitle").toString();
  settings.bootSplashSubtitle = s.value("bootSplashSubtitle").toString();
  settings.resumeFocusSplashTitle = s.value("resumeFocusSplashTitle").toString();
  settings.resumeFocusSplashSubtitle = s.value("resumeFocusSplashSubtitle").toString();

  // Runtime detection — opt-in
  settings.runtimeDetectionEnabled = s.value("runtimeDetectionEnabled", false).toBool();

  // First-run wizard gate — defaults false so a fresh install gets the
  // wizard on next launch.
  settings.firstRunComplete = s.value("firstRunComplete", false).toBool();

  // Launch history. Default is enabled with a 500-row cap so
  // a fresh install starts logging immediately; the user disables in
  // Settings → General. Negative caps land in the file via hand-edit only;
  // qBound clamps them to a sane window so trim never deletes the whole
  // table by accident.
  settings.historyEnabled = s.value("historyEnabled", true).toBool();
  settings.historyMaxEntries = qBound(10, s.value("historyMaxEntries", 500).toInt(), 50000);

  // View-mode toggles. Defaults match the.ui defaults so an
  // upgrading install sees no change until the user toggles F8/F10/F11.
  settings.showMenuBar = s.value("showMenuBar", true).toBool();
  settings.showToolbar = s.value("showToolbar", true).toBool();
  settings.fullscreen = s.value("fullscreen", false).toBool();

  // Customizable toolbar. Default visibility is "shown" so an
  // upgrading user sees the toolbar exactly as before; custom text strings
  // default to empty (use the .ui label).
  settings.toolbarShowGridViewButton = s.value("toolbarShowGridViewButton", true).toBool();
  settings.toolbarShowListViewButton = s.value("toolbarShowListViewButton", true).toBool();
  settings.toolbarShowCoverFlowViewButton =
      s.value("toolbarShowCoverFlowViewButton", true).toBool();
  settings.toolbarShowHorizontalViewButton =
      s.value("toolbarShowHorizontalViewButton", true).toBool();
  settings.toolbarShowHideSubcollectionsButton =
      s.value("toolbarShowHideSubcollectionsButton", true).toBool();
  settings.toolbarShowTypeFilter = s.value("toolbarShowTypeFilter", true).toBool();
  settings.toolbarShowTitleFilter = s.value("toolbarShowTitleFilter", true).toBool();
  settings.toolbarShowSearchModeButton = s.value("toolbarShowSearchModeButton", true).toBool();
  settings.toolbarShowSearchBar = s.value("toolbarShowSearchBar", true).toBool();
  settings.toolbarGridViewButtonText = s.value("toolbarGridViewButtonText", QString()).toString();
  settings.toolbarListViewButtonText = s.value("toolbarListViewButtonText", QString()).toString();
  settings.toolbarCoverFlowViewButtonText =
      s.value("toolbarCoverFlowViewButtonText", QString()).toString();
  settings.toolbarHorizontalViewButtonText =
      s.value("toolbarHorizontalViewButtonText", QString()).toString();
  settings.toolbarHideSubcollectionsButtonText =
      s.value("toolbarHideSubcollectionsButtonText", QString()).toString();
  settings.toolbarTitleFilterText = s.value("toolbarTitleFilterText", QString()).toString();
  s.endGroup();

  // launcher presets live at the top level (outside [General])
  // so they remain a clear, named section the user can hand-edit. Stored as
  // a QSettings array so size is implicit.
  settings.launcherPresets.clear();
  const int presetCount = s.beginReadArray("Launchers");
  settings.launcherPresets.reserve(presetCount);
  for (int i = 0; i < presetCount; ++i) {
    s.setArrayIndex(i);
    LauncherPreset preset;
    preset.id = s.value("id").toString();
    preset.name = s.value("name").toString();
    preset.launcherPath = sanitizeLoadedPath(s.value("launcherPath").toString(),
                                             QString("Launchers[%1].launcherPath").arg(i));
    preset.corePath = sanitizeLoadedPath(s.value("corePath").toString(),
                                         QString("Launchers[%1].corePath").arg(i));
    preset.launchParameters = s.value("launchParameters").toString();
    // Drop entries with no id — they can't be referenced and would shadow
    // valid presets if a hand-edit accidentally cleared the field.
    if (!preset.id.trimmed().isEmpty()) {
      settings.launcherPresets.append(preset);
    }
  }
  s.endArray();

  // Scraper credentials live in their own [Scrapers] group with
  // nested keys of the shape <provider>/<field>=<value> (QSettings'
  // built-in key hierarchy handles the slash). Provider implementations
  // read these via GeneralSettings::scraperCredentials; missing entries
  // mean "not configured" and the provider should surface a friendly
  // error rather than fall back to bundled credentials.
  settings.scraperCredentials.clear();
  s.beginGroup("Scrapers");
  for (const QString &fullKey : s.allKeys()) {
    const int slash = fullKey.indexOf('/');
    if (slash <= 0 || slash >= fullKey.size() - 1) {
      // Malformed key (no provider prefix or empty field name) — skip
      // rather than poison the credential map.
      continue;
    }
    const QString providerId = fullKey.left(slash);
    const QString fieldName = fullKey.mid(slash + 1);
    settings.scraperCredentials[providerId][fieldName] = s.value(fullKey).toString();
  }
  s.endGroup();

  // Scraper performance + behavior options. Live in a sibling
  // [ScraperOptions] group rather than under [Scrapers] so the
  // credential key-walk above doesn't pick them up as malformed
  // provider/field pairs.
  s.beginGroup("ScraperOptions");
  settings.scraperOptions.preset = static_cast<GeneralSettings::ScraperPreset>(
      s.value("preset", static_cast<int>(GeneralSettings::ScraperPreset::Balanced)).toInt());
  settings.scraperOptions.mediaMaxDimension =
      qBound(0, s.value("mediaMaxDimension", 1024).toInt(), 8192);
  settings.scraperOptions.mediaConcurrency = qBound(1, s.value("mediaConcurrency", 2).toInt(), 16);
  settings.scraperOptions.mediaThrottleMs =
      qBound(0, s.value("mediaThrottleMs", 100).toInt(), 5000);
  settings.scraperOptions.batchItemConcurrency =
      qBound(1, s.value("batchItemConcurrency", 4).toInt(), 16);
  settings.scraperOptions.rescrapeMode = static_cast<GeneralSettings::ScraperRescrapeMode>(
      s.value("rescrapeMode", static_cast<int>(GeneralSettings::ScraperRescrapeMode::FillMissing))
          .toInt());
  // Clamp 0..365 — defensive against hand-edited INIs. 0 disables the
  // time gate (skip every already-scraped item); 365 is a year, which
  // is the longest "refresh window" we expect anyone to want.
  settings.scraperOptions.skipRecentScrapeDays =
      qBound(0, s.value("skipRecentScrapeDays", 30).toInt(), 365);
  settings.scraperOptions.preferJpgOutput = s.value("preferJpgOutput", false).toBool();
  settings.scraperOptions.scrapeAutoResume = s.value("scrapeAutoResume", false).toBool();
  settings.scraperOptions.scrapeLogging = s.value("scrapeLogging", false).toBool();
  settings.scraperOptions.preferredScraperRegion =
      s.value("preferredRegion", QStringLiteral("us")).toString().trimmed().toLower();
  s.endGroup();

  settings.lastSelectedItems.clear();
  m_generalSettings = settings;
  // Bring scrape logging into effect for the running process. This is
  // the first settings read at startup, so the kartend.scrape*
  // categories are toggled before any scrape (or resume prompt) runs.
  ScrapeLogger::setEnabled(m_generalSettings.scraperOptions.scrapeLogging);
}

// Saves general settings (no legacy last_selected.dat persistence)
void SettingsManager::saveGeneralSettings(const GeneralSettings &settings) {
  m_generalSettings.rememberSelection = settings.rememberSelection;
  m_generalSettings.wrapNavigation = settings.wrapNavigation;
  m_generalSettings.selectItemOnHover = settings.selectItemOnHover;
  m_generalSettings.pixmapCacheSizeMB = qBound(10, settings.pixmapCacheSizeMB, 500);
  m_generalSettings.videoThumbnailExtractionTimeoutMs =
      qBound(1000, settings.videoThumbnailExtractionTimeoutMs, 30000);
  m_generalSettings.keyboardRepeatIntervalMs = settings.keyboardRepeatIntervalMs;
  m_generalSettings.keyboardRepeatDelayMs = settings.keyboardRepeatDelayMs;
  m_generalSettings.clickHoldDelayMs = settings.clickHoldDelayMs;
  m_generalSettings.clickHoldRepeatIntervalMs = settings.clickHoldRepeatIntervalMs;
  m_generalSettings.listKeyboardRepeatIntervalMs = settings.listKeyboardRepeatIntervalMs;
  m_generalSettings.listClickHoldRepeatIntervalMs = settings.listClickHoldRepeatIntervalMs;
  m_generalSettings.mouseWheelRows = settings.mouseWheelRows;
  m_generalSettings.scrollAnimationDurationMs = settings.scrollAnimationDurationMs;
  m_generalSettings.scrollVelocityMultiplier = qBound(0.25, settings.scrollVelocityMultiplier, 5.0);
  m_generalSettings.titleTintSaturation = settings.titleTintSaturation;
  m_generalSettings.titleTintLightness = settings.titleTintLightness;
  m_generalSettings.titleBaseColor = settings.titleBaseColor;

  m_generalSettings.showTitleInPlaceholder = settings.showTitleInPlaceholder;
  // global UI font
  m_generalSettings.globalUiFontFamily = settings.globalUiFontFamily.trimmed();
  m_generalSettings.globalUiFontPointSize = settings.globalUiFontPointSize;
  // runtime text zoom
  m_generalSettings.uiTextZoomPercent = qBound(50, settings.uiTextZoomPercent, 300);
  // preview video volume
  m_generalSettings.previewVideoVolume = qBound(0, settings.previewVideoVolume, 100);
  // startup video
  m_generalSettings.startupVideoEnabled = settings.startupVideoEnabled;
  m_generalSettings.startupVideoPath = settings.startupVideoPath.trimmed();

  // Controls
  m_generalSettings.keyNavLeft = settings.keyNavLeft;
  m_generalSettings.keyNavRight = settings.keyNavRight;
  m_generalSettings.keyNavUp = settings.keyNavUp;
  m_generalSettings.keyNavDown = settings.keyNavDown;
  m_generalSettings.keyConfirm = settings.keyConfirm;
  m_generalSettings.keyBack = settings.keyBack;
  m_generalSettings.keySearch = settings.keySearch;
  m_generalSettings.keyAlphabeticBack = settings.keyAlphabeticBack;
  m_generalSettings.keyAlphabeticForward = settings.keyAlphabeticForward;
  m_generalSettings.keyJumpFirst = settings.keyJumpFirst;
  m_generalSettings.keyJumpLast = settings.keyJumpLast;
  m_generalSettings.keyItemDetails = settings.keyItemDetails;
  m_generalSettings.keyHomeView = settings.keyHomeView;
  m_generalSettings.gamepadUseDpad = settings.gamepadUseDpad;
  m_generalSettings.gamepadUseLeftStick = settings.gamepadUseLeftStick;
  m_generalSettings.gamepadConfirmButton = settings.gamepadConfirmButton;
  m_generalSettings.gamepadBackButton = settings.gamepadBackButton;
  m_generalSettings.gamepadToggleSidebarButton = settings.gamepadToggleSidebarButton;
  m_generalSettings.artworkCycleModifier = settings.artworkCycleModifier;
  m_generalSettings.sortMode = settings.sortMode;
  m_generalSettings.excludeSubfoldersFromSort = settings.excludeSubfoldersFromSort;
  // collection categorization filters
  m_generalSettings.collectionTypeFilter = settings.collectionTypeFilter.trimmed();
  m_generalSettings.hideSubcollectionTiles = settings.hideSubcollectionTiles;
  m_generalSettings.listCollectionColumnWidth = settings.listCollectionColumnWidth;
  m_generalSettings.listArtworkColumnWidth = settings.listArtworkColumnWidth;
  m_generalSettings.startupCollection = settings.startupCollection;
  m_generalSettings.useHomeView = settings.useHomeView;
  m_generalSettings.homeViewLabel = settings.homeViewLabel.trimmed();
  m_generalSettings.homeViewIcon = settings.homeViewIcon.trimmed();
  m_generalSettings.retroarchConfigPath = settings.retroarchConfigPath.trimmed();

  // Attract mode
  m_generalSettings.attractModeEnabled = settings.attractModeEnabled;
  m_generalSettings.attractModeIdleTimeoutSec = settings.attractModeIdleTimeoutSec;
  m_generalSettings.attractModeAutoScrollEnabled = settings.attractModeAutoScrollEnabled;
  m_generalSettings.attractModeScrollSpeed = settings.attractModeScrollSpeed;
  m_generalSettings.attractModeAdvanceSelectionEnabled =
      settings.attractModeAdvanceSelectionEnabled;
  m_generalSettings.attractModeAdvanceSelectionIntervalSec =
      settings.attractModeAdvanceSelectionIntervalSec;
  m_generalSettings.attractModeAdvanceSelectionRandom = settings.attractModeAdvanceSelectionRandom;
  // Marquee secondary-monitor display
  m_generalSettings.marqueeEnabled = settings.marqueeEnabled;
  m_generalSettings.marqueeScreenName = settings.marqueeScreenName;
  m_generalSettings.marqueeMode = settings.marqueeMode;

  // Runtime detection
  m_generalSettings.runtimeDetectionEnabled = settings.runtimeDetectionEnabled;
  // First-run wizard
  m_generalSettings.firstRunComplete = settings.firstRunComplete;
  // Scraper credentials (per-provider key/value blob; written to
  // [Scrapers] in the INI by saveGeneralSettings below)
  m_generalSettings.scraperCredentials = settings.scraperCredentials;
  // Scraper performance + behavior options (clamped to defensive ranges
  // so an INI tampered with by hand can't crater the runtime).
  m_generalSettings.scraperOptions.preset = settings.scraperOptions.preset;
  m_generalSettings.scraperOptions.mediaMaxDimension =
      qBound(0, settings.scraperOptions.mediaMaxDimension, 8192);
  m_generalSettings.scraperOptions.mediaConcurrency =
      qBound(1, settings.scraperOptions.mediaConcurrency, 16);
  m_generalSettings.scraperOptions.mediaThrottleMs =
      qBound(0, settings.scraperOptions.mediaThrottleMs, 5000);
  m_generalSettings.scraperOptions.batchItemConcurrency =
      qBound(1, settings.scraperOptions.batchItemConcurrency, 16);
  m_generalSettings.scraperOptions.rescrapeMode = settings.scraperOptions.rescrapeMode;
  m_generalSettings.scraperOptions.skipRecentScrapeDays =
      qBound(0, settings.scraperOptions.skipRecentScrapeDays, 365);
  m_generalSettings.scraperOptions.preferJpgOutput = settings.scraperOptions.preferJpgOutput;
  m_generalSettings.scraperOptions.scrapeAutoResume = settings.scraperOptions.scrapeAutoResume;
  m_generalSettings.scraperOptions.scrapeLogging = settings.scraperOptions.scrapeLogging;
  m_generalSettings.scraperOptions.preferredScraperRegion =
      settings.scraperOptions.preferredScraperRegion;
  // Apply the (possibly changed) scrape-logging toggle immediately so a
  // settings-dialog change takes effect without a restart. Idempotent.
  ScrapeLogger::setEnabled(m_generalSettings.scraperOptions.scrapeLogging);
  // Launch history
  m_generalSettings.historyEnabled = settings.historyEnabled;
  m_generalSettings.historyMaxEntries = qBound(10, settings.historyMaxEntries, 50000);
  // View-mode toggles
  m_generalSettings.showMenuBar = settings.showMenuBar;
  m_generalSettings.showToolbar = settings.showToolbar;
  m_generalSettings.fullscreen = settings.fullscreen;
  // Customizable toolbar
  m_generalSettings.toolbarShowGridViewButton = settings.toolbarShowGridViewButton;
  m_generalSettings.toolbarShowListViewButton = settings.toolbarShowListViewButton;
  m_generalSettings.toolbarShowCoverFlowViewButton = settings.toolbarShowCoverFlowViewButton;
  m_generalSettings.toolbarShowHorizontalViewButton = settings.toolbarShowHorizontalViewButton;
  m_generalSettings.toolbarShowHideSubcollectionsButton =
      settings.toolbarShowHideSubcollectionsButton;
  m_generalSettings.toolbarShowTypeFilter = settings.toolbarShowTypeFilter;
  m_generalSettings.toolbarShowTitleFilter = settings.toolbarShowTitleFilter;
  m_generalSettings.toolbarShowSearchModeButton = settings.toolbarShowSearchModeButton;
  m_generalSettings.toolbarShowSearchBar = settings.toolbarShowSearchBar;
  m_generalSettings.toolbarGridViewButtonText = settings.toolbarGridViewButtonText;
  m_generalSettings.toolbarListViewButtonText = settings.toolbarListViewButtonText;
  m_generalSettings.toolbarCoverFlowViewButtonText = settings.toolbarCoverFlowViewButtonText;
  m_generalSettings.toolbarHorizontalViewButtonText = settings.toolbarHorizontalViewButtonText;
  m_generalSettings.toolbarHideSubcollectionsButtonText =
      settings.toolbarHideSubcollectionsButtonText;
  m_generalSettings.toolbarTitleFilterText = settings.toolbarTitleFilterText;
  // Splash screens
  m_generalSettings.bootSplashEnabled = settings.bootSplashEnabled;
  m_generalSettings.resumeFocusSplashEnabled = settings.resumeFocusSplashEnabled;
  m_generalSettings.bootSplashTitle = settings.bootSplashTitle;
  m_generalSettings.bootSplashSubtitle = settings.bootSplashSubtitle;
  m_generalSettings.resumeFocusSplashTitle = settings.resumeFocusSplashTitle;
  m_generalSettings.resumeFocusSplashSubtitle = settings.resumeFocusSplashSubtitle;
  // Launcher presets
  m_generalSettings.launcherPresets = settings.launcherPresets;

  QSettings s(SettingsUtils::getConfigPath(), SettingsUtils::getFormat());
  s.setAtomicSyncRequired(true);
  s.beginGroup("General");
  s.setValue("rememberSelection", m_generalSettings.rememberSelection);
  s.setValue("wrapNavigation", m_generalSettings.wrapNavigation);
  s.setValue("selectItemOnHover", m_generalSettings.selectItemOnHover);
  s.setValue("pixmapCacheSizeMB", m_generalSettings.pixmapCacheSizeMB);
  s.setValue("videoThumbnailExtractionTimeoutMs",
             m_generalSettings.videoThumbnailExtractionTimeoutMs);
  s.setValue("keyboardRepeatIntervalMs", m_generalSettings.keyboardRepeatIntervalMs);
  s.setValue("keyboardRepeatDelayMs", m_generalSettings.keyboardRepeatDelayMs);
  s.setValue("clickHoldDelayMs", m_generalSettings.clickHoldDelayMs);
  s.setValue("clickHoldRepeatIntervalMs", m_generalSettings.clickHoldRepeatIntervalMs);
  s.setValue("listKeyboardRepeatIntervalMs", m_generalSettings.listKeyboardRepeatIntervalMs);
  s.setValue("listClickHoldRepeatIntervalMs", m_generalSettings.listClickHoldRepeatIntervalMs);
  s.setValue("mouseWheelRows", m_generalSettings.mouseWheelRows);
  s.setValue("scrollAnimationDurationMs", m_generalSettings.scrollAnimationDurationMs);
  s.setValue("scrollVelocityMultiplier", m_generalSettings.scrollVelocityMultiplier);
  s.setValue("titleTintSaturation", m_generalSettings.titleTintSaturation);
  s.setValue("titleTintLightness", m_generalSettings.titleTintLightness);
  s.setValue("titleBaseColor", m_generalSettings.titleBaseColor);

  s.setValue("showTitleInPlaceholder", m_generalSettings.showTitleInPlaceholder);
  // global UI font
  s.setValue("globalUiFontFamily", m_generalSettings.globalUiFontFamily);
  s.setValue("globalUiFontPointSize", m_generalSettings.globalUiFontPointSize);
  // runtime text zoom
  s.setValue("uiTextZoomPercent", m_generalSettings.uiTextZoomPercent);
  // preview video volume
  s.setValue("previewVideoVolume", m_generalSettings.previewVideoVolume);
  // startup video
  s.setValue("startupVideoEnabled", m_generalSettings.startupVideoEnabled);
  s.setValue("startupVideoPath", m_generalSettings.startupVideoPath);
  s.setValue("keyNavLeft", m_generalSettings.keyNavLeft);
  s.setValue("keyNavRight", m_generalSettings.keyNavRight);
  s.setValue("keyNavUp", m_generalSettings.keyNavUp);
  s.setValue("keyNavDown", m_generalSettings.keyNavDown);
  s.setValue("keyConfirm", m_generalSettings.keyConfirm);
  s.setValue("keyBack", m_generalSettings.keyBack);
  s.setValue("keySearch", m_generalSettings.keySearch);
  s.setValue("keyAlphabeticBack", m_generalSettings.keyAlphabeticBack);
  s.setValue("keyAlphabeticForward", m_generalSettings.keyAlphabeticForward);
  s.setValue("keyJumpFirst", m_generalSettings.keyJumpFirst);
  s.setValue("keyJumpLast", m_generalSettings.keyJumpLast);
  s.setValue("keyItemDetails", m_generalSettings.keyItemDetails);
  s.setValue("keyHomeView", m_generalSettings.keyHomeView);
  s.setValue("gamepadUseDpad", m_generalSettings.gamepadUseDpad);
  s.setValue("gamepadUseLeftStick", m_generalSettings.gamepadUseLeftStick);
  s.setValue("gamepadConfirmButton", m_generalSettings.gamepadConfirmButton);
  s.setValue("gamepadBackButton", m_generalSettings.gamepadBackButton);
  s.setValue("gamepadToggleSidebarButton", m_generalSettings.gamepadToggleSidebarButton);
  s.setValue("artworkCycleModifier", m_generalSettings.artworkCycleModifier);
  s.setValue("sortMode", static_cast<int>(m_generalSettings.sortMode));
  s.setValue("excludeSubfoldersFromSort", m_generalSettings.excludeSubfoldersFromSort);
  // collection categorization toolbar state
  s.setValue("collectionTypeFilter", m_generalSettings.collectionTypeFilter);
  s.setValue("hideSubcollectionTiles", m_generalSettings.hideSubcollectionTiles);
  s.setValue("listCollectionColumnWidth", m_generalSettings.listCollectionColumnWidth);
  s.setValue("listArtworkColumnWidth", m_generalSettings.listArtworkColumnWidth);
  s.setValue("startupCollection", m_generalSettings.startupCollection);
  s.setValue("useHomeView", m_generalSettings.useHomeView);
  s.setValue("homeViewLabel", m_generalSettings.homeViewLabel);
  s.setValue("homeViewIcon", m_generalSettings.homeViewIcon);
  s.setValue("retroarchConfigPath", m_generalSettings.retroarchConfigPath);
  s.setValue("attractModeEnabled", m_generalSettings.attractModeEnabled);
  s.setValue("attractModeIdleTimeoutSec", m_generalSettings.attractModeIdleTimeoutSec);
  s.setValue("runtimeDetectionEnabled", m_generalSettings.runtimeDetectionEnabled);
  s.setValue("firstRunComplete", m_generalSettings.firstRunComplete);
  s.setValue("historyEnabled", m_generalSettings.historyEnabled);
  s.setValue("historyMaxEntries", m_generalSettings.historyMaxEntries);
  s.setValue("attractModeAutoScrollEnabled", m_generalSettings.attractModeAutoScrollEnabled);
  s.setValue("attractModeScrollSpeed", m_generalSettings.attractModeScrollSpeed);
  s.setValue("attractModeAdvanceSelectionEnabled",
             m_generalSettings.attractModeAdvanceSelectionEnabled);
  s.setValue("attractModeAdvanceSelectionIntervalSec",
             m_generalSettings.attractModeAdvanceSelectionIntervalSec);
  s.setValue("attractModeAdvanceSelectionRandom",
             m_generalSettings.attractModeAdvanceSelectionRandom);
  // Marquee secondary-monitor display
  s.setValue("marqueeEnabled", m_generalSettings.marqueeEnabled);
  s.setValue("marqueeScreenName", m_generalSettings.marqueeScreenName);
  s.setValue("marqueeMode", m_generalSettings.marqueeMode);
  s.setValue("bootSplashEnabled", m_generalSettings.bootSplashEnabled);
  s.setValue("resumeFocusSplashEnabled", m_generalSettings.resumeFocusSplashEnabled);
  s.setValue("bootSplashTitle", m_generalSettings.bootSplashTitle);
  s.setValue("bootSplashSubtitle", m_generalSettings.bootSplashSubtitle);
  s.setValue("resumeFocusSplashTitle", m_generalSettings.resumeFocusSplashTitle);
  s.setValue("resumeFocusSplashSubtitle", m_generalSettings.resumeFocusSplashSubtitle);
  // View-mode toggles
  s.setValue("showMenuBar", m_generalSettings.showMenuBar);
  s.setValue("showToolbar", m_generalSettings.showToolbar);
  s.setValue("fullscreen", m_generalSettings.fullscreen);
  // Customizable toolbar
  s.setValue("toolbarShowGridViewButton", m_generalSettings.toolbarShowGridViewButton);
  s.setValue("toolbarShowListViewButton", m_generalSettings.toolbarShowListViewButton);
  s.setValue("toolbarShowCoverFlowViewButton", m_generalSettings.toolbarShowCoverFlowViewButton);
  s.setValue("toolbarShowHorizontalViewButton", m_generalSettings.toolbarShowHorizontalViewButton);
  s.setValue("toolbarShowHideSubcollectionsButton",
             m_generalSettings.toolbarShowHideSubcollectionsButton);
  s.setValue("toolbarShowTypeFilter", m_generalSettings.toolbarShowTypeFilter);
  s.setValue("toolbarShowTitleFilter", m_generalSettings.toolbarShowTitleFilter);
  s.setValue("toolbarShowSearchModeButton", m_generalSettings.toolbarShowSearchModeButton);
  s.setValue("toolbarShowSearchBar", m_generalSettings.toolbarShowSearchBar);
  s.setValue("toolbarGridViewButtonText", m_generalSettings.toolbarGridViewButtonText);
  s.setValue("toolbarListViewButtonText", m_generalSettings.toolbarListViewButtonText);
  s.setValue("toolbarCoverFlowViewButtonText", m_generalSettings.toolbarCoverFlowViewButtonText);
  s.setValue("toolbarHorizontalViewButtonText", m_generalSettings.toolbarHorizontalViewButtonText);
  s.setValue("toolbarHideSubcollectionsButtonText",
             m_generalSettings.toolbarHideSubcollectionsButtonText);
  s.setValue("toolbarTitleFilterText", m_generalSettings.toolbarTitleFilterText);
  s.endGroup();

  // persist launcher presets as a top-level [Launchers] array.
  // beginWriteArray clears any existing entries with the same prefix, so a
  // preset removed via the dialog doesn't linger as a stale row.
  s.beginWriteArray("Launchers", m_generalSettings.launcherPresets.size());
  for (int i = 0; i < m_generalSettings.launcherPresets.size(); ++i) {
    s.setArrayIndex(i);
    const LauncherPreset &preset = m_generalSettings.launcherPresets[i];
    s.setValue("id", preset.id);
    s.setValue("name", preset.name);
    s.setValue("launcherPath", preset.launcherPath);
    s.setValue("corePath", preset.corePath);
    s.setValue("launchParameters", preset.launchParameters);
  }
  s.endArray();

  // Persist scraper credentials. Wipe the entire [Scrapers] group
  // first so removing a credential field via the UI actually clears
  // the row from disk (otherwise the next load would resurrect it).
  s.remove(QStringLiteral("Scrapers"));
  s.beginGroup("Scrapers");
  for (auto pIt = m_generalSettings.scraperCredentials.constBegin();
       pIt != m_generalSettings.scraperCredentials.constEnd(); ++pIt) {
    const QString &providerId = pIt.key();
    if (providerId.trimmed().isEmpty()) continue;
    for (auto fIt = pIt.value().constBegin(); fIt != pIt.value().constEnd(); ++fIt) {
      const QString &field = fIt.key();
      if (field.trimmed().isEmpty()) continue;
      // Skip empty values so a fully-cleared field doesn't write an
      // empty row that survives a round-trip.
      if (fIt.value().isEmpty()) continue;
      s.setValue(providerId + QLatin1Char('/') + field, fIt.value());
    }
  }
  s.endGroup();

  // Scraper performance + behavior options. Wipe the group first so
  // a "Reset to defaults" round-trip doesn't leave stale custom keys.
  s.remove(QStringLiteral("ScraperOptions"));
  s.beginGroup("ScraperOptions");
  s.setValue("preset", static_cast<int>(m_generalSettings.scraperOptions.preset));
  s.setValue("mediaMaxDimension", m_generalSettings.scraperOptions.mediaMaxDimension);
  s.setValue("mediaConcurrency", m_generalSettings.scraperOptions.mediaConcurrency);
  s.setValue("mediaThrottleMs", m_generalSettings.scraperOptions.mediaThrottleMs);
  s.setValue("batchItemConcurrency", m_generalSettings.scraperOptions.batchItemConcurrency);
  s.setValue("rescrapeMode", static_cast<int>(m_generalSettings.scraperOptions.rescrapeMode));
  s.setValue("skipRecentScrapeDays", m_generalSettings.scraperOptions.skipRecentScrapeDays);
  s.setValue("preferJpgOutput", m_generalSettings.scraperOptions.preferJpgOutput);
  s.setValue("scrapeAutoResume", m_generalSettings.scraperOptions.scrapeAutoResume);
  s.setValue("scrapeLogging", m_generalSettings.scraperOptions.scrapeLogging);
  s.setValue("preferredRegion", m_generalSettings.scraperOptions.preferredScraperRegion);
  s.endGroup();

  s.sync();

  if (s.status() != QSettings::NoError) {
    ErrorUtils::logError(ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                                           "Failed to persist general settings",
                                                           "SettingsManager::saveGeneralSettings")
                             .withDetails(QString("Path: %1, Status: %2")
                                              .arg(SettingsUtils::getConfigPath())
                                              .arg(static_cast<int>(s.status()))));
  }
}

// Updates a single collection's last selected item (in-memory only; persistent
// cache handled elsewhere)
void SettingsManager::setLastSelectedItem(int collectionIndex, int itemIndex) {
  if (collectionIndex < 0) {
    return;
  }
  m_generalSettings.lastSelectedItems[collectionIndex] = itemIndex;
}

auto SettingsManager::getLastSelectedItem(int collectionIndex) const -> int {
  // dynamic_cast (not qobject_cast): IMainWindow is a plain abstract base, so
  // it carries no Qt meta-object. parent() is only a MainWindow when one owns
  // this SettingsManager; otherwise this branch is skipped, exactly as the
  // previous qobject_cast<MainWindow*> behaved.
  auto *mainWindow = dynamic_cast<IMainWindow *>(parent());
  ISessionManager *session = m_ctx ? m_ctx->sessionManager() : nullptr;
  if ((mainWindow) && collectionIndex >= 0 && collectionIndex < mainWindow->collections().size()) {
    const QList<CollectionConfig> &mwCollections = mainWindow->collections();
    const CollectionConfig &cfg = mwCollections[collectionIndex];
    const bool subfolderActive = !cfg.currentSubfolder.trimmed().isEmpty();
    QString hierarchicalName = CollectionUtils::hierarchicalNameFor(cfg, mwCollections);
    int persistentIndex = -1;
    if (session) {
      if (subfolderActive) {
        const QString sessionKey = CollectionUtils::selectionSessionKeyFor(cfg, mwCollections);
        persistentIndex = session->getLastSelectedIndex(sessionKey);
      } else {
        persistentIndex = session->getLastSelectedIndex(hierarchicalName);
      }
    }
    if (persistentIndex >= 0) {
      return persistentIndex;
    }

    if (!subfolderActive) {
      QString collectionName = cfg.name;
      if (session) {
        persistentIndex = session->getLastSelectedIndex(collectionName);
      }
      if (persistentIndex >= 0) {
        return persistentIndex;
      }
    }
  }

  if (m_generalSettings.lastSelectedItems.contains(collectionIndex)) {
    return m_generalSettings.lastSelectedItems.value(collectionIndex, -1);
  }

  return -1;
}
