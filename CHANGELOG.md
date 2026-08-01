# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

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
