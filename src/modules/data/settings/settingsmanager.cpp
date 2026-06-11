// Handles config file I/O, collection settings, and the settings dialog
// interface.
#include "settingsmanager.h"
#include "applicationcontext.h"
#include "collection/generalsettings.h"
#include "configvalidation.h"
#include "errorutils.h"
#include "extensionutils.h"
#include "iartworkmanager.h"
#include "icachemanager.h"
#include "idetailspanemanager.h"
#include "inavigationmanager.h"
#include "pathutils.h"
#include "scrapelogger.h"
#include "settingshelpers.h"
#include "settingsmigrations.h"
#include "settingsutils.h"
#include "timerutils.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPointer>
#include <QScrollArea>
#include <QStandardPaths>

#include "settingskeys.h"
#include <QLoggingCategory>

namespace keys = kartend::settings::keys;
Q_LOGGING_CATEGORY(lcSettingsManager, "kartend.settingsmanager")

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

// Path-security filter for hand-editable path fields. Mirrors the write-side
// validation in saveCollections()/saveGeneralSettings() so a hand-edited config
// can't sneak shell metacharacters or null bytes into a path that's later
// passed to QProcess / QFile. Returns "" (treated as "unset") for an insecure
// value. Shared by the startup + launcher-preset section loaders.
QString SettingsManager::sanitizeLoadedPath(const QString &value, const QString &fieldName) {
  if (value.isEmpty()) {
    return value;
  }
  auto security = PathUtils::validatePathSecurity(value);
  if (security.isError()) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::InvalidFilePath,
                                          QString("Refusing to load insecure %1").arg(fieldName),
                                          "SettingsManager::sanitizeLoadedPath")
            .withDetails(QString("Value: %1, Reason: %2").arg(value, security.error().message)));
    return QString();
  }
  return value;
}

// Loads general settings. Thin orchestration: opens the config, runs the
// corruption check + schema preflight/migration, then delegates each leaf
// section to its settingsmanager_<section>.cpp helper. The [General]-group
// helpers run while this function holds that group open; launcher presets and
// the scraper section manage their own groups.
void SettingsManager::loadGeneralSettings(GeneralSettings &settings) {
  const QString configPath = SettingsUtils::getConfigPath();
  QSettings s(configPath, SettingsUtils::getFormat());

  // QSettings exposes parse errors through status() — a torn/corrupted INI
  // surfaces as FormatError here. Without this check, every value() below
  // silently falls through to its default and the next save then overwrites
  // the original (broken) file with defaults, destroying whatever the user
  // had configured. Detect corruption now, log it, and snapshot the corrupt
  // file under a timestamped sidecar so the next save can't erase the
  // forensic copy. The save path itself is unchanged — the user can re-save
  // intentionally; we just refuse to silently swallow corruption.
  if (s.status() != QSettings::NoError && QFile::exists(configPath)) {
    const QString stamp =
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmssZ"));
    const QString backupPath = configPath + QStringLiteral(".corrupt-") + stamp;
    const bool copied = QFile::copy(configPath, backupPath);
    qCWarning(lcSettingsManager) << "Settings INI failed to parse (QSettings::status() =="
                                 << static_cast<int>(s.status()) << "for" << configPath
                                 << "). Defaults will be loaded;"
                                 << (copied ? "the corrupt file has been snapshotted to"
                                            : "FAILED to snapshot to")
                                 << backupPath << "before the next save.";
  }

  // Pre-flight: detect future-versioned INIs so a stale build doesn't
  // silently drop unknown fields without leaving a breadcrumb. v0
  // (key missing) is the legacy path — load every current key with its
  // declared default. Real migration switches will hang off this in a
  // later schemaVersion bump.
  //
  // schemaVersion is the file-wide schema marker, read from (and written to)
  // [General]. Builds before this fix wrote it into [ScraperOptions] by
  // mistake, so fall back to that legacy location when [General] lacks the
  // key — an INI from any past build is then versioned correctly, and the
  // next save rewrites the sentinel into [General]. Read at the top level
  // (before beginGroup) so the group-qualified keys resolve.
  const auto schemaVersionKey = [](const char *group) {
    return QString::fromLatin1(group) + QLatin1Char('/') + QLatin1String(keys::kSchemaVersion);
  };
  const int loadedSchemaVersion = s.value(schemaVersionKey(keys::kGroupGeneral),
                                          s.value(schemaVersionKey(keys::kGroupScraperOptions), 0))
                                      .toInt();

  s.beginGroup(keys::kGroupGeneral);
  if (loadedSchemaVersion > kSettingsSchemaVersion) {
    qCWarning(lcSettingsManager)
        << "Settings INI was written with schemaVersion" << loadedSchemaVersion
        << "but this build only understands up to" << kSettingsSchemaVersion
        << "— unknown keys will be ignored on load and overwritten on save.";
  } else if (loadedSchemaVersion < kSettingsSchemaVersion) {
    // Drive in-place migration before any value() reads consume the
    // (potentially renamed/restructured) keys. The dispatcher is a no-op
    // when no steps are registered for the current span, so this is cheap
    // on every load until a real migration lands.
    kartend::settings::migrations::applyMigrations(
        s, loadedSchemaVersion, kSettingsSchemaVersion,
        QStringLiteral("SettingsManager::loadGeneralSettings"));
  }

  loadInputSection(s, settings);
  loadKeybindingsSection(s, settings);
  loadGamepadSection(s, settings);
  loadViewSection(s, settings);
  loadAppearanceSection(s, settings);
  loadStartupSection(s, settings);
  loadMediaSection(s, settings);
  loadHistorySection(s, settings);
  loadAttractSection(s, settings);
  loadMarqueeSection(s, settings);
  loadSplashSection(s, settings);
  loadRuntimeDetectionSection(s, settings);
  loadToolbarSection(s, settings);
  // retroarchConfigPath rides in [General] alongside the rest; the launcher
  // preset array is read below, outside this group.
  loadLaunchersScalars(s, settings);
  s.endGroup();

  // launcher presets live at the top level (outside [General]) so they remain
  // a clear, named section the user can hand-edit.
  loadLaunchersPresets(s, settings);

  // Scraper credentials ([Scrapers]) + performance/behavior options
  // ([ScraperOptions]). The keychain-aware credential walk lives here too.
  loadScraperSection(s, settings);

  settings.lastSelectedItems.clear();
  m_generalSettings = settings;
  // Bring scrape logging into effect for the running process. This is
  // the first settings read at startup, so the kartend.scrape*
  // categories are toggled before any scrape (or resume prompt) runs.
  ScrapeLogger::setEnabled(m_generalSettings.scraper.options.scrapeLogging);
}

// Saves general settings (no legacy last_selected.dat persistence). Thin
// orchestration: each leaf section's settingsmanager_<section>.cpp helper copies
// settings.<section> into the m_generalSettings cache, re-applies the defensive
// clamps, then persists the cached value (the cache is the clamp authority, so a
// load -> save round-trip is idempotent). This function owns the QSettings
// lifecycle, [General] group bracketing, durability/permissions, and the
// scraper-options change signal.
ErrorUtils::Result<void> SettingsManager::saveGeneralSettings(const GeneralSettings &settings) {
  // Snapshot the sub-struct we diff before any field assignment, so the
  // per-domain change signal at the bottom fires only when a value actually
  // changed. Cheap value copy of a POD-ish leaf struct.
  const ScraperOptions oldScraperOptions = m_generalSettings.scraper.options;
  // Kartend-ztc64: demotion-state snapshot. saveScraperSection recomputes
  // m_credentialDemotionReason (set on a failed keychain write, cleared when
  // every write succeeds); the change signal fires below, after a clean sync.
  const QString oldDemotionReason = m_credentialDemotionReason;

  QSettings s(SettingsUtils::getConfigPath(), SettingsUtils::getFormat());
  s.setAtomicSyncRequired(true);
  s.beginGroup(keys::kGroupGeneral);
  saveInputSection(s, settings);
  saveKeybindingsSection(s, settings);
  saveGamepadSection(s, settings);
  saveViewSection(s, settings);
  saveAppearanceSection(s, settings);
  saveStartupSection(s, settings);
  saveMediaSection(s, settings);
  saveHistorySection(s, settings);
  saveAttractSection(s, settings);
  saveMarqueeSection(s, settings);
  saveSplashSection(s, settings);
  saveRuntimeDetectionSection(s, settings);
  saveToolbarSection(s, settings);
  // retroarchConfigPath rides in [General]; the preset array is written below.
  saveLaunchersScalars(s, settings);
  s.endGroup();

  // launcher presets persist as a top-level [Launchers] array (outside any
  // group).
  saveLaunchersPresets(s);

  // Scraper credentials ([Scrapers], keychain-aware) + options ([ScraperOptions]).
  saveScraperSection(s, settings);

  // Apply the (possibly changed) scrape-logging toggle now that the scraper
  // section has copied+clamped options into the cache. Idempotent and
  // process-global, so its position relative to the disk write is immaterial.
  ScrapeLogger::setEnabled(m_generalSettings.scraper.options.scrapeLogging);

  // Stamp the file-wide schema sentinel into [General] — the same group load
  // reads it from. Written last (just before sync) so a torn write can't leave
  // a false-positive version marker ahead of the content it versions; the
  // atomic temp-file + rename in sync() is the primary guard, this ordering is
  // defence-in-depth for the rare paths that bypass it (hand-edits, tooling).
  s.beginGroup(keys::kGroupGeneral);
  s.setValue(keys::kSchemaVersion, kSettingsSchemaVersion);
  s.endGroup();

  s.sync();

  ErrorUtils::ErrorContext err;
  if (s.status() != QSettings::NoError) {
    err = ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                            "Failed to persist general settings",
                                            "SettingsManager::saveGeneralSettings")
              .withDetails(QString("Path: %1, Status: %2")
                               .arg(SettingsUtils::getConfigPath())
                               .arg(static_cast<int>(s.status())));
    ErrorUtils::logError(err);
  }

  // setAtomicSyncRequired(true) makes QSettings write the [General] /
  // [Scrapers] sections to a temp file and atomically rename. The rename
  // metadata still has to hit the disk's journal for that to survive a
  // power loss — fsync the parent directory so the rename is durable.
  // syncDirectory tolerates EINVAL filesystems (some tmpfs setups don't
  // support directory fsync) by returning true, so this is safe to call
  // unconditionally on every save.
  const QString configPath = SettingsUtils::getConfigPath();
  if (!PathUtils::syncDirectory(QFileInfo(configPath).path())) {
    qCWarning(lcSettingsManager) << "syncDirectory failed for" << QFileInfo(configPath).path()
                                 << "— atomic rename completed but its durability across a power "
                                    "loss is no longer guaranteed.";
  }

  // Cleartext scraper credentials live in [Scrapers]; clamp the INI to 0600
  // so the per-user config dir doesn't leak them to other local accounts.
  // Runs regardless of sync status — even a partial write may have created
  // or touched the file.
  SettingsUtils::tightenConfigPermissions();

  if (err.isError()) {
    return err;
  }

  // Per-domain hot-reload signals: fire AFTER a clean save so consumers
  // don't refresh against a half-written file. Compare against the
  // pre-save snapshot captured at the top of the function — emitting
  // unconditionally would wake every listener on every Apply, even when
  // nothing in that domain changed.
  if (m_generalSettings.scraper.options != oldScraperOptions) {
    emit scraperOptionsChanged(m_generalSettings.scraper.options);
  }
  // Kartend-ztc64: notify the settings dialog banner when the credential
  // demotion state flipped (set on a failed keychain write, cleared once a
  // later save's keychain writes all succeed and re-promote the plaintext).
  if (m_credentialDemotionReason != oldDemotionReason) {
    emit credentialStorageDemotionChanged(m_credentialDemotionReason);
  }

  return ErrorUtils::Result<void>::success();
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
  // Kartend-dwnis: selection memory is held in the in-process
  // m_generalSettings.lastSelectedItems map, populated by setLastSelectedItem
  // during the session. A SessionManager-backed restore that keyed off
  // dynamic_cast<IMainWindow*>(parent()) used to live here, but SettingsManager
  // is constructed with a null QObject parent and is never reparented to a
  // MainWindow, so that branch was unreachable dead code. Cross-session
  // selection restore is owned by NavigationManager / SelectionRestore
  // Coordinator, which read SessionManager directly during navigation — this
  // accessor intentionally does not duplicate that path.
  return m_generalSettings.lastSelectedItems.value(collectionIndex, -1);
}
