#ifndef KARTEND_SETTINGS_KEYS_H
#define KARTEND_SETTINGS_KEYS_H

// QSettings key and group identifiers. The C++ identifier is what changes;
// the underlying string value mirrors the on-disk INI form exactly. Renaming
// an identifier is a refactor; changing a value here is a wire-format break.

namespace kartend::settings::keys {

// Groups
inline constexpr auto kGroupGeneral = "General";
inline constexpr auto kGroupLaunchers = "Launchers";
inline constexpr auto kGroupScraperOptions = "ScraperOptions";
inline constexpr auto kGroupScrapers = "Scrapers";
inline constexpr auto kGroupSettingsDialog = "SettingsDialog";

// Keys
inline constexpr auto kAdditionalLaunchers = "additionalLaunchers";
inline constexpr auto kAdditionalParents = "additionalParents";
inline constexpr auto kArtworkCycleModifier = "artworkCycleModifier";
inline constexpr auto kArtworkDirectory = "artworkDirectory";
inline constexpr auto kArtworkDiskCacheBudgetMB = "artworkDiskCacheBudgetMB";
inline constexpr auto kAttractModeAdvanceSelectionEnabled = "attractModeAdvanceSelectionEnabled";
inline constexpr auto kAttractModeAdvanceSelectionIntervalSec =
    "attractModeAdvanceSelectionIntervalSec";
inline constexpr auto kAttractModeAdvanceSelectionRandom = "attractModeAdvanceSelectionRandom";
inline constexpr auto kAttractModeAutoScrollEnabled = "attractModeAutoScrollEnabled";
inline constexpr auto kAttractModeEnabled = "attractModeEnabled";
inline constexpr auto kAttractModeIdleTimeoutSec = "attractModeIdleTimeoutSec";
inline constexpr auto kAttractModeScrollSpeed = "attractModeScrollSpeed";
inline constexpr auto kBackdropBlurRadius = "backdropBlurRadius";
inline constexpr auto kBackgroundColor = "backgroundColor";
inline constexpr auto kBackgroundImage = "backgroundImage";
inline constexpr auto kBackgroundType = "backgroundType";
inline constexpr auto kBackgroundVideo = "backgroundVideo";
inline constexpr auto kBatchItemConcurrency = "batchItemConcurrency";
inline constexpr auto kBootSplashEnabled = "bootSplashEnabled";
inline constexpr auto kBootSplashSubtitle = "bootSplashSubtitle";
inline constexpr auto kBootSplashTitle = "bootSplashTitle";
inline constexpr auto kClickHoldDelayMs = "clickHoldDelayMs";
inline constexpr auto kClickHoldRepeatIntervalMs = "clickHoldRepeatIntervalMs";
inline constexpr auto kCollectionIcon = "collectionIcon";
inline constexpr auto kCollectionTreeJustification = "collectionTreeJustification";
inline constexpr auto kCollectionTreeMode = "collectionTreeMode";
inline constexpr auto kCollectionTreePosition = "collectionTreePosition";
inline constexpr auto kCollectionTreeIconSize = "collectionTreeIconSize";
inline constexpr auto kCollectionTreeIconStyle = "collectionTreeIconStyle";
inline constexpr auto kCollectionTreeIconTint = "collectionTreeIconTint";
/// Legacy bool, read once for migration into kCollectionTreeIconDisplay and
/// then no longer written (Kartend-j1mtg). Kept so an existing kartend.cfg
/// still lands on the mode the user had chosen.
inline constexpr auto kCollectionTreeIconsOnly = "collectionTreeIconsOnly";
inline constexpr auto kCollectionTreeIconDisplay = "collectionTreeIconDisplay";
inline constexpr auto kCollectionTreeShowLines = "collectionTreeShowLines";
inline constexpr auto kCollectionTreeScrollClippedLabels = "collectionTreeScrollClippedLabels";
inline constexpr auto kCollectionTreeScrollClippedLabelsOnHover =
    "collectionTreeScrollClippedLabelsOnHover";
inline constexpr auto kCollectionTreeColorizeSelected = "collectionTreeColorizeSelected";
inline constexpr auto kCollectionTreeHideScrollbar = "collectionTreeHideScrollbar";
inline constexpr auto kCollectionTreeVisible = "collectionTreeVisible";
inline constexpr auto kCollectionTreeWidth = "collectionTreeWidth";
inline constexpr auto kCollectionTypeFilter = "collectionTypeFilter";
// Kartend-1kkk2: the RetroArch-sourced system glyph beside the collection
// name. Deliberately NOT prefixed collectionTree* — it is a separate option
// set from the tree's row artwork, and sharing the prefix would imply the two
// migrate and clamp together when they do not.
inline constexpr auto kSystemIconEnabled = "systemIconEnabled";
inline constexpr auto kSystemIconName = "systemIconName";
inline constexpr auto kSystemIconSubject = "systemIconSubject";
inline constexpr auto kSystemIconPack = "systemIconPack";
inline constexpr auto kSystemIconPlacement = "systemIconPlacement";
inline constexpr auto kSystemIconStyle = "systemIconStyle";
inline constexpr auto kSystemIconAutoDetected = "systemIconAutoDetected";
inline constexpr auto kSystemIconUseCollectionArtwork = "systemIconUseCollectionArtwork";
inline constexpr auto kSystemIconSize = "systemIconSize";
inline constexpr auto kCorePath = "corePath";
inline constexpr auto kCornerRadius = "cornerRadius";
// Meta key inside [Scrapers] (no provider/field slash, so the credential
// key-walk skips it). Non-empty = a keychain write failed and at least one
// credential currently sits in the INI as plaintext; the value is the
// human-readable failure reason surfaced by the settings-dialog banner.
// Cleared by the next save whose keychain writes all succeed.
inline constexpr auto kCredentialDemotionReason = "credentialDemotionReason";

// Sentinel value for kCredentialDemotionReason meaning "this BUILD has no
// keychain support at all", as opposed to a runtime keychain failure whose
// value is the human-readable error. The two need different banner wording,
// not one string with a reason interpolated: a runtime failure can recover
// ("they will move back once the keychain is available"), a build without
// QtKeychain never will, and promising recovery there would be a lie
// (Kartend-4ahok). Recognised by ScraperCredentialsPanel; kept beside the key
// it is a value for so the two cannot drift apart in separate headers.
inline constexpr auto kCredentialDemotionNoKeychainBuild = "@no-keychain-build";
inline constexpr auto kCustomArtworkTypes = "customArtworkTypes";
inline constexpr auto kCustomFontFamily = "customFontFamily";
inline constexpr auto kDatFilePath = "datFilePath";
inline constexpr auto kDatFilePaths = "datFilePaths";
inline constexpr auto kDatLibraryPath = "datLibraryPath";
inline constexpr auto kDefaultLauncherIndex = "defaultLauncherIndex";
inline constexpr auto kExcludeSubfoldersFromSort = "excludeSubfoldersFromSort";
inline constexpr auto kExpandMode = "expandMode";
inline constexpr auto kExtensions = "extensions";
inline constexpr auto kExtractArchives = "extractArchives";
inline constexpr auto kExtractedExtension = "extractedExtension";
inline constexpr auto kFirstRunComplete = "firstRunComplete";
inline constexpr auto kFontSize = "fontSize";
inline constexpr auto kFullscreen = "fullscreen";
inline constexpr auto kGamepadBackButton = "gamepadBackButton";
inline constexpr auto kGamepadConfirmButton = "gamepadConfirmButton";
inline constexpr auto kGamepadToggleCollectionTreeButton = "gamepadToggleCollectionTreeButton";
inline constexpr auto kGamepadRightStickSections = "gamepadRightStickSections";
inline constexpr auto kGamepadToggleSidebarButton = "gamepadToggleSidebarButton";
inline constexpr auto kGamepadUseDpad = "gamepadUseDpad";
inline constexpr auto kGamepadUseLeftStick = "gamepadUseLeftStick";
inline constexpr auto kGeometry = "geometry";
inline constexpr auto kGlobalUiFontFamily = "globalUiFontFamily";
inline constexpr auto kGlobalUiFontPointSize = "globalUiFontPointSize";
inline constexpr auto kGridHeightSidebarHidden = "gridHeightSidebarHidden";
inline constexpr auto kGridWidth = "gridWidth";
inline constexpr auto kGridWidthSidebarHidden = "gridWidthSidebarHidden";
inline constexpr auto kHeaderLogoImage = "headerLogoImage";
inline constexpr auto kHeaderLogoPosition = "headerLogoPosition";
inline constexpr auto kHideHorizontalScrollbar = "hideHorizontalScrollbar";
inline constexpr auto kHideMissingArtwork = "hideMissingArtwork";
inline constexpr auto kHideSubcollectionTiles = "hideSubcollectionTiles";
inline constexpr auto kHideSubcollectionTitles = "hideSubcollectionTitles";
inline constexpr auto kHideSubfolderTitles = "hideSubfolderTitles";
inline constexpr auto kHideTitles = "hideTitles";
inline constexpr auto kHideVerticalScrollbar = "hideVerticalScrollbar";
inline constexpr auto kHistoryEnabled = "historyEnabled";
inline constexpr auto kHistoryMaxEntries = "historyMaxEntries";
inline constexpr auto kHomeViewIcon = "homeViewIcon";
inline constexpr auto kHomeViewLabel = "homeViewLabel";
inline constexpr auto kHorizontalAlignment = "horizontalAlignment";
inline constexpr auto kHorizontalGridHeight = "horizontalGridHeight";
inline constexpr auto kHorizontalGridHeightSidebarHidden = "horizontalGridHeightSidebarHidden";
inline constexpr auto kHorizontalSpacing = "horizontalSpacing";
inline constexpr auto kId = "id";
inline constexpr auto kImportScope = "importScope";
inline constexpr auto kImportSource = "importSource";
inline constexpr auto kImportSourceKey = "importSourceKey";
inline constexpr auto kIncludeArtworkSubfolders = "includeArtworkSubfolders";
inline constexpr auto kIncludeContentSubfolders = "includeContentSubfolders";
inline constexpr auto kItemHeight = "itemHeight";
inline constexpr auto kItemWidth = "itemWidth";
inline constexpr auto kKeyAlphabeticBack = "keyAlphabeticBack";
inline constexpr auto kKeyAlphabeticForward = "keyAlphabeticForward";
inline constexpr auto kKeyBack = "keyBack";
inline constexpr auto kKeyConfirm = "keyConfirm";
inline constexpr auto kKeyHomeView = "keyHomeView";
inline constexpr auto kKeyItemDetails = "keyItemDetails";
inline constexpr auto kKeyJumpFirst = "keyJumpFirst";
inline constexpr auto kKeyJumpLast = "keyJumpLast";
inline constexpr auto kKeyToggleCollectionTree = "keyToggleCollectionTree";
inline constexpr auto kKeyNavDown = "keyNavDown";
inline constexpr auto kKeyNavLeft = "keyNavLeft";
inline constexpr auto kKeyNavRight = "keyNavRight";
inline constexpr auto kKeyNavUp = "keyNavUp";
inline constexpr auto kKeySearch = "keySearch";
inline constexpr auto kKeyboardRepeatDelayMs = "keyboardRepeatDelayMs";
inline constexpr auto kKeyboardRepeatIntervalMs = "keyboardRepeatIntervalMs";
inline constexpr auto kLaunchParameters = "launchParameters";
inline constexpr auto kLauncherName = "launcherName";
inline constexpr auto kLauncherPath = "launcherPath";
inline constexpr auto kListAltRowColor = "listAltRowColor";
inline constexpr auto kListArtworkColumnWidth = "listArtworkColumnWidth";
inline constexpr auto kListClickHoldRepeatIntervalMs = "listClickHoldRepeatIntervalMs";
inline constexpr auto kListCollectionColumnWidth = "listCollectionColumnWidth";
inline constexpr auto kListFontSize = "listFontSize";
inline constexpr auto kListKeyboardRepeatIntervalMs = "listKeyboardRepeatIntervalMs";
inline constexpr auto kListRowColor = "listRowColor";
inline constexpr auto kListRowHeight = "listRowHeight";
inline constexpr auto kManualDirectory = "manualDirectory";
inline constexpr auto kMarqueeEnabled = "marqueeEnabled";
inline constexpr auto kMarqueeMode = "marqueeMode";
inline constexpr auto kMarqueeScreenName = "marqueeScreenName";
inline constexpr auto kMediaConcurrency = "mediaConcurrency";
inline constexpr auto kMediaDirectory = "mediaDirectory";
inline constexpr auto kMediaMaxDimension = "mediaMaxDimension";
inline constexpr auto kMediaThrottleMs = "mediaThrottleMs";
inline constexpr auto kMouseWheelRows = "mouseWheelRows";
inline constexpr auto kName = "name";
inline constexpr auto kNavSplitterState = "navSplitterState";
inline constexpr auto kParallaxStrength = "parallaxStrength";
inline constexpr auto kPath = "path";
inline constexpr auto kPattern = "pattern";
inline constexpr auto kPixmapCacheSizeMB = "pixmapCacheSizeMB";
inline constexpr auto kPlaceholderArtwork = "placeholderArtwork";
inline constexpr auto kPreferJpgOutput = "preferJpgOutput";
inline constexpr auto kPreferredRegion = "preferredRegion";
inline constexpr auto kPreset = "preset";
inline constexpr auto kPresetId = "presetId";
inline constexpr auto kPreviewVideoVolume = "previewVideoVolume";
inline constexpr auto kPrimaryColor = "primaryColor";
inline constexpr auto kToolbarColorSource = "toolbarColorSource";
inline constexpr auto kQuarantineDefaultDir = "quarantineDefaultDir";
inline constexpr auto kRememberSelection = "rememberSelection";
inline constexpr auto kRescrapeMode = "rescrapeMode";
inline constexpr auto kResumeFocusSplashEnabled = "resumeFocusSplashEnabled";
inline constexpr auto kResumeFocusSplashSubtitle = "resumeFocusSplashSubtitle";
inline constexpr auto kResumeFocusSplashTitle = "resumeFocusSplashTitle";
inline constexpr auto kRetroarchConfigPath = "retroarchConfigPath";
inline constexpr auto kRuntimeDetectionEnabled = "runtimeDetectionEnabled";
inline constexpr auto kSchemaVersion = "schemaVersion";
inline constexpr auto kScrapeAutoResume = "scrapeAutoResume";
inline constexpr auto kScrapeLogging = "scrapeLogging";
inline constexpr auto kScraperHashMode = "scraperHashMode";
inline constexpr auto kScraperMaxHashableSizeMB = "scraperMaxHashableSizeMB";
inline constexpr auto kScraperProviderId = "scraperProviderId";
inline constexpr auto kScraperRegionSource = "scraperRegionSource";
inline constexpr auto kScreenscraperHashArchive = "screenscraperHashArchive";
inline constexpr auto kScreenscraperSystemId = "screenscraperSystemId";
inline constexpr auto kScrollAnimationDurationMs = "scrollAnimationDurationMs";
inline constexpr auto kScrollVelocityMultiplier = "scrollVelocityMultiplier";
inline constexpr auto kSelectItemOnHover = "selectItemOnHover";
inline constexpr auto kSelectionColor = "selectionColor";
inline constexpr auto kShowAllSubcollectionItems = "showAllSubcollectionItems";
inline constexpr auto kShowAllSubfolderItems = "showAllSubfolderItems";
inline constexpr auto kShowHiddenFolders = "showHiddenFolders";
inline constexpr auto kShowMenuBar = "showMenuBar";
inline constexpr auto kShowTitleInPlaceholder = "showTitleInPlaceholder";
inline constexpr auto kShowToolbar = "showToolbar";
inline constexpr auto kScrollbarsOnHoverOnly = "scrollbarsOnHoverOnly";
inline constexpr auto kShowToolbarBreadcrumbs = "showToolbarBreadcrumbs";
inline constexpr auto kSidebarAccentColor = "sidebarAccentColor";
inline constexpr auto kSidebarActiveTab = "sidebarActiveTab";
inline constexpr auto kSidebarBackgroundColor = "sidebarBackgroundColor";
inline constexpr auto kSidebarBackgroundImage = "sidebarBackgroundImage";
inline constexpr auto kSidebarBackgroundType = "sidebarBackgroundType";
inline constexpr auto kSidebarFontFamily = "sidebarFontFamily";
inline constexpr auto kSidebarFontPointSize = "sidebarFontPointSize";
inline constexpr auto kSidebarHeaderBgColor = "sidebarHeaderBgColor";
inline constexpr auto kSidebarHeaderBgOpacity = "sidebarHeaderBgOpacity";
inline constexpr auto kSidebarHeight = "sidebarHeight";
inline constexpr auto kSidebarHideScrollbar = "sidebarHideScrollbar";
inline constexpr auto kSidebarJustification = "sidebarJustification";
inline constexpr auto kSidebarMode = "sidebarMode";
inline constexpr auto kSidebarPattern = "sidebarPattern";
inline constexpr auto kSidebarPatternColor = "sidebarPatternColor";
inline constexpr auto kSidebarPatternIntensity = "sidebarPatternIntensity";
inline constexpr auto kSidebarPosition = "sidebarPosition";
inline constexpr auto kSidebarSectionBgColor = "sidebarSectionBgColor";
inline constexpr auto kSidebarSectionBgOpacity = "sidebarSectionBgOpacity";
inline constexpr auto kSidebarTextColor = "sidebarTextColor";
inline constexpr auto kSidebarVisible = "sidebarVisible";
inline constexpr auto kSidebarWidth = "sidebarWidth";
inline constexpr auto kSidebarWidthLocked = "sidebarWidthLocked";
inline constexpr auto kSkipRecentScrapeDays = "skipRecentScrapeDays";
inline constexpr auto kFetchCollectionInfoOnCreate = "fetchCollectionInfoOnCreate";
inline constexpr auto kAutoScrapeEntityArtOnCreate = "autoScrapeEntityArtOnCreate";
inline constexpr auto kSortMode = "sortMode";
inline constexpr auto kSplitterState = "splitterState";
inline constexpr auto kStartupCollection = "startupCollection";
inline constexpr auto kStartupVideoEnabled = "startupVideoEnabled";
inline constexpr auto kStartupVideoPath = "startupVideoPath";
inline constexpr auto kTileColor = "tileColor";
inline constexpr auto kTitleBaseColor = "titleBaseColor";
inline constexpr auto kTitleExclusionEnabled = "titleExclusionEnabled";
inline constexpr auto kTitleExclusionPatterns = "titleExclusionPatterns";
inline constexpr auto kTitleTintEnabled = "titleTintEnabled";
inline constexpr auto kTitleTintLightness = "titleTintLightness";
inline constexpr auto kTitleTintSaturation = "titleTintSaturation";
inline constexpr auto kToolbarBackdropBlur = "toolbarBackdropBlur";
inline constexpr auto kToolbarCoverFlowViewButtonText = "toolbarCoverFlowViewButtonText";
inline constexpr auto kToolbarGridViewButtonText = "toolbarGridViewButtonText";
inline constexpr auto kToolbarHideSubcollectionsButtonText = "toolbarHideSubcollectionsButtonText";
inline constexpr auto kToolbarHorizontalViewButtonText = "toolbarHorizontalViewButtonText";
inline constexpr auto kToolbarListViewButtonText = "toolbarListViewButtonText";
inline constexpr auto kToolbarShowCoverFlowViewButton = "toolbarShowCoverFlowViewButton";
inline constexpr auto kToolbarShowGridViewButton = "toolbarShowGridViewButton";
inline constexpr auto kToolbarShowHideSubcollectionsButton = "toolbarShowHideSubcollectionsButton";
inline constexpr auto kToolbarShowHorizontalViewButton = "toolbarShowHorizontalViewButton";
inline constexpr auto kToolbarShowListViewButton = "toolbarShowListViewButton";
inline constexpr auto kToolbarShowSearchBar = "toolbarShowSearchBar";
inline constexpr auto kToolbarShowSearchModeButton = "toolbarShowSearchModeButton";
inline constexpr auto kToolbarShowTitleFilter = "toolbarShowTitleFilter";
inline constexpr auto kToolbarShowTypeFilter = "toolbarShowTypeFilter";
inline constexpr auto kToolbarTitleFilterText = "toolbarTitleFilterText";
inline constexpr auto kType = "type";
inline constexpr auto kUiTextZoomPercent = "uiTextZoomPercent";
inline constexpr auto kUseHomeView = "useHomeView";
inline constexpr auto kVerticalSpacing = "verticalSpacing";
inline constexpr auto kVideoDirectory = "videoDirectory";
inline constexpr auto kVideoThumbnailExtractionTimeoutMs = "videoThumbnailExtractionTimeoutMs";
inline constexpr auto kViewType = "viewType";
inline constexpr auto kVignetteEnabled = "vignetteEnabled";
inline constexpr auto kVignetteIntensity = "vignetteIntensity";
inline constexpr auto kWallpaperParallax = "wallpaperParallax";
inline constexpr auto kWatchFilesystem = "watchFilesystem";
/// Collapse files whose names differ only by a disc marker — "(Disc 1)",
/// "(CD 2)" — into one item backed by a generated .m3u. Off by default so an
/// existing library's tile count and titles never change under the user on
/// upgrade; opt in per collection where the media is actually split.
inline constexpr auto kGroupMultiDisc = "groupMultiDisc";
inline constexpr auto kWrapNavigation = "wrapNavigation";

} // namespace kartend::settings::keys

#endif // KARTEND_SETTINGS_KEYS_H
