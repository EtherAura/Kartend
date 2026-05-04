// Handles config file I/O, collection settings, and the settings dialog
// interface.
#include "settingsmanager.h"
#include "artworkmanager.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "configvalidation.h"
#include "errorutils.h"
#include "extensionutils.h"
#include "mainwindow.h"
#include "navigationmanager.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsdialog.h"
#include "settingsutils.h"
#include "sidebarmanager.h"
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
SettingsManager::SettingsManager(SessionManager *sessionManager, ArtworkManager *artworkManager,
                                 CacheManager *cacheManager, QObject *parent)
    : QObject(parent), m_sessionManager(sessionManager), m_artworkManager(artworkManager),
      m_cacheManager(cacheManager) {
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
  s.beginGroup("General");
  settings.rememberSelection = s.value("rememberSelection", true).toBool();
  settings.wrapNavigation = s.value("wrapNavigation", false).toBool();
  settings.selectItemOnHover = s.value("selectItemOnHover", false).toBool();
  settings.pixmapCacheSizeMB = s.value("pixmapCacheSizeMB", 50).toInt();
  // Clamp to reasonable range: 10MB - 500MB
  settings.pixmapCacheSizeMB = qBound(10, settings.pixmapCacheSizeMB, 500);
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

  // Kartend-9v0o: global UI font. Empty family / 0 size = platform default.
  // No clamp on point size beyond Qt's own validation; the spinbox in the
  // settings dialog limits user input to a sane range.
  settings.globalUiFontFamily = s.value("globalUiFontFamily", QString()).toString();
  settings.globalUiFontPointSize = s.value("globalUiFontPointSize", 0).toInt();

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

  // Controls: gamepad bindings
  settings.gamepadUseDpad = s.value("gamepadUseDpad", true).toBool();
  settings.gamepadUseLeftStick = s.value("gamepadUseLeftStick", true).toBool();
  settings.gamepadConfirmButton = s.value("gamepadConfirmButton", QString("A")).toString();
  settings.gamepadBackButton = s.value("gamepadBackButton", QString("B")).toString();
  settings.gamepadToggleSidebarButton =
      s.value("gamepadToggleSidebarButton", QString("Y")).toString();

  // Kartend-1v6: artwork-cycle modifier. Coerce hand-edited junk back to Shift
  // so the gesture is always reachable; allow only the single-modifier flags
  // we expose in the settings UI.
  {
    const int rawModifier =
        s.value("artworkCycleModifier", static_cast<int>(Qt::ShiftModifier)).toInt();
    switch (rawModifier) {
    case static_cast<int>(Qt::ShiftModifier):
    case static_cast<int>(Qt::ControlModifier):
    case static_cast<int>(Qt::AltModifier):
    case static_cast<int>(Qt::MetaModifier):
      settings.artworkCycleModifier = rawModifier;
      break;
    default:
      settings.artworkCycleModifier = static_cast<int>(Qt::ShiftModifier);
      break;
    }
  }

  // Sort preferences
  const int sortModeRaw = s.value("sortMode", static_cast<int>(SortMode::NameAscending)).toInt();
  if (sortModeRaw >= static_cast<int>(SortMode::NameAscending) &&
      sortModeRaw <= static_cast<int>(SortMode::SizeAscending)) {
    settings.sortMode = static_cast<SortMode>(sortModeRaw);
  } else {
    settings.sortMode = SortMode::NameAscending;
  }
  settings.excludeSubfoldersFromSort = s.value("excludeSubfoldersFromSort", false).toBool();
  // Kartend-dd8: collection categorization filters. Defaults are "no filter"
  // so an upgrading user sees all subcollections as before until they pick a
  // type or toggle the hide button on the toolbar.
  settings.collectionTypeFilter = s.value("collectionTypeFilter", QString()).toString().trimmed();
  settings.hideSubcollectionTiles = s.value("hideSubcollectionTiles", false).toBool();
  settings.listCollectionColumnWidth = s.value("listCollectionColumnWidth", 150).toInt();
  settings.listArtworkColumnWidth = s.value("listArtworkColumnWidth", 32).toInt();
  settings.startupCollection = s.value("startupCollection", QString()).toString();

  // Attract mode (Kartend-1pp)
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

  // Splash screens
  settings.bootSplashEnabled = s.value("bootSplashEnabled", true).toBool();
  settings.resumeFocusSplashEnabled = s.value("resumeFocusSplashEnabled", true).toBool();

  // Runtime detection (Kartend-qxv) — opt-in
  settings.runtimeDetectionEnabled = s.value("runtimeDetectionEnabled", false).toBool();

  // Launch history (Kartend-fse). Default is enabled with a 500-row cap so
  // a fresh install starts logging immediately; the user disables in
  // Settings → General. Negative caps land in the file via hand-edit only;
  // qBound clamps them to a sane window so trim never deletes the whole
  // table by accident.
  settings.historyEnabled = s.value("historyEnabled", true).toBool();
  settings.historyMaxEntries = qBound(10, s.value("historyMaxEntries", 500).toInt(), 50000);

  // Customizable toolbar (Kartend-81o). Default visibility is "shown" so an
  // upgrading user sees the toolbar exactly as before; custom text strings
  // default to empty (use the .ui label).
  settings.toolbarShowGridViewButton = s.value("toolbarShowGridViewButton", true).toBool();
  settings.toolbarShowListViewButton = s.value("toolbarShowListViewButton", true).toBool();
  settings.toolbarShowCoverFlowViewButton =
      s.value("toolbarShowCoverFlowViewButton", true).toBool();
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
  settings.toolbarHideSubcollectionsButtonText =
      s.value("toolbarHideSubcollectionsButtonText", QString()).toString();
  settings.toolbarTitleFilterText = s.value("toolbarTitleFilterText", QString()).toString();
  s.endGroup();

  // Kartend-p1jd: launcher presets live at the top level (outside [General])
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
    preset.launcherPath = s.value("launcherPath").toString();
    preset.corePath = s.value("corePath").toString();
    preset.launchParameters = s.value("launchParameters").toString();
    // Drop entries with no id — they can't be referenced and would shadow
    // valid presets if a hand-edit accidentally cleared the field.
    if (!preset.id.trimmed().isEmpty()) {
      settings.launcherPresets.append(preset);
    }
  }
  s.endArray();

  settings.lastSelectedItems.clear();
  m_generalSettings = settings;
}

// Saves general settings (no legacy last_selected.dat persistence)
void SettingsManager::saveGeneralSettings(const GeneralSettings &settings) {
  m_generalSettings.rememberSelection = settings.rememberSelection;
  m_generalSettings.wrapNavigation = settings.wrapNavigation;
  m_generalSettings.selectItemOnHover = settings.selectItemOnHover;
  m_generalSettings.pixmapCacheSizeMB = qBound(10, settings.pixmapCacheSizeMB, 500);
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
  // Kartend-9v0o: global UI font
  m_generalSettings.globalUiFontFamily = settings.globalUiFontFamily.trimmed();
  m_generalSettings.globalUiFontPointSize = settings.globalUiFontPointSize;

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
  m_generalSettings.gamepadUseDpad = settings.gamepadUseDpad;
  m_generalSettings.gamepadUseLeftStick = settings.gamepadUseLeftStick;
  m_generalSettings.gamepadConfirmButton = settings.gamepadConfirmButton;
  m_generalSettings.gamepadBackButton = settings.gamepadBackButton;
  m_generalSettings.gamepadToggleSidebarButton = settings.gamepadToggleSidebarButton;
  m_generalSettings.artworkCycleModifier = settings.artworkCycleModifier;
  m_generalSettings.sortMode = settings.sortMode;
  m_generalSettings.excludeSubfoldersFromSort = settings.excludeSubfoldersFromSort;
  // Kartend-dd8: collection categorization filters
  m_generalSettings.collectionTypeFilter = settings.collectionTypeFilter.trimmed();
  m_generalSettings.hideSubcollectionTiles = settings.hideSubcollectionTiles;
  m_generalSettings.listCollectionColumnWidth = settings.listCollectionColumnWidth;
  m_generalSettings.listArtworkColumnWidth = settings.listArtworkColumnWidth;
  m_generalSettings.startupCollection = settings.startupCollection;

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

  // Runtime detection (Kartend-qxv)
  m_generalSettings.runtimeDetectionEnabled = settings.runtimeDetectionEnabled;
  // Launch history (Kartend-fse)
  m_generalSettings.historyEnabled = settings.historyEnabled;
  m_generalSettings.historyMaxEntries = qBound(10, settings.historyMaxEntries, 50000);
  // Customizable toolbar (Kartend-81o)
  m_generalSettings.toolbarShowGridViewButton = settings.toolbarShowGridViewButton;
  m_generalSettings.toolbarShowListViewButton = settings.toolbarShowListViewButton;
  m_generalSettings.toolbarShowCoverFlowViewButton = settings.toolbarShowCoverFlowViewButton;
  m_generalSettings.toolbarShowHideSubcollectionsButton =
      settings.toolbarShowHideSubcollectionsButton;
  m_generalSettings.toolbarShowTypeFilter = settings.toolbarShowTypeFilter;
  m_generalSettings.toolbarShowTitleFilter = settings.toolbarShowTitleFilter;
  m_generalSettings.toolbarShowSearchModeButton = settings.toolbarShowSearchModeButton;
  m_generalSettings.toolbarShowSearchBar = settings.toolbarShowSearchBar;
  m_generalSettings.toolbarGridViewButtonText = settings.toolbarGridViewButtonText;
  m_generalSettings.toolbarListViewButtonText = settings.toolbarListViewButtonText;
  m_generalSettings.toolbarCoverFlowViewButtonText = settings.toolbarCoverFlowViewButtonText;
  m_generalSettings.toolbarHideSubcollectionsButtonText =
      settings.toolbarHideSubcollectionsButtonText;
  m_generalSettings.toolbarTitleFilterText = settings.toolbarTitleFilterText;
  // Splash screens
  m_generalSettings.bootSplashEnabled = settings.bootSplashEnabled;
  m_generalSettings.resumeFocusSplashEnabled = settings.resumeFocusSplashEnabled;
  // Launcher presets (Kartend-p1jd)
  m_generalSettings.launcherPresets = settings.launcherPresets;

  QSettings s(SettingsUtils::getConfigPath(), SettingsUtils::getFormat());
  s.setAtomicSyncRequired(true);
  s.beginGroup("General");
  s.setValue("rememberSelection", m_generalSettings.rememberSelection);
  s.setValue("wrapNavigation", m_generalSettings.wrapNavigation);
  s.setValue("selectItemOnHover", m_generalSettings.selectItemOnHover);
  s.setValue("pixmapCacheSizeMB", m_generalSettings.pixmapCacheSizeMB);
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
  // Kartend-9v0o: global UI font
  s.setValue("globalUiFontFamily", m_generalSettings.globalUiFontFamily);
  s.setValue("globalUiFontPointSize", m_generalSettings.globalUiFontPointSize);
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
  s.setValue("gamepadUseDpad", m_generalSettings.gamepadUseDpad);
  s.setValue("gamepadUseLeftStick", m_generalSettings.gamepadUseLeftStick);
  s.setValue("gamepadConfirmButton", m_generalSettings.gamepadConfirmButton);
  s.setValue("gamepadBackButton", m_generalSettings.gamepadBackButton);
  s.setValue("gamepadToggleSidebarButton", m_generalSettings.gamepadToggleSidebarButton);
  s.setValue("artworkCycleModifier", m_generalSettings.artworkCycleModifier);
  s.setValue("sortMode", static_cast<int>(m_generalSettings.sortMode));
  s.setValue("excludeSubfoldersFromSort", m_generalSettings.excludeSubfoldersFromSort);
  // Kartend-dd8: collection categorization toolbar state
  s.setValue("collectionTypeFilter", m_generalSettings.collectionTypeFilter);
  s.setValue("hideSubcollectionTiles", m_generalSettings.hideSubcollectionTiles);
  s.setValue("listCollectionColumnWidth", m_generalSettings.listCollectionColumnWidth);
  s.setValue("listArtworkColumnWidth", m_generalSettings.listArtworkColumnWidth);
  s.setValue("startupCollection", m_generalSettings.startupCollection);
  s.setValue("attractModeEnabled", m_generalSettings.attractModeEnabled);
  s.setValue("attractModeIdleTimeoutSec", m_generalSettings.attractModeIdleTimeoutSec);
  s.setValue("runtimeDetectionEnabled", m_generalSettings.runtimeDetectionEnabled);
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
  s.setValue("bootSplashEnabled", m_generalSettings.bootSplashEnabled);
  s.setValue("resumeFocusSplashEnabled", m_generalSettings.resumeFocusSplashEnabled);
  // Customizable toolbar (Kartend-81o)
  s.setValue("toolbarShowGridViewButton", m_generalSettings.toolbarShowGridViewButton);
  s.setValue("toolbarShowListViewButton", m_generalSettings.toolbarShowListViewButton);
  s.setValue("toolbarShowCoverFlowViewButton",
             m_generalSettings.toolbarShowCoverFlowViewButton);
  s.setValue("toolbarShowHideSubcollectionsButton",
             m_generalSettings.toolbarShowHideSubcollectionsButton);
  s.setValue("toolbarShowTypeFilter", m_generalSettings.toolbarShowTypeFilter);
  s.setValue("toolbarShowTitleFilter", m_generalSettings.toolbarShowTitleFilter);
  s.setValue("toolbarShowSearchModeButton", m_generalSettings.toolbarShowSearchModeButton);
  s.setValue("toolbarShowSearchBar", m_generalSettings.toolbarShowSearchBar);
  s.setValue("toolbarGridViewButtonText", m_generalSettings.toolbarGridViewButtonText);
  s.setValue("toolbarListViewButtonText", m_generalSettings.toolbarListViewButtonText);
  s.setValue("toolbarCoverFlowViewButtonText",
             m_generalSettings.toolbarCoverFlowViewButtonText);
  s.setValue("toolbarHideSubcollectionsButtonText",
             m_generalSettings.toolbarHideSubcollectionsButtonText);
  s.setValue("toolbarTitleFilterText", m_generalSettings.toolbarTitleFilterText);
  s.endGroup();

  // Kartend-p1jd: persist launcher presets as a top-level [Launchers] array.
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
  auto *mainWindow = qobject_cast<MainWindow *>(parent());
  if ((mainWindow) && collectionIndex >= 0 && collectionIndex < mainWindow->m_collections.size()) {
    const CollectionConfig &cfg = mainWindow->m_collections[collectionIndex];
    const bool subfolderActive = !cfg.currentSubfolder.trimmed().isEmpty();
    QString hierarchicalName = CollectionUtils::hierarchicalNameFor(cfg, mainWindow->m_collections);
    int persistentIndex = -1;
    if (m_sessionManager) {
      if (subfolderActive) {
        const QString sessionKey =
            CollectionUtils::selectionSessionKeyFor(cfg, mainWindow->m_collections);
        persistentIndex = m_sessionManager->getLastSelectedIndex(sessionKey);
      } else {
        persistentIndex = m_sessionManager->getLastSelectedIndex(hierarchicalName);
      }
    }
    if (persistentIndex >= 0) {
      return persistentIndex;
    }

    if (!subfolderActive) {
      QString collectionName = cfg.name;
      if (m_sessionManager) {
        persistentIndex = m_sessionManager->getLastSelectedIndex(collectionName);
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
