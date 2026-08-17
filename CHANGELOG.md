# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **One Sidebar settings page — pick each side panel's side and shape.**
  A single page in Settings to put the details pane and the new collection tree on
  the left or the right, per collection, plus a new **justification**
  choice for each: *Below toolbar* keeps the classic layout where the
  toolbar spans the full window width, while *Full height* lets a panel
  run the entire height of the window with the toolbar stopping at its
  edge. The details pane's existing Position control keeps working — it
  and the new page edit the same setting.

- **The shoulder buttons now toggle the sidebars.** On a gamepad, L1
  folds the left sidebar (the collection tree) and R1 the right one (the
  details pane) — matching their physical sides. Existing custom bindings
  are preserved; the old Y-button default migrates automatically. Keyboard
  shortcuts remain F6 and F9, and both bindings stay remappable under
  Settings → Controls.

- **The collection tree opens fully unfolded and reads better at a
  glance.** Every branch now starts expanded — only the branches you close
  are remembered (across sessions). Category rows — anything with children,
  Playlists included — show a bold label over a faint band so structure
  stands out without relying on icon size. The branch connector lines are
  gone by default (the chevrons carry the structure); a new **Tree lines**
  option on the Sidebar page brings them back. The mouse wheel now scrolls
  the tree when the pointer is over it, instead of changing the selected
  item in the grid — and once the tree hits its top or bottom, further
  ticks do nothing rather than leaking into the grid. When the tree is
  hidden, a slim arrow marker stays at its dock edge; clicking it brings
  the tree back (same per-collection memory as F6). And the hairline outline that keeps low-contrast logos
  readable in the tree is applied to item tiles and cards too — only for
  logo-like art that would blend into the card colour; photos and covers
  are never touched.

- **Collection and platform artwork now arrives with the collection scrape —
  and your sidebar shows it.** Scraping a collection also fetches its
  platform art (logo, illustration, background) and wires the logo in as the
  collection's icon, including for every subcollection pulled in through a
  parent. Hand-made manufacturer collections get logos too: publishers seen
  during game scrapes are remembered and matched to your collection names
  (a "Sony" collection claims "Sony Computer Entertainment"), with Wikimedia
  Commons as a fallback source for anything ScreenScraper cannot name —
  vector SVGs preferred, so logos stay crisp at any size. Matching also runs
  at startup, so art that landed while the app was closed appears on launch.

- **The collection tree shows each collection's artwork.** Rows display the
  same icon your tiles resolve (the collection's own icon, or an image named
  after it in the parent's artwork folder), left-aligned, never wider than
  the panel, and with a hairline outline added automatically when a logo
  would blend into the theme background. The panel is now resizable by
  dragging its inner edge, remembers its expansion across sessions, and
  offers per-collection icon options on the Sidebar settings page:
  icons-only rows, icon height, and colour, monochrome (dark/light) or
  accent-tinted rendering — monochrome uses the real monochrome/SVG logo art
  when the scrape has delivered one.

- **"Copy Settings From" now transfers sidebar preferences.** A new
  Sidebars category covers every details-pane and navigation-sidebar
  option, and is included when you copy everything.

- **A collection tree panel — see your whole library at a glance and jump
  anywhere in one click.** A new panel lists every collection as a
  collapsible tree (nested subcollections included; a collection linked
  under several parents appears under each), with playlists grouped in
  their own section below. Clicking a row jumps straight to that
  collection, and the highlight follows you however you navigate — tiles,
  breadcrumbs, the command palette, or the back key. The panel is chrome
  you control per collection: toggle it with **F6** (or View → Show
  Collection Tree, a rebindable key, or a controller button you assign),
  dock it on the left or right via View → Collection Tree Position, and
  each collection remembers the visibility and side you chose while
  browsing it. Arrow keys work inside the panel when it has focus, and it
  picks up each collection's accent color like the rest of the window
  furniture.

- **A release split across several discs can now appear as one item.** A
  recording or film that arrives as `Recital (Disc 1).flac` and `Recital
  (Disc 2).flac` used to browse as two tiles, each holding half of
  something you think of as one thing. Turn on **Group multi-disc releases
  into one item** for a collection and files whose names differ only by a
  disc marker — `(Disc 1)`, `(CD 2)`, `[Side A]`, with `disk` spelled
  either way — collapse into a single tile named for what they have in
  common, which plays every part in order through a playlist Kartend
  generates. That playlist lives in Kartend's own data directory, never
  beside your media: the feature reads your folders and does not write to
  them. Metadata is merged rather than picked from whichever file was
  scanned first — the first disc wins where two discs disagree, later
  discs fill in anything it lacks, and tags accumulate across all of them,
  so artwork or notes attached to a later part are not lost. Anything you
  edit on the grouped item afterwards outranks all of it and survives
  rescans. An art folder filed per disc needs no renaming either: a
  grouped item with no cover of its own now takes the art of its lowest
  disc, and a cover named for the release still wins where you have one.
  Grouping is per-collection and off by default, so existing
  libraries look exactly as they did until you ask for this. A file
  standing alone is never collapsed, and identically-named releases in
  different folders stay separate. Turning it back off restores the
  individual items, per-disc notes and ratings included, and removes the
  playlists it generated — nothing is left behind either way.

- **Games installed while Kartend is open now appear on their own.** A
  launcher collection used to refresh at startup or when you asked it to, so
  a game installed in Steam or Heroic mid-session stayed invisible until the
  next launch. Kartend now watches the folders those launchers write their
  manifests into and re-syncs shortly after they change. It waits for the
  dust to settle first — a Steam download rewrites its manifest repeatedly
  while it runs, and the sync is silent and idempotent, so nothing interrupts
  you and a burst of writes still costs one pass. Only sources you have
  actually imported are watched.

- **A smart playlist can hold more than one rule.** Until now it held
  exactly one, so "recently launched" and "favourite" were each expressible
  but "recently launched *and* favourite" was not. A playlist now takes a
  list of rules and matches items that satisfy **all** of them or **any** of
  them. Each rule keeps its own ordering and its own limit — "top 20 played"
  still means the top 20 of that rule, not the whole set trimmed afterwards
  — and the results are ordered by the first rule you wrote. An item
  matching two rules appears once. Existing playlists are untouched: a
  one-rule playlist is stored exactly as it always was, so nothing needs
  migrating and nothing is rewritten on upgrade.

- **Launch parameters can now name the parts of an item's path.** A
  template could already place the whole path with `%1`; it can now also
  use `%name%` for the title, `%dir%` for the containing folder, plus
  `%filename%` and `%ext%`. That is enough to tell a player which title to
  show in its window bar, or to point a subtitle flag at the file sitting
  next to the video — both of which previously meant wrapping the launcher
  in a shell script. Tokens are substituted inside each argument after the
  parameter string has been split, so a title containing spaces stays a
  single argument instead of becoming several. Items imported from Steam,
  Flatpak and the like launch through a shortcut rather than a file on
  disk: `%name%` still gives their title, the path-part tokens come out
  empty, and **Preview launch command…** now says so instead of leaving you
  to wonder why an argument went blank.

- **ES-DE libraries can be imported, one collection per system.** File →
  Import → "Import from Launcher…" now detects ES-DE and brings in each of
  its systems as its own collection — SNES games in a SNES collection, PS2
  in a PS2 one — because each system needs its own emulator and a single
  mixed collection could never launch them all. Titles, descriptions,
  developers, genres and the artwork ES-DE has scraped all come across, and
  games you hid in ES-DE stay hidden. The games come from your ROM folders
  rather than from ES-DE's metadata files, so you get your whole library
  and not just the parts you happen to have scraped. One thing is left to
  you: each collection arrives without a launcher, because ES-DE keeps its
  emulator settings somewhere Kartend cannot read — point it at your
  emulator once, as you would for any ROM collection. The import tells you
  so when it finishes, and if you start a game before setting one, Kartend
  names the collection and where to set it rather than just refusing.

- **Heroic and itch.io games now arrive with their cover art.** Neither
  launcher keeps covers as files on disk, only as web links, so those
  collections used to import onto blank placeholder tiles. Kartend now
  fetches each cover in the background right after the import — from the
  launcher's own artwork, matched by the id the launcher recorded rather
  than guessed by name — and the grid fills in as they land. Art you
  scraped or placed by hand is never overwritten, a re-sync only fetches
  what is still missing, and a failed download costs you nothing but that
  one cover.

- **Launcher import now covers Heroic, itch.io, Bottles, and your
  application menu.** Alongside Steam, Flatpak, and Lutris, File → Import →
  "Import from Launcher…" now detects four more sources and builds the same
  ready-made, self-syncing collections from them. **Heroic** brings in the
  Epic, GOG, Amazon, and sideloaded games it has installed, launching each
  back through Heroic so your per-game Wine/Proton settings still apply.
  **itch.io** brings in what the itch app has installed, skipping the
  tools, asset packs, and soundtracks itch also hosts. **Bottles** brings
  in the programs you added to each bottle, launched through `bottles-cli`
  in the right bottle — with the bottle's name in the title when you keep
  more than one. **Desktop Menu** covers everything else: games installed
  by your package manager, found the way your application menu finds them,
  including their icons. Sources never step on each other — a menu entry
  that belongs to Steam, Lutris, Heroic, Bottles, itch, or Flatpak is left
  to the source that owns it, so ticking every box imports no duplicates —
  and entries left behind by uninstalled packages are skipped. Every source
  brings whatever art it has: on-disk art is copied, and the covers Heroic
  and itch.io keep only as web links are fetched in the background (see
  above).

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
  overwrites art or fields you already have. Flatpak games get the same
  automatic background pass from **Flathub** — description, developer,
  genre, release date, and licence, resolved by exact app id — so apps that
  no game database has ever heard of still arrive with a filled-in details
  pane, and a Flatpak collection stays pinned to the Flathub scraper for
  later manual scrapes.

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

- **Two hard-to-describe glitches can now be traced instead of guessed at.**
  Running with `KARTEND_PERF_TRACE=1` reports when Cover Flow rebuilds its
  card list — including how much already-decoded artwork each rebuild throws
  away — and which of the two automatic paths moved the selection, a restore
  finishing late or attract mode advancing on its timer. Both are silent
  unless asked for and cost nothing when off. They exist because "the covers
  load in and out" and "the selection reverted" are almost impossible to
  attribute by watching the screen: the trace names the culprit in a single
  run (Kartend-i3mmq, Kartend-ic4h6).

- **Ctrl+Shift+I opens "Import from Launcher".** It was reachable only through
  File → Import → Import from Launcher, two levels of menu, and re-running it
  is the ordinary way to pick up games installed since the last import — so
  the trip was one you made repeatedly.

### Changed

- **Importing a .kart can no longer touch files you already have.** An
  import used to unpack straight into whatever directory you picked — and
  a hostile bundle didn't need any path trickery to abuse that: pointed at
  a directory full of your files (say, your home folder), it could drop
  `.config/autostart/…` entries or overwrite dotfiles using perfectly
  ordinary relative paths. Imports now always unpack into a **new folder
  named for the bundle** inside the directory you choose; the preflight
  dialog tells you the folder's name and how many files will be written
  before you pick it. If a non-empty folder by that name already exists,
  the import is refused rather than merged over it. Underneath, the
  extractor itself now refuses any non-empty target — dotfiles count —
  never overwrites an existing file, and rejects a bundle carrying two
  entries that would land on the same path, including pairs that only
  collide on Windows or macOS filesystems (Kartend-qbfk1).

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
- **Removing a launcher collection can now clean up after itself.** The
  removal prompt gains an opt-in checkbox — "Also delete the imported
  shortcut files and copied artwork" — for collections that came from a
  launcher import. It only ever deletes inside Kartend's own managed
  `launcher-imports` folder: a collection you re-pointed at your personal
  artwork folder keeps that folder untouched even with the box ticked, and
  anything else you stored alongside the managed folders survives. Left
  unchecked (the default), the imported files stay behind for a future
  re-import to pick up, as before (Kartend-i366w).

### Fixed

- **A .kart backup no longer forgets your notes, ratings, pins — or your
  hand-picked covers.** Exporting a collection silently dropped the
  personal half of each item's metadata: notes, rating, source URL and
  the pinned / hidden / continue-later flags never entered the bundle,
  and hand-linked artwork (covers you attached through the links dialog
  or the Artwork Wizard) wasn't bundled at all — so an export-then-import
  quietly lost exactly the data you can't re-scrape. All of it now
  round-trips: the six fields travel in the manifest (older bundles
  still import, with those fields simply unset), linked artwork files
  are copied into the bundle and re-linked on import — without ever
  replacing a link you already made on the importing machine — and the
  merge dialog offers the new fields per-side like every other field,
  with the safety rule that a merge never silently clears a local pin.
  The backup guide's "lost on a round-trip" warning is gone because the
  loss is gone.

- **Clicking empty space in Cover Flow no longer throws the carousel
  back to the first item.** A left-click that missed every card — easy
  to do given the carousel's large margins, or when clicking the window
  just to focus it — fell through to the item grid's click handling.
  The grid is hidden while Cover Flow is showing, so the click found
  nothing there and cleared the selection, which the carousel rendered
  by gliding to the first item with a selection border while the
  position counter and thumbnail strip still showed the item you were
  on. Clicks that miss the cards are now simply ignored in Cover Flow,
  and a cleared selection no longer masquerades as "the first item is
  selected" — the carousel holds its place until a real selection
  arrives.

- **A selection made with the mouse wheel no longer snaps back a moment
  later.** Scrolling the wheel moved the selection but — unlike the
  arrow keys — never recorded the new position as the collection's
  remembered selection. Anything that reloads the view a moment later
  (a background rescan finishing, a settings save, the
  remember-selection restore) would then politely put the selection
  back where the record said it should be: wherever you were *before*
  the wheel. Wheel moves are now recorded exactly like keyboard moves,
  in every view. Cover Flow, where this was most visible, also no
  longer runs the grid's scroll animation against its hidden item
  grid — that animation's completion could re-center the carousel on
  stale geometry a second and a half after the wheel stopped, and its
  half-armed scroll state was left dangling with nothing to clear it.

- **A real front cover now beats the composite image on the tile.** Two
  separate leftovers of older scrapes conspired to keep the multi-panel
  composite (`mixrbv`) image on grid tiles and cover-flow cards even
  when a proper front cover exists. First, older scrapes kept a copy of
  "the best cover available at the time" directly in the artwork
  folder — often the composite — and the artwork folder was always
  searched before `front/`; the search now looks in `front/` first.
  Second, every scrape records where it saved its non-standard images
  (`mixrbv1`, `box-3d`, …) so the sidebar gallery can list them — and
  that bookkeeping record was mistaken for a hand-linked cover, which
  outranks everything. Those records no longer drive the tile: only a
  link on a standard cover type (what the links dialog and the Artwork
  Wizard create) does. Everything else keeps its old rank: a cover you
  drop directly into the artwork folder still beats the box, mix and
  screenshot fallbacks, a hand-set link still beats everything, and the
  composite still shows for items that genuinely have nothing better.
  If you *want* a composite as an item's tile face, link it on the
  Front Cover type.

- **Importing a `.kart` package now shows you what it will run, and asks
  before it registers it.** A package brings its own launcher settings —
  the program to start, the core to load and the arguments to pass — and
  those were chosen by whoever built the package, not by you. Kartend
  weighed only the folder the program sits in against a short list of
  ordinary locations, so a package naming something that lives in one of
  them was registered in silence, and the preflight review reported no
  validation issues while never having looked at the arguments at all.
  The arguments are the half that decides what a program actually does.
  Preflight now lists every launcher setting a package carries, each value
  written out exactly as it will be used, and an import carrying any of
  them asks you to confirm before it is registered — including a
  drag-and-drop import, which never showed the review in the first place.
  Ordinary-looking settings are listed quietly; ones that read as unusual
  are called out, whether that is a program whose arguments are themselves
  instructions, an argument carrying a command of its own, or a file the
  package shipped for itself. That last case is now recognised for cores
  and arguments too rather than only for the program, which matters
  because a core is read into the player rather than started as a program
  of its own. The all-clear banner is now reserved for a package that asks
  to run nothing at all, so it can no longer vouch for settings nothing
  examined. A package you exported yourself imports exactly as before —
  you simply get to see what is in it first (Kartend-kxqqf,
  Kartend-f8y08).

- **A cover you assign by hand now counts as artwork, and the artwork wizard
  stops offering you items you have already done.** Kartend finds covers two
  ways: by matching file names in a collection's artwork folder, and by the
  per-item links you set yourself through **Edit artwork links…** or the
  **Assign Missing Artwork…** wizard. Only the first counted. So the *Has
  artwork* and *Missing artwork* smart playlists, the `has:artwork` and
  `missing:artwork` search terms and the missing-artwork count in Collection
  Health all reported a hand-linked item as having nothing — and the wizard,
  whose worklist is built the same way, handed you back every item you had
  just assigned the next time you opened it, with no way to work through a
  large library short of renaming files. Links now count everywhere those do,
  and they count the moment you save one rather than at the collection's next
  scan, so the worklist shrinks as you go. A link only counts while the image
  it points at is still there: delete that file and the item goes back to
  whatever its name matches, rather than claiming a cover nothing can show.
  Links on `logo` and on custom artwork types stay gallery-only, since those
  are never used as an item's cover. One case still waits for a scan —
  clearing a link on an item that also has a name-matched cover reports it as
  missing artwork until that collection is next scanned.

- **A cover you assign by hand now shows on the item's tile.** Assigning one
  put it in the sidebar gallery and on the detail page, but the grid went on
  painting the procedural placeholder, and the cover-flow card the same — the
  two surfaces that actually show you a cover were the two that never looked at
  your links. Picking an image by hand is how you fix what automatic matching
  gets wrong, so it was the one case where the fix did not visibly take. Tiles
  and cards now show a hand-linked cover, and show it ahead of anything matched
  by file name, which is the order the rest of Kartend has always used. The new
  cover appears as soon as you save the link rather than waiting for you to
  leave the collection and come back. The **hide missing artwork** filter
  follows: an item kept off the grid because nothing in the artwork folder
  answers to its name comes back the moment you link a cover for it, so the
  filter no longer hides an item that would render one. A link to a file you
  have since deleted still shows nothing, and links on `logo` or a custom
  artwork type remain gallery-only.

- **Collections that keep artwork in matching subfolders now find it even
  when the content folder is a symbolic link.** With **Include Artwork
  Subfolders** on, Kartend looks for an item's cover in the artwork subfolder
  matching the item's own content subfolder — and working out which subfolder
  that is assumed the item sat under the content folder exactly as you spelled
  it. A collection pointed at a symbolic link does not: its items are known by
  the real location behind the link, which is a different path. The
  disagreement turned into a chain of `..` steps that climbed out of the
  artwork folder entirely, so the search happened somewhere belonging to
  neither setting and every tile in a subfolder fell back to the placeholder.
  An item that does not sit under the content folder now has its cover looked
  for in the artwork folder itself, which is the same place a scan records it,
  so tiles and the scan's record agree. Cover flow resolved covers through its
  own copy of this and had the same fault; both now share one answer.

- **Everything that answers "does this item have a cover?" now agrees with
  what you can see.** The *Has artwork* and *Missing artwork* smart-playlist
  rules, the `has:artwork` and `missing:artwork` search terms, the
  missing-artwork count in Collection Health and the artwork wizard's
  worklist all read a record of the cover found for each item — and the scan
  never wrote that record. So they reported an entire library as artless
  while its tiles painted covers perfectly well: *Missing artwork* returned
  everything, *Has artwork* returned nothing, and the health dashboard put
  the missing count at 100%. Scanning a collection now files each item's
  cover as it finds it, using the same search the grid uses — the artwork
  folder and its typed subfolders, matched on the item's name, including the
  case where a multi-disc release takes the art of one of its discs. The
  record is refreshed on every scan, so art you add or delete is reflected
  the next time that collection is scanned, and the grid itself is unchanged:
  it still looks at your folders directly and remains the last word on what
  you see.

- **A subcollection's chosen icon shows on its tile again.** Setting
  `collectionIcon` on a subcollection left its Grid tile on the striped
  placeholder unless the image file happened to be named after the
  subcollection itself — so pointing "Games" at `SuperTuxKart.png` showed
  nothing, while the same file renamed to `Games.png` worked. The tile fell
  back to the placeholder silently, which read as missing artwork rather
  than a setting being ignored.

- **Games whose icon is an SVG now get a cover when imported from the
  application menu.** Modern desktop packages increasingly ship only a
  scalable icon, and those imported with no artwork at all — on a test
  machine, only one of three menu games had a cover. Scalable icons are now
  rendered into the collection's artwork folder, drawn at full size from the
  vector so they stay sharp on a 4K grid rather than being blown up from a
  small preview. Icons that only exist at small sizes (48 or 32 pixels) are
  picked up too; a small cover beats none. A real raster is still preferred
  whenever the icon theme has one.

- **The details sidebar shows a file's size and modified date again.** Both
  read "…" and stayed that way for any item without scraped metadata —
  which, in a freshly scanned library, is most of them. The values were
  being fetched correctly in the background but only ever painted onto the
  File tab, while the Item tab shows the same rows whenever an item has no
  metadata to display instead. A file that has since been deleted now
  settles on "-" rather than sitting on the loading placeholder for good.

- **Custom placeholder artwork now actually renders.** The per-collection
  "Placeholder Artwork" image — the picture shown in place of the
  procedural cross-hatch for items with no artwork — never appeared: the
  path check it went through only accepted folders, so pointing the
  setting at a real image file silently resolved to nothing and every
  layout kept the cross-hatch. Configured placeholder images now show up,
  and `~` and `%collection%` in the path work as the documentation always
  said (Kartend-80h8o).
- **A manual rescan can no longer be short-circuited by background
  activity.** "Rescan collection" waits for its cache to actually clear
  before reloading; it used to proceed on the first cache-clear
  notification from *any* collection — such as a launcher collection's
  background metadata refresh finishing at that moment — and could then
  quietly reload the old, uncleared data instead of rescanning. It now
  waits for its own collection's clear specifically (Kartend-1fhgz).
- **Background images, background videos, header logos, and the startup
  video accept `~` paths now too.** The same class of problem as the two
  fixes below: these four "single asset path" settings were used exactly
  as typed, so a value written as `~/wallpapers/space.png` in a
  hand-edited config or a theme preset silently showed nothing. All now
  resolve `~` (and `%collection%`, for the per-collection three) through
  the same shared rule as the collection icon (Kartend-4wa6i).
- **Collection icons written with `~` or `%collection%` now resolve.** The
  `collectionIcon` path was used exactly as stored by all three places that
  render it — the Cover Flow card, the marquee banner, and the Grid/List
  subcollection tile — so an icon configured in a hand-edited INI or an
  imported `.kart` collection as `~/icons/films.png` silently rendered as no
  artwork everywhere, even though the documentation promises `~` works for
  asset paths. All three now resolve the icon through one shared rule:
  trimmed, `~` expanded to your home folder, and `%collection%` replaced
  with the collection's name — the same treatment other configured paths
  get (Kartend-dkh90).
- **A launcher collection's background metadata fetch no longer rebuilds
  the view you are looking at.** When the automatic Steam store-details
  fetch finished for an imported collection, the app refreshed whatever
  collection was currently on screen instead — a full reload with visible
  scroll, artwork, and carousel churn, seconds after startup, even on huge
  unrelated collections. The refresh now stays invisible unless you are
  actually viewing the imported collection: anywhere else, the fetched
  details are simply there the next time you open it (Kartend-xkdxn).
- **Collections with "hide items without artwork" enabled no longer open
  empty.** With the per-collection toggle on, opening the app on such a
  collection — or switching into one — showed a permanent "No items" even
  though the title bar counted them correctly, in every view. The filter
  passed judgement before the item list (or the artwork folder's freshly
  started scan) had produced any data, hid every row as "missing artwork",
  and hiding everything also stopped the loading those rows would have
  triggered. Rows now stay visible while their data or the artwork scan is
  still pending, and genuinely artwork-less items are hidden as the answers
  arrive; the "N / M items" readout tracks each step. In Cover Flow this
  also avoids rebuilding the carousel — and discarding its already-decoded
  covers — once per arriving batch while such a collection loads
  (Kartend-l66sn).
- **Translations of the first-run wizard's media-folder page can now be
  found at runtime.** The page's nine strings were catalogued for
  translators under one name but looked up under another, so any future
  translation of them would have silently fallen back to English. The two
  names now agree, translation tooling runs warning-clean, and the build
  gate fails if a mismatch of this kind is ever reintroduced
  (Kartend-r4tno).
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
- **The full-size artwork preview now actually appears in Cover Flow, and the
  strip's thumbnails show a hand cursor.** The preview window was attached to
  the item grid — which Cover Flow hides while the carousel is on screen — so
  asking for a preview there built one and displayed it into something
  invisible, and the click looked dead. It now attaches to the main window, so
  it works from every view; the same preview opened from the grid covers the
  whole window rather than just the item area. The strip also gave no hover
  feedback, so nothing suggested the thumbnails could be clicked
  (Kartend-4hr3d).
- **Clicking a thumbnail in Cover Flow's artwork strip now opens it full
  size, and the strip no longer flickers.** A click used to swap the centred
  cover for the artwork you clicked, which is not what the strip is for — the
  card already shows the cover, and the reason to click a thumbnail is to look
  at that image properly. It now opens full size, the same as clicking a
  thumbnail in the sidebar gallery. Separately, the strip rebuilt its
  thumbnails every time the selection settled, even when the artwork list had
  not changed, so the row visibly blinked back to blank tiles and re-decoded
  itself; an unchanged strip is now left alone (Kartend-4hr3d).
- **The selected item no longer reverts on its own.** When a collection
  opens, Kartend restores your last selection and then double-checks a moment
  later that the restore stuck. That check read "the selection isn't on the
  restored item" as "the restore failed" — but it is equally what things look
  like once you have simply moved on, so it put the old item back. Clicking in
  the grid and using the arrow keys told the check to stand down; selecting in
  Cover Flow and scrolling the selection with the mouse wheel did not, so a
  second or two after opening a collection your new selection could snap back.
  Separately, a selection moved by the wheel was never written to the place
  the restore reads from, so any background refresh — the Steam store details
  fetch finishing, for instance — would reload the view and faithfully restore
  a selection you had already moved away from. All three are fixed: both input
  paths now stand the check down like every other one, and every reload first
  records the selection you actually have, so what comes back is where you
  were (Kartend-ic4h6).
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
- **Artwork filed in folders that mirror your media now counts as artwork.**
  Where a collection's artwork folder repeats the shape of its media folder —
  a cover in `Artwork/Concertos/` for a recording in `Media/Concertos/` — the
  grid painted those covers, but nothing that asks the library whether an item
  *has* artwork agreed. The *Has artwork* and *Missing artwork* smart
  playlists, the `has:artwork` and `missing:artwork` search terms, the
  missing-artwork count in Collection Health and the artwork wizard's worklist
  all called every item in a subfolder artless, because a scan looked only at
  the top of the artwork folder while the grid looked inside the matching one.
  A scan now looks where the grid looks, folder by folder, so what the library
  records matches what you can see. Collections whose artwork sits in one flat
  folder resolve exactly as they did (Kartend-35wqh).
- **A cover you assign by hand now gives its row an artwork preview in List
  view.** Hand-linked covers already counted everywhere else, but a List row
  showed no preview button unless a file named after the item happened to
  exist as well — so for an item whose link was the only cover it had, the
  row looked bare next to the same item's tile in Grid. The button appears for
  a linked item now, and opens the image you linked rather than hunting for
  one by name, which is also what it does when the item's collection has no
  artwork folder configured at all (Kartend-ni68u).
- **A grouped multi-disc release filed in a subfolder now finds its cover.**
  With **Group multi-disc releases into one item** on, a release whose discs
  sit in a subfolder of a collection that mirrors its artwork folders showed
  the placeholder, even with `Recital (Disc 1).png` filed right beside the
  discs. Grouping gives the release a playlist of its own to play, and the
  cover search followed that playlist — which lives in Kartend's data folder,
  not next to your media — so it looked for the art at the top of the artwork
  folder instead of in the one matching where the release actually sits. It
  now looks where the release is (Kartend-srg3i).
- **Hide missing artwork no longer hides items that have artwork.** For a
  collection whose artwork folders mirror its media folders, turning on **hide
  missing artwork** made items in subfolders disappear — including ones whose
  cover was sitting in the matching artwork subfolder and visibly painted on
  the tile. The filter only ever looked at the top of the artwork folder, so
  anything filed deeper counted as missing. It now looks in the same folder
  the tile does (Kartend-7f76f).
- **A scrape no longer replaces a cover you chose yourself.** The scraper
  documentation said a cover you had linked by hand was left alone; nothing
  actually checked, so a scrape overwrote it — and the mode that overwrites
  is the one most scrapes run in. If an item has a cover you linked through
  **Item Artwork Links** or the **Assign Missing Artwork…** wizard, a scrape
  now skips that artwork type and says so in its summary, whichever re-scrape
  mode you picked. Other artwork types on the same item still scrape as
  before, and re-scraping something the scraper itself fetched earlier is
  unaffected. The link stops protecting the type once the image it points at
  is gone (Kartend-yibgw).

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
