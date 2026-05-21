#ifndef KARTEND_UTILS_APP_COLLECTION_GENERALSETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_GENERALSETTINGS_H

// Application-wide settings god-struct extracted from collectionutils.h
// (Kartend-0yz3 step 13). GeneralSettings carries everything the user
// can configure that isn't per-collection: input timing (keyboard
// repeat, click-hold, wheel rows, scroll animation), keybindings,
// gamepad bindings, mouse modifiers, scraper preset + per-asset
// concurrency / throttle / rescrape policy + credentials, attract mode,
// marquee / secondary monitor, splash screens, runtime detection, first-
// run wizard flag, launch history toggle, collection-categorisation
// filters, view-mode toggles (menubar/toolbar/fullscreen), toolbar
// button customisation, the global LauncherPreset registry, and the
// last-selected-item map across collections.
//
// Lives in its own translation-unit-input so settings dialog panels +
// the toolbar / scroll pipeline / attract mode / scraper service can
// take `const GeneralSettings &` without dragging in CollectionContext
// + CollectionHierarchyCache + the rest of the umbrella.

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtCore/Qt>

#include "../collectiontypes.h"
#include "launcherpreset.h"

struct GeneralSettings {
  bool rememberSelection = false;
  bool wrapNavigation = false;
  // When enabled, moving the pointer over a visible item updates the current
  // selection without requiring a click. Kept as a global interaction preference
  // because it changes input semantics application-wide.
  bool selectItemOnHover = false;
  int pixmapCacheSizeMB = 50; // Default 50MB, user configurable
  // Hard cap per video-thumbnail extraction. If the decoder hasn't produced a
  // frame in this window, the request is abandoned and a null pixmap is cached
  // so the queue advances. Tunable for slow systems where the default 4s can
  // miss legitimate videos. Bounds enforced at load/save: 1000-30000 ms.
  int videoThumbnailExtractionTimeoutMs = 4000;
  int keyboardRepeatIntervalMs = 260;     // Grid view keyboard repeat interval in ms
  int keyboardRepeatDelayMs = 260;        // Initial delay before keyboard repeat starts
  int clickHoldDelayMs = 500;             // Click-hold activation delay in ms
  int clickHoldRepeatIntervalMs = 320;    // Grid view interval between click-hold repeat steps
  int listKeyboardRepeatIntervalMs = 50;  // List view keyboard repeat interval in ms
  int listClickHoldRepeatIntervalMs = 80; // List view interval between click-hold repeat steps
  int mouseWheelRows = 1;                 // Rows to scroll per wheel step
  int scrollAnimationDurationMs = 1500;   // Scroll animation duration in ms
  // Global scroll-velocity multiplier applied to both mouse-wheel steps and
  // held-arrow key-repeat cadence. 1.0 = unchanged; >1 faster; <1 slower.
  // Clamped at load/save time to [0.25, 5.0] so the user can't pick values
  // that would stall or saturate the animation pipeline.
  double scrollVelocityMultiplier = 1.0;
  // Text appearance settings
  int titleTintSaturation = 180; // Title text saturation (0-255)
  int titleTintLightness = 60;   // Title text lightness (0-255)
  QString titleBaseColor;        // Base color for title text (empty = use highlight)

  // when an item has no real artwork and falls back to placeholder
  // art (procedural hatch tile or user-supplied placeholder image), draw the
  // item's title text on top of the placeholder. Independent of the existing
  // hideTitles flag — pairs well with hideTitles=on so the title only appears
  // where it adds information (i.e., on tiles that would otherwise be blank).
  bool showTitleInPlaceholder = false;

  // ─────────────────────────────────────────────────────────────────────────
  // Global UI font
  // Applied via QApplication::setFont() after settings load so every widget
  // without an explicit font (menus, dialogs, toolbar, sidebar, item titles
  // that haven't set a per-collection customFontFamily, etc.) inherits it.
  // Empty family / 0 point size means "use the platform default" — that way
  // an upgrading install keeps Qt's chosen UI font until the user picks one.
  // ─────────────────────────────────────────────────────────────────────────
  QString globalUiFontFamily;
  int globalUiFontPointSize = 0;

  // ─────────────────────────────────────────────────────────────────────────
  // Runtime text zoom
  // Application-wide multiplier applied on top of every font size — global UI
  // font, per-collection grid/list/coverflow item titles, and the metadata
  // sidebar. Stored as percent (100 = unscaled). Bound at runtime to
  // Ctrl+= / Ctrl+- / Ctrl+0 (zoom in / out / reset). Clamped at load/save
  // so a hand-edited value can't render text at 0pt or blow it out past 3×.
  // ─────────────────────────────────────────────────────────────────────────
  int uiTextZoomPercent = 100;

  // ─────────────────────────────────────────────────────────────────────────
  // Preview video volume
  // Global volume for sidebar / overlay preview audio, 0–100. Applied to all
  // VideoPreviewWidget instances via the static setGlobalVolume() hook so a
  // single toolbar slider controls every preview surface.
  // ─────────────────────────────────────────────────────────────────────────
  int previewVideoVolume = 100;

  // ─────────────────────────────────────────────────────────────────────────
  // Startup video
  // Optional one-shot intro clip (logo / branding) shown above the main
  // window on launch. Skippable via any key or mouse click. The enable bool
  // is independent of the path so the user can keep a path configured but
  // disable it temporarily without losing the value.
  // ─────────────────────────────────────────────────────────────────────────
  bool startupVideoEnabled = false;
  QString startupVideoPath;

  // ─────────────────────────────────────────────────────────────────────────
  // Controls: Keyboard bindings (single-key, no modifier semantics)
  // Defaults match current hard-coded behavior.
  // ─────────────────────────────────────────────────────────────────────────
  int keyNavLeft = Qt::Key_Left;
  int keyNavRight = Qt::Key_Right;
  int keyNavUp = Qt::Key_Up;
  int keyNavDown = Qt::Key_Down;
  int keyConfirm = Qt::Key_Return; // Return/Enter treated as equivalent
  int keyBack = Qt::Key_Escape;
  int keySearch = Qt::Key_Slash;
  int keyAlphabeticBack = Qt::Key_PageUp;
  int keyAlphabeticForward = Qt::Key_PageDown;
  int keyJumpFirst = Qt::Key_Home;
  int keyJumpLast = Qt::Key_End;
  // opens the dedicated item-detail page (full-window overlay)
  // for the current selection. Default I = "info"; ignored while the search
  // bar has focus so typing "i" in the filter still works.
  int keyItemDetails = Qt::Key_I;
  // Jumps directly to the synthetic Home view from any nesting depth. Default
  // 0 = unbound so an upgrading install picks up no surprise shortcut; only
  // honored when useHomeView is enabled.
  int keyHomeView = 0;

  // ─────────────────────────────────────────────────────────────────────────
  // Controls: Gamepad bindings
  // Button names are symbolic; backends map them as available.
  // ─────────────────────────────────────────────────────────────────────────
  bool gamepadUseDpad = true;
  bool gamepadUseLeftStick = true;
  QString gamepadConfirmButton = "A";
  QString gamepadBackButton = "B";
  QString gamepadToggleSidebarButton = "Y";
  // ─────────────────────────────────────────────────────────────────────────
  // Mouse: artwork-cycle modifier
  // Stored as the integer value of a single Qt::KeyboardModifier flag
  // (Shift / Control / Alt / Meta). Combined with middle-click to cycle the
  // grid widget through the item's available artwork types. Default Shift
  // chosen to leave plain middle-click on its existing media-preview action
  // Loader clamps unrecognized values back to Shift so a
  // hand-edit can't disable the gesture entirely.
  // ─────────────────────────────────────────────────────────────────────────
  int artworkCycleModifier = static_cast<int>(Qt::ShiftModifier);
  SortMode sortMode = SortMode::NameAscending; // Current sort mode
  bool excludeSubfoldersFromSort = false;      // Exclude subfolders/subcollections from sorting
  int listCollectionColumnWidth = 150;         // Collection column width in list view
  int listArtworkColumnWidth = 32;             // Artwork column width in list view
  // Name of the collection to open on startup. When empty (default), the first
  // root-level collection is opened. When set, takes priority over the default
  // root-collection selection. If the named collection no longer exists, falls
  // back to the default behavior.
  QString startupCollection;

  // Use a synthetic "Home" view at startup that shows one tile per root
  // collection. Takes priority over startupCollection when both are set.
  // `Back` from any root-level collection returns to the home view instead
  // of routing to the first root collection.
  bool useHomeView = false;

  // Optional override for the synthetic Home view's title label and toolbar
  // button. Empty homeViewLabel falls back to the localized "Home" string.
  // homeViewIcon is an absolute path; when set, the toolbar Home button uses
  // it instead of the themed icon.
  QString homeViewLabel;
  QString homeViewIcon;

  // Optional override pointing at a RetroArch install so the Core
  // picker can list installed libretro cores. Accepts a retroarch.cfg
  // file or a core directory; empty means probe the standard per-OS
  // retroarch.cfg locations. See RetroArchUtils::resolveCoreDirectory.
  QString retroarchConfigPath;

  // ─────────────────────────────────────────────────────────────────────────
  // Attract mode / autoscroll
  // ─────────────────────────────────────────────────────────────────────────
  bool attractModeEnabled = false;
  int attractModeIdleTimeoutSec = 120;      // Seconds of idle before activation
  bool attractModeAutoScrollEnabled = true; // Sub-toggle: viewport autoscroll
  double attractModeScrollSpeed = 1.0;      // Pixels per tick (0.1-10, sub-pixel via accumulator)
  bool attractModeAdvanceSelectionEnabled = false;
  int attractModeAdvanceSelectionIntervalSec = 5; // Seconds between selection advances
  bool attractModeAdvanceSelectionRandom = false; // Pick random vs. sequential next

  // ─────────────────────────────────────────────────────────────────────────
  // Marquee / secondary monitor display
  // Bartop and arcade-cabinet users on dual-monitor setups can pin a second
  // window to a marquee/topper screen that mirrors the currently-selected
  // item's artwork (mode 0) or the current collection's icon (mode 1). The
  // marquee window is frameless, always-on-top of its own screen, and ignores
  // input focus. When the named QScreen is no longer present (display
  // unplugged after settings were saved) it falls back to the primary screen
  // and logs a warning so the user knows their topper didn't show up.
  // ─────────────────────────────────────────────────────────────────────────
  bool marqueeEnabled = false;
  QString marqueeScreenName; // QScreen::name() (e.g. "HDMI-A-1"); empty = primary screen
  int marqueeMode = 0;       // 0 = selected item's artwork; 1 = current collection's icon

  // Splash screens
  // Empty title/subtitle strings mean "use the built-in default" (app display
  // name for boot title, localized "Welcome back" for resume title, etc.).
  // Custom values are used verbatim — no %1 substitution — so a user who
  // wants the app name in the text needs to type it.
  bool bootSplashEnabled = true;
  bool resumeFocusSplashEnabled = true;
  QString bootSplashTitle;
  QString bootSplashSubtitle;
  QString resumeFocusSplashTitle;
  QString resumeFocusSplashSubtitle;

  // ─────────────────────────────────────────────────────────────────────────
  // Runtime detection
  // When enabled, launched media items are tracked via a non-detached
  // QProcess so the UI can sleep behind a "Now Playing" overlay while the
  // child runs and automatically restore + raise the window when it exits.
  // ─────────────────────────────────────────────────────────────────────────
  bool runtimeDetectionEnabled = false;

  // ─────────────────────────────────────────────────────────────────────────
  // First-run wizard
  // Flipped to true after the wizard either completes or is explicitly
  // skipped, so the wizard never re-prompts on subsequent launches. Users
  // can still re-run it from Help → Setup Wizard… without flipping this
  // back — once a user has seen it, the auto-launch is permanently off.
  // ─────────────────────────────────────────────────────────────────────────
  bool firstRunComplete = false;

  // ─────────────────────────────────────────────────────────────────────────
  // Scraper credentials
  // Per-provider key/value blobs — one inner QHash per provider id
  // (e.g. "tmdb", "screenscraper") keyed on the credential field name
  // ("api_token", "dev_id", "user_password"). Persisted as
  // `[Scrapers]` INI keys of the shape `<provider>/<field>=<value>`.
  // Empty inner hash = "not configured"; the provider is responsible
  // for surfacing a "please set credentials in Settings → Scrapers"
  // error message when its required keys are missing.
  // ─────────────────────────────────────────────────────────────────────────
  QHash<QString, QHash<QString, QString>> scraperCredentials;

  // ─────────────────────────────────────────────────────────────────────────
  // Scraper performance + behavior options
  // Speed/quality presets and re-scrape policy. The four numeric fields
  // are tied to a preset combo (Fastest / Balanced / BestQuality /
  // Custom); switching to Custom unlocks them. RescrapeMode controls
  // how an item already in the DB is treated on a second scrape and
  // applies per-asset (an existing cover skips, a new fanart still
  // downloads). UpdateChanged downloads bytes anyway to compare, so
  // it's intentionally the slowest mode; the UI surfaces this.
  // ─────────────────────────────────────────────────────────────────────────
  enum class ScraperPreset { Fastest = 0, Balanced = 1, BestQuality = 2, Custom = 3 };
  enum class ScraperRescrapeMode {
    Overwrite = 0,     // always replace existing files + rows
    FillMissing = 1,   // only download / write what's not on disk
    UpdateChanged = 2, // download all, compare bytes, write only if different
    Skip = 3,          // skip the whole item if any metadata exists
  };
  struct ScraperOptions {
    ScraperPreset preset = ScraperPreset::Balanced;
    int mediaMaxDimension = 1024; // px; 0 = full resolution
    // Default 2: empirical sweet spot for the SS CDN on typical home
    // connections. Higher values (6-8) caused individual replies to
    // stretch from seconds to minutes as TCP fairness collapsed
    // between competing streams to the same host. With HTTP/2 enabled
    // (httpclient.cpp send()) streams multiplex over one connection,
    // sidestepping that collapse — so users on a healthy network can
    // safely bump this up.
    int mediaConcurrency = 2;  // 1..16
    int mediaThrottleMs = 100; // 0..1000
    // Number of items that can be scraped *in parallel* during a batch
    // scrape. Default 4 matches Skyscraper's `--threads 4`. Each item
    // still pays its own media-concurrency budget, so the total
    // in-flight network requests is `batchItemConcurrency *
    // mediaConcurrency` — keep this product sane for your link.
    // 1 = strictly serial (the legacy behavior).
    int batchItemConcurrency = 4; // 1..16
    ScraperRescrapeMode rescrapeMode = ScraperRescrapeMode::FillMissing;
    // Refresh window (in days) used by the rescrape modes that
    // pre-filter the queue (Skip and FillMissing) to decide whether
    // an already-covered item should be re-scraped this run.
    //   0      = no time gate; skip every covered item (the legacy
    //            behaviour, preserved for users that never want to
    //            refresh).
    //   N > 0  = skip items whose last scrape (item_metadata.updated_at,
    //            or the sidecar JSON's mtime when there is no DB row)
    //            falls within the last N days. Items covered longer
    //            ago become eligible to refresh. Overwrite and
    //            UpdateChanged ignore this — they visit every item by
    //            design.
    // Range clamped 0..365 by SettingsManager. Default 30 — long enough
    // to let users page through a quota-limited backlog over multiple
    // days without re-paying for items just scraped, short enough that
    // a monthly refresh works without manual intervention.
    int skipRecentScrapeDays = 30;
    // Stamp `outputformat=jpg` onto image media URLs so SS re-encodes
    // assets as smaller (lossy) JPGs. Only sensible on the Fastest
    // preset where bandwidth dominates fidelity; the preset toggles
    // this field automatically, but a Custom user can flip it on too.
    bool preferJpgOutput = false;
    // When true, an interrupted scrape (process exit / crash mid-batch
    // with a `pending-scrape.json` snapshot on disk) silently resumes
    // on next launch instead of surfacing the modal Resume / Discard
    // prompt. Default off so first-time users see the prompt and learn
    // the recovery path; power users running unattended overnight
    // batches flip this on so a crash + relaunch self-heals without a
    // dialog blocking the resume. Consumed by MainWindow's startup
    // hook around ScraperService::loadPendingState (Kartend-1uvp).
    bool scrapeAutoResume = false;
    // When true, scrape diagnostic logging is enabled: the kartend.scrape*
    // logging categories are raised to debug+info verbosity and their
    // output is teed to a size-capped `scrape.log` in the app config
    // directory. Default off. A GUI build has no visible stderr, so this
    // is the only way for a user to capture what a scrape did when it
    // misbehaves (a crash, a stuck resume). Consumed by ScrapeLogger,
    // toggled from SettingsManager load/save.
    bool scrapeLogging = false;
    // Fallback region for ScreenScraper's region-keyed fields (title,
    // release date, box art), as an SS region shortname ("us", "eu",
    // "jp", "wor", ...). Each scraped item first honours its own
    // matched-ROM region so a Japanese cart keeps its Japanese title
    // and art; this value only backstops items whose region has no
    // entry. Default "us" preserves the historical behaviour.
    QString preferredScraperRegion = QStringLiteral("us");
  };
  ScraperOptions scraperOptions;

  // ─────────────────────────────────────────────────────────────────────────
  // Launch history
  // Chronological log of every successful launch, persisted in the
  // `launch_history` SQLite table and rendered by the History tab of the
  // Statistics dialog. Disabled flips off both new-row inserts and the
  // dialog's tab so existing rows stop changing but stay viewable until
  // the user clicks "Clear history…". `historyMaxEntries` is the soft cap
  // applied after every insert; values <= 0 mean "unlimited".
  // ─────────────────────────────────────────────────────────────────────────
  bool historyEnabled = true;
  int historyMaxEntries = 500;

  // ─────────────────────────────────────────────────────────────────────────
  // Collection categorization
  // Toolbar-driven filters that hide subcollection tiles when navigating into
  // a parent. `collectionTypeFilter` is a user-visible label that matches
  // CollectionConfig::type (with parent-chain inheritance via
  // CollectionUtils::effectiveCollectionType); empty means "show all types".
  // `hideSubcollectionTiles` collapses every subcollection tile out of the
  // current view regardless of type, leaving only media items.
  // ─────────────────────────────────────────────────────────────────────────
  QString collectionTypeFilter;
  bool hideSubcollectionTiles = false;

  // ─────────────────────────────────────────────────────────────────────────
  // View-mode toggles
  // Persisted state for the View → Show Menu Bar (F10), Show Toolbar (F8),
  // and Fullscreen (F11) actions so the chosen UI chrome survives across
  // launches. Defaults match the original .ui-defined "all visible, not
  // fullscreen" state so a fresh install opens unchanged.
  // ─────────────────────────────────────────────────────────────────────────
  bool showMenuBar = true;
  bool showToolbar = true;
  bool fullscreen = false;

  // ─────────────────────────────────────────────────────────────────────────
  // Customizable toolbar
  // Per-button visibility flags and text overrides for the items-page top bar.
  // Empty *Text strings mean "use the .ui default" so existing installs see no
  // visual change after upgrade. The search-mode button is icon-only and the
  // type-filter combo box has no user-editable text, so only visibility is
  // exposed for those.
  // ─────────────────────────────────────────────────────────────────────────
  bool toolbarShowGridViewButton = true;
  bool toolbarShowListViewButton = true;
  bool toolbarShowCoverFlowViewButton = true;
  bool toolbarShowHorizontalViewButton = true;
  bool toolbarShowHideSubcollectionsButton = true;
  bool toolbarShowTypeFilter = true;
  bool toolbarShowTitleFilter = true;
  bool toolbarShowSearchModeButton = true;
  bool toolbarShowSearchBar = true;
  QString toolbarGridViewButtonText;
  QString toolbarListViewButtonText;
  QString toolbarCoverFlowViewButtonText;
  QString toolbarHorizontalViewButtonText;
  QString toolbarHideSubcollectionsButtonText;
  QString toolbarTitleFilterText;

  // ─────────────────────────────────────────────────────────────────────────
  // Launcher presets
  // Globally-registered, reusable launcher configurations referenced by
  // collection-level launcher entries via LauncherConfig::presetId. Stored
  // in the [Launchers] settings array; managed via the "Launchers" tab in
  // the settings dialog.
  // ─────────────────────────────────────────────────────────────────────────
  QList<LauncherPreset> launcherPresets;

  QHash<int, int> lastSelectedItems;
  GeneralSettings() = default;
};

#endif
