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
  // Load text appearance settings
  settings.titleTintSaturation = s.value("titleTintSaturation", 180).toInt();
  settings.titleTintLightness = s.value("titleTintLightness", 60).toInt();
  settings.titleBaseColor = s.value("titleBaseColor", QString()).toString();

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

  // Sort preferences
  const int sortModeRaw = s.value("sortMode", static_cast<int>(SortMode::NameAscending)).toInt();
  if (sortModeRaw >= static_cast<int>(SortMode::NameAscending) &&
      sortModeRaw <= static_cast<int>(SortMode::Random)) {
    settings.sortMode = static_cast<SortMode>(sortModeRaw);
  } else {
    settings.sortMode = SortMode::NameAscending;
  }
  settings.excludeSubfoldersFromSort = s.value("excludeSubfoldersFromSort", false).toBool();
  settings.listCollectionColumnWidth = s.value("listCollectionColumnWidth", 150).toInt();
  settings.listArtworkColumnWidth = s.value("listArtworkColumnWidth", 32).toInt();
  settings.startupCollection = s.value("startupCollection", QString()).toString();
  s.endGroup();

  settings.lastSelectedItems.clear();
  m_generalSettings = settings;
}

// Saves general settings (no legacy last_selected.dat persistence)
void SettingsManager::saveGeneralSettings(const GeneralSettings &settings) {
  m_generalSettings.rememberSelection = settings.rememberSelection;
  m_generalSettings.wrapNavigation = settings.wrapNavigation;
  m_generalSettings.pixmapCacheSizeMB = qBound(10, settings.pixmapCacheSizeMB, 500);
  m_generalSettings.keyboardRepeatIntervalMs = settings.keyboardRepeatIntervalMs;
  m_generalSettings.keyboardRepeatDelayMs = settings.keyboardRepeatDelayMs;
  m_generalSettings.clickHoldDelayMs = settings.clickHoldDelayMs;
  m_generalSettings.clickHoldRepeatIntervalMs = settings.clickHoldRepeatIntervalMs;
  m_generalSettings.listKeyboardRepeatIntervalMs = settings.listKeyboardRepeatIntervalMs;
  m_generalSettings.listClickHoldRepeatIntervalMs = settings.listClickHoldRepeatIntervalMs;
  m_generalSettings.mouseWheelRows = settings.mouseWheelRows;
  m_generalSettings.scrollAnimationDurationMs = settings.scrollAnimationDurationMs;
  m_generalSettings.titleTintSaturation = settings.titleTintSaturation;
  m_generalSettings.titleTintLightness = settings.titleTintLightness;
  m_generalSettings.titleBaseColor = settings.titleBaseColor;

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
  m_generalSettings.sortMode = settings.sortMode;
  m_generalSettings.excludeSubfoldersFromSort = settings.excludeSubfoldersFromSort;
  m_generalSettings.listCollectionColumnWidth = settings.listCollectionColumnWidth;
  m_generalSettings.listArtworkColumnWidth = settings.listArtworkColumnWidth;
  m_generalSettings.startupCollection = settings.startupCollection;

  QSettings s(SettingsUtils::getConfigPath(), SettingsUtils::getFormat());
  s.setAtomicSyncRequired(true);
  s.beginGroup("General");
  s.setValue("rememberSelection", m_generalSettings.rememberSelection);
  s.setValue("wrapNavigation", m_generalSettings.wrapNavigation);
  s.setValue("pixmapCacheSizeMB", m_generalSettings.pixmapCacheSizeMB);
  s.setValue("keyboardRepeatIntervalMs", m_generalSettings.keyboardRepeatIntervalMs);
  s.setValue("keyboardRepeatDelayMs", m_generalSettings.keyboardRepeatDelayMs);
  s.setValue("clickHoldDelayMs", m_generalSettings.clickHoldDelayMs);
  s.setValue("clickHoldRepeatIntervalMs", m_generalSettings.clickHoldRepeatIntervalMs);
  s.setValue("listKeyboardRepeatIntervalMs", m_generalSettings.listKeyboardRepeatIntervalMs);
  s.setValue("listClickHoldRepeatIntervalMs", m_generalSettings.listClickHoldRepeatIntervalMs);
  s.setValue("mouseWheelRows", m_generalSettings.mouseWheelRows);
  s.setValue("scrollAnimationDurationMs", m_generalSettings.scrollAnimationDurationMs);
  s.setValue("titleTintSaturation", m_generalSettings.titleTintSaturation);
  s.setValue("titleTintLightness", m_generalSettings.titleTintLightness);
  s.setValue("titleBaseColor", m_generalSettings.titleBaseColor);
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
  s.setValue("sortMode", static_cast<int>(m_generalSettings.sortMode));
  s.setValue("excludeSubfoldersFromSort", m_generalSettings.excludeSubfoldersFromSort);
  s.setValue("listCollectionColumnWidth", m_generalSettings.listCollectionColumnWidth);
  s.setValue("listArtworkColumnWidth", m_generalSettings.listArtworkColumnWidth);
  s.setValue("startupCollection", m_generalSettings.startupCollection);
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
