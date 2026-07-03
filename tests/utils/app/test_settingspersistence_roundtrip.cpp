// Field-level save -> load round-trip tests for every per-domain settings
// persistence unit in src/utils/app/collection (Kartend-dwxuc).
//
// Each slot builds its domain struct with EVERY persisted field set to a
// non-default value, saves it through the domain's save() into a fresh INI,
// loads it back through the matching load(), and compares with the struct's
// own operator== (those operators are themselves pinned by
// test_collectionconfigequality.cpp / test_generalsettingsequality.cpp).
//
// What this catches that nothing else did: an asymmetric key typo between
// save() and load() (saved under "fooEnabled", read from "fooEnabld"), a
// default-value drift between the two sides, or a field added to the struct
// + save() but forgotten in load() — all of which silently reset or lose
// user settings on the next restart.
//
// Conventions:
//  - Numeric fields are bumped relative to the struct default (+1 / +0.25)
//    so the values stay inside every load-side qBound() clamp without this
//    file having to mirror the bound constants. The one exception is
//    previewVideoVolume, whose default IS its upper clamp (100), so it is
//    decremented instead.
//  - Bool fields are flipped from their struct default.
//  - The guard QVERIFY in the helper rejects an all-defaults input, so a
//    slot that forgets to set anything fails loudly instead of vacuously
//    passing.
//  - Path sanitizers are passed as identity: the sanitizing behaviour is the
//    caller's policy, not part of the save/load symmetry under test.

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include "collection/appearance_settings_persistence.h"
#include "collection/archiveoptions_persistence.h"
#include "collection/attract_settings_persistence.h"
#include "collection/collectionbackground_persistence.h"
#include "collection/collectionconfig.h"
#include "collection/collectionfilterpreferences_persistence.h"
#include "collection/collectionscalars_persistence.h"
#include "collection/folderbrowsingoptions_persistence.h"
#include "collection/gamepad_settings_persistence.h"
#include "collection/gridlayoutpreferences_persistence.h"
#include "collection/history_settings_persistence.h"
#include "collection/input_settings_persistence.h"
#include "collection/keybinding_settings_persistence.h"
#include "collection/launcher_settings_persistence.h"
#include "collection/launcherprofile_persistence.h"
#include "collection/listviewoptions_persistence.h"
#include "collection/marquee_settings_persistence.h"
#include "collection/media_settings_persistence.h"
#include "collection/runtime_detection_settings_persistence.h"
#include "collection/scraper_settings_persistence.h"
#include "collection/scraperoverrides_persistence.h"
#include "collection/sidebarappearance_persistence.h"
#include "collection/splash_settings_persistence.h"
#include "collection/startup_settings_persistence.h"
#include "collection/toolbar_settings_persistence.h"
#include "collection/view_settings_persistence.h"

namespace {

// Identity path sanitizer — converts to every domain's PathSanitizer alias
// (they are all std::function<QString(const QString &, const QString &)>).
QString identitySanitize(const QString &value, const QString & /*fieldId*/) {
  return value;
}

} // namespace

class TestSettingsPersistenceRoundtrip : public QObject {
  Q_OBJECT

private slots:
  void appearanceSettings();
  void archiveOptions();
  void attractSettings();
  void collectionBackground();
  void collectionFilterPreferences();
  void collectionScalars();
  void folderBrowsingOptions();
  void gamepadSettings();
  void gridLayoutPreferences();
  void historySettings();
  void inputSettings();
  void keybindingSettings();
  void launcherProfile();
  void launcherSettings();
  void listViewOptions();
  void marqueeSettings();
  void mediaSettings();
  void runtimeDetectionSettings();
  void scraperOptions();
  void scraperOverrides();
  void sidebarAppearance();
  void splashSettings();
  void startupSettings();
  void toolbarSettings();
  void viewSettings();

private:
  QString freshIniPath();

  // Save `configured` into a fresh INI via `save`, load it back via `load`,
  // and require the loaded struct to compare equal. The guard check rejects
  // an all-defaults input (which could never detect a dropped field).
  template <typename T, typename SaveFn, typename LoadFn>
  void verifyRoundTrip(const char *domain, const T &configured, SaveFn save, LoadFn load) {
    QVERIFY2(!(configured == T{}),
             qPrintable(QStringLiteral("%1: test input is all-defaults — set every field")
                            .arg(QLatin1String(domain))));
    const QString iniPath = freshIniPath();
    {
      QSettings out(iniPath, QSettings::IniFormat);
      save(out, configured);
      out.sync();
      QCOMPARE(out.status(), QSettings::NoError);
    }
    T loaded;
    QSettings in(iniPath, QSettings::IniFormat);
    load(in, loaded);
    QVERIFY2(loaded == configured,
             qPrintable(QStringLiteral("%1: save->load round trip lost or mutated a field")
                            .arg(QLatin1String(domain))));
  }

  QTemporaryDir m_tmp;
  int m_iniCounter = 0;
};

QString TestSettingsPersistenceRoundtrip::freshIniPath() {
  return m_tmp.filePath(QStringLiteral("roundtrip_%1.ini").arg(++m_iniCounter));
}

void TestSettingsPersistenceRoundtrip::appearanceSettings() {
  AppearanceSettings in;
  in.titleTintSaturation += 1;
  in.titleTintLightness += 1;
  in.titleBaseColor = QStringLiteral("#112233");
  in.globalUiFontFamily = QStringLiteral("Test Sans");
  in.globalUiFontPointSize += 1;
  in.uiTextZoomPercent += 1; // stays inside the TextZoom clamp
  verifyRoundTrip("AppearanceSettings", in, AppearanceSettingsPersistence::save,
                  AppearanceSettingsPersistence::load);
}

void TestSettingsPersistenceRoundtrip::archiveOptions() {
  ArchiveOptions in;
  in.extractArchives = !in.extractArchives;
  in.extractedExtension = QStringLiteral("mp4");
  verifyRoundTrip("ArchiveOptions", in, ArchiveOptionsPersistence::save,
                  ArchiveOptionsPersistence::load);
}

void TestSettingsPersistenceRoundtrip::attractSettings() {
  AttractSettings in;
  in.attractModeEnabled = !in.attractModeEnabled;
  in.attractModeIdleTimeoutSec += 1;
  in.attractModeAutoScrollEnabled = !in.attractModeAutoScrollEnabled;
  in.attractModeScrollSpeed += 0.25; // exactly representable; inside the clamp
  in.attractModeAdvanceSelectionEnabled = !in.attractModeAdvanceSelectionEnabled;
  in.attractModeAdvanceSelectionIntervalSec += 1;
  in.attractModeAdvanceSelectionRandom = !in.attractModeAdvanceSelectionRandom;
  verifyRoundTrip("AttractSettings", in, AttractSettingsPersistence::save,
                  AttractSettingsPersistence::load);
}

void TestSettingsPersistenceRoundtrip::collectionBackground() {
  CollectionBackground in;
  in.backgroundType = BackgroundType::Image;
  in.backgroundColor = QStringLiteral("#0a0a0a");
  in.backgroundImage = QStringLiteral("/tmp/bg.png");
  in.backgroundVideo = QStringLiteral("/tmp/bg.mp4");
  in.primaryColor = QStringLiteral("#111111");
  in.tileColor = QStringLiteral("#222222");
  in.selectionColor = QStringLiteral("#333333");
  in.headerLogoImage = QStringLiteral("/tmp/logo.png");
  in.headerLogoPosition = HeaderLogoPosition::TopLeft;
  in.vignetteEnabled = !in.vignetteEnabled;
  in.vignetteIntensity += 1;
  in.wallpaperParallax = !in.wallpaperParallax;
  in.parallaxStrength += 1;
  in.toolbarBackdropBlur = !in.toolbarBackdropBlur;
  in.backdropBlurRadius += 1;
  verifyRoundTrip(
      "CollectionBackground", in,
      [](QSettings &s, const CollectionBackground &v) {
        CollectionBackgroundPersistence::save(s, v, identitySanitize);
      },
      [](QSettings &s, CollectionBackground &v) {
        CollectionBackgroundPersistence::load(s, v, QStringLiteral("TestCol"), identitySanitize);
      });
}

void TestSettingsPersistenceRoundtrip::collectionFilterPreferences() {
  CollectionFilterPreferences in;
  // Patterns may legitimately contain commas/brackets — the array storage
  // must keep them intact without delimiter escaping.
  in.titleExclusionPatterns =
      QStringList{QStringLiteral("beta, with comma"), QStringLiteral("(sample)[disc 2]")};
  in.titleExclusionEnabled = !in.titleExclusionEnabled;
  verifyRoundTrip("CollectionFilterPreferences", in, CollectionFilterPreferencesPersistence::save,
                  CollectionFilterPreferencesPersistence::load);
}

void TestSettingsPersistenceRoundtrip::collectionScalars() {
  // Only the scalar fields owned by CollectionScalarsPersistence are set;
  // sub-cluster fields (gridLayout, sidebar, …) have their own slots.
  // videoDirectory/manualDirectory stay empty: load() clears them by design
  // (collapsed into the single-root artwork layout). preservedKeys is
  // excluded from CollectionConfig::operator==, so the load-side stash of
  // known keys does not break the comparison.
  CollectionConfig in;
  in.name = QStringLiteral("Round Trip Col");
  in.type = QStringLiteral("video");
  in.additionalParentNames = QStringList{QStringLiteral("Parent A"), QStringLiteral("Parent B")};
  in.mediaDirectory = QStringLiteral("/tmp/media");
  in.artworkDirectory = QStringLiteral("/tmp/art");
  in.placeholderArtwork = QStringLiteral("/tmp/placeholder.png");
  in.expandMode = !in.expandMode;
  in.watchFilesystem = !in.watchFilesystem;
  in.collectionIcon = QStringLiteral("/tmp/icon.png");
  // Already-normalized (bare, lowercase, deduped) so load's normalization
  // pass is the identity and needsRewrite stays false.
  in.extensions = QStringList{QStringLiteral("mkv"), QStringLiteral("mp4")};
  in.customArtworkTypes = QStringList{QStringLiteral("poster"), QStringLiteral("banner")};
  in.showAllSubcollectionItems = !in.showAllSubcollectionItems;
  in.horizontalAlignment = HorizontalAlignment::Left;
  in.viewType = ViewType::List;
  in.hideMissingArtwork = !in.hideMissingArtwork;
  in.hideTitles = !in.hideTitles;
  in.hideSubcollectionTitles = !in.hideSubcollectionTitles;
  in.customFontFamily = QStringLiteral("Test Serif");
  verifyRoundTrip(
      "CollectionScalars", in,
      [](QSettings &s, const CollectionConfig &v) {
        CollectionScalarsPersistence::save(s, v, QStringLiteral("TestCol"), identitySanitize);
      },
      [](QSettings &s, CollectionConfig &v) {
        bool needsRewrite = false;
        CollectionScalarsPersistence::load(s, v, needsRewrite, identitySanitize);
        // Normalized input must not trigger a rewrite — if it does, save()
        // wrote something load() considers non-canonical.
        QVERIFY2(!needsRewrite, "CollectionScalars: clean round trip flagged needsRewrite");
      });
}

void TestSettingsPersistenceRoundtrip::folderBrowsingOptions() {
  FolderBrowsingOptions in;
  in.includeContentSubfolders = !in.includeContentSubfolders;
  in.includeArtworkSubfolders = !in.includeArtworkSubfolders;
  in.showAllSubfolderItems = !in.showAllSubfolderItems;
  in.hideSubfolderTitles = !in.hideSubfolderTitles;
  in.showHiddenFolders = !in.showHiddenFolders;
  // currentSubfolder is documented runtime-only (NOT persisted; reset on
  // collection load) — deliberately left default here.
  verifyRoundTrip("FolderBrowsingOptions", in, FolderBrowsingOptionsPersistence::save,
                  FolderBrowsingOptionsPersistence::load);
}

void TestSettingsPersistenceRoundtrip::gamepadSettings() {
  GamepadSettings in;
  in.gamepadUseDpad = !in.gamepadUseDpad;
  in.gamepadUseLeftStick = !in.gamepadUseLeftStick;
  in.gamepadConfirmButton = QStringLiteral("X");
  in.gamepadBackButton = QStringLiteral("Start");
  in.gamepadToggleSidebarButton = QStringLiteral("L1");
  verifyRoundTrip("GamepadSettings", in, GamepadSettingsPersistence::save,
                  GamepadSettingsPersistence::load);
}

void TestSettingsPersistenceRoundtrip::gridLayoutPreferences() {
  GridLayoutPreferences in;
  in.gridWidth += 1;
  in.horizontalGridHeight += 1;
  in.gridWidthSidebarHidden += 1;
  in.horizontalGridHeightSidebarHidden += 1;
  in.gridHeightSidebarHidden += 1;
  in.horizontalSpacing += 1;
  in.verticalSpacing += 1;
  in.hideHorizontalScrollbar = !in.hideHorizontalScrollbar;
  in.hideVerticalScrollbar = !in.hideVerticalScrollbar;
  in.itemWidth += 1;
  in.itemHeight += 1;
  in.fontSize += 1;
  in.cornerRadius += 1;
  verifyRoundTrip("GridLayoutPreferences", in, GridLayoutPreferencesPersistence::save,
                  GridLayoutPreferencesPersistence::load);
}

void TestSettingsPersistenceRoundtrip::historySettings() {
  HistorySettings in;
  in.historyEnabled = !in.historyEnabled;
  in.historyMaxEntries += 1;
  verifyRoundTrip("HistorySettings", in, HistorySettingsPersistence::save,
                  HistorySettingsPersistence::load);
}

void TestSettingsPersistenceRoundtrip::inputSettings() {
  InputSettings in;
  in.rememberSelection = !in.rememberSelection;
  in.wrapNavigation = !in.wrapNavigation;
  in.selectItemOnHover = !in.selectItemOnHover;
  in.keyboardRepeatIntervalMs += 1;
  in.keyboardRepeatDelayMs += 1;
  in.clickHoldDelayMs += 1;
  in.clickHoldRepeatIntervalMs += 1;
  in.listKeyboardRepeatIntervalMs += 1;
  in.listClickHoldRepeatIntervalMs += 1;
  in.mouseWheelRows += 1;
  in.scrollAnimationDurationMs += 1;
  in.scrollVelocityMultiplier += 0.25; // exactly representable; inside the clamp
  in.artworkCycleModifier = static_cast<int>(Qt::ControlModifier);
  verifyRoundTrip("InputSettings", in, InputSettingsPersistence::save,
                  InputSettingsPersistence::load);
}

void TestSettingsPersistenceRoundtrip::keybindingSettings() {
  KeybindingSettings in;
  in.keyNavLeft += 1;
  in.keyNavRight += 1;
  in.keyNavUp += 1;
  in.keyNavDown += 1;
  in.keyConfirm += 1;
  in.keyBack += 1;
  in.keySearch += 1;
  in.keyAlphabeticBack += 1;
  in.keyAlphabeticForward += 1;
  in.keyJumpFirst += 1;
  in.keyJumpLast += 1;
  in.keyItemDetails += 1;
  in.keyHomeView += 1;
  verifyRoundTrip("KeybindingSettings", in, KeybindingSettingsPersistence::save,
                  KeybindingSettingsPersistence::load);
}

void TestSettingsPersistenceRoundtrip::launcherProfile() {
  LauncherProfile in;
  in.launcherPath = QStringLiteral("/usr/bin/mpv");
  in.corePath = QStringLiteral("/tmp/core.so");
  in.launchParameters = QStringLiteral("--fs {file}");
  in.launcherName = QStringLiteral("Media Player");
  LauncherConfig alt;
  alt.name = QStringLiteral("Alternate Player");
  alt.launcherPath = QStringLiteral("/usr/bin/vlc");
  alt.corePath = QStringLiteral("/tmp/core2.so");
  alt.launchParameters = QStringLiteral("--loop {file}");
  alt.presetId = QStringLiteral("preset-1");
  in.additionalLaunchers.append(alt);
  in.defaultLauncherIndex = 1;
  verifyRoundTrip(
      "LauncherProfile", in,
      [](QSettings &s, const LauncherProfile &v) {
        LauncherProfilePersistence::save(s, v, identitySanitize);
      },
      [](QSettings &s, LauncherProfile &v) {
        bool needsRewrite = false;
        LauncherProfilePersistence::load(s, v, needsRewrite, QStringLiteral("TestCol"),
                                         identitySanitize);
        // A fully-populated additional launcher must not be dropped as an
        // empty slot (Kartend-gjeu cleanup applies only to unlaunchable rows).
        QVERIFY2(!needsRewrite, "LauncherProfile: clean round trip flagged needsRewrite");
      });
}

void TestSettingsPersistenceRoundtrip::launcherSettings() {
  LauncherSettings in;
  in.retroarchConfigPath = QStringLiteral("/tmp/player.cfg");
  LauncherPreset presetA;
  presetA.id = QStringLiteral("preset-a");
  presetA.name = QStringLiteral("Preset A");
  presetA.launcherPath = QStringLiteral("/usr/bin/mpv");
  presetA.corePath = QStringLiteral("/tmp/coreA.so");
  presetA.launchParameters = QStringLiteral("--fs");
  LauncherPreset presetB;
  presetB.id = QStringLiteral("preset-b");
  presetB.name = QStringLiteral("Preset B");
  presetB.launcherPath = QStringLiteral("/usr/bin/vlc");
  presetB.corePath = QStringLiteral("/tmp/coreB.so");
  presetB.launchParameters = QStringLiteral("--loop");
  in.launcherPresets = {presetA, presetB};
  verifyRoundTrip(
      "LauncherSettings", in,
      [](QSettings &s, const LauncherSettings &v) {
        LauncherSettingsPersistence::saveScalars(s, v);
        LauncherSettingsPersistence::savePresets(s, v);
      },
      [](QSettings &s, LauncherSettings &v) {
        LauncherSettingsPersistence::loadScalars(s, v);
        LauncherSettingsPersistence::loadPresets(s, v, identitySanitize);
      });
}

void TestSettingsPersistenceRoundtrip::listViewOptions() {
  ListViewOptions in;
  in.listFontSize += 1;
  in.listRowHeight += 1;
  in.listRowColor = QStringLiteral("#445566");
  in.listAltRowColor = QStringLiteral("#556677");
  verifyRoundTrip("ListViewOptions", in, ListViewOptionsPersistence::save,
                  ListViewOptionsPersistence::load);
}

void TestSettingsPersistenceRoundtrip::marqueeSettings() {
  MarqueeSettings in;
  in.marqueeEnabled = !in.marqueeEnabled;
  in.marqueeScreenName = QStringLiteral("HDMI-A-1");
  in.marqueeMode = 2; // inside the 0..2 clamp, non-default
  verifyRoundTrip("MarqueeSettings", in, MarqueeSettingsPersistence::save,
                  MarqueeSettingsPersistence::load);
}

void TestSettingsPersistenceRoundtrip::mediaSettings() {
  MediaSettings in;
  in.pixmapCacheSizeMB += 1;
  in.artworkDiskCacheBudgetMB += 256; // inside the [256, 32768] clamp, non-default
  in.videoThumbnailExtractionTimeoutMs += 1;
  in.previewVideoVolume -= 1; // the default (100) IS the upper clamp
  verifyRoundTrip("MediaSettings", in, MediaSettingsPersistence::save,
                  MediaSettingsPersistence::load);

  // The 0 = unlimited sentinel must survive the load clamp — it is the one
  // value below MIN_ARTWORK_DISK_CACHE_MB that is NOT snapped up.
  MediaSettings unlimited;
  unlimited.artworkDiskCacheBudgetMB = 0;
  verifyRoundTrip("MediaSettings/unlimitedDiskBudget", unlimited, MediaSettingsPersistence::save,
                  MediaSettingsPersistence::load);
}

void TestSettingsPersistenceRoundtrip::runtimeDetectionSettings() {
  RuntimeDetectionSettings in;
  in.runtimeDetectionEnabled = !in.runtimeDetectionEnabled;
  verifyRoundTrip("RuntimeDetectionSettings", in, RuntimeDetectionSettingsPersistence::save,
                  RuntimeDetectionSettingsPersistence::load);
}

void TestSettingsPersistenceRoundtrip::scraperOptions() {
  ScraperOptions in;
  in.preset = ScraperPreset::Custom;
  in.mediaMaxDimension += 1;
  in.mediaConcurrency += 1;
  in.mediaThrottleMs += 1;
  in.batchItemConcurrency += 1;
  in.rescrapeMode = ScraperRescrapeMode::UpdateChanged;
  in.skipRecentScrapeDays += 1;
  in.preferJpgOutput = !in.preferJpgOutput;
  in.scrapeAutoResume = !in.scrapeAutoResume;
  in.scrapeLogging = !in.scrapeLogging;
  in.preferredScraperRegion = QStringLiteral("eu"); // load lowercases+trims
  in.hashMode = ScraperOptions::ScraperHashMode::SizeGated;
  in.maxHashableSizeMB += 1;
  in.regionSource = ScraperOptions::ScraperRegionSource::FilenameWhenAvailable;
  in.datLibraryPath = QStringLiteral("/library/catalogues"); // load trims
  verifyRoundTrip("ScraperOptions", in, ScraperSettingsPersistence::saveOptions,
                  ScraperSettingsPersistence::loadOptions);
}

void TestSettingsPersistenceRoundtrip::scraperOverrides() {
  ScraperOverrides in;
  in.screenscraperSystemId = 42;
  in.screenscraperHashArchive = !in.screenscraperHashArchive;
  // Comma in an entry exercises the array storage (no delimiter escaping).
  in.datFilePaths = QStringList{QStringLiteral("/tmp/a.dat"), QStringLiteral("/tmp/b, c.dat")};
  in.scraperProviderId = QStringLiteral("screenscraper"); // load trims
  verifyRoundTrip("ScraperOverrides", in, ScraperOverridesPersistence::save,
                  ScraperOverridesPersistence::load);
}

void TestSettingsPersistenceRoundtrip::sidebarAppearance() {
  SidebarAppearance in;
  in.sidebarVisible = !in.sidebarVisible;
  in.sidebarMode = DetailsPaneMode::Expand;
  in.sidebarPosition = DetailsPanePosition::Left;
  in.sidebarBackgroundType = DetailsPaneBackgroundType::Pattern;
  in.sidebarBackgroundColor = QStringLiteral("#101010");
  in.sidebarBackgroundImage = QStringLiteral("/tmp/side.png");
  // sidebarPattern: Crosshatch is the enum's ONLY value, so a non-default
  // pick is impossible; the surrounding fields still cover its key.
  in.sidebarPatternIntensity += 1;
  in.sidebarPatternColor = QStringLiteral("#202020");
  in.sidebarTextColor = QStringLiteral("#303030");
  in.sidebarAccentColor = QStringLiteral("#404040");
  in.sidebarHeaderBgColor = QStringLiteral("#505050");
  in.sidebarSectionBgColor = QStringLiteral("#606060");
  in.sidebarHeaderBgOpacity += 1;
  in.sidebarSectionBgOpacity += 1;
  in.sidebarWidth += 1;
  in.sidebarHeight += 1;
  in.sidebarWidthLocked = !in.sidebarWidthLocked;
  in.sidebarActiveTab = DetailsPaneTab::Collection;
  in.sidebarFontFamily = QStringLiteral("Test Mono");
  in.sidebarFontPointSize += 1;
  verifyRoundTrip(
      "SidebarAppearance", in,
      [](QSettings &s, const SidebarAppearance &v) {
        SidebarAppearancePersistence::save(s, v, identitySanitize);
      },
      [](QSettings &s, SidebarAppearance &v) {
        SidebarAppearancePersistence::load(s, v, QStringLiteral("TestCol"), identitySanitize);
      });
}

void TestSettingsPersistenceRoundtrip::splashSettings() {
  SplashSettings in;
  in.bootSplashEnabled = !in.bootSplashEnabled;
  in.resumeFocusSplashEnabled = !in.resumeFocusSplashEnabled;
  in.bootSplashTitle = QStringLiteral("Boot Title");
  in.bootSplashSubtitle = QStringLiteral("Boot Subtitle");
  in.resumeFocusSplashTitle = QStringLiteral("Resume Title");
  in.resumeFocusSplashSubtitle = QStringLiteral("Resume Subtitle");
  verifyRoundTrip("SplashSettings", in, SplashSettingsPersistence::save,
                  SplashSettingsPersistence::load);
}

void TestSettingsPersistenceRoundtrip::startupSettings() {
  StartupSettings in;
  in.startupCollection = QStringLiteral("Documentaries");
  in.useHomeView = !in.useHomeView;
  in.homeViewLabel = QStringLiteral("Home");
  in.homeViewIcon = QStringLiteral("/tmp/home.png");
  in.firstRunComplete = !in.firstRunComplete;
  in.startupVideoEnabled = !in.startupVideoEnabled;
  in.startupVideoPath = QStringLiteral("/tmp/intro.mp4");
  verifyRoundTrip("StartupSettings", in, StartupSettingsPersistence::save,
                  [](QSettings &s, StartupSettings &v) {
                    StartupSettingsPersistence::load(s, v, identitySanitize);
                  });
}

void TestSettingsPersistenceRoundtrip::toolbarSettings() {
  ToolbarSettings in;
  in.toolbarShowGridViewButton = !in.toolbarShowGridViewButton;
  in.toolbarShowListViewButton = !in.toolbarShowListViewButton;
  in.toolbarShowCoverFlowViewButton = !in.toolbarShowCoverFlowViewButton;
  in.toolbarShowHorizontalViewButton = !in.toolbarShowHorizontalViewButton;
  in.toolbarShowHideSubcollectionsButton = !in.toolbarShowHideSubcollectionsButton;
  in.toolbarShowTypeFilter = !in.toolbarShowTypeFilter;
  in.toolbarShowTitleFilter = !in.toolbarShowTitleFilter;
  in.toolbarShowSearchModeButton = !in.toolbarShowSearchModeButton;
  in.toolbarShowSearchBar = !in.toolbarShowSearchBar;
  in.toolbarGridViewButtonText = QStringLiteral("Grid!");
  in.toolbarListViewButtonText = QStringLiteral("List!");
  in.toolbarCoverFlowViewButtonText = QStringLiteral("Flow!");
  in.toolbarHorizontalViewButtonText = QStringLiteral("Wide!");
  in.toolbarHideSubcollectionsButtonText = QStringLiteral("Hide!");
  in.toolbarTitleFilterText = QStringLiteral("Filter!");
  verifyRoundTrip("ToolbarSettings", in, ToolbarSettingsPersistence::save,
                  ToolbarSettingsPersistence::load);
}

void TestSettingsPersistenceRoundtrip::viewSettings() {
  ViewSettings in;
  in.sortMode = SortMode::DateDescending;
  in.excludeSubfoldersFromSort = !in.excludeSubfoldersFromSort;
  in.listCollectionColumnWidth += 1;
  in.listArtworkColumnWidth += 1;
  in.collectionTypeFilter = QStringLiteral("video"); // load trims
  in.hideSubcollectionTiles = !in.hideSubcollectionTiles;
  in.showTitleInPlaceholder = !in.showTitleInPlaceholder;
  in.showMenuBar = !in.showMenuBar;
  in.showToolbar = !in.showToolbar;
  in.fullscreen = !in.fullscreen;
  verifyRoundTrip("ViewSettings", in, ViewSettingsPersistence::save, ViewSettingsPersistence::load);
}

QTEST_MAIN(TestSettingsPersistenceRoundtrip)
#include "test_settingspersistence_roundtrip.moc"
