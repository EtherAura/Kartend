# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **Scraping now shows your ScreenScraper request quota and stops
  automatically when the daily limit is reached.** The scrape window
  displays a live "requests today" count with the time the quota
  resets, and as soon as ScreenScraper reports the daily request (or
  failed-lookup) quota is exhausted the scrape stops instead of
  burning the rest of the run against an empty allowance — your place
  is kept so you can resume once the quota resets.

### Changed

- **The Settings dialog has a new sidebar layout.** Settings categories
  now live in a navigation list down the left side — next to the
  collection tree and a new search box — instead of nested rows of
  tabs. Pick a category to show its page on the right; a header names
  the category and, for per-collection pages, the collection being
  edited. A labelled **Save** button sits in the dialog's button row,
  draggable dividers resize the rail and split its height between the
  collection tree and the category list, and the dialog remembers its
  size and divider positions between runs.

### Fixed

- **Large scrapes no longer crash on PDF manuals.** When a scrape saved a
  manual as a PDF, the artwork loader could hand that PDF to Qt's image
  decoder, which routed it to the PDF plugin and aborted the whole
  application mid-scrape. Non-image files are now kept out of every
  artwork image loader, so a downloaded manual can no longer take the
  app down.
- **The scrape window's collection label no longer freezes.** When a
  collection's items all failed (a quota or provider error, say), the
  progress window kept showing an earlier collection's name while the
  scrape had already moved on. The label now updates as each collection
  is reached, whether or not its items succeed.
- **The scrape error list is now scrollable.** Clicking the error count
  during a scrape opened a dialog that showed only the first few
  messages with no way to reach the rest. It now opens a resizable,
  scrollable list, and far more error detail is kept.

### Security

- **The scrape log no longer records credentials.** With scrape logging
  enabled, `scrape.log` wrote full request URLs — including the
  ScreenScraper developer and account passwords — in plain text. Password
  and account-id parameters are now masked as `<redacted>` before any URL
  is logged.

## [0.0.7] - 2026-05-17

### Added

- **Pick a libretro core from a list.** Every **Core** field for a
  RetroArch launcher now offers a dropdown of the cores found in your
  RetroArch install — the New Collection dialog, the per-collection
  Launcher tab in Settings, and the launcher-preset / additional-
  launcher editor — so there's no more browsing to a
  `.so` / `.dll` / `.dylib` by hand (the path field and Browse button
  stay for cores outside that directory). Kartend reads RetroArch's
  core directory from its `retroarch.cfg`, auto-detecting the standard
  location; a new **RetroArch Config** field in General settings lets
  you point at a specific `retroarch.cfg` or core directory when the
  install lives somewhere non-standard.
- **Auto-detected ScreenScraper system.** The **ScreenScraper
  System** field now fills itself from the collection's identity
  instead of needing a manual pick — in the New Collection dialog as
  you type the name, and on the Settings → Configuration tab when you
  edit the media type or file extensions. A hand-picked system
  freezes the choice; "Auto-detect" remains selectable.
- **Clickable scrape error count.** In the scraper's progress view,
  the **Errors** count is now a link when non-zero — clicking it
  opens a dialog listing the recorded failure messages, so a failed
  scrape can be diagnosed without digging through logs.
- **Scrape metadata sidecar.** A scrape now writes a human-readable
  JSON file at `{Artwork}/metadata/{baseName}.json` next to the
  downloaded art — the scraped title, description, genre, developer,
  publisher, release date, rating, players, source, and any
  provider-specific custom fields. Useful for inspecting or
  exporting what a scrape captured without opening the database.
  Written for single-item and batch scrapes alike; skipped when the
  scrape returned no metadata or the **Metadata** checkbox was off,
  and a `Fill missing` re-scrape leaves an existing sidecar intact.
- **Scraper fallback region.** Scraper settings gain a **Fallback
  region** dropdown. Each scraped item already follows its own region
  for the title, release date, and box art; this setting only decides
  what to fall back to when the item's own region has no entry.

- **Richer New Collection dialog.** Adding a collection now opens a
  short form for its name, content folder, artwork folder, launcher,
  media type, and metadata scraper instead of a bare name prompt — a
  fresh collection is usable without an immediate follow-up trip
  through Settings. The **Media Type** field is a dropdown of curated presets
  (Video, Audio, Images, Documents, Games) and stays editable so a
  custom category can still be typed. The **Scraper** field
  auto-follows the chosen type (Video → TMDB, Audio → MusicBrainz,
  Documents → Open Library, Games → ScreenScraper) and can be
  overridden — pinning a scraper explicitly is what makes a
  custom-typed collection scrapable. Two rows are revealed only when
  relevant: a **ScreenScraper System** field for game-category media,
  and a libretro **Core** field once the launcher path is RetroArch.
  The same media-type dropdown and a new **Metadata Scraper** row also
  appear on each collection's Configuration tab in Settings; the scraper
  choice is remembered per-collection.
- **Single-root artwork layout.** Collections now need only one
  Artwork folder configured — scrape auto-creates the per-kind
  subdirectories underneath as needed: `front/` (primary cover),
  `box/`, `screenshot/`, `title/`, `marquee/`, `fanart/`, `logo/`,
  plus `video/` and `manual/` for non-image media. The grid tile and
  details-pane preview resolve an item's cover straight from these
  typed subdirectories — no duplicate copy is kept at the artwork
  root. `front` is now a recognised standard artwork
  type (sidebar gallery surfaces it as **Front Cover**) alongside the
  pre-existing `box` slot, so scraped covers and hand-curated box art
  can coexist per-item. The per-collection Artwork tab loses the
  separate Video / Manual folder rows; the details pane's video
  preview and manual button now look under `{Artwork}/video/` and
  `{Artwork}/manual/` by default, and only consult the (now-hidden
  but still-persisted) `videoDirectory` / `manualDirectory` fields
  when set, so existing libraries with bespoke layouts keep working.
- **Batch scrape current collection.** Help → **Batch Scrape Current
  Collection…** walks every unscrapped item in the active collection
  through the first applicable API-backed metadata provider for the
  collection's type (MusicBrainz for audio, TMDB for video, Open
  Library for reference, ScreenScraper for games when credentials are
  configured). Auto-picks the top-ranked candidate per item — no
  per-item confirmation — so scraping a 500-item music library is one
  click. The runner downloads each item's primary `"front"` cover
  alongside its metadata so the grid populates with real artwork on
  the same pass; cover-fetch failures are non-fatal (metadata still
  saves, the user can re-pull the image per-item later). Progress
  dialog shows per-item status and a Cancel button; cancel stops
  cleanly between items so partial-row writes don't leak.
  Already-scraped items (`ItemMetadata.source` non-empty) are skipped
  automatically — re-running the action only touches new or
  unscrapped entries. Provider rate limits are honoured via the
  existing per-host HTTP throttle (MusicBrainz at 1 req/s, etc.), so
  a 10k-item run paces itself instead of getting throttled by the
  provider. Summary box at completion reports scraped / skipped /
  errors with the first 5 failure messages.
- **Secondary monitor / marquee window.** Bartop and arcade-cabinet
  setups with a topper / second monitor can pin a frameless Kartend
  window to that screen. Two display modes: **Item Artwork**
  (follows the current selection, ideal for cocktail cabinets) and
  **Collection Icon** (stable banner that stays put as you scroll).
  Configure under Settings → Preferences → **Marquee**: enable the
  toggle, pick the target screen from the dropdown (auto-populated
  from connected displays + their resolutions), pick a mode. The
  window doesn't accept keyboard focus, so input always stays on
  the main Kartend window. Hot-reload — saving the Settings dialog
  appears / moves / closes the topper without a restart. If the
  configured screen is unplugged after the fact, Kartend falls back
  to the primary screen and logs a warning. Video/attract-loop
  display mode is intentionally deferred to a follow-up.
- Each collection's **DAT File** row is now a **DAT Files** list that
  accepts multiple DATs per collection. The scrape pipeline walks the
  list top-to-bottom and takes the first hash hit, so power users
  with one DAT per system (No-Intro Mega Drive + No-Intro SNES + DOSBox-X
  arcade DATs, etc.) can stack them all on one collection and let
  Kartend pick. Drag rows to reorder; multi-select Add… picks a
  folder of DATs in one shot; Remove deletes the selected rows.
  Existing configs with the single-key `datFilePath=` shape upgrade
  to a one-entry list on first load — no manual migration.
- DAT-file lookups are now backed by an on-disk sqlite cache under
  the user's XDG cache dir (`~/.cache/kartend/datcache.sqlite` on
  Linux). The XML re-parse only happens once per `(absolute path,
  mtime)` — subsequent scrapes against the same DAT go through
  indexed `SELECT` queries instead of re-reading the file. For
  small No-Intro / Redump DATs the difference is invisible (the
  parse was already sub-second); for MAME's full listxml (~100MB /
  ~250k entries) cold-start lookup goes from multi-second parse to
  a single SELECT. Editing the DAT in another tool invalidates the
  cached records automatically on the next scrape; pointing the
  picker at a different file ingests it alongside the others
  (multiple DATs coexist in one cache file).
- DAT-file lookup now recognises two XML dialects, auto-picked from
  the root element: **Logiqx `<datafile>`** (No-Intro, Redump, TOSEC
  — they all share this schema) and **MAME `<mame>` listxml**. For
  MAME files, the parser uses each machine's `<description>` text
  ("Pac-Man (Midway)") as the canonical game name rather than the
  cryptic set-id ("pacman"), and skips `<rom status="nodump">`
  placeholder entries that would otherwise pollute the index with
  zero-hash collisions. TOSEC files with `<release>` region/date
  metadata children parse cleanly — the metadata is ignored, the
  `<rom>` hashes still come through. Pointing the picker at a
  non-DAT XML now surfaces a clear "expected `<datafile>` or
  `<mame>` root" message instead of silently parsing zero records.
- Settings → Configuration → Artwork now has an **Export placeholder PNGs
  for missing covers…** button. For every item in the collection that has
  no cover image, Kartend writes a procedural placeholder PNG into the
  configured Artwork folder. Existing artwork is left untouched; generated
  files can be deleted or replaced with real covers later.
- **Smart playlists** that auto-rebuild from a saved filter on each open.
  Right-click → Add to playlist → **New smart playlist…** to create one;
  six built-in criteria are available — Recently launched, Most played,
  Never launched, By extension, Has artwork, and Recently added. Smart
  playlists nest into the sidebar like ordinary playlists; right-click
  inside one and pick **Edit smart filter…** to change the criterion or
  limit. The JSON export format is bumped to v2 to preserve the smart
  flag and filter on round-trip; v1 files still import as static
  playlists.
- **Recently added** smart-playlist criterion — items added to your
  library within the last N days. The "added at" stamp is set the first
  time the scanner sees an item; existing items from before this update
  show up as "unknown date" and are excluded from recency-window
  matches until they're re-scanned or replaced.
- **Color scheme picker** in Settings → Appearance → Colors. Pick from
  three Kartend-bundled themes (Dark, Light, Neon) plus any KDE Plasma
  color scheme installed on the system (`*.colors` files under
  `$XDG_DATA_DIRS/color-schemes/` — typically the full Breeze / Oxygen
  set on KDE installs). Applying a scheme overwrites the per-collection
  color fields and the global title base color; vignette, blur,
  parallax, and font choices are left as-is.
- Settings → Launchers has a new **Detect installed…** button that
  probes your PATH for well-known media players, document readers,
  image viewers, and emulators (mpv, VLC, Okular, Gwenview, RetroArch,
  and ~30 others) and lets you pick which detected binaries to add as
  launcher presets in one go. Re-running the detect ignores binaries
  whose display name already matches an existing preset, so it's safe
  to use as a "what's new" sweep after installing more tools.
- **Look up online** submenu on the right-click context menu for an
  item — opens the user's browser to a search URL on a metadata
  provider relevant to the collection's type (free-form `type` field
  in the collection's settings, e.g. "Games" / "Movies" / "Music" /
  "Books"). Nine providers ship in this release: ScreenScraper.fr /
  MobyGames / IGDB for games; The Movie Database / IMDb for video;
  MusicBrainz / Discogs for audio; Open Library / Google Books for
  reference. The provider list is curated by collection type with a
  small synonym table so common labels ("Movies" → video, "ROMs" →
  games, "Books" → reference) all match without per-collection
  configuration. This is the foundation for future API-based metadata
  scrapers; for now, every provider is browser-only and no API
  credentials are required.
- **Scrape with MusicBrainz…** action on the right-click context menu
  for items in audio collections. Searches MusicBrainz for the item's
  name, presents the matched releases in a candidate-picker dialog
  with title / artist / year / format / front-cover thumbnail, and
  lets you preview the full release detail (publisher / genre / track
  count / annotation) plus pick which media variants (front cover /
  back cover) to download. Honours MusicBrainz's 1 req/sec rate
  limit. No credentials required. Applying the scrape persists
  everything to disk: each accepted media variant writes to
  `{artworkDirectory}/{type}/{basename}.png` (per-type subdir
  auto-discovered by the sidebar gallery and by the grid tile, which
  resolves the cover straight from those subdirs), and non-standard
  types (e.g. MusicBrainz "back")
  get an `item_artwork` row pointing at the file so the gallery
  surfaces them. Scraped metadata fields land on the per-item
  typed columns (title / publisher / release date / genre / etc.);
  user-entered fields the scrape doesn't fill are preserved (scrape
  wins on shared keys, user wins on unshared ones). The collection
  reloads automatically so changes appear in the grid without a
  restart.
- **Scrape with Open Library…** action on the right-click context menu
  for items in reference / book collections. Same flow as MusicBrainz
  (candidate dialog → media checkboxes → on-Apply persistence). Tries
  an ISBN-keyed lookup first when the filename contains a recognisable
  ISBN-10 or ISBN-13 (handles dashes, optional "ISBN" prefix); falls
  back to a free-text title search otherwise. Cover Art is fetched
  from covers.openlibrary.org at medium size for the primary tile and
  large size as a secondary fanart-slotted asset. No credentials
  required.
- **Scrape with The Movie Database…** action on the right-click context
  menu for items in video / movies / tv collections. Same flow as the
  other API providers. Returns both movies and TV shows in one
  combined search; routes to the right detail endpoint based on the
  matched media type. Maps title / overview / genres / production
  companies / US content rating / runtime onto Kartend's typed
  metadata fields; surfaces TMDB id, tagline, and rating in the
  customFields gallery. Poster goes to the primary tile; backdrop
  lands as a fanart-slotted asset. **Requires a free TMDB API token**
  — sign up at themoviedb.org → Settings → API → "API Read Access
  Token (v4 auth)" and paste it into Help → **Scraper Credentials…**
  under the TMDB section. No bundled keys — every user supplies
  their own.
- New **Help → Scraper Credentials…** dialog for managing per-provider
  API tokens. Persisted under a new `[Scrapers]` section of
  `kartend.cfg` keyed as `<provider>/<field>=<value>`; sensitive
  fields use Qt's password-echo mode. Future API providers will
  register here too.
- **Scrape with ScreenScraper.fr…** action on the right-click context
  menu for items in game collections. Same flow as the other API
  providers (candidate dialog → media checkboxes → on-Apply
  persistence). Kartend ships with bundled SS developer credentials
  (shared across all users — the same model Skyscraper uses) so
  scraping works out of the box without per-user signup; users who
  hit the shared rate-limit ceiling can register their own dev
  account on the SS forum and override the bundled dev_id /
  dev_password under Help → Scraper Credentials. Adding your own
  SS.fr user account login (user_id / user_password — register free
  at screenscraper.fr/membreinscription.php) raises your per-account
  request quota above the shared bundled limit. API requests go to
  api.screenscraper.fr (the canonical endpoint host). **Comprehensive metadata mapping**:
  every field SS exposes for a game lands somewhere — the typed
  ItemMetadata columns get title (US-region preferred) / synopsis
  (en preferred) / publisher / developer / players / release date /
  genres / ESRB rating; the customFields gallery surfaces the SS
  user rating (notes), staff-favourite flag (topstaff), screen
  rotation, native resolution, color count, controls, game series
  (familles), game modes, every other rating board (PEGI / USK /
  CERO / ... keyed as classification_<board>), full ROM info
  (filename / size / md5 / sha1 / crc / type / serial / region /
  languages), cloneof parent id, and the notgame flag. **Expanded
  media routing**: SS's `box-2D` lands as the primary cover; SS's
  `screenshot` / `sstitle` / `screenmarquee` / `marquee` / `wheel`
  / `fanart` map onto Kartend's standard artwork types
  (screenshot / title / marquee / logo / fanart) so the sidebar
  gallery auto-discovers them via the per-type subdirectory
  layout. Other SS media types (`box-3D`, `box-back`,
  `support-2D`, `manuel`, `video`, etc.) preserve their original
  names so the item_artwork-row branch surfaces them too.
  Per-collection systemeid is auto-detected from the collection's
  name / type / extensions against the live ScreenScraper catalog
  (fetched on first use from `systemesListe.php`, cached locally
  for 30 days). No platform names are bundled in Kartend — every
  alias and extension comes from ScreenScraper itself. Override
  autodetect from the collection's **Configuration** tab in
  Settings → **Scrapers** → **ScreenScraper System** (dropdown
  populated from the cached catalog; falls back to Auto-detect
  only until the first successful scrape populates the cache).
- New per-collection setting `screenscraperSystemId` on every
  collection (defaults to -1 = unset). Stored in the collection's
  INI section; consumed by the ScreenScraper provider above.
- **Offline ROM identification via No-Intro / Redump DAT files.**
  Each collection can point at a DAT file (Settings → Configuration
  → Scrapers → **DAT File**); when the ScreenScraper provider hashes
  a ROM and the digests match a DAT entry, it sends the DAT's
  canonical filename to ScreenScraper as the search query instead
  of the on-disk name. Result: messy library names ("Game [proto]
  v1.1 (En,Fr) [!].smc") still match cleanly because ScreenScraper
  receives the canonical "Game (USA) (Rev 1).sfc" the DAT supplies.
  Streaming XML parser handles multi-MB DATs without loading the
  whole document; per-collection cache keyed on path + mtime so an
  edited DAT reloads automatically. v1 ingests the No-Intro /
  Redump `<datafile>` shape (the most widely distributed format);
  TOSEC / MAME XML and a sqlite-backed cache for very large DATs
  are deferred follow-ups.
- ScreenScraper scrapes now hash the source file (MD5 + SHA-1) and
  pass the digests to `jeuInfos.php` alongside the filename — when
  ScreenScraper recognises the hashes the match is exact regardless
  of how the ROM was renamed locally, so re-organised libraries no
  longer fall back to fuzzy filename matching. Files are stream-
  hashed in 1 MiB chunks so multi-gigabyte disc images don't pin
  RAM; hashing failures degrade silently to filename-only. For
  zipped libraries, Kartend extracts the archive (via the same
  7z / unzip / bsdtar lookup the launcher uses) and hashes the
  largest inner file rather than the archive bytes — ScreenScraper
  indexes inner-ROM hashes, so this is what actually lands the
  hash-match. The behaviour is per-collection (Settings →
  Configuration → Scrapers → **Hash Inner ROM in Archives**, on
  by default); switch off to skip the extraction cost on huge
  archives.

### Changed

- **Smoother wheel scrolling and arrow navigation.** The details pane
  used to refresh on every selection change — four database queries
  plus a handful of filesystem probes per tick. During a fast wheel
  sweep that meant 30+ refreshes back-to-back. The refresh is now
  debounced: rapid selection changes coalesce into a single pane
  update once the selection settles, while clicks and arrow taps
  still feel immediate. Post-edit refreshes (context-menu metadata
  changes, tab switches) bypass the debounce so the pane updates the
  moment the user expects. Cover Flow view's per-item preview-video
  and artwork-gallery resolution gets the same debounce treatment so
  sweeping through a large carousel no longer does a database query
  and filesystem scan on every ~10ms tick. The three per-item
  database lookups (metadata, artwork links, usage stats) are now
  fronted by a 256-entry LRU cache, so scrolling back and forth
  across recently-viewed items, returning to a previous selection,
  or rebuilding the gallery after a context-menu tweak no longer
  hits the database — the cached rows are reused. Writes invalidate
  the affected entries; rescans, kart imports, and stats resets
  clear the relevant slice automatically.

### Fixed

- Opening a second Kartend window while a scrape is running no longer
  offers to "resume" that same scrape. The interrupted-scrape snapshot
  stays on disk for the whole run, so a second instance used to see it
  and could start the scrape a second time — two runs racing on the
  same database and artwork files. A running scrape is now marked as
  owned by its live process; only a scrape whose owner has actually
  exited (e.g. a crash) is offered for resume.
- A long-running scrape no longer hangs forever when a network
  request stalls. With no transfer timeout, a dead connection could
  freeze the whole batch — no further media downloaded, the estimated
  time-remaining still climbing — until the app was restarted.
  Stalled requests are now aborted after 30 seconds; the affected
  item is recorded as an error and the scrape continues.
- Renaming a collection no longer strands its scanned items and play
  history. A collection is identified by its name + media folder, so
  a rename used to leave the old rows unreachable — which inflated the
  Statistics **Total items** above the real per-collection counts.
  Renames now migrate the data to the new identity, and rows orphaned
  by past renames or removals are purged on startup, so the Statistics
  total matches the collections.
- Scraping now computes each ROM's CRC-32 and uses it for DAT-file
  matching and ScreenScraper hash lookups. Only MD5 and SHA-1 were
  computed before, so DAT entries that list just a CRC — the
  identifier No-Intro / Redump publish — could never match. Works
  for ROMs inside archives and for symlinked items (hashed through
  to their target) alike.
- The scraper now pulls every media type it offers. The **Support /
  cart** art was silently skipped — its checkbox key was mixed-case
  but the download filter matched lowercased, so it never matched —
  and the media-type list itself was an incomplete subset. The list
  now covers the full range ScreenScraper serves: box spine, back and
  3D box, box / cart textures, carbon and steel wheels, screen
  marquees, Steam grid, figurine, pictograms, and more.
- Resuming an interrupted scrape no longer resets the media count to
  zero — the number of media files written before the interruption is
  now restored along with the scraped / skipped / error counts.
- In the scraper's collection tree, checking a parent collection now
  cascades to its subcollections — the whole subtree is selected (and
  unchecking a parent clears it). Previously only the parent row was
  ticked and its subcollections stayed unselected.
- ScreenScraper results no longer collapse every item to its US
  title and box art. Each scraped item now follows its own region
  for the title, release date, and artwork — a Japanese cartridge
  keeps its Japanese title and box — while descriptions, genres, and
  other free-text fields follow the application language.
- In the Home view, entering a shell collection that gathers items
  from several subcollections sorted those items by subcollection and
  then by name. With a Name sort selected they now sort by name alone,
  as a single flat list.
- After a scrape (single-item or batch) the sidebar Details pane
  used to keep showing the item's pre-scrape state until you clicked
  another item and back. The post-apply path now refreshes the
  sidebar's metadata view for the current selection so the new
  title / description / genre / etc. land immediately.
- After a scrape, the grid tile would stay on its placeholder even
  though the cover file had been written, until the user navigated
  away from and back to the collection. Root cause was the artwork
  directory-listing cache holding the pre-scrape "no file here"
  result; the post-apply path now invalidates it (alongside the
  existing collection reload) so the grid picks the new cover up on
  the first repaint.
- The just-scraped primary cover now lands at
  `{artwork}/front/{base}.<ext>` and surfaces in the sidebar gallery
  as **Front Cover** — the cross-provider `"front"` tag (SS, MB,
  TMDB, Open Library all normalise their cover to it) is now a
  first-class standard artwork type. The grid tile and details-pane
  primary preview resolve the cover by walking the typed subdirs in
  priority order: front → box → box-3D → mixrbv1/2 → screenshot →
  title → fanart → marquee. So even when ScreenScraper has only
  screenshots / fanart / box-3D for a game (and no `box-2D`/`front`
  cover), the grid still gets a meaningful primary thumbnail instead
  of the placeholder — and no duplicate cover file is written to the
  artwork root.
- The details-pane primary preview tile (artwork QLabel + video
  preview widget) was anchored to the left of the sidebar because
  the .ui declared a fixed 200×200 size but no layout-item
  alignment. Both widgets are now force-centred in the parent
  QVBoxLayout via `setAlignment(widget, Qt::AlignHCenter)`, so the
  primary tile sits centred regardless of sidebar width.
- Scraped videos and manuals used to be dumped under `{artwork}/...`
  with a hardcoded `.png` extension, leaving an unplayable MP4 named
  `Pacman.png` in a directory the details pane didn't read from. The
  scraper now routes by kind into `{artwork}/video/{base}.<ext>` and
  `{artwork}/manual/{base}.<ext>` (extension inferred from the source
  URL, default `.mp4` / `.pdf` when the URL has no recognisable
  suffix). Neither kind produces an `item_artwork` row — videos and
  manuals aren't artwork, and the details pane discovers them by
  basename in those subdirectories. The (still-persisted but
  UI-hidden) `videoDirectory` / `manualDirectory` collection fields
  remain a power-user override for libraries with bespoke layouts.
- **Blank, undeletable collections no longer appear at the top level.**
  Kartend's internal settings groups (launcher presets, scraper
  configuration) were being mis-read as nameless collections at startup —
  they showed as empty rows that came back after every restart no matter
  how many times you deleted them. Startup now recognises these as
  settings storage and skips them.
- **Deleting a collection no longer strands its subcollections or
  crashes.** Removing a collection that had nested subcollections used to
  leave the siblings pointing at the wrong parent, so they were re-homed
  under the wrong collection or detached to the top level; a follow-up
  delete on a detached entry could crash the app. Deletion now re-links
  every surviving collection to its correct parent.
- **Expand-mode items with no artwork or video can be launched again.**
  In a collection with expand mode enabled, activating an item that had
  neither artwork nor a video used to do nothing — the first press tried
  to open a preview with nothing to show and swallowed the launch, and
  no later press could get past it. Such an item now launches normally.
- **Scraping a parent collection saves artwork to the right
  subcollection.** When a parent collection displays the items of its
  subcollections, scraping it used to write every item's artwork and
  metadata against the parent — so the results never appeared on the
  subcollection items they belonged to. Each scraped item is now routed
  to the subcollection that actually owns it.
- **The details pane's Collection tab no longer shows an oversized empty
  backdrop.** The rounded backdrop behind a collection's summary
  stretched to the full sidebar height — most obvious on a not-yet-
  scraped subcollection, whose summary is only a couple of rows. The
  backdrop now hugs the summary content.
- **The File menu's Recently / Most launched lists populate, and
  play-time tracking works.** Launch counts and timestamps were written
  against absolute file paths, but the library stored each item's path
  relative to its collection's media folder — so the update matched no
  row and `play_count` / `last_played` / play-time stayed empty. Item
  paths are now stored absolutely (a one-time pass upgrades existing
  libraries); this also restores the Statistics dialog and the sidebar's
  per-item usage stats.
- **Renaming a collection to a blank name in the settings tree is
  rejected.** Clearing a collection's name inline in the tree used to
  leave a nameless "ghost" row with nowhere valid to save; the rename
  now reverts to the previous name.
- **Mouse and keyboard work in the Home view again.** Selecting,
  activating, and scrolling collection tiles in the synthetic Home view
  did nothing — the input handlers treated it as "no collection
  selected" and dropped every click and keypress. They now recognise the
  Home view as an interactive view.

## [0.0.6] - 2026-05-11

### Added

- Helper-namespace extractions for five manager classes so the pure
  decision logic is unit-testable without their QWidget / ApplicationContext
  graphs:
  - `DetailPagePayloadBuilder` (display title fallback, artwork-row override
    map + custom-type filter, file-info field assembly)
  - `SelectionRestoreHelpers` (search-active predicate, restore-decision
    AND-chain, total/selIdx clamp, collection+token validity check)
  - `AttractHelpers` (linear + direction-biased random advance-index,
    sub-pixel scroll accumulator, boundary-aware scroll position)
  - `OverlayHelpers` (distance-to-duration glide policy with min/max clamp,
    visibility-aware animation start rect, item rect inset geometry)
  - `GamepadHelpers` (axis-to-direction with on/off-threshold hysteresis,
    d-pad + stick vertical-priority combine, SDL Sint16 normalize,
    case-insensitive confirm/back/toggle button resolution)
- `+102` new unit tests covering the extracted helpers (every helper has
  edge-case coverage for empty inputs, hysteresis hold/release, the
  bounce-fallback at boundaries, defensive clamps, and priority ladders)
- `KARTEND_PORTABLE_RELEASE` CMake option for distro packaging
  (drops `-march=native`/`-O3`/fast-math, keeps LTO + hardening)
- `KARTEND_LINKER_MAP` CMake option (replaces fragile `_MAP_DIR`
  filesystem walk; build script flips it on for debug builds)
- `KARTEND_ENABLE_QT_GAMEPAD` / `KARTEND_ENABLE_SDL2_GAMEPAD` /
  `KARTEND_ENABLE_ZSTD` toggles so distro USE flags actually
  control auto-detection
- CMake `uninstall` target (reads `install_manifest.txt`); replaces
  the readme's shell-loop instructions
- `build.sh` flags: `--prefix=PATH`, `--jobs=N`, `--uninstall`,
  `--clang`
- CPack integration (`cpack -G TGZ` for source/binary tarballs)
- Tag-triggered GitHub Release workflow with sha256 publishing
- `.github/dependabot.yml` for weekly GitHub Actions bumps
- `.editorconfig` for cross-editor indent/EOL/whitespace
- PKGBUILD `check()` running ctest at package time

### Changed

- All project CMake options namespaced to `KARTEND_*`
  (`MAINTENANCE` → `KARTEND_MAINTENANCE`, `BUILD_TESTS` →
  `KARTEND_BUILD_TESTS`, `ENABLE_*`/`USE_PGO`/`PGO_*` likewise)
- Release flags split into always-on hardening
  (`-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`, `-fPIE`,
  `-Wl,-z,relro`, `-Wl,-z,now`) and Release-only performance flags
- Pinned minimum Qt version to 6.4 in `find_package`
- PKGBUILD: switched to tarball source; added `qt6-gamepad` dep
- Flatpak: added `--socket=pulseaudio` for video preview audio;
  switched to Release + `KARTEND_PORTABLE_RELEASE=ON`
- ebuild: `gamepad`/`sdl`/`zstd` USE flags now drive the matching
  CMake toggles (previously only affected RDEPEND)
- CI ccache key uses `github.run_id` instead of source-file hash
  (was rotating the cache on every change)
- `build.sh`: unknown flags now exit with code 2 instead of being
  silently ignored

### Fixed

- `PlaylistManager::exportToJson` / `exportToM3U` now use `QSaveFile`
  (temp file + atomic rename) plus `PathUtils::syncDirectory` for parent-dir
  durability — a crash or power loss mid-export no longer leaves a
  half-written `.json` / `.m3u` clobbering a previously good file at the
  user's chosen path
- `QueryManager::needsRescan` (three sites that previously used `(void)exec()`
  or bare `exec()` calls) now logs an `ErrorContext` warning with
  `lastError().text()` when the SQL fails, so a locked DB or constraint
  failure no longer desyncs scan metadata from disk in silence
- `DbMigrations::setUserVersion` now checks the `PRAGMA user_version` exec
  result and logs on failure; previously a failed PRAGMA after a successful
  migration body left the version stale and re-ran the migration on next
  launch with no log trail
- `SessionManager` now logs a `qCWarning` when the metadata file exists on
  disk but `open(ReadOnly)` fails (permissions, FS error) — previously
  silently fell back to default empty state with no signal
- Re-enabled `-Wunused-parameter` and `-Wunused-lambda-capture` in the
  base lib's `target_compile_options` and cleared the fallout: removed
  stale `this` captures in three lambdas (launch / kart / querymanager
  reconnect logging), removed a const-int capture clang correctly treats
  as constant-expression, and `Q_UNUSED` for four genuinely-dead
  parameters that survived prior refactors
- PGO instrumentation never reached the compiler — `list(APPEND
  RELEASE_COMPILE_OPTS ...)` ran after `target_compile_options()`
  had already substituted the variable text
- `option(PGO_PROFILE_DIR "..." "${path}")` was being coerced to a
  boolean; replaced with `set(... CACHE PATH ...)`
- Duplicate `-Wno-unused-lambda-capture` in compile flags
- Three identical `RELEASE_COMPILE_OPTS` branches collapsed to one
- `ENABLE_SANITIZERS=ON` with non-Debug config used to silently
  produce no instrumentation; now errors at configure time
- `docs/building.md` first line was a corrupt paste of an emerge
  command concatenated to the heading
- readme stale `--no-archive`/`--no-reports`/`--fast` references
- readme wrong icon install path (`pixmaps/` → `icons/hicolor/...`)
- Missing `Multimedia`/`MultimediaWidgets` from readme + CONTRIBUTING
  Qt component lists
- Metainfo XML missing 0.0.5 release entry
- ebuild duplicate `dobin`/`domenu` calls (CMake `install()` already
  handled them)
- CI workflow + PKGBUILD + ebuild missing `qt6-multimedia` runtime
  dep that v0.0.5's video preview / overlays / startup-video
  features require — every CI matrix job and downstream package
  failed configure with `Failed to find required Qt component
  "Multimedia"`
- `launchmanager.h` forward-declared `QProcess` for a
  `QPointer<QProcess>` member, which broke `mocs_compilation` on
  Ubuntu 24.04 / Qt 6.4 (older `qpointer.h` casts through the
  complete type); replaced with a real `#include <QProcess>`
- Replaced `QIcon::ThemeIcon` enum (Qt 6.7+) with XDG icon-name
  string literals across dialogs and `settingsdialog.ui` so
  builds work on Qt 6.4
- `usagestatsstore.cpp` used `QTimeZone::UTC` (Qt 6.5+); replaced
  with the equivalent `QTimeZone::utc()` accessor (also valid on
  later Qt) for Qt 6.4 compatibility
- `tests/test_kartreader.cpp` ASan global-buffer-overflow: the
  `QByteArray` ctor was told to read 28 bytes from a 26-byte
  literal; corrected to the real content length
- Added LSan suppressions for known-leak third-party paths in
  GStreamer / `QPlatformMediaIntegration::instance` so sanitizer
  CI runs aren't drowned in non-actionable noise

## [0.0.5] - 2026-05-06

### Added

- Sorting by date and file size
- Optional, configurable splash screens (startup + focus-return)
  with per-collection placeholder artwork and title overlays
- Per-collection expand mode with two-stage activation
- Per-collection 'hide items without artwork' toggle
- Per-collection regex title-exclusion filter
- Per-collection multi-launcher with chooser dialog, per-item
  launcher overrides, and global launcher presets
- Per-collection appearance settings, sidebar tabs, sidebar
  promoted to a top-level Settings tab, and per-collection
  sidebar font override
- Settings: scope selector with gating for non-propagatable
  fields, duplicate-collection + apply-to-selected picker,
  collection-tree context menu with drag-drop reparenting,
  alias-parent links with linked-appearance rendering,
  collection type metadata + toolbar filter + hide-subs,
  config import/export via Backup section
- Playlists: data model + virtual collection foundation,
  built-in Favorites playlist, M3U + JSON import/export,
  documented playlist→source launcher chooser contract
- Chronological launch history log
- Per-item usage tracking and Statistics dialog
- Item metadata: extended fields with sidebar Details section,
  user-defined custom fields, per-item manual links
- Artwork: item_artwork storage with standard-type
  auto-discovery, custom artwork types, per-item manual-link
  editor, sidebar artwork gallery, cycle item artwork type
  with modifier+middle-click
- Video: per-item video previews in sidebar, video-first
  overlay with middle-click peek, gallery video tile,
  unmuted sidebar/overlay audio
- Optional runtime detection with Now Playing overlay
- Coverflow 3D carousel view mode with gallery toolbar,
  horizontal scrolling view mode
- Customizable items-page toolbar (kde-breeze icons, view
  dropdown, in-field search, consolidated filter)
- Hamburger fallback when the menu bar is hidden;
  randomize / recent / layout menu entries
- Sidebar: collection summary when no item is selected,
  upper width limit removed
- Typography: global UI font family + size, runtime text
  zoom (Ctrl+/-/0)
- Attract mode: advance-selection + sub-pixel scroll
- Contextual empty-state widget
- Details pane (renamed from sidebar): orientation menu and
  view-aware grid sizing
- Packaging: Arch PKGBUILD and Flatpak manifest
- Tests: integration test harness for UI-coordinator managers,
  ApplicationManager + NavigationManager lifecycle coverage,
  and unit tests for new metadata, artwork, history, playlist,
  usage-stats, kart, title-filter, video, and grid-layout modules

### Changed

- Refactor: split `ApplicationContext` into ui/managers/collection
  sub-structs
- Refactor: promoted `scrollmanagervirtual.cpp` into a
  `VirtualScrollEngine` class
- Project-wide clang-format pass

### Fixed

- Allow ampersands in launch paths
- Render video-preview frames via `QVideoSink` instead of
  `QVideoWidget`
- Sidebar: expand video/artwork/manual paths and apply
  owner-aware lookup; center artwork/name region, scale
  previews, wrap long values
- Filter toolbar: always-enabled with explicit 'Filter' label
- Toolbar position label refreshes on wheel scroll
- Treat launch as attract activity to suppress spurious
  attract-mode triggers

## [0.0.4] - 2026-04-25

### Added

- Attract mode settings UI under General → Attract Mode (enable toggle,
  idle timeout, scroll speed)

### Changed

- Attract mode timeout reset behavior now reacts to item selection changes
  instead of generic mouse movement/activity

### Fixed

- Attract idle timer interval synchronization when attract mode is toggled
  from disabled to enabled
- Attract scrolling stop behavior when mode is disabled mid-scroll

## [0.0.3] - 2026-04-24

### Added

- DatabaseManager lifecycle and path-resolution unit tests

### Changed

- Settings dialog UI polish (approved compact-layout tweaks)
- `build.sh`: `.backups` archive and reports are now opt-in; dropped
  redundant `--no-archive` / `--no-reports` / `--fast` flags
- Documentation sync: architecture, testing, and copilot instructions
  updated to match current module layout
- Readme build-script section expanded with a workflow table
- Ebuild `HOMEPAGE`/`SRC_URI` pointed at `EtherAura/Kartend`
- `SearchManager::currentMode` marked `[[nodiscard]]`; implicit-bool
  nullptr checks and `qCWarning` adoption in `configvalidation`

### Fixed

- Crash on shutdown: widget pool now uses `QPointer` for raw captures
  and `deleteLater` so queued callbacks cannot outlive their targets
- Scroll/artwork: synchronous artwork lookup fallback for list-mode
  rows and correct `hasArtwork` flag update from the prewarm
  reconfigure path

## [0.0.2] - 2026-04-21

### Added

- Unit tests for StringUtils and CollectionUtils helpers
- Prereleases included in the readme Release badge

### Changed

- Large LOC-reduction refactor: split `ScrollManager`, `QueryManager`,
  `SettingsManager`, `NavigationManager`, `InteractionManager`,
  `ViewportManager`, `ItemWidget`, `MainWindow`, and `SettingsDialog`
  into focused sibling translation units
- Extracted `DataSourceManager` and `SelectionDisplayManager` from
  `ScrollManager`
- `build.sh` now invokes clang-format with `--style=file` so the
  project `.clang-format` is honored

### Fixed

- UBSan vptr violation on `EventManager` during shutdown:
  `~InteractionManager` now detaches the qApp event filter before
  owned sub-managers are destroyed
- Missing `<set>` include in `settingsdialogtree.cpp` that broke the
  build on Ubuntu 24.04 libstdc++
- CI workflow YAML: removed orphan trailing `run:` line that caused
  GitHub Actions to schedule zero jobs
- Core-dump gitignore rules anchored to the repo root
- License section formatting in the readme

## [0.0.1] - 2026-04-18

### Added

- Virtual scrolling with widget pooling for large collections
- Async artwork loading with QtConcurrent and in-memory cache with disk persistence
- Collection hierarchy with parent-child relationships and subcollection navigation
- Session persistence for selection state and scroll position
- Configurable launchers (emulators, RetroArch cores, native applications)
- Full keyboard navigation (arrow keys, alphabetic jumping, search)
- Glide scroll animations with configurable easing
- Grid and list view modes with sortable columns
- Metadata sidebar with artwork display
- Settings dialog for collection configuration
- SQLite database with FTS5 search and worker thread queries
- Gamepad support (Qt6 Gamepad / SDL2 fallback)
- Gentoo ebuild packaging
- CI with build, test, sanitizer, and maintenance checks

[Unreleased]: https://github.com/EtherAura/Kartend/compare/v0.0.7...HEAD
[0.0.7]: https://github.com/EtherAura/Kartend/compare/v0.0.6...v0.0.7
[0.0.6]: https://github.com/EtherAura/Kartend/compare/v0.0.5...v0.0.6
[0.0.5]: https://github.com/EtherAura/Kartend/compare/v0.0.4...v0.0.5
[0.0.4]: https://github.com/EtherAura/Kartend/compare/v0.0.3...v0.0.4
[0.0.3]: https://github.com/EtherAura/Kartend/compare/v0.0.2...v0.0.3
[0.0.2]: https://github.com/EtherAura/Kartend/compare/v0.0.1...v0.0.2
[0.0.1]: https://github.com/EtherAura/Kartend/releases/tag/v0.0.1
