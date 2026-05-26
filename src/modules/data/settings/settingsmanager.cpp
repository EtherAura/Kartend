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
#include "textzoom.h"
#include "timerutils.h"
#include "uiconstants/attract.h"
#include "uiconstants/cache.h"
#include "uiconstants/launch.h"
#include "uiconstants/scroll.h"
#include "uiconstants/timing.h"
#include <algorithm>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPointer>
#include <QScrollArea>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

#ifdef KARTEND_HAVE_QTKEYCHAIN
#include <qt6keychain/keychain.h>
#endif

#include "settingskeys.h"
#include <QLoggingCategory>

namespace keys = kartend::settings::keys;
Q_LOGGING_CATEGORY(lcSettingsManager, "kartend.settingsmanager")
#define debugLog(msg) qCDebug(lcSettingsManager) << msg

namespace {
// Stamped into [General/schemaVersion] on every save. Read on load to
// detect INIs written by a build that knows fields this build doesn't
// (warn) vs INIs that predate the sentinel (treat as legacy v0 — all
// current keys load with their declared defaults where missing).
// Future migration logic, when it lands, will branch on this value.
constexpr int kSettingsSchemaVersion = 1;

// Sentinel value stored in QSettings [Scrapers/<provider>/<field>] when
// the real credential lives in the platform keychain. On load, finding
// this sentinel triggers a keychain lookup; anything else is treated as
// either an empty/missing credential or a legacy plaintext value
// awaiting migration on the next save.
constexpr const char *kKeychainSentinel = "@keychain";
constexpr const char *kKeychainService = "io.github.EtherAura.Kartend.scrapers";

#ifdef KARTEND_HAVE_QTKEYCHAIN
// Synchronous wrappers around QKeychain's async Job API. Credentials are
// read/written only at settings load (once at startup) and save (when
// the user clicks Save in the Settings dialog), never on a hot path, so
// blocking the calling thread on a local QEventLoop is acceptable. The
// insecureFallback flag is left at its default (false) so a missing
// secret service surfaces as NoBackendAvailable rather than silently
// dropping a plaintext copy into QSettings — settingsmanager handles the
// fallback explicitly so the caller can see the boundary and so the
// migration logic isn't confused by QKeychain doubling up its own
// plaintext copy.
// Cap nested QEventLoop::exec() so an unresponsive secret service daemon
// can't wedge the GUI thread. 5s is generous for a local keychain RPC; the
// startup/save callers degrade to a "keychain unavailable" failure path on
// timeout.
constexpr int kKeychainTimeoutMs = 5000;

// Runs the QKeychain job to completion with a bounded event loop. Returns
// true if the job finished within the timeout, false if the timer fired
// first. Either way the caller still inspects job.error() — on a real
// timeout we synthesize a warning so users see the daemon stall.
bool runKeychainJobBounded(QKeychain::Job &job, const char *opName) {
  QEventLoop loop;
  bool finished = false;
  QObject::connect(&job, &QKeychain::Job::finished, &loop, [&loop, &finished]() {
    finished = true;
    loop.quit();
  });
  job.start();
  // Watchdog: QKeychain has no built-in timeout. If the platform secret
  // service daemon stalls (GNOME Keyring on first-unlock prompt, KWallet
  // not running, etc.), this timer fires and quits the bounded loop so
  // the caller falls back to plaintext storage instead of wedging forever.
  QTimer::singleShot(kKeychainTimeoutMs, &loop, &QEventLoop::quit);
  loop.exec();
  if (!finished) {
    qWarning() << "SettingsManager: keychain" << opName << "timed out after" << kKeychainTimeoutMs
               << "ms";
  }
  return finished;
}

QString syncReadKeychain(const QString &key, bool *ok) {
  const QString service = QLatin1String(kKeychainService);
  QKeychain::ReadPasswordJob job(service);
  job.setAutoDelete(false);
  job.setKey(key);
  if (!runKeychainJobBounded(job, "read")) {
    if (ok) *ok = false;
    return {};
  }
  if (job.error() == QKeychain::NoError) {
    if (ok) *ok = true;
    return job.textData();
  }
  if (ok) *ok = false;
  return {};
}

bool syncWriteKeychain(const QString &key, const QString &value) {
  const QString service = QLatin1String(kKeychainService);
  QKeychain::WritePasswordJob job(service);
  job.setAutoDelete(false);
  job.setKey(key);
  job.setTextData(value);
  if (!runKeychainJobBounded(job, "write")) {
    return false;
  }
  return job.error() == QKeychain::NoError;
}

bool syncDeleteKeychain(const QString &key) {
  const QString service = QLatin1String(kKeychainService);
  QKeychain::DeletePasswordJob job(service);
  job.setAutoDelete(false);
  job.setKey(key);
  if (!runKeychainJobBounded(job, "delete")) {
    return false;
  }
  // EntryNotFound on a delete is expected (key was already gone) — treat
  // as success so a re-save without the key doesn't log a warning.
  return job.error() == QKeychain::NoError || job.error() == QKeychain::EntryNotFound;
}
#endif // KARTEND_HAVE_QTKEYCHAIN
} // namespace

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
  const QString configPath = SettingsUtils::getConfigPath();
  QSettings s(configPath, SettingsUtils::getFormat());

  // QSettings exposes parse errors through status() — a torn/corrupted INI
  // surfaces as FormatError here. Without this check, every s.value() below
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

  s.beginGroup(keys::kGroupGeneral);
  // Pre-flight: detect future-versioned INIs so a stale build doesn't
  // silently drop unknown fields without leaving a breadcrumb. v0
  // (key missing) is the legacy path — load every current key with its
  // declared default. Real migration switches will hang off this in a
  // later schemaVersion bump.
  const int loadedSchemaVersion = s.value(keys::kSchemaVersion, 0).toInt();
  if (loadedSchemaVersion > kSettingsSchemaVersion) {
    qCWarning(lcSettingsManager)
        << "Settings INI was written with schemaVersion" << loadedSchemaVersion
        << "but this build only understands up to" << kSettingsSchemaVersion
        << "— unknown keys will be ignored on load and overwritten on save.";
  }
  settings.rememberSelection = s.value(keys::kRememberSelection, true).toBool();
  settings.wrapNavigation = s.value(keys::kWrapNavigation, false).toBool();
  settings.selectItemOnHover = s.value(keys::kSelectItemOnHover, false).toBool();
  settings.pixmapCacheSizeMB =
      s.value(keys::kPixmapCacheSizeMB, UIConstants::Cache::DEFAULT_PIXMAP_CACHE_MB).toInt();
  settings.pixmapCacheSizeMB =
      qBound(UIConstants::Cache::MIN_PIXMAP_CACHE_MB, settings.pixmapCacheSizeMB,
             UIConstants::Cache::MAX_PIXMAP_CACHE_MB);
  settings.videoThumbnailExtractionTimeoutMs =
      s.value(keys::kVideoThumbnailExtractionTimeoutMs,
              UIConstants::Timing::DEFAULT_VIDEO_THUMBNAIL_TIMEOUT_MS)
          .toInt();
  settings.videoThumbnailExtractionTimeoutMs =
      qBound(UIConstants::Timing::MIN_VIDEO_THUMBNAIL_TIMEOUT_MS,
             settings.videoThumbnailExtractionTimeoutMs,
             UIConstants::Timing::MAX_VIDEO_THUMBNAIL_TIMEOUT_MS);
  // Load timing settings (direct ms/count values)
  settings.keyboardRepeatIntervalMs = s.value(keys::kKeyboardRepeatIntervalMs, 260).toInt();
  settings.keyboardRepeatDelayMs = s.value(keys::kKeyboardRepeatDelayMs, 260).toInt();
  settings.clickHoldDelayMs = s.value(keys::kClickHoldDelayMs, 500).toInt();
  settings.clickHoldRepeatIntervalMs = s.value(keys::kClickHoldRepeatIntervalMs, 320).toInt();
  settings.listKeyboardRepeatIntervalMs = s.value(keys::kListKeyboardRepeatIntervalMs, 50).toInt();
  settings.listClickHoldRepeatIntervalMs =
      s.value(keys::kListClickHoldRepeatIntervalMs, 80).toInt();
  settings.mouseWheelRows = s.value(keys::kMouseWheelRows, 1).toInt();
  settings.scrollAnimationDurationMs = s.value(keys::kScrollAnimationDurationMs, 1500).toInt();
  settings.scrollVelocityMultiplier =
      s.value(keys::kScrollVelocityMultiplier, UIConstants::Scroll::DEFAULT_VELOCITY_MULTIPLIER)
          .toDouble();
  // Bounds keep the multiplier from stalling or saturating the animation
  // pipeline.
  settings.scrollVelocityMultiplier =
      qBound(UIConstants::Scroll::MIN_VELOCITY_MULTIPLIER, settings.scrollVelocityMultiplier,
             UIConstants::Scroll::MAX_VELOCITY_MULTIPLIER);
  // Load text appearance settings
  settings.titleTintSaturation = s.value(keys::kTitleTintSaturation, 180).toInt();
  settings.titleTintLightness = s.value(keys::kTitleTintLightness, 60).toInt();
  settings.titleBaseColor = s.value(keys::kTitleBaseColor, QString()).toString();
  // opt-in title overlay on placeholder art
  settings.showTitleInPlaceholder = s.value(keys::kShowTitleInPlaceholder, false).toBool();

  // global UI font. Empty family / 0 size = platform default.
  // No clamp on point size beyond Qt's own validation; the spinbox in the
  // settings dialog limits user input to a sane range.
  settings.globalUiFontFamily = s.value(keys::kGlobalUiFontFamily, QString()).toString();
  settings.globalUiFontPointSize = s.value(keys::kGlobalUiFontPointSize, 0).toInt();

  // runtime text zoom. Persisted as percent so a hand-edited
  // value reads obviously; bounds keep typography legible and avoid
  // absurdly tiny / huge widget sizes the layout pipeline wasn't built for.
  settings.uiTextZoomPercent = qBound(
      TextZoom::MIN_PERCENT, s.value(keys::kUiTextZoomPercent, TextZoom::DEFAULT_PERCENT).toInt(),
      TextZoom::MAX_PERCENT);

  // preview video volume (0-100). Clamped on read so a
  // hand-edited out-of-range value can't mute audio permanently or push
  // QAudioOutput into undefined territory.
  settings.previewVideoVolume = qBound(0, s.value(keys::kPreviewVideoVolume, 100).toInt(), 100);

  // startup video. Stored even when disabled so the user can
  // keep a path configured and toggle it off temporarily.
  settings.startupVideoEnabled = s.value(keys::kStartupVideoEnabled, false).toBool();
  settings.startupVideoPath = sanitizeLoadedPath(
      s.value(keys::kStartupVideoPath, QString()).toString(), "startupVideoPath");

  // Controls: keyboard bindings
  settings.keyNavLeft = s.value(keys::kKeyNavLeft, static_cast<int>(Qt::Key_Left)).toInt();
  settings.keyNavRight = s.value(keys::kKeyNavRight, static_cast<int>(Qt::Key_Right)).toInt();
  settings.keyNavUp = s.value(keys::kKeyNavUp, static_cast<int>(Qt::Key_Up)).toInt();
  settings.keyNavDown = s.value(keys::kKeyNavDown, static_cast<int>(Qt::Key_Down)).toInt();
  settings.keyConfirm = s.value(keys::kKeyConfirm, static_cast<int>(Qt::Key_Return)).toInt();
  settings.keyBack = s.value(keys::kKeyBack, static_cast<int>(Qt::Key_Escape)).toInt();
  settings.keySearch = s.value(keys::kKeySearch, static_cast<int>(Qt::Key_Slash)).toInt();
  settings.keyAlphabeticBack =
      s.value(keys::kKeyAlphabeticBack, static_cast<int>(Qt::Key_PageUp)).toInt();
  settings.keyAlphabeticForward =
      s.value(keys::kKeyAlphabeticForward, static_cast<int>(Qt::Key_PageDown)).toInt();
  settings.keyJumpFirst = s.value(keys::kKeyJumpFirst, static_cast<int>(Qt::Key_Home)).toInt();
  settings.keyJumpLast = s.value(keys::kKeyJumpLast, static_cast<int>(Qt::Key_End)).toInt();
  // detail-page key (opens DetailPageOverlay).
  settings.keyItemDetails = s.value(keys::kKeyItemDetails, static_cast<int>(Qt::Key_I)).toInt();
  settings.keyHomeView = s.value(keys::kKeyHomeView, 0).toInt();

  // Controls: gamepad bindings
  settings.gamepadUseDpad = s.value(keys::kGamepadUseDpad, true).toBool();
  settings.gamepadUseLeftStick = s.value(keys::kGamepadUseLeftStick, true).toBool();
  settings.gamepadConfirmButton = s.value(keys::kGamepadConfirmButton, QString("A")).toString();
  settings.gamepadBackButton = s.value(keys::kGamepadBackButton, QString("B")).toString();
  settings.gamepadToggleSidebarButton =
      s.value(keys::kGamepadToggleSidebarButton, QString("Y")).toString();

  // artwork-cycle modifier. Coerce hand-edited junk back to Shift
  // so the gesture is always reachable; allow only the single-modifier flags
  // we expose in the settings UI.
  settings.artworkCycleModifier = SettingsHelpers::coerceArtworkCycleModifier(
      s.value(keys::kArtworkCycleModifier, static_cast<int>(Qt::ShiftModifier)).toInt());

  // Sort preferences
  settings.sortMode = SettingsHelpers::coerceSortMode(
      s.value(keys::kSortMode, static_cast<int>(SortMode::NameAscending)).toInt());
  settings.excludeSubfoldersFromSort = s.value(keys::kExcludeSubfoldersFromSort, false).toBool();
  // collection categorization filters. Defaults are "no filter"
  // so an upgrading user sees all subcollections as before until they pick a
  // type or toggle the hide button on the toolbar.
  settings.collectionTypeFilter =
      s.value(keys::kCollectionTypeFilter, QString()).toString().trimmed();
  settings.hideSubcollectionTiles = s.value(keys::kHideSubcollectionTiles, false).toBool();
  settings.listCollectionColumnWidth = s.value(keys::kListCollectionColumnWidth, 150).toInt();
  settings.listArtworkColumnWidth = s.value(keys::kListArtworkColumnWidth, 32).toInt();
  settings.startupCollection = s.value(keys::kStartupCollection, QString()).toString();
  settings.useHomeView = s.value(keys::kUseHomeView, false).toBool();
  settings.homeViewLabel = s.value(keys::kHomeViewLabel, QString()).toString();
  settings.homeViewIcon = s.value(keys::kHomeViewIcon, QString()).toString();
  settings.retroarchConfigPath = s.value(keys::kRetroarchConfigPath, QString()).toString();

  // Attract mode
  settings.attractModeEnabled = s.value(keys::kAttractModeEnabled, false).toBool();
  settings.attractModeIdleTimeoutSec = qBound(
      UIConstants::Attract::MIN_IDLE_TIMEOUT_SEC,
      s.value(keys::kAttractModeIdleTimeoutSec, UIConstants::Attract::DEFAULT_IDLE_TIMEOUT_SEC)
          .toInt(),
      UIConstants::Attract::MAX_IDLE_TIMEOUT_SEC);
  settings.attractModeAutoScrollEnabled =
      s.value(keys::kAttractModeAutoScrollEnabled, true).toBool();
  settings.attractModeScrollSpeed =
      qBound(UIConstants::Attract::MIN_SCROLL_SPEED_PX,
             s.value(keys::kAttractModeScrollSpeed, UIConstants::Attract::DEFAULT_SCROLL_SPEED_PX)
                 .toDouble(),
             UIConstants::Attract::MAX_SCROLL_SPEED_PX);
  settings.attractModeAdvanceSelectionEnabled =
      s.value(keys::kAttractModeAdvanceSelectionEnabled, false).toBool();
  settings.attractModeAdvanceSelectionIntervalSec =
      qBound(UIConstants::Attract::MIN_ADVANCE_INTERVAL_SEC,
             s.value(keys::kAttractModeAdvanceSelectionIntervalSec,
                     UIConstants::Attract::DEFAULT_ADVANCE_INTERVAL_SEC)
                 .toInt(),
             UIConstants::Attract::MAX_ADVANCE_INTERVAL_SEC);
  settings.attractModeAdvanceSelectionRandom =
      s.value(keys::kAttractModeAdvanceSelectionRandom, false).toBool();

  // Marquee / secondary monitor — opt-in. Mode is clamped to the known
  // range so a hand-edited INI with a bogus value doesn't propagate into
  // the runtime (defaults to 0 = item artwork).
  settings.marqueeEnabled = s.value(keys::kMarqueeEnabled, false).toBool();
  settings.marqueeScreenName = s.value(keys::kMarqueeScreenName).toString();
  settings.marqueeMode = qBound(0, s.value(keys::kMarqueeMode, 0).toInt(), 2);

  // Splash screens
  settings.bootSplashEnabled = s.value(keys::kBootSplashEnabled, true).toBool();
  settings.resumeFocusSplashEnabled = s.value(keys::kResumeFocusSplashEnabled, true).toBool();
  settings.bootSplashTitle = s.value(keys::kBootSplashTitle).toString();
  settings.bootSplashSubtitle = s.value(keys::kBootSplashSubtitle).toString();
  settings.resumeFocusSplashTitle = s.value(keys::kResumeFocusSplashTitle).toString();
  settings.resumeFocusSplashSubtitle = s.value(keys::kResumeFocusSplashSubtitle).toString();

  // Runtime detection — opt-in
  settings.runtimeDetectionEnabled = s.value(keys::kRuntimeDetectionEnabled, false).toBool();

  // First-run wizard gate — defaults false so a fresh install gets the
  // wizard on next launch.
  settings.firstRunComplete = s.value(keys::kFirstRunComplete, false).toBool();

  // Launch history. Default is enabled with a 500-row cap so
  // a fresh install starts logging immediately; the user disables in
  // Settings → General. Negative caps land in the file via hand-edit only;
  // qBound clamps them to a sane window so trim never deletes the whole
  // table by accident.
  settings.historyEnabled = s.value(keys::kHistoryEnabled, true).toBool();
  settings.historyMaxEntries = qBound(
      UIConstants::Launch::MIN_HISTORY_MAX_ENTRIES,
      s.value(keys::kHistoryMaxEntries, UIConstants::Launch::DEFAULT_HISTORY_MAX_ENTRIES).toInt(),
      UIConstants::Launch::MAX_HISTORY_MAX_ENTRIES);

  // View-mode toggles. Defaults match the.ui defaults so an
  // upgrading install sees no change until the user toggles F8/F10/F11.
  settings.showMenuBar = s.value(keys::kShowMenuBar, true).toBool();
  settings.showToolbar = s.value(keys::kShowToolbar, true).toBool();
  settings.fullscreen = s.value(keys::kFullscreen, false).toBool();

  // Customizable toolbar. Default visibility is "shown" so an
  // upgrading user sees the toolbar exactly as before; custom text strings
  // default to empty (use the .ui label).
  settings.toolbarShowGridViewButton = s.value(keys::kToolbarShowGridViewButton, true).toBool();
  settings.toolbarShowListViewButton = s.value(keys::kToolbarShowListViewButton, true).toBool();
  settings.toolbarShowCoverFlowViewButton =
      s.value(keys::kToolbarShowCoverFlowViewButton, true).toBool();
  settings.toolbarShowHorizontalViewButton =
      s.value(keys::kToolbarShowHorizontalViewButton, true).toBool();
  settings.toolbarShowHideSubcollectionsButton =
      s.value(keys::kToolbarShowHideSubcollectionsButton, true).toBool();
  settings.toolbarShowTypeFilter = s.value(keys::kToolbarShowTypeFilter, true).toBool();
  settings.toolbarShowTitleFilter = s.value(keys::kToolbarShowTitleFilter, true).toBool();
  settings.toolbarShowSearchModeButton = s.value(keys::kToolbarShowSearchModeButton, true).toBool();
  settings.toolbarShowSearchBar = s.value(keys::kToolbarShowSearchBar, true).toBool();
  settings.toolbarGridViewButtonText =
      s.value(keys::kToolbarGridViewButtonText, QString()).toString();
  settings.toolbarListViewButtonText =
      s.value(keys::kToolbarListViewButtonText, QString()).toString();
  settings.toolbarCoverFlowViewButtonText =
      s.value(keys::kToolbarCoverFlowViewButtonText, QString()).toString();
  settings.toolbarHorizontalViewButtonText =
      s.value(keys::kToolbarHorizontalViewButtonText, QString()).toString();
  settings.toolbarHideSubcollectionsButtonText =
      s.value(keys::kToolbarHideSubcollectionsButtonText, QString()).toString();
  settings.toolbarTitleFilterText = s.value(keys::kToolbarTitleFilterText, QString()).toString();
  s.endGroup();

  // launcher presets live at the top level (outside [General])
  // so they remain a clear, named section the user can hand-edit. Stored as
  // a QSettings array so size is implicit.
  settings.launcherPresets.clear();
  const int presetCount = s.beginReadArray(keys::kGroupLaunchers);
  settings.launcherPresets.reserve(presetCount);
  for (int i = 0; i < presetCount; ++i) {
    s.setArrayIndex(i);
    LauncherPreset preset;
    preset.id = s.value(keys::kId).toString();
    preset.name = s.value(keys::kName).toString();
    preset.launcherPath = sanitizeLoadedPath(s.value(keys::kLauncherPath).toString(),
                                             QString("Launchers[%1].launcherPath").arg(i));
    preset.corePath = sanitizeLoadedPath(s.value(keys::kCorePath).toString(),
                                         QString("Launchers[%1].corePath").arg(i));
    preset.launchParameters = s.value(keys::kLaunchParameters).toString();
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
  //
  // When KARTEND_HAVE_QTKEYCHAIN is defined, the INI value @keychain is
  // a sentinel meaning the real credential lives in the platform secret
  // service; any other non-empty value is either a legacy plaintext
  // credential (will be migrated to the keychain on next save) or
  // came from a build without keychain support.
  settings.scraperCredentials.clear();
  s.beginGroup(keys::kGroupScrapers);
  for (const QString &fullKey : s.allKeys()) {
    const int slash = fullKey.indexOf('/');
    if (slash <= 0 || slash >= fullKey.size() - 1) {
      // Malformed key (no provider prefix or empty field name) — skip
      // rather than poison the credential map.
      continue;
    }
    const QString providerId = fullKey.left(slash);
    const QString fieldName = fullKey.mid(slash + 1);
    QString resolvedValue = s.value(fullKey).toString();
#ifdef KARTEND_HAVE_QTKEYCHAIN
    if (resolvedValue == QLatin1String(kKeychainSentinel)) {
      bool ok = false;
      const QString fromKeychain = syncReadKeychain(fullKey, &ok);
      if (ok) {
        resolvedValue = fromKeychain;
      } else {
        // Keychain backend dropped or the entry was wiped externally —
        // surface as a missing credential rather than handing the
        // sentinel back to the provider as if it were a real password.
        qCWarning(lcSettingsManager)
            << "Scraper credential" << fullKey << "marked @keychain but lookup failed; "
            << "treating as missing. Re-enter in Settings → Scrapers to repopulate.";
        resolvedValue.clear();
      }
    }
#endif
    settings.scraperCredentials[providerId][fieldName] = resolvedValue;
  }
  s.endGroup();

  // Scraper performance + behavior options. Live in a sibling
  // [ScraperOptions] group rather than under [Scrapers] so the
  // credential key-walk above doesn't pick them up as malformed
  // provider/field pairs.
  s.beginGroup(keys::kGroupScraperOptions);
  settings.scraperOptions.preset = static_cast<GeneralSettings::ScraperPreset>(
      s.value(keys::kPreset, static_cast<int>(GeneralSettings::ScraperPreset::Balanced)).toInt());
  settings.scraperOptions.mediaMaxDimension =
      qBound(0, s.value(keys::kMediaMaxDimension, 1024).toInt(), 8192);
  settings.scraperOptions.mediaConcurrency =
      qBound(1, s.value(keys::kMediaConcurrency, 2).toInt(), 16);
  settings.scraperOptions.mediaThrottleMs =
      qBound(0, s.value(keys::kMediaThrottleMs, 100).toInt(), 5000);
  settings.scraperOptions.batchItemConcurrency =
      qBound(1, s.value(keys::kBatchItemConcurrency, 4).toInt(), 16);
  settings.scraperOptions.rescrapeMode = static_cast<GeneralSettings::ScraperRescrapeMode>(
      s.value(keys::kRescrapeMode,
              static_cast<int>(GeneralSettings::ScraperRescrapeMode::FillMissing))
          .toInt());
  // Clamp 0..365 — defensive against hand-edited INIs. 0 disables the
  // time gate (skip every already-scraped item); 365 is a year, which
  // is the longest "refresh window" we expect anyone to want.
  settings.scraperOptions.skipRecentScrapeDays =
      qBound(0, s.value(keys::kSkipRecentScrapeDays, 30).toInt(), 365);
  settings.scraperOptions.preferJpgOutput = s.value(keys::kPreferJpgOutput, false).toBool();
  settings.scraperOptions.scrapeAutoResume = s.value(keys::kScrapeAutoResume, false).toBool();
  settings.scraperOptions.scrapeLogging = s.value(keys::kScrapeLogging, false).toBool();
  settings.scraperOptions.preferredScraperRegion =
      s.value(keys::kPreferredRegion, QStringLiteral("us")).toString().trimmed().toLower();
  s.endGroup();

  settings.lastSelectedItems.clear();
  m_generalSettings = settings;
  // Bring scrape logging into effect for the running process. This is
  // the first settings read at startup, so the kartend.scrape*
  // categories are toggled before any scrape (or resume prompt) runs.
  ScrapeLogger::setEnabled(m_generalSettings.scraperOptions.scrapeLogging);
}

// Saves general settings (no legacy last_selected.dat persistence)
ErrorUtils::Result<void> SettingsManager::saveGeneralSettings(const GeneralSettings &settings) {
  m_generalSettings.rememberSelection = settings.rememberSelection;
  m_generalSettings.wrapNavigation = settings.wrapNavigation;
  m_generalSettings.selectItemOnHover = settings.selectItemOnHover;
  m_generalSettings.pixmapCacheSizeMB =
      qBound(UIConstants::Cache::MIN_PIXMAP_CACHE_MB, settings.pixmapCacheSizeMB,
             UIConstants::Cache::MAX_PIXMAP_CACHE_MB);
  m_generalSettings.videoThumbnailExtractionTimeoutMs =
      qBound(UIConstants::Timing::MIN_VIDEO_THUMBNAIL_TIMEOUT_MS,
             settings.videoThumbnailExtractionTimeoutMs,
             UIConstants::Timing::MAX_VIDEO_THUMBNAIL_TIMEOUT_MS);
  m_generalSettings.keyboardRepeatIntervalMs = settings.keyboardRepeatIntervalMs;
  m_generalSettings.keyboardRepeatDelayMs = settings.keyboardRepeatDelayMs;
  m_generalSettings.clickHoldDelayMs = settings.clickHoldDelayMs;
  m_generalSettings.clickHoldRepeatIntervalMs = settings.clickHoldRepeatIntervalMs;
  m_generalSettings.listKeyboardRepeatIntervalMs = settings.listKeyboardRepeatIntervalMs;
  m_generalSettings.listClickHoldRepeatIntervalMs = settings.listClickHoldRepeatIntervalMs;
  m_generalSettings.mouseWheelRows = settings.mouseWheelRows;
  m_generalSettings.scrollAnimationDurationMs = settings.scrollAnimationDurationMs;
  m_generalSettings.scrollVelocityMultiplier =
      qBound(UIConstants::Scroll::MIN_VELOCITY_MULTIPLIER, settings.scrollVelocityMultiplier,
             UIConstants::Scroll::MAX_VELOCITY_MULTIPLIER);
  m_generalSettings.titleTintSaturation = settings.titleTintSaturation;
  m_generalSettings.titleTintLightness = settings.titleTintLightness;
  m_generalSettings.titleBaseColor = settings.titleBaseColor;

  m_generalSettings.showTitleInPlaceholder = settings.showTitleInPlaceholder;
  // global UI font
  m_generalSettings.globalUiFontFamily = settings.globalUiFontFamily.trimmed();
  m_generalSettings.globalUiFontPointSize = settings.globalUiFontPointSize;
  // runtime text zoom
  m_generalSettings.uiTextZoomPercent =
      qBound(TextZoom::MIN_PERCENT, settings.uiTextZoomPercent, TextZoom::MAX_PERCENT);
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
  m_generalSettings.historyMaxEntries =
      qBound(UIConstants::Launch::MIN_HISTORY_MAX_ENTRIES, settings.historyMaxEntries,
             UIConstants::Launch::MAX_HISTORY_MAX_ENTRIES);
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
  s.beginGroup(keys::kGroupGeneral);
  s.setValue(keys::kRememberSelection, m_generalSettings.rememberSelection);
  s.setValue(keys::kWrapNavigation, m_generalSettings.wrapNavigation);
  s.setValue(keys::kSelectItemOnHover, m_generalSettings.selectItemOnHover);
  s.setValue(keys::kPixmapCacheSizeMB, m_generalSettings.pixmapCacheSizeMB);
  s.setValue(keys::kVideoThumbnailExtractionTimeoutMs,
             m_generalSettings.videoThumbnailExtractionTimeoutMs);
  s.setValue(keys::kKeyboardRepeatIntervalMs, m_generalSettings.keyboardRepeatIntervalMs);
  s.setValue(keys::kKeyboardRepeatDelayMs, m_generalSettings.keyboardRepeatDelayMs);
  s.setValue(keys::kClickHoldDelayMs, m_generalSettings.clickHoldDelayMs);
  s.setValue(keys::kClickHoldRepeatIntervalMs, m_generalSettings.clickHoldRepeatIntervalMs);
  s.setValue(keys::kListKeyboardRepeatIntervalMs, m_generalSettings.listKeyboardRepeatIntervalMs);
  s.setValue(keys::kListClickHoldRepeatIntervalMs, m_generalSettings.listClickHoldRepeatIntervalMs);
  s.setValue(keys::kMouseWheelRows, m_generalSettings.mouseWheelRows);
  s.setValue(keys::kScrollAnimationDurationMs, m_generalSettings.scrollAnimationDurationMs);
  s.setValue(keys::kScrollVelocityMultiplier, m_generalSettings.scrollVelocityMultiplier);
  s.setValue(keys::kTitleTintSaturation, m_generalSettings.titleTintSaturation);
  s.setValue(keys::kTitleTintLightness, m_generalSettings.titleTintLightness);
  s.setValue(keys::kTitleBaseColor, m_generalSettings.titleBaseColor);

  s.setValue(keys::kShowTitleInPlaceholder, m_generalSettings.showTitleInPlaceholder);
  // global UI font
  s.setValue(keys::kGlobalUiFontFamily, m_generalSettings.globalUiFontFamily);
  s.setValue(keys::kGlobalUiFontPointSize, m_generalSettings.globalUiFontPointSize);
  // runtime text zoom
  s.setValue(keys::kUiTextZoomPercent, m_generalSettings.uiTextZoomPercent);
  // preview video volume
  s.setValue(keys::kPreviewVideoVolume, m_generalSettings.previewVideoVolume);
  // startup video
  s.setValue(keys::kStartupVideoEnabled, m_generalSettings.startupVideoEnabled);
  s.setValue(keys::kStartupVideoPath, m_generalSettings.startupVideoPath);
  s.setValue(keys::kKeyNavLeft, m_generalSettings.keyNavLeft);
  s.setValue(keys::kKeyNavRight, m_generalSettings.keyNavRight);
  s.setValue(keys::kKeyNavUp, m_generalSettings.keyNavUp);
  s.setValue(keys::kKeyNavDown, m_generalSettings.keyNavDown);
  s.setValue(keys::kKeyConfirm, m_generalSettings.keyConfirm);
  s.setValue(keys::kKeyBack, m_generalSettings.keyBack);
  s.setValue(keys::kKeySearch, m_generalSettings.keySearch);
  s.setValue(keys::kKeyAlphabeticBack, m_generalSettings.keyAlphabeticBack);
  s.setValue(keys::kKeyAlphabeticForward, m_generalSettings.keyAlphabeticForward);
  s.setValue(keys::kKeyJumpFirst, m_generalSettings.keyJumpFirst);
  s.setValue(keys::kKeyJumpLast, m_generalSettings.keyJumpLast);
  s.setValue(keys::kKeyItemDetails, m_generalSettings.keyItemDetails);
  s.setValue(keys::kKeyHomeView, m_generalSettings.keyHomeView);
  s.setValue(keys::kGamepadUseDpad, m_generalSettings.gamepadUseDpad);
  s.setValue(keys::kGamepadUseLeftStick, m_generalSettings.gamepadUseLeftStick);
  s.setValue(keys::kGamepadConfirmButton, m_generalSettings.gamepadConfirmButton);
  s.setValue(keys::kGamepadBackButton, m_generalSettings.gamepadBackButton);
  s.setValue(keys::kGamepadToggleSidebarButton, m_generalSettings.gamepadToggleSidebarButton);
  s.setValue(keys::kArtworkCycleModifier, m_generalSettings.artworkCycleModifier);
  s.setValue(keys::kSortMode, static_cast<int>(m_generalSettings.sortMode));
  s.setValue(keys::kExcludeSubfoldersFromSort, m_generalSettings.excludeSubfoldersFromSort);
  // collection categorization toolbar state
  s.setValue(keys::kCollectionTypeFilter, m_generalSettings.collectionTypeFilter);
  s.setValue(keys::kHideSubcollectionTiles, m_generalSettings.hideSubcollectionTiles);
  s.setValue(keys::kListCollectionColumnWidth, m_generalSettings.listCollectionColumnWidth);
  s.setValue(keys::kListArtworkColumnWidth, m_generalSettings.listArtworkColumnWidth);
  s.setValue(keys::kStartupCollection, m_generalSettings.startupCollection);
  s.setValue(keys::kUseHomeView, m_generalSettings.useHomeView);
  s.setValue(keys::kHomeViewLabel, m_generalSettings.homeViewLabel);
  s.setValue(keys::kHomeViewIcon, m_generalSettings.homeViewIcon);
  s.setValue(keys::kRetroarchConfigPath, m_generalSettings.retroarchConfigPath);
  s.setValue(keys::kAttractModeEnabled, m_generalSettings.attractModeEnabled);
  s.setValue(keys::kAttractModeIdleTimeoutSec, m_generalSettings.attractModeIdleTimeoutSec);
  s.setValue(keys::kRuntimeDetectionEnabled, m_generalSettings.runtimeDetectionEnabled);
  s.setValue(keys::kFirstRunComplete, m_generalSettings.firstRunComplete);
  s.setValue(keys::kHistoryEnabled, m_generalSettings.historyEnabled);
  s.setValue(keys::kHistoryMaxEntries, m_generalSettings.historyMaxEntries);
  s.setValue(keys::kAttractModeAutoScrollEnabled, m_generalSettings.attractModeAutoScrollEnabled);
  s.setValue(keys::kAttractModeScrollSpeed, m_generalSettings.attractModeScrollSpeed);
  s.setValue(keys::kAttractModeAdvanceSelectionEnabled,
             m_generalSettings.attractModeAdvanceSelectionEnabled);
  s.setValue(keys::kAttractModeAdvanceSelectionIntervalSec,
             m_generalSettings.attractModeAdvanceSelectionIntervalSec);
  s.setValue(keys::kAttractModeAdvanceSelectionRandom,
             m_generalSettings.attractModeAdvanceSelectionRandom);
  // Marquee secondary-monitor display
  s.setValue(keys::kMarqueeEnabled, m_generalSettings.marqueeEnabled);
  s.setValue(keys::kMarqueeScreenName, m_generalSettings.marqueeScreenName);
  s.setValue(keys::kMarqueeMode, m_generalSettings.marqueeMode);
  s.setValue(keys::kBootSplashEnabled, m_generalSettings.bootSplashEnabled);
  s.setValue(keys::kResumeFocusSplashEnabled, m_generalSettings.resumeFocusSplashEnabled);
  s.setValue(keys::kBootSplashTitle, m_generalSettings.bootSplashTitle);
  s.setValue(keys::kBootSplashSubtitle, m_generalSettings.bootSplashSubtitle);
  s.setValue(keys::kResumeFocusSplashTitle, m_generalSettings.resumeFocusSplashTitle);
  s.setValue(keys::kResumeFocusSplashSubtitle, m_generalSettings.resumeFocusSplashSubtitle);
  // View-mode toggles
  s.setValue(keys::kShowMenuBar, m_generalSettings.showMenuBar);
  s.setValue(keys::kShowToolbar, m_generalSettings.showToolbar);
  s.setValue(keys::kFullscreen, m_generalSettings.fullscreen);
  // Customizable toolbar
  s.setValue(keys::kToolbarShowGridViewButton, m_generalSettings.toolbarShowGridViewButton);
  s.setValue(keys::kToolbarShowListViewButton, m_generalSettings.toolbarShowListViewButton);
  s.setValue(keys::kToolbarShowCoverFlowViewButton,
             m_generalSettings.toolbarShowCoverFlowViewButton);
  s.setValue(keys::kToolbarShowHorizontalViewButton,
             m_generalSettings.toolbarShowHorizontalViewButton);
  s.setValue(keys::kToolbarShowHideSubcollectionsButton,
             m_generalSettings.toolbarShowHideSubcollectionsButton);
  s.setValue(keys::kToolbarShowTypeFilter, m_generalSettings.toolbarShowTypeFilter);
  s.setValue(keys::kToolbarShowTitleFilter, m_generalSettings.toolbarShowTitleFilter);
  s.setValue(keys::kToolbarShowSearchModeButton, m_generalSettings.toolbarShowSearchModeButton);
  s.setValue(keys::kToolbarShowSearchBar, m_generalSettings.toolbarShowSearchBar);
  s.setValue(keys::kToolbarGridViewButtonText, m_generalSettings.toolbarGridViewButtonText);
  s.setValue(keys::kToolbarListViewButtonText, m_generalSettings.toolbarListViewButtonText);
  s.setValue(keys::kToolbarCoverFlowViewButtonText,
             m_generalSettings.toolbarCoverFlowViewButtonText);
  s.setValue(keys::kToolbarHorizontalViewButtonText,
             m_generalSettings.toolbarHorizontalViewButtonText);
  s.setValue(keys::kToolbarHideSubcollectionsButtonText,
             m_generalSettings.toolbarHideSubcollectionsButtonText);
  s.setValue(keys::kToolbarTitleFilterText, m_generalSettings.toolbarTitleFilterText);
  s.endGroup();

  // persist launcher presets as a top-level [Launchers] array.
  // beginWriteArray clears any existing entries with the same prefix, so a
  // preset removed via the dialog doesn't linger as a stale row.
  s.beginWriteArray(keys::kGroupLaunchers, m_generalSettings.launcherPresets.size());
  for (int i = 0; i < m_generalSettings.launcherPresets.size(); ++i) {
    s.setArrayIndex(i);
    const LauncherPreset &preset = m_generalSettings.launcherPresets[i];
    s.setValue(keys::kId, preset.id);
    s.setValue(keys::kName, preset.name);
    s.setValue(keys::kLauncherPath, preset.launcherPath);
    s.setValue(keys::kCorePath, preset.corePath);
    s.setValue(keys::kLaunchParameters, preset.launchParameters);
  }
  s.endArray();

  // Persist scraper credentials. Wipe the entire [Scrapers] group
  // first so removing a credential field via the UI actually clears
  // the row from disk (otherwise the next load would resurrect it).
  //
  // With KARTEND_HAVE_QTKEYCHAIN, credential values go to the platform
  // secret service and the INI holds only the @keychain sentinel as a
  // presence marker. When the keychain backend is unavailable (headless
  // Linux without dbus, etc.) we fall back to writing the plaintext
  // value into the INI — same behaviour as a build without keychain
  // support. The pre-wipe snapshot of old INI keys is used to drop
  // keychain entries for credentials that the user removed via the UI.
#ifdef KARTEND_HAVE_QTKEYCHAIN
  QStringList preWipeKeys;
  {
    s.beginGroup(keys::kGroupScrapers);
    preWipeKeys = s.allKeys();
    s.endGroup();
  }
  QSet<QString> retainedKeys;
#endif
  s.remove(keys::kGroupScrapers);
  s.beginGroup(keys::kGroupScrapers);
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
      const QString fullKey = providerId + QLatin1Char('/') + field;
#ifdef KARTEND_HAVE_QTKEYCHAIN
      if (syncWriteKeychain(fullKey, fIt.value())) {
        s.setValue(fullKey, QLatin1String(kKeychainSentinel));
        retainedKeys.insert(fullKey);
      } else {
        // No backend available — fall back to plaintext INI (matches a
        // build without keychain support; the security improvement is
        // best-effort, not load-bearing).
        qCWarning(lcSettingsManager)
            << "Keychain write failed for" << fullKey << "; falling back to plaintext INI";
        s.setValue(fullKey, fIt.value());
      }
#else
      s.setValue(fullKey, fIt.value());
#endif
    }
  }
  s.endGroup();
#ifdef KARTEND_HAVE_QTKEYCHAIN
  // Sweep keychain entries that the user removed in the UI. preWipeKeys
  // is the union of keys present before the wipe (legacy plaintext +
  // existing @keychain sentinels); anything not retained this round
  // gets a delete. EntryNotFound from the delete is fine — it means we
  // already cleaned up on a prior save.
  for (const QString &k : preWipeKeys) {
    if (!retainedKeys.contains(k)) {
      syncDeleteKeychain(k);
    }
  }
#endif

  // Scraper performance + behavior options. Wipe the group first so
  // a "Reset to defaults" round-trip doesn't leave stale custom keys.
  s.remove(keys::kGroupScraperOptions);
  s.beginGroup(keys::kGroupScraperOptions);
  s.setValue(keys::kPreset, static_cast<int>(m_generalSettings.scraperOptions.preset));
  s.setValue(keys::kMediaMaxDimension, m_generalSettings.scraperOptions.mediaMaxDimension);
  s.setValue(keys::kMediaConcurrency, m_generalSettings.scraperOptions.mediaConcurrency);
  s.setValue(keys::kMediaThrottleMs, m_generalSettings.scraperOptions.mediaThrottleMs);
  s.setValue(keys::kBatchItemConcurrency, m_generalSettings.scraperOptions.batchItemConcurrency);
  s.setValue(keys::kRescrapeMode, static_cast<int>(m_generalSettings.scraperOptions.rescrapeMode));
  s.setValue(keys::kSkipRecentScrapeDays, m_generalSettings.scraperOptions.skipRecentScrapeDays);
  s.setValue(keys::kPreferJpgOutput, m_generalSettings.scraperOptions.preferJpgOutput);
  s.setValue(keys::kScrapeAutoResume, m_generalSettings.scraperOptions.scrapeAutoResume);
  s.setValue(keys::kScrapeLogging, m_generalSettings.scraperOptions.scrapeLogging);
  s.setValue(keys::kPreferredRegion, m_generalSettings.scraperOptions.preferredScraperRegion);
  // Stamp the schema sentinel LAST so a torn write doesn't leave a
  // false-positive version marker on a partial-content file. The atomic
  // temp-file + rename mechanism above is the primary protection; this
  // ordering is defence-in-depth for the rare paths that bypass it
  // (hand-edits, external tooling, etc.) — load can refuse to treat a
  // file without the sentinel as v1, and the future migration dispatcher
  // gets a single point to branch on.
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
  // dynamic_cast (not qobject_cast): IMainWindow is a plain abstract base, so
  // it carries no Qt meta-object. parent() is only a MainWindow when one owns
  // this SettingsManager; otherwise this branch is skipped, exactly as the
  // previous qobject_cast<MainWindow*> behaved.
  auto *mainWindow = dynamic_cast<IMainWindow *>(parent());
  ISessionManager *session = m_ctx ? m_ctx->sessionManager() : nullptr;
  if ((mainWindow) && collectionIndex >= 0 && collectionIndex < mainWindow->collections().size()) {
    const QList<CollectionConfig> &mwCollections = mainWindow->collections();
    const CollectionConfig &cfg = mwCollections[collectionIndex];
    const bool subfolderActive = !cfg.folderBrowsing.currentSubfolder.trimmed().isEmpty();
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
