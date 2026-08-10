# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **Collections can now be built straight from Steam, Flatpak, and Lutris
  libraries.** File → Import → "Import from Launcher…" detects what's
  installed through each launcher and creates a ready-made collection per
  source, nested wherever you choose in your hierarchy — every installed
  game appears as a launchable item, using the
  cover art the launcher already has on disk, with no shortcuts to create
  by hand and no network involved. Launcher collections re-sync themselves
  at startup and on demand (File → Import → "Sync Launcher Collections"),
  so installs and uninstalls follow through automatically; art you scrape
  or place by hand is never overwritten by a sync. Under the hood each game
  is a small `.kartlink` shortcut file, so search, playlists, statistics,
  and artwork behave exactly as they do for file-based collections. Steam
  games also arrive with their metadata — developer, publisher, release
  date, genres, player modes, review scores — read straight from the Steam
  client's local cache; hand-edited and scraped fields are never
  overwritten. The parts only the store has — descriptions, screenshots,
  store backgrounds, and trailers — are fetched **automatically right after
  the import**, in the background, with no scrape to run by hand and no
  account or key needed; because every imported game knows its exact Steam
  app id, it never mismatches a title the way name-based scraping can. Both
  passes are fill-missing, so re-importing costs nothing and never
  overwrites art or fields you already have.

- **Steam imports can reach past the games you have installed.** A new
  choice in the import dialog decides how much of the library to bring in:
  just what is installed, everything you have played on this computer, or
  every game Steam has heard of. The picker shows how many games each
  choice would add before you commit. "Games you own" is the default — it
  is the widest setting that cannot list something you do not have, since
  it goes by what you have actually played here; the widest setting reads
  Steam's own metadata cache, which also describes games you do not own,
  including the free Valve titles every Steam install carries. A game you
  own but have never launched on this computer will not appear, because
  nothing on disk records it. Games that are not installed still launch —
  Steam offers to install them first — and they arrive with the cover art
  and details Steam already cached. Your choice is remembered per
  collection, so later syncs keep the same breadth instead of quietly
  dropping everything that is not installed; re-running the import over an
  existing collection is how you change your mind.

- **The Steam details fetch now tells you it is working.** Descriptions,
  screenshots, and trailers come from the store, and Steam paces those
  requests, so a large collection fills in over minutes rather than all at
  once. The status bar now says how many games it is about to fetch and
  that it can take a while, then counts them off ("47 of 123") — an import
  still working no longer looks like one that quietly dropped half your
  descriptions. If it is interrupted, the next sync resumes exactly where
  it stopped instead of leaving those games bare forever.

### Changed

- **Uses roughly half the memory during long sessions.** Three things were
  adding up. Cover art was decoded at a fixed size chosen for the largest
  place it might ever appear, so a grid showing 200-pixel covers still kept
  400-pixel images in memory for every item; art is now decoded to fit the
  size it is actually drawn at. The "artwork cache" setting quietly funded
  two separate caches with the full amount each, so a configured 500 MB
  reserved a gigabyte — it now means what it says, split across both. And
  the memory allocator was holding on to freed image buffers instead of
  returning them to the system. Measured on a large collection over a
  twelve-hour session: memory settles around 1 GB instead of climbing, with
  no swap. If covers look soft after resizing the window, raising the
  artwork cache setting restores the previous crispness. Two further
  trims followed: the index of your artwork folders stored the full path
  of every file, repeating the same folder name thousands of times over,
  and the app now periodically hands memory it has finished with back to
  the system instead of holding on to it between bursts of activity.
- **Browsing does less repeated work per scroll and per search.** The
  database worker's prepared-statement cache turned out to cache nothing —
  every "cached" statement was recompiled on each use and the whole cache
  was emptied at the start of every fetch. Statements are now genuinely
  reused across a session, and the two hottest lookups (the scroll-page
  range query and the find-item-position probe) go through the cache too.
- **The window stays responsive through work it used to do on the interface
  thread.** Saving your collection list rewrote and flushed the entire
  configuration file on every toolbar filter toggle, sidebar drag, and menu
  radio switch; refreshing the window title re-listed and sorted the media
  directory on every navigation event; the "hide items without artwork"
  filter re-resolved artwork for every item on every keystroke; and pointing
  the watcher at a collection walked its whole folder tree before the window
  could redraw. Those now coalesce, cache, or run off the interface thread.
  Quitting is quicker as well — the artwork subsystem's shutdown stages each
  waited out their own budget in sequence, which could hold the window on
  screen for several seconds after you asked it to close.
- **Video previews no longer convert every frame on the interface thread**,
  and the still-frame extractor shuts down before the application does
  instead of after.

### Fixed

- **Scraped images, videos, and manuals are now saved with a file extension
  that matches what is actually inside the file.** When a provider's download
  link carries no usable file extension, the scraper used to fall back to a
  fixed default per kind — so a WebP image arrived named `.png`, and the name
  lied to anything that trusts extensions (exporting to another program, a
  file manager preview, sorting by type). The saved bytes are now inspected
  and the file named for what it really is; links that do carry an extension
  keep behaving exactly as before (Kartend-aiws7).
- **A "fill missing" scrape no longer permanently skips items that have
  metadata but no media yet.** The skip logic decides which media types are
  worth requiring by looking at what is already on disk across the
  collection — but on a collection with no media at all, nothing qualifies,
  so every item counted as fully covered and the scrape reported "skipped"
  without fetching anything. Once a collection reached "metadata complete,
  zero media", no amount of re-running would ever fetch media. Items with
  none of the requested media now stay in the queue until the provider has
  been asked, and only genuinely media-less titles settle down as skipped
  (Kartend-1wfi2).
- **Breadcrumb links and error messages now stay readable on both light and
  dark themes.** The clickable collection links in the window title were
  drawn in a fixed light shade derived from the accent colour — tuned for
  dark backgrounds, nearly invisible on a light theme (measured 1.95:1
  contrast where the accessibility guideline asks 4.5:1). The red used for
  validation and error text sat just under the guideline on both themes.
  Both are now adjusted against the actual window background at the moment
  they are used, keeping their hue but guaranteeing readable contrast — and
  the optional item-title accent tint gets the same guarantee when enabled
  (Kartend-q40q0).
- **`kartend --version` and `kartend --help` now work on a machine with no
  desktop running.** They crashed instead of printing, because the application
  window system was started before the command line was read — so the two
  options that exist precisely to be scripted were the ones that could not be
  used from a script, a packaging check, or an SSH session (Kartend-3edfq).
- **Changes you make are no longer lost when you log out or restart.** When
  the system shuts down it asks each open app to close; Kartend never heard
  the request, because the gamepad library it uses was intercepting that
  signal and nothing was listening for it. The app stayed open until the
  system gave up waiting and forced it closed — and because settings are
  written while closing, anything adjusted during that session was lost.
  Kartend now closes normally when asked, saving on the way out, so logging
  out or rebooting keeps your changes (Kartend-ewl6x).
- **The window title no longer keeps a search's item count after you clear the
  search.** Searching narrows the collection, and clearing it restored the grid
  but left the title reading e.g. "Documentaries (1 items)" for a collection of
  six — so the title quietly misreported how big the collection was for the
  rest of the session. The title now returns to the real total, in the same
  style it uses everywhere else (Kartend-4ex9z).
- **Item titles now use your theme's normal text colour.** They were always
  tinted towards the accent colour, and the shipped tint was dark enough to be
  hard to read on a dark theme — measured at 1.6:1 contrast against Breeze
  Dark, where the accessibility guideline asks for 4.5:1. If you prefer the
  tinted look it is still there, as **Settings > Appearance > Colors > Tint item
  titles with accent colour**; installs that had already customised the tint
  keep it switched on (Kartend-bbcu6).
- **Apps you launch are no longer affected by Kartend's own video settings.**
  The video-decoder preference Kartend sets for itself was being inherited by
  anything it launched, so a launched app built on the same toolkit quietly
  picked up Kartend's choice instead of making its own. Launched apps now get
  a clean slate. If you set that preference yourself, it is still passed
  through untouched.
- **Long sessions with video no longer grow without bound.** On some
  graphics setups — typically a processor with built-in graphics alongside a
  separate graphics card — the video decoder handed each clip to a
  driver path that never released it afterwards. Every switch to a new video
  left another one behind, so browsing a video-heavy collection, and attract
  mode in particular, piled up graphics resources and memory for as long as
  the app stayed open. An overnight session was measured holding several
  gigabytes while sitting idle. Video now stays on decoder paths that clean
  up after themselves, and memory settles instead of climbing.
- **A damaged database no longer leaves the app silently empty.** A corrupt
  media.db "opened" successfully and every lookup quietly returned nothing —
  collections appeared empty with no explanation and no way back short of
  deleting the file by hand. The damaged file is now set aside automatically
  (kept next to the original as `media.db.corrupt-<timestamp>`, in case
  anything can be salvaged from it), a fresh database is created in its
  place, and a one-time notice explains that collections will be rescanned.
  Play counts, ratings, and history from the damaged file cannot be
  recovered automatically.
- **Opening the DAT Audit from the settings dialog no longer produces a
  frozen window.** The settings dialog is modal, and the audit window it
  opened couldn't receive clicks or keys until settings closed. It now
  accepts input immediately, and it stays open (moving back under the main
  window) when the settings dialog closes.
- Collection validation no longer contradicts itself about a launcher that
  exists but is not executable (it warned "not executable" and "does not
  exist" about the same file), and circular parent chains between
  collections are now reported — previously only a collection listed as its
  own parent was caught.
- **Filtered and sorted views no longer show the wrong items.** Folders shown
  as tiles were left out of the index the filter works in, so a filtered view
  could misclassify them and hide or misplace the items around them. With
  types interleaved under the unified sort, the grid could paint blank or
  mismatched tiles; activating a cover in Cover Flow while a filter was
  active could open a different item than the one on screen; and the
  "N of M" readout disagreed with itself depending on which path produced it.
- **Settings you saved and then cancelled are no longer left half-applied.**
  Saving inside the settings dialog and then pressing Cancel or Esc wrote the
  changes to disk but skipped everything that normally follows a save, so a
  renamed or relocated collection stranded its items and play history under
  its old identity — the reason the Statistics "total items" figure drifted
  above the sum of the per-collection totals. The follow-up now runs for
  whatever was actually persisted. In the same dialog: pressing OK after
  Discard no longer re-applies the edits you just discarded; the
  startup-collection dropdown no longer goes stale after collections are
  added or renamed in the same session (a renamed target used to fall back to
  *(Default)* without saying so); the extensions panel no longer prompts for a
  rescan when nothing changed; and a recursive import no longer copies the
  parent's DAT lists, scraper system, alias parents, and icon into every
  generated subcollection.
- **Loading a configuration profile sticks.** The imported file was applied
  and then silently overwritten with the pre-import settings at exit.
- **Artwork fixes.** The artwork button in list mode stayed disabled when the
  artwork came from the cache; a card composed before a live tile-size change
  was composited a second time, doubling its border and re-masking its
  corners; on mixed-DPI setups artwork was decoded and composed for the
  primary screen's scale rather than the screen the window is actually on,
  and images loaded from the disk cache carried no scale tag at all; and the
  small triangle indicator added a fresh label to an item on every repaint.
- **Stale results no longer overwrite what you are looking at.** The detail
  page could reappear after you dismissed it when a slow lookup finished
  afterwards, and Cover Flow could accept a gallery result for the item you
  had already moved away from.
- **Selection restore lands on the right item.** Restoring by file path
  emitted an unfiltered position into a filtered view, keyboard selection
  validated against cached totals instead of what was rendered, and clearing
  a search restored the pre-search view without its toolbar filters.
- **Smart playlists and search rank correctly.** An extension filter
  containing an underscore matched more than it should have (`m_4` also
  matched `mp4`) because the pattern's wildcards were not escaped, and fuzzy
  search did not treat an underscore as a word boundary, ranking snake_case
  names below where they belonged.
- **Playlist edits no longer leave a stale view behind.** Adding or removing
  an item was committed even when stamping the playlist failed, and that
  stamp is what tells the cache to re-read — so the playlist could keep
  serving its previous contents for the rest of the session. Playlist export
  now writes atomically and skips paths containing line breaks, which
  previously round-tripped into a corrupt file.
- **A gamepad direction held while a binding is being captured no longer
  keeps repeating** under whatever you launched next.
- **.kart bundle import and export are safer and no longer memory-hungry.**
  Entries stream through compression in fixed slices instead of being held
  whole in memory (an entry may legitimately reach 8 GiB, and importing one
  peaked at roughly twice that); path validation now rejects dot-only and
  space-only segments and Windows reserved device names; the manifest's item
  and playlist lists are bounded rather than only the manifest text; the
  guard against maliciously oversized packs no longer computes a negative
  ceiling on a nearly full volume, which killed legitimate imports; and
  quitting during an export or import waits a bounded time instead of
  indefinitely.
- **Archive handling fails closed.** Hashing an item inside an archive
  extracted it without the safety scan every other path applies, and the
  scanners reported success when they recognised no entries at all — an
  unfamiliar listing format passed as clean.
- **Linux `.deb` releases ship the features CI tests.** The release build was
  configured without libarchive and SDL2, so the packaged binary quietly lost
  archive support and the primary gamepad backend; the release now asserts
  each optional backend was detected and fails instead of shipping without.
- Registering an overlay could leave the stacking table out of order, so a
  later restack put the wrong overlay on top.
- The About box and the parent-collection *None* entry are translatable.
- **Cover Flow no longer writes an item off as artless before it has looked
  everywhere.** Cover art is searched for in the artwork folder and then in
  each of its typed sub-folders — `front`, `box`, `screenshot` and the rest,
  which is where scraped covers actually land. Those sub-folders are indexed
  separately from the top-level one and moments later, but Cover Flow decided
  an item simply had no artwork as soon as the top-level folder was done, so a
  cover sitting in `front` could be given up on before the folder holding it
  had been read — after which nothing retried it. Cover Flow now waits for
  every folder it would search, and asks for all of them to be indexed up
  front instead of just the top level (Kartend-t4rjw).
- **Double-clicking a thumbnail in Cover Flow's artwork strip now opens it full
  size.** The double click was passed to the covers behind the strip, which
  matched nothing there, so it did nothing at all. It now opens that exact
  image or video in the same full-screen preview the sidebar gallery uses.
  Single-clicking a thumbnail still just swaps the centre cover, as before
  (Kartend-5jtyw).
- **Cover Flow shows a subcollection's tile picture the same way Grid does.**
  A subcollection tile takes its picture from the collection's own Collection
  Icon, or failing that from an image named after the subcollection sitting in
  the parent's artwork folder. Cover Flow only ever read the first, so a
  subcollection relying on the naming convention — the older of the two, and
  the only one that worked in Grid until recently — showed the generated
  placeholder there. Both layouts now do both steps, in the same order
  (Kartend-5dhlv).
- **Cover Flow covers now appear as soon as they load, instead of staying
  blank until you move the selection.** Covers are decoded in the background,
  and a card shows a placeholder while that happens. The resized copy of that
  placeholder was being filed under the cover's own name, so once the real
  cover arrived the card kept finding — and drawing — the placeholder instead.
  Moving the selection resizes the card, which asked for a differently-sized
  copy and finally showed the artwork; that is why it looked like the covers
  were waiting for a click. The same mix-up applied to the artwork variants in
  the strip along the bottom (Kartend-ce0b4).
- **A subcollection's Collection Icon now shows on its tile in Grid and List.**
  The setting is offered for every collection and the documentation described
  it as the image painted on a subcollection's tile, but only Cover Flow and
  the marquee ever read it — in the two most-used layouts, setting it did
  nothing and the tile stayed on the generated placeholder. Grid and List now
  use it first, falling back as before to an image named after the
  subcollection sitting in the parent's artwork folder. That older convention
  is unchanged, and is now written down too (Kartend-kb2vx).
- **OK in the Settings dialog now saves and closes instead of asking whether
  you want to save.** With an edit pending, OK raised the "Save changes before
  closing the dialog?" prompt — the one that belongs to Cancel. Pressing OK
  already says to save, so the question was redundant, and one of its answers
  was Discard, which throws away the edits the button was meant to keep.
  Cancel still asks, which is where the question is genuinely open
  (Kartend-1g46b).

## [0.0.18] - 2026-07-29

### Changed

- **The menu bar has been reorganized, and File is no longer a dumping
  ground.** File had grown to 25 entries — every new dialog was appended to
  it — and because they were appended *after* the entry that ends the menu,
  **Exit** sat stranded in the middle with fourteen items below it. Library
  maintenance now lives in a new **Tools** menu (Scraper, DAT Audit,
  Collection Health, Review Missing Metadata, Assign Missing Artwork,
  Duplicates and Variants, Bulk Edit); the four import/export entries are
  grouped into **Import ▸** and **Export ▸** submenus; and Layout Profiles
  and Presentation Profiles moved to **View**, next to the layout controls
  they capture. File is down to eleven rows, opening with New Library Wizard
  and ending, once again, with Exit. **Help** was mis-ordered the same way —
  About and About Qt appeared above everything else — and now closes the
  menu as expected. Nothing was removed; every entry is still reachable, and
  the hamburger menu shown when the menu bar is hidden mirrors the new
  layout.

### Fixed

- **Play counts and history no longer go missing when the database is
  busy.** A write that found the database locked was retried a few times
  and then discarded outright, so launching an item while something else
  held the database could leave no trace of it in your play counts or
  history at all. Contended writes are now requeued instead of dropped —
  up to five times, backing off between attempts — and the views that
  depend on them refresh once the write has actually landed rather than
  when it was handed off.
- **"Recently played" now reflects when you launched something, not when
  the database got around to recording it.** Launch times were stamped at
  the moment the queued write ran, so a launch delayed by a busy database
  could be recorded as happening *later* than one that genuinely followed
  it — leaving history and recently-played lists in an order that
  contradicted what you did. Launch times are now captured when the launch
  happens, and history is ordered by that time.
- **Two more strings can now be translated.** The "never" shown for an item
  that has never been scanned, and the seconds-suffix duration format, live
  in header-only helpers that were left out of their directory's build
  source list. The string extractor therefore never saw them, and no amount
  of refreshing the translator seed would have picked them up — they stayed
  English in every locale. Both are now extracted.
- **DAT Manager — the audit status filter can now be translated.** The ten
  entries in the Audit page's status-filter dropdown ("All", "Files I own",
  "Catalogue completeness", and the individual statuses) were marked for
  translation in a way the string extractor rejected, so they never reached
  the translator seed and stayed English in every locale. They are now
  extracted under the same context the UI looks them up in.

## [0.0.17] - 2026-07-19

### Fixed

- **Windows packaging:** the Windows `.zip` and installer failed to start
  ("archive.dll was not found") ever since the archive engine became a hard
  dependency in 0.0.14 — the packaging step's hand-maintained DLL list was
  never updated. Runtime DLLs are now staged from the executable's actual
  import table, so the package stays complete by construction. Linux
  packages were unaffected.

## [0.0.16] - 2026-07-18

### Added

- **Scraper — setup options inside the scrape window.** The scrape window
  gains an options panel for tuning the re-scrape policy, fallback region,
  and media speed/quality preset without a round-trip through Settings →
  Scraper; both places share the same preset logic, and the window gains a
  vertical splitter to fit the controls.
- **Scraper — video and manual media now download.** Media fetches negotiate
  content type and size per media kind, so video previews and manuals are
  retrieved correctly instead of being dropped.

### Changed

- **Scraper — smarter retry policy.** A provider answering 429 with a
  Retry-After header is treated as transient, with a three-strike escalation
  shared by batch, entity, and interactive scrapes; cancelling a scrape is no
  longer misclassified as a retryable failure; and per-media download
  failures are now counted in the completion summary and survive a resumed
  run.
- **Snappier UI under heavy work.** Drag-and-drop kart import and artwork
  preview decoding run on background workers (no more window stalls during
  large imports), filesystem-watcher reconciliation happens off the GUI
  thread, and rapid-fire settings writes (column drag, volume, text zoom)
  are debounced instead of hitting disk per tick.

### Fixed

- **Interactive scrapes skip completed items:** Skip and "download only
  what's missing" scrapes started interactively now pre-filter
  already-complete items the same way automatic mode does, instead of
  prompting for items that need nothing.
- **Settings:** in-progress edits are guarded by an unsaved-edit gate, so
  deselecting a field can no longer silently discard them.
- **Launching & navigation:** gamepad input and attract mode stay suspended
  while a detached launch is active; restoring the navigation stack can no
  longer resurrect stale entries; launch arguments that are empty are quoted
  correctly; and cancelling the chooser twice in quick succession is
  debounced.
- **Legacy INI import:** percent-encoded characters in legacy INI files now
  round-trip correctly, with a `.legacy.bak` backup written before rewrite.
- **Stability:** closed a use-after-free window in the artwork cache's size
  walk, bounded cache teardown so shutdown can't hang on a stuck worker,
  guarded truncated clrmamepro DAT files, and fixed a playlist row-reuse bug
  that could attach entries to the wrong scope.
- **Accessibility:** the cover flow view exposes proper accessible roles and
  names to screen readers.
- **Scraper — "download only what's missing":** now correctly skips items that are
  already complete. Metadata counts as done only when its core fields are filled
  (so partially-filled entries get completed instead of being skipped forever),
  and existing artwork is now recognised regardless of the media folder's letter
  case — previously mixed-case folders (e.g. `box-2D-back`) went unrecognised on
  case-sensitive filesystems, so every item was needlessly re-scraped on each run.
- **Scraper — "download only what's missing" no longer re-chases unavailable art:**
  when you request media types a provider doesn't supply for an item (e.g.
  ScreenScraper has no map/marquee for most PlayStation games), the item is now
  remembered as complete instead of being re-scraped on every run. The provider is
  asked once; if it later starts supplying the type, that's picked up automatically.

## Older releases

Releases 0.0.1 through 0.0.15 are archived in
[docs/changelogs/v0.0.x.md](docs/changelogs/v0.0.x.md).

[Unreleased]: https://github.com/EtherAura/Kartend/compare/v0.0.18...HEAD
[0.0.18]: https://github.com/EtherAura/Kartend/compare/v0.0.17...v0.0.18
[0.0.17]: https://github.com/EtherAura/Kartend/compare/v0.0.16...v0.0.17
[0.0.16]: https://github.com/EtherAura/Kartend/compare/v0.0.15...v0.0.16
