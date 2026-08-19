# Configuration Reference

Every INI key Kartend reads, grouped by purpose. The Settings Dialog
covers the same surface; this page is for hand-editing, scripting,
diff-tracking dotfiles, or just orientation.

The configuration file lives at `~/.config/kartend/kartend.cfg` (see
[File Locations](File-Locations.md)). Format is standard INI:

- `[General]` — global settings.
- `[Launchers]` — global launcher-preset registry (QSettings array).
- `[Scrapers]` — per-provider credential blobs (see
  [Keychain](Keychain.md)).
- `[ScraperOptions]` — global scraper performance and behavior options.
- `[Collection Name]` — one section per top-level collection.
- `[Parent > Child]` — subcollections, where `> ` (with space) is the
  literal hierarchy separator.

Restart Kartend after editing the file by hand. Changes through the
Settings Dialog are saved immediately.

> **Path expansion** — paths support `~` for the user home directory
> (only a bare `~` or a leading `~/`; a `~` mid-path is a literal
> character) and `%collection%` for the owning collection's name.
> Expansion happens when the path is *used*, not when it is read — the
> INI keeps whatever you typed. Environment variables like `$HOME` and
> `$XDG_*` are *not* expanded; use `~` or absolute paths.
>
> **Path rejection** — path values are screened on load and on save.
> A value containing a shell metacharacter (semicolon, pipe, backtick,
> dollar, angle bracket), a newline, a NUL byte, a `..` traversal
> segment, or — outside Windows — a backslash is
> refused and read back as empty, with a warning in the log. Ampersands,
> parentheses and brackets are deliberately allowed — they are ordinary
> in real filenames and safe when paths are passed as process arguments
> rather than through a shell. If a path key you hand-edited comes
> back blank, check [Logging & Diagnostics](Logging-and-Diagnostics.md)
> before assuming the key was ignored.

> **Config-only keys** — most keys on this page have a matching
> control in the [Settings Dialog](Settings-Dialog.md), but a handful
> are intentionally hand-edit only. They're flagged inline below
> with **(config-only)**. To change them, edit `kartend.cfg`
> directly and restart Kartend. Today: `hideSubcollectionTiles`,
> `listCollectionColumnWidth`, `listArtworkColumnWidth`.

## Conventions

| Type | Format | Examples |
|------|--------|----------|
| `bool` | `true` / `false` | `rememberSelection=true` |
| `int` | integer | `gridWidth=6` |
| `float` | decimal | `attractModeScrollSpeed=1.5` |
| `path` | filesystem path with optional `~` | `mediaDirectory=~/Videos` |
| `csv` | comma-separated values; spaces around commas are trimmed on read | `extensions=mkv,mp4,webm` |
| `hex` | `#RRGGBB` color | `backgroundColor=#1a1a2e` |
| `enum` | one of a fixed value list (documented inline) | `viewType=list` |
| `int (0–255)` | 8-bit integer (used for alpha and tint) | `sidebarHeaderBgOpacity=200` |

## `[General]` — global settings

### Selection & navigation

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `rememberSelection` | bool | `true` | Persist per-collection selection across sessions. |
| `wrapNavigation` | bool | `false` | Wrap selection at grid edges. |
| `selectItemOnHover` | bool | `false` | Auto-select when the pointer enters a tile. |
| `startupCollection` | string | empty | Open this collection on launch. Empty = first root collection. |
| `useHomeView` | bool | `false` | Open a synthetic Home view at startup that shows one tile per root collection. Only opens at startup when `startupCollection` is empty — a pinned collection wins, so a per-launch override still lands where you asked. `Back` from any root-level collection returns here regardless. See [Shell Collections](Shell-Collections.md#nesting-shells). |
| `homeViewLabel` | string | empty | Override the Home view's title and toolbar-button label. Empty = localized **Home**. |
| `homeViewIcon` | path | empty | Absolute path to a custom Home toolbar-button icon. Empty = themed icon. |

### Performance & caching

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `pixmapCacheSizeMB` | int | `50` | Qt pixmap cache budget (10–500). |
| `artworkDiskCacheBudgetMB` | int | `2048` | Budget for the on-disk decoded-artwork cache. A background walk evicts the oldest-touched entries once the directory exceeds this. `0` = unlimited (eviction disabled); any other value clamps into 256–32768 so a typo can't turn the cache into permanent churn. |
| `scrollAnimationDurationMs` | int | `1500` | Scroll ease duration. |
| `scrollVelocityMultiplier` | float | `1.0` | Global scroll speed multiplier (0.25–5.0). |
| `videoThumbnailExtractionTimeoutMs` | int | `4000` | Hard cap per video-thumbnail extraction in ms (1000–30000). After this window a null pixmap is cached so the queue advances. |

### Keyboard repeat

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `keyboardRepeatIntervalMs` | int | `260` | Key-repeat cadence in grid views. |
| `keyboardRepeatDelayMs` | int | `260` | Initial delay before repeat. |
| `listKeyboardRepeatIntervalMs` | int | `50` | Key-repeat cadence in List view. |
| `clickHoldDelayMs` | int | `500` | Hold duration before click-hold scrolling activates. |
| `clickHoldRepeatIntervalMs` | int | `320` | Hold-scroll cadence in grid views. |
| `listClickHoldRepeatIntervalMs` | int | `80` | Hold-scroll cadence in List view. |
| `mouseWheelRows` | int | `1` | Rows scrolled per wheel tick. |

### Keyboard bindings

All values are `Qt::Key` numeric codes. The Settings Dialog provides a
key-capture widget so you don't need to look these up.

| Key | Default | Description |
|-----|---------|-------------|
| `keyNavLeft` | `Qt::Key_Left` | Move selection left. |
| `keyNavRight` | `Qt::Key_Right` | Move selection right. |
| `keyNavUp` | `Qt::Key_Up` | Move selection up. |
| `keyNavDown` | `Qt::Key_Down` | Move selection down. |
| `keyConfirm` | `Qt::Key_Return` | Launch / enter subcollection. |
| `keyBack` | `Qt::Key_Escape` | Back / close overlay. |
| `keySearch` | `Qt::Key_Slash` | Focus search bar. |
| `keyAlphabeticBack` | `Qt::Key_PageUp` | Jump to previous starting letter. |
| `keyAlphabeticForward` | `Qt::Key_PageDown` | Jump to next starting letter. |
| `keyJumpFirst` | `Qt::Key_Home` | Jump to first item. |
| `keyJumpLast` | `Qt::Key_End` | Jump to last item. |
| `keyItemDetails` | `Qt::Key_I` | Open the full-screen detail page. |
| `keyHomeView` | `0` (unbound) | Jump directly to the Home view from any nesting depth. Only honored when `useHomeView` is enabled. Default unbound so upgrading installs don't pick up a surprise shortcut. |

See [Input & Controls](Input-and-Controls.md#keyboard) for the user-
facing names and rebinding workflow.

### Gamepad

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `gamepadUseDpad` | bool | `true` | Enable D-pad navigation. |
| `gamepadUseLeftStick` | bool | `true` | Enable left-stick navigation. |
| `gamepadConfirmButton` | string | `A` | Confirm / launch button name. |
| `gamepadBackButton` | string | `B` | Back / escape button. |
| `gamepadToggleSidebarButton` | string | `R1` | Toggle the details pane (right sidebar). |
| `gamepadToggleCollectionTreeButton` | string | `L1` | Toggle the collection tree (left sidebar). |
| `gamepadRightStickSections` | bool | `true` | Right-stick flicks move focus between sections. |
| `scrollbarsOnHoverOnly` | bool | `false` | Slim overlay scrollbars that fade in and reserve no space. |
| `toolbarColorSource` | string | `titlebar` | Toolbar fill: `titlebar`, `accent`, `highlight` or `collection`. |
| `collectionTreeColorizeSelected` | bool | `false` | With a monochrome/tinted tree style, show the active collection's logo in colour. |

Button names are the SDL / Qt6::Gamepad standard labels (`A`, `B`, `X`,
`Y`, `LB`, `RB`, etc.).

### Mouse

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `artworkCycleModifier` | int | `33554432` (Shift) | Modifier paired with middle-click to cycle artwork types. Stored as the numeric `Qt::KeyboardModifier` value, not a name: `33554432` Shift, `67108864` Ctrl, `134217728` Alt, `268435456` Meta. Anything else is coerced back to Shift on load, so pick from the Settings Dialog dropdown rather than hand-editing. |

### Sorting

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `sortMode` | int | `0` Name A→Z | Stored as an integer, not a name: `0` Name A→Z · `1` Name Z→A · `2` Collection A→Z · `3` Collection Z→A · `4` Artwork first · `5` Artwork last · `6` Random · `7` Date newest · `8` Date oldest · `9` Size largest · `10` Size smallest. Anything outside 0–10 is coerced back to `0` on load. |
| `excludeSubfoldersFromSort` | bool | `false` | Keep subcollection / virtual-folder tiles at the top regardless of sort. |

### Type filter & subcollection visibility

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `collectionTypeFilter` | string | empty | Limit the visible collections to ones with this `type` value. |
| `hideSubcollectionTiles` | bool | `false` | Hide subcollection tiles, show only direct media items. **(config-only)** |

### View toggles (persistent)

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `showMenuBar` | bool | `true` | Persistent state of `F10`. |
| `showToolbar` | bool | `true` | Persistent state of `F8`. |
| `fullscreen` | bool | `false` | Persistent state of `F11`. |

### Toolbar customization

Visibility and label of each items-page toolbar control. All boolean
visibility keys default to `true`; text overrides default to empty
(meaning "use the .ui default").

| Key | Type | Description |
|-----|------|-------------|
| `toolbarShowGridViewButton` | bool | Show **Grid** view button. |
| `toolbarShowListViewButton` | bool | Show **List** view button. |
| `toolbarShowCoverFlowViewButton` | bool | Show **Cover Flow** button. |
| `toolbarShowHorizontalViewButton` | bool | Show **Horizontal** button. |
| `toolbarShowHideSubcollectionsButton` | bool | Show **Hide Subcollections** toggle. |
| `toolbarShowTypeFilter` | bool | Show **Type Filter** dropdown. |
| `toolbarShowTitleFilter` | bool | Show **Title Filter** entry. |
| `toolbarShowSearchModeButton` | bool | Show search-mode toggle. |
| `toolbarShowSearchBar` | bool | Show the search input. |
| `toolbarGridViewButtonText` | string | Override **Grid** button label. |
| `toolbarListViewButtonText` | string | Override **List** label. |
| `toolbarCoverFlowViewButtonText` | string | Override **Cover Flow** label. |
| `toolbarHorizontalViewButtonText` | string | Override **Horizontal** label. |
| `toolbarHideSubcollectionsButtonText` | string | Override **Hide Subcollections** label. |
| `toolbarTitleFilterText` | string | Override **Filter** label. |

### List view column widths

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `listCollectionColumnWidth` | int | `150` | Width of the collection / icon column. **(config-only)** |
| `listArtworkColumnWidth` | int | `32` | Width of the artwork thumbnail column. **(config-only)** |

### Text, fonts, zoom

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `globalUiFontFamily` | string | empty | Font family for the entire UI. Empty = platform default. |
| `globalUiFontPointSize` | int | `0` | Point size for the entire UI. `0` = platform default. |
| `uiTextZoomPercent` | int | `100` | Text zoom percentage; bound to `Ctrl+=` / `Ctrl+-` / `Ctrl+0`. |
| `titleTintSaturation` | int (0–255) | `180` | Tile-title tint saturation. |
| `titleTintLightness` | int (0–255) | `60` | Tile-title tint lightness. |
| `titleBaseColor` | hex | empty | Tile-title base color. Empty = use selection color. |
| `titleTintEnabled` | bool | see note | Master toggle for tile-title tinting. New installs default to off. On upgrade the default is inferred: if this key has never been written but any of `titleTintSaturation` / `titleTintLightness` / `titleBaseColor` has, the tint stays **on** — flipping a library's titles to plain text on upgrade would be an unannounced appearance change. |
| `showTitleInPlaceholder` | bool | `false` | Overlay item title on placeholder tiles. |

### Splash screens & startup video

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `bootSplashEnabled` | bool | `true` | Show splash overlay on launch. |
| `bootSplashTitle` | string | empty | Override the boot-splash title. Empty = app display name. Used verbatim — no `%1` substitution. |
| `bootSplashSubtitle` | string | empty | Override the boot-splash subtitle. Empty = built-in default. |
| `resumeFocusSplashEnabled` | bool | `true` | Show splash when window regains focus after a launched item exits. |
| `resumeFocusSplashTitle` | string | empty | Override the resume-focus splash title. Empty = localized **Welcome back**. |
| `resumeFocusSplashSubtitle` | string | empty | Override the resume-focus splash subtitle. Empty = built-in default. |
| `startupVideoEnabled` | bool | `false` | Play an intro video on launch. |
| `startupVideoPath` | path | empty | Path to startup video. |

### Preview video

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `previewVideoVolume` | int (0–100) | `100` | Volume for sidebar / overlay video previews. |

### Runtime detection & history

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `runtimeDetectionEnabled` | bool | `false` | Track when launched items run; show **Now Playing** overlay. See [Splash & Now Playing](Splash-and-Now-Playing.md). |
| `historyEnabled` | bool | `true` | Record launch history. |
| `historyMaxEntries` | int | `500` | Cap on retained history rows; the oldest are trimmed past it. Clamped to 10–50000 on load, so there is no "unlimited" value — set it high rather than to `0`. |

### Marquee / secondary display

For dual-monitor setups (typically arcade-cabinet toppers). The marquee
window is frameless, always-on-top, and ignores input focus. If the
named `QScreen` disappears between sessions, the window falls back to
the primary screen and logs a warning.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `marqueeEnabled` | bool | `false` | Master toggle for the secondary marquee window. |
| `marqueeScreenName` | string | empty | `QScreen::name()` of the target screen (e.g. `HDMI-A-1`). Empty = primary screen. |
| `marqueeMode` | enum | `0` | `0` = selected item's artwork; `1` = current collection's icon; `2` = video / attract loop (item's preview video, falling back to the collection's `backgroundVideo`). See [Marquee](Marquee.md). |

### RetroArch integration

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `retroarchConfigPath` | path | empty | Override pointing at a RetroArch install so the Core picker can list installed libretro cores. Accepts a `retroarch.cfg` file or a core directory. Empty = probe standard per-OS locations. |

### First-run wizard

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `firstRunComplete` | bool | `false` | Auto-set after the wizard completes or is skipped. Re-running it from **Help → Setup Wizard…** does *not* flip this back; the auto-launch is permanently off once set. |

### Attract mode

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `attractModeEnabled` | bool | `false` | Master toggle. See [Attract Mode](Attract-Mode.md). |
| `attractModeIdleTimeoutSec` | int | `120` | Seconds of inactivity before attract starts. |
| `attractModeAutoScrollEnabled` | bool | `true` | Smoothly scroll the viewport during attract. |
| `attractModeScrollSpeed` | float | `1.0` | Pixels-per-tick scroll speed (0.1–10). |
| `attractModeAdvanceSelectionEnabled` | bool | `false` | Periodically move the selection. |
| `attractModeAdvanceSelectionIntervalSec` | int | `5` | Seconds between selection advances. |
| `attractModeAdvanceSelectionRandom` | bool | `false` | Random vs. sequential advance. |

### Launcher presets

Reusable launcher configs are **not** `[General]` keys — they live in their
own top-level `[Launchers]` array group, documented
[below](#launchers--global-launcher-preset-registry).

### Session state

Session state — which item was selected last in each collection, so
`rememberSelection` has something to restore — is **not** in
`kartend.cfg`. It lives in `~/.cache/kartend/metadata/session.json`, and
is deliberately outside the config file: it's regenerable, changes on
every navigation, and would otherwise churn a dotfile you're
diff-tracking. Deleting it loses nothing but the remembered positions.
See [File Locations](File-Locations.md).

## `[Scrapers]` — credential storage

Per-provider credentials, one INI key per field. Keys take the form
`<providerId>/<fieldName>=<value>`, e.g.:

```ini
[Scrapers]
tmdb/api_token=…
screenscraper/dev_id=…
screenscraper/user_password=@keychain
```

The `@keychain` sentinel means the real value is stored in the OS
keychain (QtKeychain build) rather than in plaintext. See
[Keychain](Keychain.md) for the storage model, fallback behavior, and
per-platform install notes.

## `[ScraperOptions]` — global scraper performance & behavior

Speed/quality preset, per-asset concurrency, throttling, and re-scrape
policy. Lives in a sibling INI group rather than under `[Scrapers]`
so the credentials walker doesn't trip on these keys. Tied to a UI
preset combo in **Settings → Scrapers** (Fastest / Balanced / Best
Quality / Custom); switching to Custom unlocks the numeric fields.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `preset` | enum | `1` Balanced | `0` Fastest / `1` Balanced / `2` Best Quality / `3` Custom. |
| `mediaConcurrency` | int | `2` | Parallel downloads per item (1–16). With HTTP/2 enabled, healthy networks can bump this up. |
| `mediaMaxDimension` | int | `1024` | Max image pixel dimension (0–8192; 0 = full resolution). |
| `mediaThrottleMs` | int | `100` | Delay between requests in ms (0–5000). |
| `batchItemConcurrency` | int | `4` | Items scraped in parallel during a batch (1–16). Total in-flight requests = `batchItemConcurrency × mediaConcurrency` — keep the product sane. `1` = strictly serial. |
| `rescrapeMode` | enum | `1` FillMissing | `0` Overwrite (always replace) / `1` FillMissing (only download what's missing) / `2` UpdateChanged (compare bytes, write if different — slowest) / `3` Skip (skip items already in DB). |
| `skipRecentScrapeDays` | int | `30` | Refresh window in days used by FillMissing and Skip (0–365). `0` = no time gate (legacy behavior); `N` = re-scrape items last covered more than N days ago. Overwrite and UpdateChanged ignore this. |
| `preferJpgOutput` | bool | `false` | Ask ScreenScraper to re-encode assets as JPGs. Only sensible on Fastest where bandwidth dominates fidelity. |
| `scrapeAutoResume` | bool | `false` | Silently resume an interrupted batch on next launch instead of showing the Resume / Discard prompt. Off by default so first-time users see the prompt and learn the recovery path. |
| `scrapeLogging` | bool | `false` | Raise the `kartend.scrape*` logging categories to debug+info and tee output to a size-capped `scrape.log` in the config directory. The only way to capture scrape diagnostics from a GUI build. |
| `preferredRegion` | string | `us` | Fallback ScreenScraper region (`us` / `eu` / `jp` / `wor` / …). Items still honor their own matched-ROM region first; this only backstops items with no region entry. |
| `scraperHashMode` | enum | `0` Always | When to hash a file for content-based identification. `0` Always / `1` SizeGated (hash only files at or under `scraperMaxHashableSizeMB`; larger ones fall straight through to filename matching) / `2` Never (filename-only — cheapest, for libraries whose filenames you trust). Clamped to 0–2. |
| `scraperMaxHashableSizeMB` | int | `4096` | Size gate used by `scraperHashMode=1` (1–65536). The default covers most disc images in an archive while excluding ones that would never extract inside a sane timeout. |
| `scraperRegionSource` | enum | `0` TrustScraperFirst | How an item's region is decided. `0` TrustScraperFirst (the provider's matched-file region wins; a filename tag such as `(Japan)` is the fallback only when hashing didn't narrow the candidate) / `1` FilenameWhenAvailable (the filename tag always preempts the provider) / `2` ScraperOnly (never read the filename). Clamped to 0–2. |
| `datLibraryPath` | path | empty | Folder of catalogue (DAT) files probed at startup to *propose* collection matches. Proposals are confirm-only — nothing is applied without you accepting it. Empty = feature off. See [DAT Audit](DAT-Audit.md). |
| `quarantineDefaultDir` | path | empty | Prefills the quarantine-folder field in the DAT auditor's Fix dialog when the audited collection has no per-run value. |

## `[Launchers]` — global launcher-preset registry

QSettings array of reusable launcher configs (id / name / path / core /
parameters). Managed from **Settings → Launchers**, referenced from
per-collection `additionalLaunchers` entries by `presetId`.

## `[<Collection Name>]` — per-collection settings

Every collection has its own section. The section header is the
display name; renaming a collection rewrites the section header.

### Identity & paths

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `name` | string | section header | Display name. |
| `type` | string | empty | Media-type tag — a preset (Video / Audio / Images / Documents / Games) or a custom value. Used by the [type filter](Search-Sort-Filter.md#type-filter) and to pick a scraper. |
| `scraperProviderId` | string | empty | Pinned metadata scraper id (`tmdb`, `screenscraper`, `musicbrainz`, `openlibrary`). Empty = resolve automatically from `type`. |
| `mediaDirectory` | path | empty | Folder of items. Empty = parent-only. |
| `artworkDirectory` | path | empty | Root of the media-asset tree — cover images plus the `video/` and `manual/` subfolders (see [Artwork](Artwork.md)). |
| `extensions` | csv | empty | File extensions to scan. Empty = all. |
| `collectionIcon` | path | empty | Subcollection tile icon. |
| `placeholderArtwork` | path | empty | Custom placeholder image. |
| `watchFilesystem` | bool | `false` | Watch this collection's media folder and rescan when files change, instead of only on demand. |
| `importSource` | string | empty | [Launcher-import](Launcher-Import.md) marker (`steam` / `flatpak` / `lutris`). Non-empty = this collection's media folder holds `.kartlink` stubs Kartend re-syncs from that launcher at startup and on demand. Set by the import flow; not normally hand-edited. |
| `importScope` | string | empty | Which slice of the source launcher's library the import covered. Written by the import flow alongside `importSource`. |
| `importSourceKey` | string | empty | Identifies the specific source instance (e.g. which Steam library or Flatpak installation) so re-sync targets the right one. Written by the import flow. |

> **Retired keys** — `videoDirectory` and `manualDirectory` were separate
> folders once. They were folded into a single asset root: preview videos
> now live under `<artworkDirectory>/video/` and manuals under
> `<artworkDirectory>/manual/`. Kartend no longer *reads* either key, and
> actively removes them from the INI on the next save, so setting one by
> hand does nothing and does not survive. Move the files instead.

### Hierarchy

The primary parent / child relationship is encoded in the **section
header** itself — `[Parent > Child]` with `> ` (space-padded) as the
separator. The in-memory fields `parentCollectionIndex` and
`isSubcollection` you may see in source code are derived from the
section structure at load time; they are *not* INI keys and writing
them by hand has no effect.

The only persisted hierarchy key is `additionalParents`, used for
secondary linked parents:

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `additionalParents` | array | empty | Linked secondary parents — persisted as a QSettings array (`additionalParents\1\name=…`). See [Collections → Linked Parents](Collections.md#linked-parents-alias-parents). |

### Launcher

See [Launchers](Launchers.md) for the complete model.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `launcherPath` | path | empty | Primary launcher executable. |
| `launcherName` | string | empty | Display name. Empty = filename of `launcherPath`. |
| `corePath` | path | empty | LibRetro core (RetroArch only). |
| `launchParameters` | string | empty | Extra arguments passed before the file path. |
| `additionalLaunchers` | array | empty | Secondary launchers, persisted as a QSettings array (`additionalLaunchers\1\name=…`) with `name` / `launcherPath` / `corePath` / `launchParameters` / `presetId` per entry. An entry with neither a `launcherPath` nor a `presetId` can't launch anything, so it is dropped on load and the INI rewritten. |
| `defaultLauncherIndex` | int | `0` | Default selection index (0 = primary). Clamped to the number of launchers actually present, so a stale index from a hand-edit can't point past the end. |

### Scraper overrides

Pinning a scraper for this collection, overriding ScreenScraper's
system id, and pointing at DAT files for offline ROM ID all live
here. See [Scraper](Scraper.md) for the workflow side.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `screenscraperSystemId` | int | `-1` | ScreenScraper.fr `systemeid` override. `-1` = unset (provider autodetects). |
| `screenscraperHashArchive` | bool | `true` | When the scraped item is an archive (`.zip` / `.7z`), extract and hash the inner file rather than the outer archive bytes. |
| `datFilePaths` | array | empty | List of DAT-file paths (No-Intro / Redump / TOSEC Logiqx, or MAME listxml) used for offline ROM ID. Walked in order — first hash hit wins. Persisted as a QSettings array (`datFilePaths\1\path=…`). |
| `datFilePath` | path | empty | **Legacy single-path key**, retained for reading old configs. New writes go to `datFilePaths`. |

### Content / scanning

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `includeContentSubfolders` | bool | `false` | Show subfolders as virtual-folder tiles. |
| `includeArtworkSubfolders` | bool | `false` | Mirror content subfolders into the artwork folder when matching artwork. |
| `showAllSubfolderItems` | bool | `false` | Mix subfolder items into the parent grid. |
| `showAllSubcollectionItems` | bool | `false` | Mix descendant collection items into this grid. |
| `showHiddenFolders` | bool | `false` | Include dot-prefixed folders. |
| `extractArchives` | bool | `false` | Auto-extract archives before launch. |
| `extractedExtension` | string | empty | Which extension to launch from inside an archive. |
| `groupMultiDisc` | bool | `false` | Collapse files that differ only by a disc marker — `(Disc 1)`, `(CD 2)`, `[Side A]` — into one item backed by a playlist generated under Kartend's data directory. See [Multi-disc grouping](Collections.md#multi-disc-grouping). |

### Grid layout

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `viewType` | enum | `grid` | `grid` / `list` / `coverflow` / `horizontal`. |
| `gridWidth` | int | `4` | Items per row in Grid / List views. |
| `gridWidthSidebarHidden` | int | `0` | Override `gridWidth` when the **Expand** sidebar is hidden. `0` = no override; follow `gridWidth`. |
| `horizontalGridHeight` | int | `0` | Items per column in Horizontal view. `0` = fall back to `gridWidth`. |
| `horizontalGridHeightSidebarHidden` | int | `0` | Override for Horizontal when sidebar hidden. `0` = no override. |
| `gridHeightSidebarHidden` | int | `0` | Override when **Expand** sidebar is docked top/bottom. `0` = no override. |
| `itemWidth` | int | `220` | Tile width (pixels). |
| `itemHeight` | int | `245` | Tile height (pixels). |
| `fontSize` | int | `8` | Tile-title font size (points). |
| `cornerRadius` | int | `0` | Tile rounded-corner radius. `0` = square corners. |
| `horizontalSpacing` | int | `20` | Pixel gap between columns. |
| `verticalSpacing` | int | `20` | Pixel gap between rows. |
| `horizontalAlignment` | enum | `center` | `left` / `center` / `right`. |
| `hideTitles` | bool | `false` | Hide tile titles. |
| `hideSubcollectionTitles` | bool | `false` | Hide titles on subcollection tiles. |
| `hideSubfolderTitles` | bool | `false` | Hide titles on virtual-folder tiles. |
| `hideMissingArtwork` | bool | `false` | Hide items with no artwork. |
| `hideHorizontalScrollbar` | bool | `false` | Hide horizontal scrollbar. |
| `hideVerticalScrollbar` | bool | `false` | Hide vertical scrollbar. |

### List view

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `listFontSize` | int | `8` | Row text size (points). |
| `listRowHeight` | int | `32` | Row height in pixels. |
| `listRowColor` | hex | derived | Row background. |
| `listAltRowColor` | hex | derived | Alternating row background. |

### Background & visual effects

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `backgroundType` | enum | `color` | `color` / `image` / `video`. |
| `backgroundColor` | hex | empty | Solid background color. Empty = no override; the viewport keeps the system/theme background. |
| `backgroundImage` | path | empty | Wallpaper image. |
| `backgroundVideo` | path | empty | Looping muted video wallpaper. |
| `primaryColor` | hex | derived | Toolbar / menu bar / chrome color. |
| `tileColor` | hex | derived | Item tile / placeholder color. |
| `selectionColor` | hex | derived | Selection rectangle color. |
| `vignetteEnabled` | bool | `false` | Darken the viewport corners. |
| `vignetteIntensity` | int (0–100) | `60` | Vignette strength. `0` = no effect, `100` = pitch black at the corners. |
| `wallpaperParallax` | bool | `false` | Background image scrolls slower than items. |
| `parallaxStrength` | int (0–100) | `30` | Parallax scrolling factor. |
| `toolbarBackdropBlur` | bool | `false` | Blur the toolbar background. |
| `backdropBlurRadius` | int | `12` | Blur radius (pixels). |

### Title cleanup

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `titleExclusionPatterns` | array | empty | Regex patterns stripped from displayed titles. Persisted as a QSettings array (`titleExclusionPatterns\1\pattern=…`), *not* a comma-separated list — a regex is full of commas, brackets and backslashes, and a delimited list would need escaping rules nobody would get right by hand. |
| `titleExclusionEnabled` | bool | `true` | Toggle the pattern list without losing it. Defaults on, so a pattern you add applies immediately rather than silently doing nothing. |

### Header logo

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `headerLogoImage` | path | empty | Logo image painted at the top of the grid. |
| `headerLogoPosition` | enum | `topcenter` | `topleft` / `topcenter` / `topright`. An unrecognized value falls back to `topcenter` and logs a warning. |

### Sidebar (per-collection styling)

See [Sidebar & Details Pane](Sidebar-and-Details-Pane.md) for behavior.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `sidebarVisible` | bool | `false` | Sidebar shown by default. |
| `sidebarMode` | enum | `overlay` | `overlay` (floating) / `fixed` (docked — the **Expand** mode in the UI). The INI spelling is `fixed`, not `expand`; anything other than `fixed` reads as `overlay`. |
| `sidebarPosition` | enum | `right` | `right` / `left` / `top` / `bottom`. An unrecognized value falls back to `right` and logs a warning. |
| `sidebarWidth` | int | `300` | Preferred width (pixels). |
| `sidebarHeight` | int | `280` | Preferred height for top/bottom dock. |
| `sidebarWidthLocked` | bool | `true` | Disable resize dragging. |
| `sidebarBackgroundType` | enum | `color` | `color` / `image` / `pattern`. |
| `sidebarBackgroundColor` | hex | derived | Sidebar background color. |
| `sidebarBackgroundImage` | path | empty | Sidebar background image. |
| `sidebarPattern` | enum | `crosshatch` | Pattern fill (only `crosshatch` today). |
| `sidebarPatternIntensity` | int (0–100) | `50` | Pattern opacity. |
| `sidebarPatternColor` | hex | derived | Pattern tint color. |
| `sidebarTextColor` | hex | derived | Sidebar text color. |
| `sidebarAccentColor` | hex | derived | Sidebar highlight color. |
| `sidebarHeaderBgColor` | hex | derived | Section-header bubble color. |
| `sidebarHeaderBgOpacity` | int (0–255) | `200` | Section-header bubble alpha. |
| `sidebarSectionBgColor` | hex | derived | Body-section bubble color. |
| `sidebarSectionBgOpacity` | int (0–255) | `170` | Body-section bubble alpha. |
| `sidebarFontFamily` | string | empty | Sidebar font family. Empty = inherit. |
| `sidebarFontPointSize` | int | `0` | Sidebar font size. `0` = inherit. |
| `sidebarActiveTab` | enum | `item` | Active tab on first show: `item` / `collection` / `file`. An unrecognized value falls back to `item` and logs a warning. |

### Per-collection display behavior

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `customFontFamily` | string | empty | Per-collection font family override. |
| `expandMode` | bool | `false` | Two-stage activation (preview, then launch). |
| `customArtworkTypes` | csv | empty | Free-form custom artwork-type ids. See [Artwork](Artwork.md). |

### Runtime-only fields (never written to the INI)

You may see these field names in source code or in a bug report. They are
**not** INI keys — they exist only in the in-memory `CollectionConfig`, are
never loaded from or saved to `kartend.cfg`, and writing them by hand has
no effect.

| Field | Description |
|-------|-------------|
| `currentSubfolder` | Relative path to the active subfolder when virtual-folder navigation is in use. Reset on every launch by design — where you were browsing inside a collection is navigation state, not configuration. |
| `isPlaylist` | Marker for synthesized playlist collections. Playlists are rows in the SQLite database; a matching INI section would shadow the row, so the save path skips them entirely. |
| `playlistId` | UUID of the underlying playlist row. |
| `playlistReservedKind` | Empty for user playlists, `favorites` for the built-in favorites playlist. |

### Forward-compatible keys

Any key in a collection section that this build doesn't recognize is
stashed on load and replayed on save, so opening an older Kartend against
a config written by a newer one doesn't silently drop the newer keys. The
exceptions are the retired `videoDirectory` / `manualDirectory`, which are
deliberately dropped rather than preserved.

## Example: a complete media collection

```ini
[General]
rememberSelection=true
attractModeEnabled=true
attractModeIdleTimeoutSec=180
historyEnabled=true
runtimeDetectionEnabled=true

[Films]
name=Films
type=Video
mediaDirectory=~/Videos/Films
; preview videos go under ~/Videos/Films/_assets/video/,
; manuals under ~/Videos/Films/_assets/manual/
artworkDirectory=~/Videos/Films/_assets
extensions=mkv,mp4,zip
launcherPath=/usr/bin/mpv
launchParameters=--fullscreen
extractArchives=true
extractedExtension=mkv

gridWidth=8
itemWidth=180
itemHeight=240
horizontalSpacing=12
verticalSpacing=16
backgroundType=image
backgroundImage=~/Pictures/wallpapers/films.jpg
wallpaperParallax=true
parallaxStrength=40
vignetteEnabled=true
vignetteIntensity=60
toolbarBackdropBlur=true
sidebarVisible=true
sidebarMode=overlay
sidebarPosition=right
titleExclusionEnabled=true
titleExclusionPatterns\size=2
titleExclusionPatterns\1\pattern=\s*\(USA\)
titleExclusionPatterns\2\pattern=\s*\[!\]
hideMissingArtwork=true
```

## Where to next

- [Settings Dialog](Settings-Dialog.md) — the GUI equivalent of this
  reference, with field-by-field tour
- [Apply Settings](Settings-Dialog.md#apply-settings) — propagate keys
  across multiple collections
- [File Locations](File-Locations.md) — where this config file lives
- [Backup & Migration](Backup-and-Migration.md) — `.kart` packages
  bundle a collection's config, per-item metadata, and playlists for
  backup or transfer between your own machines

## For developers

- The two top-level structs are `GeneralSettings` and `CollectionConfig`,
  each in its own leaf header under
  [src/utils/app/collection/](../../src/utils/app/collection/)
  ([generalsettings.h](../../src/utils/app/collection/generalsettings.h),
  [collectionconfig.h](../../src/utils/app/collection/collectionconfig.h)).
- Per-collection leaf clusters split out of `CollectionConfig` include
  `ArchiveOptions`, `CollectionBackground`, `CollectionFilterPreferences`,
  `FolderBrowsingOptions`, `GridLayoutPreferences`, `LauncherConfig` /
  `LauncherProfile`, `LauncherPreset`, `ListViewOptions`, `ScraperOverrides`,
  `SidebarAppearance`.
- All INI key strings are constants in
  [src/utils/app/settingskeys.h](../../src/utils/app/settingskeys.h) —
  changing a value there is a wire-format break, renaming the C++
  identifier is a refactor.
- INI read/write happens in
  [src/modules/data/settings/settingsmanager*.cpp](../../src/modules/data/settings/);
  validation is in
  [src/utils/fs/configvalidation.cpp](../../src/utils/fs/configvalidation.cpp).
- Adding a new key: extend the struct (or the right leaf header), add
  load/save in `settingsmanager`, add UI in
  [src/ui/dialogs/settings/](../../src/ui/dialogs/settings/), update
  `applysettingsdialog` if the key should be propagatable, and add a
  row to this page.
- Keys that should *not* propagate to other collections (paths,
  extensions, parent linkage, launchers) are gated in
  `applysettingsdialog`.
