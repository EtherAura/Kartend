# Launcher Import

Games installed through a launcher — Steam, Flatpak, Lutris, Heroic,
itch.io, Bottles — or through your distribution's package manager are not
files in a folder, so a normal folder-scan collection can't see them.
Launcher import bridges that: Kartend reads the launcher's own records of
what is installed and builds a collection from them, complete with the
artwork the launcher already has on disk. Everything happens locally — no
account, no API key, no network.

## Creating a launcher collection

**File → Import → Import from Launcher…** probes for every supported
source and shows what it found, e.g. "Steam — 214 games found"; sources you
don't have are listed as "not detected". Tick the sources you want, pick
where the new collections should live with **Create in** (top level, or
nested under any existing collection — a `Games` shell, say), and press
Import. Each source becomes its own collection ("Steam", "Flatpak Games",
"Heroic"…), pre-configured and ready to browse — no shortcuts to create by
hand. A nested collection inherits its parent's layout and sidebar look,
exactly like one added by hand, and can be moved later by drag-and-drop in
the settings dialog's collection tree.

What each source provides:

| Source | Detected from | Games listed | Artwork |
|--------|--------------|--------------|---------|
| **Steam** | The local install's `steamapps` manifests, including libraries on other drives | Installed games (Proton/runtime tooling is filtered out) | The covers, logos, and hero banners Steam has already cached |
| **Flatpak** | Exported desktop entries (system and per-user) with the `Game` category | Installed Flatpak games | The app's exported icon |
| **Lutris** | Lutris' local database | Installed, non-hidden entries | Lutris' cover art and banners |
| **Heroic** | Heroic's own library caches for Epic, GOG, Amazon, and sideloaded games | Games Heroic records as installed, across all four | Usually none — see below |
| **itch.io** | The itch app's database of installed games | Installed games (tools, asset packs, and soundtracks are skipped) | None — see below |
| **Bottles** | Each bottle's config, listing the programs you added to it | Every program across every bottle | The program's icon, when you set a real image as one |
| **Desktop Menu** | Application-menu entries with the `Game` category, system-wide and per-user | Natively installed games — typically what your package manager put there | The entry's icon from the icon theme |

Only *installed* games appear. Titles you own but haven't installed live
solely in the launcher's online account and are out of scope — the one
exception being Steam, where the **Steam library** dropdown in the same
dialog can widen the import to games you have played on this computer but
since uninstalled, or to everything Steam's local metadata cache describes.

Sources never import each other's games. The Desktop Menu source is the one
where that matters most, because launchers write menu entries too: an entry
that runs Steam, Lutris, Heroic, Bottles, itch, or `flatpak run` is left to
the source that owns it, so ticking every box doesn't produce duplicates.

**Heroic and itch.io covers are fetched in the background.** Neither keeps
its covers as files on disk — they are remote URLs their clients fetch on
demand — so Kartend downloads them for you just after the import, straight
from the launcher's own artwork, and the grid fills in as they arrive. The
status bar reports progress. Because every cover comes from the id the
launcher itself recorded, this never guesses by name the way scraping can.
It is fill-missing like everything else here: art you scraped or placed by
hand is never downloaded over, and a re-sync only fetches what is still
missing. If a download fails the import is unaffected — you simply get the
placeholder for that one game.

## How it works

For every game the importer writes a small **shortcut stub** — a
`.kartlink` file named after the game (`SuperTuxKart.kartlink`) — into a
folder Kartend manages under its data directory. The collection is an
ordinary folder collection pointed at that folder, so scanning, titles,
search, artwork lookup, playlists, and usage statistics all work exactly as
they do for file-based collections.

Launching is the one special step: when you launch a stub, the launcher
receives the stub's *target* — a `steam://`, `lutris:`, `heroic://`, or
`itch://` URL, a Flatpak application id, a program name, or the path of a
menu entry — instead of the stub's path. The pre-configured launcher hands
the game to whatever owns it, which handles its own runtime setup (Proton,
Wine prefixes, sandboxing) as usual:

| Source | Launched with |
|--------|---------------|
| Steam, Lutris, Heroic, itch.io | `xdg-open` on the launcher's own URL — this works whether the client is installed natively or as a Flatpak |
| Flatpak | `flatpak run <app-id>` |
| Bottles | `bottles-cli run -p <program> -b <bottle> --` |
| Desktop Menu | `gio launch <entry>`, which runs the entry exactly as the application menu would |

## Keeping collections in sync

A launcher's library changes without the stub folder changing, so launcher
collections refresh themselves in two ways:

- **At startup**, a few seconds after the window appears, every launcher
  collection re-reads its source in the background. New installs appear;
  uninstalled games disappear.
- **On demand** via **File → Import → Sync Launcher Collections** (also in
  the command palette) — use this after installing something while Kartend
  is running.

A sync only ever touches the stubs it wrote itself: hand-made `.kartlink`
files and other sources' stubs in the same folder are left alone. Play
counts and ratings survive uninstall/reinstall cycles because the stub path
comes back identical.

## Metadata (Steam)

Steam's client keeps a local metadata cache for every installed game, and
the importer reads it — no scraping, no network, no API key. On import and
on every sync, Steam items get: title (with the punctuation filenames can't
carry), developer, publisher, release date, genres, player modes
(single-player / co-op / online…), mature-content descriptors, plus
Metacritic score, Steam review percentage, and controller support as custom
fields in the sidebar.

Like artwork, metadata is **fill-missing only**: a field you edited by hand
or one the scraper filled is never overwritten, and rows you've touched
keep their attribution. Steam doesn't store game *descriptions* locally, so
that one field stays empty — which is where the scraper comes in.

**Importing a Steam library fetches the rest automatically.** Right after
the games appear, Kartend contacts the Steam store in the background for
what the local caches can't provide — the description, a screenshot, the
store background, and the trailer, which then plays as the item's [video
preview](Video-Previews.md). The status bar reports progress and the grid
refreshes when it finishes; there is no scrape to run by hand. Because
every game carries its exact Steam app id, this never guesses by name —
each one resolves to precisely its own store page.

It's fill-missing throughout: re-importing or re-syncing only fetches
what's absent, so scraped art and hand-edited fields are left alone. The
collection also stays pinned to the **Steam Store** scraper, so a manual
scrape later (for a game whose store page has since gained a trailer, say)
uses the same exact-id matching. When you open the scrape dialog on a
launcher collection, the "What to scrape" boxes come pre-ticked with what
the store actually supplies — cover, screenshot, background, and video —
rather than the disc-and-box set used for file collections.

## Metadata (Flatpak)

Flatpak games get the same treatment from **Flathub**, the store that
published them. Right after an import or sync, Kartend fetches each app's
description, developer, genre, latest-release date, and licence from
Flathub's public catalogue in the background — no account, no API key, and
no name-guessing, because every stub carries its exact Flatpak app id.
It's the same fill-missing rule as Steam: hand-edited fields keep their
values and their attribution. The collection stays pinned to the
**Flathub** scraper for later manual scrapes. Flathub supplies text only;
covers come from the app's own exported icon, which the import already
copies.

## Artwork

Imported artwork lands in the collection's artwork folder using the same
layout the scraper uses (`front/`, `logo/`, `fanart/`), keyed by the stub's
name. The importer **fills gaps only** — if a slot already has an image
(scraped or hand-placed), it is never overwritten, so you can freely
re-scrape a launcher collection for richer art and later syncs won't undo
it. Files are copied, not linked, so the art survives the launcher pruning
its own cache.

## Notes and limits

- Steam's `steamapps` manifests carry Proton runtimes and other tooling;
  those are filtered out by name. Proton/Windows games themselves import
  like any other.
- The Flatpak and Desktop Menu sources list apps whose desktop entry
  declares the `Game` category. Launchers that are themselves Flatpaks
  (Steam, Lutris) are skipped — they're covered by their own sources — and
  so are gaming-adjacent *tools* that pair `Game` with a utility category
  the way ProtonUp-Qt does. Emulator frontends (RetroArch, Dolphin, ES-DE)
  are **not** skipped: they're launchable programs, and putting one on the
  grid so it can be started from the couch is a perfectly good reason to
  import it. If you'd rather not see one, hide it from the item's
  right-click menu. If something unwanted still slips through, hide it from
  the item's right-click menu; deleting its stub won't stick, because the
  next sync faithfully mirrors the launcher's library and recreates it.
- The Desktop Menu source lists what the *menu* knows about, which is a
  slightly different question from what is installed. An entry whose
  `TryExec` binary is gone (a leftover from an uninstalled package) is
  skipped, as are entries marked hidden or no-display; an entry your own
  `~/.local/share/applications` overrides wins over the system copy, the
  same way the menu resolves it.
- Bottles lists the programs *you added* to each bottle, which is what its
  own library shows — not every `.exe` inside the prefix. When you have
  more than one bottle, program names are suffixed with the bottle they
  live in, since the same game installed in two prefixes is a normal
  Bottles setup.
- Removing a launcher collection never touches the launcher's own install.
  The removal prompt offers an opt-in **"Also delete the imported shortcut
  files and copied artwork"** checkbox; leave it unchecked (the default)
  and the stub and artwork folders stay behind under Kartend's data
  directory (`launcher-imports/<source>/`), where a future re-import picks
  them straight back up. The checkbox only ever deletes inside that managed
  folder — if you re-pointed the collection at your own artwork folder, it
  is never touched.
- The stub format is plain JSON — a `.kartlink` file can be hand-written to
  point anywhere a launcher template can take it, which is also how you'd
  add a one-off entry to a launcher collection.

See [Collections](Collections.md) for everything the resulting collection
shares with ordinary ones, and [Launchers](Launchers.md) for how launch
templates and `%1` substitution work.
