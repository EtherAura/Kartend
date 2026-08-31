# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- **Dim hint text is readable on dark themes again.** The muted "hint"
  style used across dialogs took the theme's mid palette colour, which on
  dark themes lands nearly on the dialog background — the Setup Wizard's
  folder-page note about recursive scanning was effectively invisible.
  Muted labels now repair their colour against the actual window
  background up to a minimum contrast, and stay exactly as dim as before
  on themes where they were already legible.

- **Re-running the Setup Wizard no longer promises "your first
  collection."** The wizard doubles as the advertised way to add more
  collections (Help → Setup Wizard…), but its welcome text and
  folder-page subtitle read as if the library were empty. With at least
  one collection configured they now say "a new collection"; the true
  first run keeps the original wording.

- **Collections named after an extended console resolve to the right
  ScreenScraper system.** When a collection's name matched two nested
  system aliases — "Game Boy" inside "Game Boy Advance" — catalog order
  decided the tie, which could hand the collection's platform identity
  (and its scraped platform artwork and metadata) to the base console.
  The longer, more specific alias now wins the tie from either catalog
  position.

## [0.0.19] - 2026-08-30

### Added

- **Smart playlists can be built from more than one rule.** The editor
  gained **Add rule**, and with two or more rules a **Match** selector
  above them choosing *all of the rules* (an intersection) or *any of
  them* (a union) — so "never launched **and** a PDF" is one playlist
  instead of a workaround. **Remove** drops a rule; the last one stays,
  since a playlist with nothing to match on has no meaning. OK stays
  disabled until *every* rule is filled in, not just the first — a
  match-all set with one blank rule would quietly match nothing.
  Existing playlists are untouched and store exactly the bytes they did
  before.

  This also fixes a way multi-rule playlists could be silently mangled.
  The storage format has understood rule sets for a while, and a
  `smart_filter` written by hand or by another tool was honoured when the
  playlist was displayed — but importing one, from a `.kart` bundle or a
  playlist JSON, kept only its first rule and reported success. The
  format deliberately repeats the first rule at the top level so older
  builds can still read a newer playlist, which is exactly why the loss
  was invisible.

- **The collection tree's two bindings are settable without editing a
  file.** Both already worked — a key to show or hide the navigation
  sidebar, and a gamepad button for the same — but neither had a row in
  the settings dialog, so the only way to change them was by hand in
  `kartend.cfg`. Settings → Controls now carries *Toggle Collection Tree*
  in both the keyboard and gamepad sections, the latter with the same
  Detect… button the other gamepad bindings use. The keyboard one ships
  unbound and can be cleared back to unbound, like Home View; the gamepad
  one stays on L1.

- **Filters worth composing twice can now be kept.** The toolbar's filter
  dropdown gained **Save current filter as…**, a list of whatever you have
  saved, and **Manage saved filters…**. A saved filter holds the search box's
  contents together with the state around it — sort order,
  exclude-subfolders, the type filter, hide-subcollection-tiles — so
  "unplayed soundtracks, newest first" comes back by name instead of being
  retyped. Applying one sets all of it, including the search box and the
  View menu's sort selection, which the search box alone could not carry.
  A filter can hold no search text at all and just be a sort-and-filter
  arrangement. The list is global rather than per-collection, and lives in
  `search_presets.json` beside the config file, so it survives upgrades and
  can be copied between machines.

- **Release downloads can now be checked against where they were built.**
  Every asset the release pipeline builds — the source tarball, the `.deb`,
  the Windows portable `.zip` and the Windows installer — now carries a
  build provenance attestation recording which workflow, at which commit,
  produced those exact bytes. `gh attestation verify <file> --repo
  EtherAura/Kartend` answers "did this really come from Kartend?", which
  the `.sha256` sidecars could not: anyone able to swap an asset could swap
  its checksum file alongside it. The Windows assets are attested after
  signing, so the attestation covers the bytes the release page actually
  serves. This is provenance, not code signing — the Windows builds are
  still unsigned and SmartScreen still warns on first launch. Each release
  also publishes a CycloneDX software bill of materials, one for the Linux
  assets and one for the Windows assets, both attested alongside the
  binaries. The Windows document is the one that closes a real gap: it
  lists the Qt runtime and the SDL2 and zstd libraries bundled next to the
  executable, which a Windows user has no package manager to ask about.

- **New collections arrive with their artwork already fetched.** Creating a
  collection — adding one in settings, duplicating an existing one,
  importing a `.kart` bundle, or picking one up from an installed
  launcher — now fetches its logo and background in the background, with
  no dialog and no separate step, so the sidebar and home icons fill in
  as part of building the collection instead of staying blank until you
  remember to ask. It is deliberately quiet: no progress window, no
  completion box, and a collection whose platform cannot be identified is
  a miss rather than an error, with the details in the scrape log. Only
  collections that have a scraper configured and no artwork set yet are
  touched, so a hand-picked icon is never replaced, and renaming a
  collection does not re-fetch anything. Collections created while
  another scrape is running wait for it to finish rather than being
  dropped. The switch is "Fetch collection artwork when a collection is
  created", on the scraper settings page, and it is on out of the box;
  turning it off restores the previous behaviour of fetching only when
  asked.

- **The right stick reaches the toolbar on its own.** Flicking up while
  the grid or list has focus now hops straight to the toolbar — no more
  holding Select first — and flicking down from the toolbar drops back.
  Left/right still walks between the navigation sidebar, the grid/list,
  and the details pane. While the pane's ring is mid-walk the vertical
  axis stays with the pane, so stepping back up through its regions
  still works; the ring's one-second idle timeout hands up-to-toolbar
  back afterwards.

- **System scrapes now bring home the whole press kit.** A platform scrape
  pulls every still image its ScreenScraper catalog advertises — wheels,
  icons, bezels, photos, whatever exists — plus the console photograph
  from Wikimedia Commons, all landing in the collection's shared art
  (config-wired icon/logo/background picks are unchanged). The details
  pane shows them: the collection views grow the same thumbnail gallery
  items have, on both the overview and the Collection tab, with clicks
  swapping the big preview. Game platforms also gain a scraped spec
  sheet — CPU, GPU, console generation, units sold, predecessor and
  successor — resolved from Wikidata in the same batched lookup and
  rendered as rows under the description; the field registry is
  per-media-type, so film and music collections can grow their own
  sheets later. Very wide wordmark art — scraped platform wheels like the
  SNES logo — now sits inside side margins on grid tiles instead of
  spanning the card edge-to-edge; box covers and screenshots are
  untouched. Art wiring also learned scope precedence: the per-system
  catalog's platform art can no longer be displaced in the collection
  icon/logo slots by the Wikipedia/Wikidata name-search fallback (whose
  logo, running later in the same queue, used to steal the slot — and on
  some entities is a degenerate SVG that renders as a zoomed crop).
  Platform art also picks its region variant more sensibly: an explicit
  region choice wins as before, but the generic "World" default now defers
  to the machine's locale before ScreenScraper's world tag — a US machine
  gets the orange Dreamcast swirl instead of the blue PAL one, while
  European machines keep the PAL branding. And because platform art files
  carry no region in their names, entity art under the "Fill missing"
  re-scrape policy now byte-compares instead of blindly keeping the
  existing file — a changed region preference actually replaces the old
  variant on the next scrape, while unchanged art still isn't rewritten.

- **Collections now get scraped metadata of their own — description,
  manufacturer, release year — not just artwork.** The scraper's
  collection/platform pass, which already fetched logos and backgrounds,
  now also captures what the entity *is*: ScreenScraper contributes each
  console's manufacturer and production span, and a new Wikipedia/Wikidata
  source contributes a prose description, the manufacturer, and the release
  year for any collection whose name matches a real thing — a console, a
  film franchise, a studio, a label. Every collection scrape fetches it:
  the entity jobs that ride along with an ordinary scrape now include a
  collection-data job even when the collection's own scraper (like
  ScreenScraper) doesn't speak collection-level data — those route to the
  Wikipedia/Wikidata source automatically. Re-running the scrape refreshes
  it. What lands is shown in the details pane's collection views: the
  Collection tab and the no-selection overview both lead with the
  description, manufacturer and release year. The right-click entry is now
  called *Scrape collection info & artwork* to match.

- **New collections can fetch their own info at creation.** The
  create-collection dialog gains a *Fetch collection info & artwork online
  after creating* checkbox — tick it and the scraper's collection pass runs
  as soon as you leave Settings, so a freshly-made "Nintendo 64" collection
  arrives with its logo, description and manufacturer already in place.
  Off by default; the box remembers your last choice.

- **Collections now carry a fuller fact sheet — and every sidebar tab has a
  collection story.** The Wikipedia/Wikidata source additionally resolves a
  collection's country of origin, developer, publisher, genre and official
  website (one batched lookup, not five), and the details pane shows them
  on the Collection tab and the no-selection overview — the website as a
  clickable link. The File tab joins in: with a collection selected it now
  tells the on-disk story — media and artwork folders, item count, total
  size on disk, last scan — instead of "No item selected". Several fixes
  ride along: the bold field labels automatically fall back to the plain
  text colour when a collection's accent is too close to the card colour
  to read, the summary cards now fill the sidebar's full height like item
  details do (no more short cards hugging a few rows, no clipped last
  rows, no mid-pane float), the size row stays hidden until the scan has
  actually recorded file sizes rather than reporting "0 bytes", and
  switching Item → Collection → Item with a subcollection selected no
  longer resurrects the previously selected item over the subcollection's
  card. The Collection tab also reliably shows the SELECTED ITEM's owning
  collection in aggregated views now: the owner lookup gained the same
  filename fallbacks artwork resolution already had, plus a media-directory
  fallback (longest prefix wins, so a subcollection beats its parent) for
  libraries whose database rows spell paths differently than the browser
  does — previously those quietly fell back to the viewed shell. A selected
  subcollection tile keeps its own card in the Item area while the
  Collection tab now describes the tile's parent — the collection you are
  browsing — so the tab consistently answers "where does this live?" for
  every kind of selection. The File page is tighter too: the duplicate
  "File Information" header is gone and the path/size/modified rows sit
  compactly at the top instead of drifting apart across the pane.

- **Scrape info for your whole library in one go.** The scraper dialog
  gains a *Collection info only — skip items* switch: turning it on
  pre-checks your shell collections (the parents whose items live in
  subcollections — exactly the ones item scraping never touches) and the
  Scrape button then fetches description, manufacturer, dates and logo
  art for every checked collection without scraping a single item. Check
  or uncheck collections freely while the mode is on; turning it off
  restores your previous selection.

- **The details pane always has something worth reading.** Three states
  that used to collapse into bare placeholder text now render the same
  skeleton a fully-scraped item gets. With nothing selected, the pane
  presents the collection you are browsing — its artwork (a
  subcollection's grid tile, or the collection icon), its name, and a card
  with its type, item count and last scan. Selecting a subcollection tile
  presents that subcollection the same way. And an item with no scraped
  metadata keeps the full section list — a dimmed *No description
  available* with placeholder rows for genre, developer and release date,
  in the theme's own placeholder colour — above its file information,
  instead of dropping to a bare file listing. None of the no-selection
  views show filesystem paths; those stay on the Collection tab.

### Changed

- **Empty states no longer decorate themselves with emoji.** "No items
  found", "No collections yet", "This collection has no items", "No
  media directory configured" and the empty search result used to carry
  a mailbox, folder or magnifying-glass pictograph above the text. The
  message and its hint now stand on their own.

- **Attract mode now moves the Cover Flow carousel.** Autoscroll used to
  do nothing at all in Cover Flow: it drives the item scroll area's
  scrollbar, and Cover Flow hides that area with both scrollbars forced
  off, so the feature silently declined to start. It now drifts the
  carousel instead — cards flow continuously through the centre and the
  selection travels with them, changing to whichever card is nearest the
  middle, so the centred card and the selected item never disagree.
  Scroll speed keeps its meaning, pixels of travel per tick, measured
  against the width one card crosses through the centre; the carousel
  bounces at the first and last card the way the viewport bounces at the
  top and bottom, and glides back onto the selected card when attract
  stops, so it is never left parked half-way between two. Advance-selection
  still works as before, so in Cover Flow you now want one or the other
  rather than both.

- **The Collection tab follows the selection.** It used to pin to the
  collection being browsed. It now answers "where does this live?" for
  whatever is selected: browsing an aggregated parent with *Show
  subcollection items* enabled and selecting a child's item shows that
  child's details; selecting a subcollection tile shows the tile's parent
  — the collection you are browsing — while the tile's own card appears
  in the Item area; with nothing selected it describes the collection
  being viewed, as before.

### Added

- **A scraped library now survives losing its database.** Scraping
  writes two things next to your artwork: the images, and a small file
  per item holding everything it found — title, description, genre,
  developer, publisher, release date, rating, players, tags. The images
  came back by themselves on the next scan, because Kartend re-reads the
  artwork folder. Those metadata files were never read back at all, so
  the database was the only copy: lose it to corruption, a fresh
  install, or moving the library to another machine, and you had to
  scrape the whole library again while the answers sat on disk the whole
  time. Scanning now reads them back for any item that has no metadata
  yet. Items that already have metadata are left completely alone —
  including anything you typed yourself, and including fields you
  deliberately left blank.

### Fixed

- **Favorites no longer appears twice in the collection trees after the
  setup wizard.** On a brand-new profile, every surface that lists
  collections — the navigation sidebar, the settings dialog, the
  scraper's collection picker — showed two identical Favorites rows,
  both backed by the same single playlist. The first playlist refresh
  after the wizard created the built-in Favorites entry and then
  reacted to its own creation signal by running again mid-flight, so
  the row was added once by the nested pass and once by the original.
  The refresh now ignores that re-entrant call; one Favorites, like it
  says on the tin.

- **Switching KDE activities now recolours the item tiles in place.** The
  toolbar, sidebars and details pane already followed the new colours, but
  each tile's artwork card had the previous activity's backdrop baked into
  its pixels when it was composed, so tiles kept the old colour until you
  scrolled them away and back. Stale cards are now detected and quietly
  re-composed against the new colours the moment the desktop changes.

- **The selection ring now hugs the tile's edge when titles are hidden.**
  It used to sit in the reserved caption band; now that a hidden-titles
  tile is all artwork, the ring wraps the tile itself — same look list
  rows have always had — and stays visible above the artwork.

- **Grid spacing is now simply the gap between one tile and the next, in
  pixels, and can no longer be negative.** It used to be drawn on an
  unlabelled 0-150 scale where 100 meant "no gap", so a stored value of
  -80 appeared as "20" and read naturally as 20 pixels while actually
  packing tiles closer together than their own size. Setting 20 now gives
  a 20px gap, and 0 puts tiles edge to edge. Collections that used a
  negative value are converted on load into the tile size and gap they
  were really being drawn at, so **your grid keeps its column count and
  its appearance** — but Item Width and Spacing will read differently in
  the settings, because they now describe what you can actually see.

- **The grid spacing fields now show the real value.** They were drawn
  on an unlabelled 0-150 scale where 100 meant zero spacing, so a stored
  gap of -80 was displayed as "20" and read naturally as 20 pixels. The
  fields are now the pixel value itself, negatives included, and the
  tooltip says so. **Your stored spacing is unchanged and your grid will
  look exactly as it did** — but the number in the box will differ from
  what you saw before, because it is now the number actually in use.

- **"Item Width: 325" now draws a 325px tile.** With titles hidden it
  drew 277 at best, and 237 once tight spacing was in play — the cell
  reserved room for a caption that was not being drawn, and counted the
  same margin twice besides. An untitled tile now fills its cell, so the
  number you type is the tile you get and the gap between tiles is the
  spacing you set. Tiles with titles shown gain 20px of artwork from the
  double-counting fix. Very tight spacing still shrinks tiles rather than
  letting them collide.

- **Covers no longer overlap their neighbours when grid spacing is
  negative and titles are hidden.** Tiles clamp their artwork to the
  spacing you actually set, but that clamp was only applied when a
  settings change swept an existing tile — a tile drawn for the first
  time as you scrolled it into view was never clamped, so its cover grew
  into the tile beside it. Every tile is now clamped from the moment it
  is built.

- **A collection with no artwork folder can now keep its logo.** Fetching
  a collection's info when it had no artwork directory of its own looked
  like it worked — the text arrived, nothing reported an error — but the
  logo and background had nowhere to be written to, so the icon stayed
  empty and the whole thing read as having done nothing. The art now goes
  to the first artwork folder any of your collections has. Nothing can
  collide there: collection artwork is filed under a shared subfolder
  with the collection's own identity in the filename, and Kartend already
  looks through every collection's artwork folder when resolving icons.
  The borrowed folder's own collection is untouched — it lends the space,
  it does not receive the art. If no collection has an artwork folder at
  all, the text is still fetched and a line in the scrape log says the
  art could not be kept.

- **Counts in the interface now agree in number.** Twenty-six messages
  showed their plural marker instead of choosing a form — "1 DAT file(s)
  failed to load", "1 error(s) logged", "1 game(s) imported" — because
  English was treated as needing no translation file, and without one Qt
  has no way to pick between "file" and "files". Kartend now ships a real
  English catalogue, so those read correctly at every count, and the window
  title's own counts ("1 subcollection", "(1 Item)") go through the same
  machinery rather than a hand-rolled rule beside it. If you run Kartend in
  a language it has not been translated into yet, it falls back to English
  rather than to raw text, so counts stay correct there too.

- **Navigation sidebar settings now travel with a `.kart` bundle.**
  Exporting a collection and importing it elsewhere silently reset every
  option for the tree panel — which side it docked on, its width, whether
  it overlapped or pushed the grid, row display mode, icon size, style and
  tint, scrollbar policy — along with the whole system-glyph block. They
  are all carried now, so a bundle arrives arranged the way its author
  left it. Bundles made before this still import exactly as they did:
  a missing setting reads as its default rather than as "off". The glyph's
  system and icon-pack names travel as names, not paths, and are resolved
  against whatever RetroArch the receiving machine has — so a bundle can
  neither reveal where the sender kept their files nor arrive pointing at
  somewhere that does not exist.

- **Hiding titles gives the space back to the artwork.** The three-line
  caption band under each tile was reserved whether or not a title was
  actually drawn, so turning titles off just left a strip of dead space.
  It now goes to the cover. This was tried once before and reverted the
  same hour because it made artwork overlap artwork: with negative grid
  spacing your tiles sit closer together than their own size implies, and
  the enlarged cover grew straight into its neighbour. Covers are now
  capped to the spacing you actually set, so they can grow into the freed
  band but never into the tile next door. Positive or zero spacing is
  unaffected.

- **Arrow keys now stop attract mode.** Moving the selection with the
  keyboard while attract was running did nothing to interrupt it — in every
  view, not just one. Scrolling the wheel, hovering, or clicking all stopped
  it as expected, which made the gap look like a Cover Flow quirk rather than
  what it was: the keyboard takes a different route through the selection
  code than the mouse does, and only the mouse's route was telling attract
  that you were back. Attract now yields to keyboard navigation the same way
  it always yielded to the mouse, and still ignores the selection moves it
  makes itself.

- **Programs installed under Program Files no longer look suspicious on
  Windows.** Importing a `.kart` bundle on Windows flagged practically every
  launcher it named as sitting outside the trusted install locations — the
  list of those locations only ever held Linux ones, so an ordinary
  `C:\Program Files\...` player was reported in the same breath as something
  genuinely unusual. Kartend now recognises `Program Files` and
  `Program Files (x86)` on any drive, so a normal Windows install reads as
  ordinary. Every launcher a bundle asks for is still shown to you before the
  import goes ahead; this only changes which ones are called out as worth a
  second look.

- **Titles that collide with reserved Windows device names keep their name.**
  A handful of names — `CON`, `PRN`, `AUX`, `NUL`, `COM1`–`COM9` and
  `LPT1`–`LPT9` — cannot be used as filenames on Windows at any extension.
  A launcher-import stub or multi-disc playlist for a title named exactly one
  of those produced a file Windows refused to create. Those names are now
  prefixed rather than rejected, so the item keeps a name you can recognise
  instead of falling back to "Untitled". Trailing dots and spaces are also
  cleaned up more thoroughly: a title ending in something like `. .` used to
  leave one dot behind, which Windows then dropped, so two differently-named
  items could end up fighting over one file.

- **Recursive Import keeps your place in the collections tree.** Every run
  collapsed the settings dialog's tree back to the top level and cleared the
  selection, so the header went from "Editing collection: Nintendo" to "No
  collection selected" — meaning a re-expand and a re-select before each of
  the next vendor's imports, on the one workflow that is inherently
  repetitive. The tree now stays as you left it, with the collection you
  imported into still selected and opened to show its new children.

- **The settings dialog's "Mode:" chooser shows its full text.** It was cut
  mid-word to "Current colle" — sharing a row with three buttons left it too
  narrow for its own entries, at any dialog width, including maximised. It
  now sits on its own row and shows entries like "Current + subcollections"
  in full.

- **A wider details pane no longer cuts off its own contents.** Widening
  the details pane — or opening a collection that had been saved wide —
  left everything inside it laid out for a narrower pane: the artwork
  lost its right edge, the item's title and the section heading were cut
  mid-word, and the metadata split into two columns too narrow to hold
  it, with no way to scroll across to what was missing. The pane worked
  out how much room it had before it was actually on screen, and never
  revisited the answer. It now measures again once it is visible.
  Hiding and re-showing the pane used to fix it for the session, which
  is why it could seem to come and go.

- **The marquee follows its monitor being unplugged.** If you run a
  second screen as a marquee and disconnected it mid-session, Kartend
  carried on addressing a screen that was no longer there — the marquee
  stayed lost until you next saved settings or restarted, with nothing
  said about it. Kartend now notices monitors coming and going: unplug
  the marquee screen and the window moves to your primary one, plug it
  back in and the marquee returns to it. Docking or undocking a laptop,
  which announces several changes at once, is handled as a single move.

- **Settings no longer claims you have unsaved changes when you don't.**
  A few settings apply the moment you change them and save themselves
  straight away — the boot and resume splash screens, the interface font,
  and the title base colour. Kartend went on treating those as pending
  work anyway: the Save button kept glowing for the rest of the visit,
  and closing raised a "you have unsaved changes" question about a
  setting that was already written to disk. Since these are exactly the
  settings that feel finished the instant you set them, the prompt named
  changes you had no memory of making. Kartend now updates its idea of
  "last saved" when one of those writes completes, so the question is
  only asked when something really is pending.

- **Cover art imported from another launcher can no longer be
  redirected anywhere.** Steam, Lutris, itch and Heroic keep their game
  lists in ordinary files on your disk, and Kartend reads cover-image
  addresses out of them when it imports. Those addresses were fetched
  without any restriction on where they could lead: a tampered launcher
  database could hand over an address that bounced the request onward to
  a machine on your own network, and the reply was written into your
  artwork folder. Kartend now only follows cover addresses belonging to
  the launcher's own image hosts, and checks that again at every hop.
  Nothing changes for real covers; they already come from those hosts.

- **A password in a scraper address is now hidden by default rather
  than by name.** Kartend masks credentials before writing a web address
  into its logs, but it did so by looking for a fixed list of names —
  `sspassword`, `token` and a handful of others. Anything not on the
  list was written out in full, so a future scraper naming its password
  field something unanticipated would have leaked it silently. The rule
  is now the other way round: only the parts Kartend recognises as
  harmless — the image-size preset, the output format, the search text —
  are written, and everything else is masked. The diagnostic detail the
  logs are there for is unchanged.

- **The disc-image repair step can no longer pass a booby-trapped
  archive along.** When Kartend renames files inside a `.zip` or `.7z`
  to match a verification database, it rebuilds the archive and replaces
  your original with the result. The rebuild copied each entry's
  description across untouched, so a shortcut-style entry, or one whose
  name climbs out of the folder, came through into the new archive — and
  the replacement name taken from the downloaded database was never
  checked at all. Kartend now refuses to rebuild an archive containing
  either, and refuses a database rename that tries to escape the folder.
  Unpacking already had these checks; the repair step now has them too,
  so nothing hostile gets quietly reissued as a clean file.

- **A hostile or broken response can no longer wedge the window.**
  Several places sized their work directly from what arrived: a scraper
  reply claiming a million results had memory set aside for a million
  results and then compared each one against every other, all on the
  thread that draws the window. A `.dat` catalogue and Steam's app
  database were read whole with no ceiling at all. Every one of those
  now has a limit, set far above anything a real response or file
  reaches — searches ask for ten results, a release lists a handful of
  genres — so nothing legitimate is affected, but a reply that has
  stopped making sense is refused instead of followed.

- **A crafted bundle can no longer claim gigabytes it does not
  contain.** Kartend bundles store each file compressed, alongside a
  note of how big it should come out. For files packed with the older of
  the two compression methods, the unpacking step ignored that note and
  instead trusted a size written inside the compressed data itself —
  which a hostile bundle controls. A thirty-byte entry could therefore
  announce four gigabytes and have the memory reserved for it before
  anything checked. The declared size is now compared against the
  bundle's own record before a byte is unpacked, and a disagreement
  refuses the entry. Bundles you made yourself are unaffected: the two
  numbers always agree.

- **The builds that always store scraper passwords in the clear now say
  so.** Kartend keeps scraper credentials in your system keychain where
  one is available, and falls back to storing them unencrypted in
  `kartend.cfg` where it is not. A warning about that fallback already
  existed — but only builds *with* keychain support could ever show it.
  A build compiled without keychain support, which stores every
  credential in the clear every time, was the one configuration that
  stayed silent, showing the same reassuring masked password box as any
  other. It now warns, and says plainly that this will not change on
  that build rather than promising the credentials will move back to a
  keychain that isn't there. The warning also names the right file: it
  used to point at `settings.ini`, which Kartend has never written.

- **Only shortcuts Kartend created can add options to a launcher
  command.** Shortcut stubs may carry extra command-line arguments —
  Bottles needs the bottle's name alongside the program, so one
  substitution is not enough. Those arguments were taken from any stub,
  including one that merely turned up during a library scan, and passed
  straight to the launcher. Since a stub is an ordinary file in a folder
  you scan, anything able to write there could hand your launcher
  options of its choosing. Arguments are now accepted only from stubs in
  the folder Kartend writes them to itself. A stub found anywhere else
  still launches its game — it simply cannot contribute options. Nothing
  changes for libraries imported the normal way.

- **A huge file wearing a shortcut's name can no longer be swallowed
  whole.** Shortcut stubs — the tiny placeholder files standing in for
  games installed through Steam, Lutris and friends — are found by
  scanning your library, which means Kartend meets whatever happens to
  be sitting there with that file extension, not only the ones it wrote.
  It read the whole file into memory before checking anything, so a
  multi-gigabyte file dropped into a scanned folder became a
  multi-gigabyte read. Anything implausibly large for a stub is now
  refused before a byte of it is loaded. Real stubs are a few hundred
  bytes and are unaffected.

- **Search results from other collections bring their artwork with
  them.** Searching from a parent collection — or across the whole
  library — found the right items but drew them all as blank hatched
  tiles. The same items showed their artwork the moment you went into
  the collection they live in, which made it look like the search had
  found something different. Kartend was looking for their images in the
  artwork folder of the collection you were searching *from*, rather
  than the one each result actually belongs to. It now looks in the
  right folder per result, whenever a search reaches past the current
  collection.

- **The details pane keeps up with selections you did not make by
  hand.** Two ways of selecting something moved the ring and updated the
  toolbar counter but left the pane describing whatever was there before.
  On launch, the tile Kartend restores was drawn as selected while the
  pane still showed the collection overview — the state every session
  began in, before you touched anything. Pressing Home to jump to the
  first item did the same. Clicking always worked, which is what made it
  look arbitrary. Both paths were committing the selection without the
  step that publishes it, and the retry meant to cover a tile that had
  not finished loading only ever republished ordinary items, never
  subcollections. Both now go through the same publish the click path
  uses.

- **Subcollection rows in List layout show their name again.** A
  subcollection listed inside its parent drew a folder icon and nothing
  beside it — a row identified only by a generic glyph, while every item
  row below it read normally and the same subcollection displayed fine in
  Grid. The "hide subcollection titles" setting was being applied to list
  rows, where there is no artwork to fall back on and the row's text is
  the only thing naming it. Hiding titles is now understood as the grid
  setting it always was: grid tiles still honour it, list rows always
  print their name. Virtual folder rows were silently affected the same
  way and are fixed with them.

- **Loading a saved configuration profile no longer offers you a button
  that undoes it.** After confirming the switch and acknowledging
  "Kartend will now exit", the app did not exit: a third prompt appeared
  on top, asking whether to save changes before closing the settings
  dialog. That dialog was still holding the configuration you had just
  replaced, so "Save" — the button that reads as *keep my work* — wrote
  the old settings back over the profile you had been told was already
  loaded, and "Discard" was what actually preserved it. The settings
  dialog now closes without asking once a profile has been loaded; the
  file on disk is the one that counts, and the app exits as promised.

- **An imported launcher library can no longer surface a file you never
  chose to share.** When importing from Steam, Lutris or Heroic, Kartend
  looks for cover art the launcher has already downloaded, using an
  identifier taken from that launcher's own database to build the filename.
  Those identifiers were used exactly as written, so one crafted to point
  somewhere else — up and out of the launcher's folder — made Kartend read
  the image it named and show it as a game's cover. Identifiers are now
  checked to be plain filenames before any file is looked for, and an
  identifier that fails simply gets no artwork. Nothing was ever written
  outside the collection, and the identifiers come from applications
  already installed on your own machine.

- **The sidebar and the grid agree on the order of the same
  subcollections.** A collection holding NEC, Nintendo, SNK, Sega and
  Sharp listed them in one order down the navigation sidebar and a
  different one across the grid — both on screen at once. The sidebar was
  ordering strictly by character code, which files an all-caps name like
  SNK ahead of Sega and Sharp; the grid had always ignored case, the way
  a reader would. The sidebar now does the same, so the two lists match.
  Names differing only in capitalisation keep the order they are
  configured in.

- **Counts in the window title read correctly when there is only one of
  something.** A collection with a single subcollection announced "1
  subcollections", and one holding a single file read "(1 Items)". Both
  now drop the "s". Larger counts, and the stacked totals shown while
  you are inside a subcollection, are unchanged.

- **The toolbar's Layout menu shows which layout you are actually in.**
  Opening it straight after launch showed all four entries — Grid, List,
  Cover Flow, Horizontal — with empty radio buttons, even though the view
  was plainly rendering as one of them. Picking any entry fixed it for the
  rest of the session, so the menu was blank exactly when you were most
  likely to open it to ask which view you were in. It now reads the active
  collection's saved layout each time it opens.

- **Changing only the Toolbar colour now takes effect straight away.**
  Picking a different source on the Appearance page — titlebar, accent,
  highlight, or the collection's own primary colour — left the toolbar
  looking exactly as it did. The choice was saved correctly and came back
  on the next launch, and it also applied immediately if you happened to
  change some other background setting during the same visit, which is
  what made it look intermittent. On its own it never reached the
  toolbar: the check that decides whether a collection's background
  settings changed had simply never been taught to look at this one.

- **The New Library Wizard offers Games as a media type.** Its type list
  was written out by hand and had drifted from the one the settings dialog
  shows — it said "Reference", "Image" and "Other" where the rest of the
  app says "Documents" and "Images", and it left out Games altogether. So
  the one guided path into a new collection could not name the type most
  of Kartend's feature surface is built for, and anyone picking from the
  list rather than typing over it landed on "Other". Both pickers now read
  the same list and cannot drift apart again. Typing a custom type still
  works, exactly as before.

- **The wizard's Confirm step shows the launcher you just picked.** The
  final review screen listed the name, media folder and type only: the
  launcher chosen one step earlier was missing from it entirely, and the
  artwork folder vanished from the summary whenever it had been left
  blank. Both settings were applied correctly regardless — this was the
  summary under-reporting, not a lost setting — but it left the one screen
  whose whole job is to show the configuration before it is created
  quietly omitting a step's worth of it. Every step is now listed, and the
  ones left unset say so instead of disappearing.

- **A RetroArch collection no longer passes its core and ROM twice.**
  Launching handed the emulator `-L <core> <rom> -L <core> <rom>` — the
  launch parameters already name the core and the file, and Kartend then
  added its own copy of both on the end. RetroArch quietly ignored the
  repeat, so games still ran, but a collection whose parameters pointed at
  a hand-picked core had Kartend's core appended after it, competing with
  the choice the user had made. Kartend now adds only the parts the
  parameters left out.

- **Switching to List and back no longer empties the grid of its cover
  art.** Coming back to Grid left every tile as a blank hatched
  placeholder, squared off instead of poster-shaped, and it stayed that
  way — only visiting another collection brought the art back. Behind it,
  a tile whose artwork folder had a scan pending skipped its cover and
  waited for that scan to finish, but nothing was actually scheduled to
  run it: the only thing that did needed a fresh batch of items to arrive
  from the database, which a layout switch never asks for. The wait is now
  something the app actually follows through on. A cover is also no longer
  written off as missing while only part of the artwork folder has been
  looked at — covers live in per-type subfolders that finish scanning
  after the top level does.

- **List view and search results now start at the top of the view, not
  part-way down it.** A collection whose content was shorter than the window
  was being vertically centred — a look that suits a small grid of tiles, but
  in List view it floated the rows away from their pinned column headers,
  leaving a blank band under them, and after typing a search it dropped the
  few matches roughly a row lower than where the unfiltered grid begins.
  Lists now hug their headers, and narrowed grids — whether narrowed by the
  search box or by a filter — put their results where your eye already is.
  Short unfiltered tile grids keep the centred, hub-page look, and the
  Horizontal layout keeps its centred strip.

- **The selection border in List view is no longer clipped.** Two separate
  trims conspired against the top row: the column header painted four pixels
  taller than the space reserved for it, covering the top edge of the first
  row and whatever selection border was drawn there, and the border's own
  stroke was drawn so close to the row's edge that its outer pixel fell
  outside the row on every side. The header now occupies exactly its reserved
  height, and the stroke sits fully inside the row, so a selected first row
  shows all four edges.

### Added

- **A console or controller icon beside each game collection's name, from
  your RetroArch install.** Game collections can now carry a small icon to
  the left of their name in the navigation sidebar — the machine's
  controller, the console itself, or the cartridge or disc it played — taken
  straight from the RetroArch you already have installed. Nothing is
  downloaded, and nothing is copied: the icons are read where they sit, so
  they match the RetroArch version you are running and follow it when you
  update or re-theme it. New game collections detect their system as you type
  the name, including shorthand — a collection called *SNES* finds the Super
  Nintendo. Existing collections get there in one press of **Detect** on the
  new *System Icon* section under Settings → Appearance → Sidebars, or you can
  pick the system yourself from a searchable list. Controller and console come
  from different RetroArch icon sets — a set holds one icon per system, so the
  set decides which you get — and the set list offers only those that actually
  hold art for what you asked for, with the system count beside each name. Put the icon
  **before the name, after it, or out at the panel edge**, where a column of
  them lines up however long the names are. Off until you ask for it, and the
  section explains itself when RetroArch is not installed. This is separate
  from *Rows show* — that setting governs a collection's scraped artwork,
  while this small glyph appears alongside the name in every mode that shows
  one, including the default *Name only*.

  Setting it up on a parent collection and saving with **Mode** on *Current +
  subcollections* fits out a whole shelf at once: the look is copied down,
  but each subcollection detects its own system from its own name, so you get
  a different icon per platform rather than the parent's repeated. Collections
  imported from a launcher — Steam, Flatpak, Lutris — are left alone, since a
  storefront is not a machine, and so are grouping shells like a *Nintendo* or
  *Sega* folder: those show the company logo already on file instead, at the
  same size and place as the platform icons beneath them. Re-running the apply
  corrects rows that picked up a wrong system earlier, rather than leaving them
  as they were.

  **Right-click any sidebar row** to set a custom icon from a file, detect the
  system from the row's name, or remove the icon — acting on the row you
  clicked rather than the collection you happen to be in. Detection also
  revises guesses it made itself, so a row that picked up the wrong icon in an
  earlier attempt is corrected by running it again; an icon you chose by hand
  is never overwritten.

  An **Icon style** choice — normal, monochrome dark or light, or tinted —
  inks system icons and collection artwork the same way, so a manufacturer
  logo matches the platform icons beneath it instead of being the one thing in
  colour. And the System list now opens with two answers that are not systems:
  *None* for no icon at all, and *this collection's own artwork*, which is how
  you clear or override a single row where the logo just repeats the name.

- **One Sidebar settings page — pick each side panel's side and shape.**
  A single page in Settings to put the details pane and the new collection tree on
  the left or the right, per collection, plus a new **justification**
  choice for each: *Below toolbar* keeps the classic layout where the
  toolbar spans the full window width, while *Full height* lets a panel
  run the entire height of the window with the toolbar stopping at its
  edge. The details pane's existing Position control keeps working — it
  and the new page edit the same setting.

- **Choose how each scrollbar behaves: Show, Auto-hide, or Hide.** Every
  scrollbar setting is now a three-way choice instead of an on/off box —
  the items grid (per axis), the details pane, and the navigation sidebar.
  *Auto-hide* keeps the bar out of sight until you move the pointer near
  the edge it lives on, then fades it in. *Hide* removes it entirely and
  gives the reserved strip back to your content. Scrolling itself is never
  affected: the wheel, the arrow keys and drag work in all three modes.
  Existing settings carry over — anything previously ticked reads as Hide.
  Auto-hide needs the slim overlay scrollbars (Settings → General); without
  them it behaves as Show.

- **The navigation sidebar can overlap the grid instead of pushing it.** A
  new *Mode* choice on the Sidebars settings page: *Push the content aside*
  keeps today's behaviour, where opening or resizing the sidebar moves the
  grid to make room; *Overlap the content* floats it above the grid instead,
  so the items underneath never shift — the sidebar simply covers them.
  Set per collection and off by default, so nothing changes until you pick it.

- **Room to breathe around cover art.** Grid artwork now sits inset from the
  tile behind it rather than running flush to its edges, and the details
  pane's preview no longer draws a hairline outline around the image.

- **Round and square logos in the navigation sidebar are easier to see.**
  Sizing previously favoured wide wordmarks, which are given extra height so
  they do not render as hairlines — a square or circular platform mark got no
  such lift and sat noticeably fainter beside them. Compact marks are now
  raised toward the same visual weight. Rows holding one are a little taller
  as a result; wide logos are unchanged.

- **Navigation sidebar rows line up.** Rows carrying a logo now share one
  height instead of each hugging its own artwork, so a column of platforms
  reads as an even list. Logos are fitted to a common box, so a wide
  wordmark and a round platform mark take up comparable space instead of
  the wordmark dwarfing it. The rounded highlights behind rows also render
  with cleaner edges.
- **Attract mode holds still while you are looking at artwork.** With an
  artwork open fullscreen, auto-advance stops — from either the grid's
  preview or the details pane's — and dismissing it starts a fresh idle
  countdown rather than advancing immediately.
- **Scroll over the sidebar's artwork strip to change the picture.**
  Hovering the media gallery in the details pane and scrolling now steps
  that item's artwork, stopping at the last one instead of looping.
- **Slim overlay scrollbars.** A new option under Settings → General
  replaces the chunky bars on the item grid, collection tree and details
  pane with a thin handle drawn *on top* of the content — no groove, no
  divider line. It reserves no space, so items never shift when it appears,
  and it fades in while scrolling or hovering and fades out when idle.
- **Choose what colours the toolbar.** A new Toolbar colour option on the
  Appearance page picks between the desktop titlebar, the desktop accent,
  the highlight colour, or the collection's own primary colour. It
  defaults to matching the titlebar — the accent and the titlebar are
  different colours under Plasma's accent-from-wallpaper, which is why
  accent-tinted chrome never lined up with the window decoration.
- **Sidebar icons keep the size you set.** The icon height now follows the
  option exactly instead of shrinking with a narrower sidebar, and
  resizing the sidebar applies to every collection, so clicking an entry
  no longer resizes the panel under you.
- **The toolbar and sidebars share one tone.** The top bar took the item
  grid's colour, which read as two-tone against the sidebars; all the
  chrome now uses the same role, and the collection tree's top-level rows
  match the toolbar's height so the two line up.
- **Items with no artwork show the familiar hatched tile.** Stepping
  through the library full-screen no longer leaves the previous item's
  picture up when the next one has nothing scraped — it shows the same
  cross-hatch placeholder the grid uses, titled.
- **Expanded artwork now owns the controller.** With an artwork open
  fullscreen, Back closes the artwork instead of leaving the collection,
  and the directions — from gamepad, keyboard, or the mouse wheel (one
  artwork per flick, however hard you spin it) — cycle that item's artwork — then, instead of looping
  back to the first picture, carry on to the previous or next **item** and
  its artwork. Gamepad and keyboard take the same path, so they behave
  identically. The expanded view also receives
  the sidebar's full artwork strip, so there is something to cycle
  through. This affected the keyboard too: the app-level key routing did
  not recognise the details pane's expanded view and was claiming its keys.
- **The collection you are viewing can keep its colours.** A new Sidebar
  option renders the active collection's logo in full colour while the
  rest of the tree stays monochrome or tinted, so your place in the list
  is obvious at a glance.
- **Pointing at artwork previews it.** Moving the ring onto an artwork
  tile swaps the sidebar's main preview immediately — no button press —
  and after a second of no stick input the pane hands control back to the
  grid on its own.
- **Confirming an artwork tile opens it fullscreen.** Ringing a tile in
  the details pane and pressing confirm now both swaps the pane's main
  preview and expands the artwork, instead of only swapping it.
- **Traversing collections from the tree keeps the tree in hand.** The
  collection switch no longer returns focus to the window, so the right
  stick carries on moving through collections instead of silently handing
  itself to the details pane after the first switch.
- **Collection tree rows highlight across the full panel width**, instead
  of stopping short at the indent column.
- **The right stick drives the details pane, with the selection ringed.**
  Up and down now travel the whole pane: the selected region scrolls, and
  once it reaches its end the selection steps on to the next one — artwork
  strip, description, metadata — so the stick reaches every part of the
  pane rather than only the metadata. A thin, rounded, pulsing outline
  marks whatever is selected. Left and right still move focus between the
  sidebars and the grid, a focused collection tree keeps up/down for its
  own rows, and a focused toolbar keeps left/right for its buttons.
- **Holding Select is now the only way the stick reaches the toolbar.**
  With the modifier held, directions switch sections (up to the toolbar)
  and the display shows where focus is: the focused section stays sharp
  and ringed while everything else softens to a blurred, gently
  desaturated backdrop, with a pill naming it. Confirm activates the
  focused row or button rather than launching the grid's selection. The
  d-pad and left stick always control the grid, pulling focus back to it.

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
  ticks do nothing rather than leaking into the grid. And the hairline outline that keeps low-contrast logos
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

- **Point at a long collection name to read all of it.** In a narrow sidebar,
  names that do not fit end in an ellipsis — unhelpful precisely when several
  of them start the same way, since the part that tells them apart is the part
  being hidden. Hovering a row whose name is cut off now scrolls it sideways
  so you can read the whole thing, pausing at each end rather than crawling,
  and putting it back when you move away. Only the row under the pointer
  moves, and names that already fit never move at all. On by default as
  *Scroll the row under the pointer* on the Sidebars settings page; a second
  option there, *Scroll names that do not fit*, does the same for every
  clipped row at once and is off by default.

- **Level-one collections line up with the Playlists heading.** The row at the
  top of the sidebar names the collection you are in and reads as a heading, so
  the collections beneath it no longer sit indented under it — they start at
  the same place as *Playlists*, with their own subcollections stepping in from
  there.

- **Tidier highlights in the navigation sidebar.** With rows set to show just
  their names, the rounded highlight behind a row is now sized to the name it
  sits behind — close around the text, the same height on every row, and with
  the name centred inside it — instead of running most of the way across the
  panel and standing as tall as whatever artwork the row could have held. A
  short name like *Sega* no longer wears a backdrop several times its own size.
  The **Playlists** heading gets exactly the same highlight as every other
  section, where it previously had a square band of a different height.

- **The details pane is open by default.** A new library now starts with the
  pane showing, so an item's artwork, metadata and file information are
  visible straight away instead of waiting behind an F9 nobody had been told
  about. It is per collection and still toggles from View → Show Details Pane,
  and collections you have already used keep the state you left them in.

- **The toolbar no longer repeats the collection name by default.** The
  window title already names the collection you are in, and the navigation
  sidebar shows where that sits in your library, so the breadcrumb across
  the toolbar was saying the same thing a third time and taking room from
  the controls. It is still a setting — tick *Collection Breadcrumb* under
  Settings → Appearance → Toolbar to bring it back, along with its
  clickable path segments — and if you had already set it either way, your
  choice is kept.

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

- **Collection tree rows can show their name, their icon, or both.** A row
  that had artwork used to lose its name entirely — the tree drew the picture
  and nothing else — which made a library of logos hard to read and had no
  setting to change it. The Sidebars page now has a **Rows show** choice:
  *Name only* keeps the artwork on file but draws just the text and is the
  new default, *Icon and name* puts a small icon beside the name, and *Icon
  only* is the previous picture-only look with the name in the tooltip. Rows
  without artwork show their name in every mode, as before. Your existing
  choice is kept either way: an icons-only setting carries over to *Icon
  only*, anything else to *Icon and name*, so only a fresh library starts on
  *Name only*.

- **Only the topmost sidebar row is painted as chrome.** Every top-level
  collection took the titlebar colour, so a library with several of them
  turned the sidebar into a stack of full-width bars with no sense of
  hierarchy. The point of that fill is to carry the toolbar across the top of
  the panel, and only the first row touches the toolbar — the rest now look
  like the collections they are.

### Fixed

- **On Windows, scraped SVG artwork could be saved with the wrong extension.**
  When a download's URL had lost its suffix — through a redirect, or a
  `cover.php`-style endpoint — Kartend inspects the bytes to decide what the
  file is. That inspection deferred to Qt's image plugins, and the SVG
  plugin's detection differs between Qt versions: the one Windows builds
  against accepts any XML document as a drawing, so a non-SVG response could
  land on disk as `.svg` and then fail to render. The decision is now made
  from the payload itself — a document with no `<svg>` element in it is not
  treated as one — which also makes the result identical on every platform.

- **The window titlebar and the app's own chrome are now exactly the same
  colour.** With the toolbar colour set to follow the titlebar the two were
  close but never equal, leaving a faint seam where the toolbar met the
  titlebar. Kartend read the colour from the older of the two places KDE keeps
  it; Plasma has painted window decorations from the newer one since 5.23, and
  they hold slightly different values. It now reads the one the decoration
  actually uses, falling back to the older key for colour schemes that lack it.
  Switching activities or colour schemes still recolours everything without a
  restart.

- **The keyboard shortcuts dialog no longer lists a quick-filter that does not
  exist.** It advertised "Type letters → Quick filter (when search not
  focused)", but typing in the grid has never done anything of the kind — the
  user guide says so correctly, and the dialog was the odd one out. Use the
  Search key to focus the search bar.

- **Scrolling after clicking an item no longer drags the selection and the
  view back a row at a time.** Clicking an item on a different row than the
  current one put the selection into a short-lived "commit this when the
  gesture ends" state — the same mechanism a held arrow key or a hold-scroll
  uses. Those gestures each end and clear it; a single click has no end, so
  the state stayed armed for the rest of the session, still pointing at the
  item you clicked. From then on, every place that asks "is a selection
  pending?" answered yes and used that clicked item instead of wherever you
  had scrolled to. The end of each wheel glide is one of those places, so the
  selection rectangle jumped back to the clicked row on every notch, and the
  view animated back after it — the faster you scrolled, the more the two
  fought each other. A click now clears the state as soon as it has done its
  job, and a wheel step stands down any that an interrupted gesture left
  behind, so wheel scrolling after a click tracks the wheel and nothing else.

- **Dragging the details pane's edge no longer blanks the cover art.** Resizing
  the pane threw away every visible tile's widget and built fresh ones, and a
  rebuilt tile shows the hatched placeholder until the artwork pass catches up
  about a tenth of a second later — so the whole grid blinked to placeholders
  mid-drag and filled back in. A resize only moves tiles around; it does not
  change what they are. The grid now rebuilds only when the layout genuinely
  changes shape — switching between grid and list, or a change in tile size or
  row width — and reflows in place otherwise, keeping the art on screen
  throughout the drag (Kartend-8pxzi).

- **Subcollections in the collection tree now sit indented under their
  parent.** Every row's highlight pill started at the same fixed left edge
  regardless of depth, while the categories above them yielded space for their
  folding arrow — which left a child's pill sitting one step further left than
  its parent's, so a category and the collections inside it read as siblings.
  Each row now anchors to its own indented position, and the hierarchy steps
  rightward with depth as expected.

- **The details pane no longer draws a stray line down its grid-facing edge.**
  A two-pixel highlight band with a centre tick was painted along that edge to
  advertise the resize handle, but the pane carries no border of its own, so
  the band read as an unexplained line boxing the pane in. The handle keeps its
  hit area and its resize cursor, so dragging is unchanged — the cursor is now
  the affordance. The pane's inner content also sits flush to its edges rather
  than inside a fixed margin.

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

## Older releases

Releases 0.0.1 through 0.0.16 are archived in
[docs/changelogs/v0.0.x.md](docs/changelogs/v0.0.x.md).

[Unreleased]: https://github.com/EtherAura/Kartend/compare/v0.0.19...HEAD
[0.0.19]: https://github.com/EtherAura/Kartend/compare/v0.0.18...v0.0.19
[0.0.18]: https://github.com/EtherAura/Kartend/compare/v0.0.17...v0.0.18
[0.0.17]: https://github.com/EtherAura/Kartend/compare/v0.0.16...v0.0.17
