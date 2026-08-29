# Scraper

The scraper fetches metadata (titles, descriptions, genres, release
dates) and downloadable media (cover art, fanart, screenshots) for the
items in your library from external services. It can run against a
single item, the current selection, or every item in the current
collection.

Kartend ships with built-in adapters for several services:

| Provider | Categories it applies to | Capabilities |
|----------|--------------------------|--------------|
| **TMDB** (The Movie Database) | `video` (`movies`, `tv` normalise onto it) | Metadata + media |
| **MusicBrainz** | `audio` (`music`) | Metadata + media |
| **OpenLibrary** | `reference` (`book`, `documents`) | Metadata + media |
| **ScreenScraper.fr** | `games` (`game`, `rom`, `emulator`) | Metadata + media (quota-limited) |
| **Steam Store** | `games` | Metadata + media — used by [launcher import](Launcher-Import.md) |
| **Flathub** | `games` | Metadata only |
| **MobyGames**, **IGDB** | `games` | URL only — opens a search in your browser |
| **IMDb** | `video` | URL only |
| **Discogs** | `audio` | URL only |
| **Google Books** | `reference` | URL only |

There is no universal fallback provider: every entry declares the
categories it applies to, so a collection typed `Images`, or with a
custom type no provider claims, resolves to nothing.

The provider that runs for a collection is normally picked
automatically from the collection's [`type`](Collections.md). You can
override the pick per collection — see [Pinning a provider](#pinning-a-provider).

> **Where to find this** — Application menu **Tools → Scraper…** for
> batch / collection scrape, or right-click an item → **Scraper…** for
> single-item scrape. Credentials live in **Help → Scraper
> Credentials…** or **Settings → Scrapers**.

## Setting up credentials

Some providers require API keys or accounts before they'll return
results.

| Provider | What you need |
|----------|---------------|
| TMDB | API read-access token |
| MusicBrainz | None — there is no credentials panel for it |
| OpenLibrary | None |
| ScreenScraper | Optional username + password, which raise your daily quota above the anonymous floor. Developer credentials are bundled with the build and are not user-editable — the panel strips them on save. |

Open **Help → Scraper Credentials…** or **Settings → Scrapers**.
Only TMDB and ScreenScraper have credential panels; the others need nothing. After
saving, the values are stored in the OS keychain when QtKeychain is
available, or in `~/.config/kartend/kartend.cfg` under `[Scrapers]`
as plaintext otherwise. See [Keychain](Keychain.md) for the storage
model.

> **Web Search** has no credentials and is always available — it falls
> back to a search-engine URL for the matched query, useful when no
> API provider matches the collection's type.

## Provider registry

**Help → Scraper Providers…** opens a read-only registry of every
built-in metadata scraper. The dialog is a diagnostic surface — it
doesn't let you edit credentials or add new providers (that lives in
Settings → Scrapers → Credentials), but it answers "what providers
are wired in, what categories do they cover, and are mine
authenticated?" at a glance.

| Column | Contents |
|--------|----------|
| **Provider** | Display name (TMDB, MusicBrainz, OpenLibrary, ScreenScraper, Web Search). |
| **Categories** | Media types the provider can scrape (Video, Audio, Reference, Games). |
| **Capabilities** | One or more of `Web` (opens a search URL), `Metadata` (fetches fields), `Media` (downloads assets). |
| **Credentials** | `configured` if Kartend currently holds a non-empty value for any of the provider's fields; `not configured` otherwise. This reads the loaded settings, so it tells you whether Kartend *has* the credential — not whether the keychain is reachable right now. |
| **Test query** | Type a query in the field above the list, then double-click a row: the URL that provider would use is shown as text. Nothing is opened in a browser and no request is made — it is there to let you check the URL shape by eye. |

Useful when troubleshooting an "all scrapes fail" state — the dialog
tells you whether the registry sees the credentials at all (vs the
keychain not being unlocked, or the entry never having been saved).

## Pinning a provider

A collection's `type` field (`video`, `audio`, `games`, …) decides
which provider runs by default. To force a specific provider:

- In **Settings → Collection → General**, set **Scraper provider** to
  the provider you want
- Or hand-edit `[Collection Name] scraperProviderId=tmdb` (see
  [Configuration Reference](Configuration-Reference.md#scraper-overrides))

Use the empty string to fall back to automatic resolution by `type`.

## Running a scrape

### Single item

Right-click any item → **Scraper…**. The result dialog opens with the
selected item pre-loaded. The dialog has a **Scrape** button that
starts the lookup; you can keep the dialog open and hop between items
in the grid without restarting the scrape engine.

### Selection (multiple items)

Select multiple items with `Ctrl+click` / `Shift+click`, right-click
→ **Scraper…**. Acts as a small batch limited to the chosen items.

### Whole collection (batch)

**Tools → Scraper…** opens the unified dialog targeted at the *current
collection*. Click **Scrape** to start. The dialog shows per-item
progress, the running counts (`Scraped N · Skipped N · Errors N`),
and (for ScreenScraper) the daily quota counter.

Batch scrapes can be paused and resumed; closing the window keeps the
scrape running in the background.

### Collection artwork, fetched on creation

Separate from item scraping, a collection has artwork of its own: a
platform or collection logo and a background. When you create a
collection — adding one in **Settings → Collections**, duplicating an
existing one, importing a `.kart` bundle, or picking one up from an
installed launcher — Kartend fetches that artwork in the background so
the sidebar and home icons fill in as part of building the collection.

This runs silently: no progress window and no completion box. A
collection whose platform cannot be identified simply gets no artwork —
that is a miss, not an error — and the details go to the
[scrape log](#scrape-diagnostic-logging). Collections created while
another scrape is running wait for it to finish.

Only collections that resolve a scraper and have **no artwork set yet**
are fetched, so an icon you chose by hand is never replaced. Renaming a
collection does not re-fetch anything.

Turn it off with **Fetch collection artwork when a collection is
created** in **Settings → Scrapers**
([`autoScrapeEntityArtOnCreate`](Configuration-Reference.md#scraperoptions--global-scraper-performance--behavior)),
and fetch on demand instead with **Scrape collection info & artwork…**
from a collection's right-click menu.

### Re-scrape policy

The **Rescrape mode** in **Settings → Scrapers** controls what happens
when an item already has metadata:

| Mode | What it does | When to use |
|------|--------------|-------------|
| **Overwrite** | Always replace existing files and DB rows — except artwork types you linked by hand, which are never replaced | Migrating from one provider to another |
| **Fill missing** *(default)* | Only download / write fields and assets that are missing | Day-to-day catalog top-ups |
| **Update changed** | Download every field anyway, write only if bytes differ | "I think the source data changed but I want to compare first" — intentionally the slowest mode |
| **Skip** | Skip the whole item if any metadata exists | Strictly additive scrapes |

`Fill missing` and `Skip` also honor the **Skip recent** window
(default 30 days, see [`skipRecentScrapeDays`](Configuration-Reference.md#scraperoptions--global-scraper-performance--behavior)):
items scraped inside that window are passed over. `Overwrite` and
`Update changed` ignore the recency window — they visit every item.

## What it writes

A successful scrape can produce:

- **Item metadata** — title, description, genre, release date, custom
  fields. Stored in the `item_metadata` table; visible in the
  [details sidebar](Sidebar-and-Details-Pane.md) and the
  [detail page](Item-Metadata.md).
- **Artwork** — every image is saved into its **typed subdirectory**,
  `<artworkDirectory>/<type>/<item base name>.<ext>` — `front/`,
  `screenshot/`, `fanart/`, `logo/` and so on. Nothing is copied to the
  artwork root; the grid tile resolves the primary cover straight out
  of the typed subfolders. Videos land in `video/`, manuals in
  `manual/`. See [Artwork](Artwork.md).
- **Metadata sidecar** — alongside the database row, a readable JSON
  copy is written to `<artworkDirectory>/metadata/<base name>.json`.

> **A cover you linked by hand is left alone.** If an item has a manual
> link for an artwork type — set through **Item Artwork Links** or the
> **Assign Missing Artwork…** wizard — a scrape skips that type and
> counts it as skipped, in every re-scrape mode including **Overwrite**.
> Other types on the same item still scrape normally. The link stops
> counting once the image it points at is gone, so deleting that file
> (or clearing the link) lets the next scrape fill the type again.

## Performance and pacing

**Settings → Scrapers** exposes a small set of performance knobs
(visible when the preset is **Custom** — the **Fastest**, **Balanced**,
**Best Quality** presets drive them automatically):

| Knob | Default | What it controls |
|------|---------|------------------|
| **Media concurrency** | `2` | Parallel downloads *per item*. Higher = faster, but a single host eventually fairness-collapses. |
| **Batch item concurrency** | `4` | Items scraped in parallel during a batch. Total in-flight requests = `batchItemConcurrency × mediaConcurrency`. |
| **Media throttle** | `100 ms` | Delay between requests. Useful when a provider's rate-limiter is the bottleneck. |
| **Max media dimension** | `1024 px` | Cap on cover-art resolution. `0` = full resolution (largest file size). |
| **Prefer JPG output** | off | Ask ScreenScraper for re-encoded JPGs instead of PNGs. Lossy; only sensible on Fastest. |

See [`[ScraperOptions]`](Configuration-Reference.md#scraperoptions--global-scraper-performance--behavior)
for the full INI surface.

## ScreenScraper quotas

ScreenScraper imposes a per-day request quota that depends on whether
you're hitting it anonymously, with developer credentials, or with a
paid contributor account. The scraper dialog shows the running
`requests today / daily allowance` counter and the reset time
returned by the API.

When the quota is exhausted mid-batch, the scrape stops cleanly with
a "ScreenScraper's daily quota is exhausted" message. Re-launch after
the reset (typically midnight in the API's timezone) or resume
manually.

To raise the cap:

1. Create an account on screenscraper.fr.
2. Optionally subscribe to bump your user-level quota.
3. Enter your ScreenScraper **username and password** in **Scraper
   Credentials**. (Developer credentials are bundled with the build and
   are deliberately not exposed — the panel strips any `dev_id` /
   `dev_password` keys it finds on save.)
   `user_id` + `user_password` raise the cap further.

## DAT file identification (ROMs)

For ROM-style collections, Kartend can hash each file and look up the
canonical title from a DAT file (No-Intro / Redump / TOSEC Logiqx, or
MAME listxml). When a hash hits the DAT, the matched title is used as
the search query into ScreenScraper, which improves match accuracy
dramatically for region/revision variants.

Configure DAT files per-collection in **Settings → Collection →
Scraper** (or via the `datFilePaths` array — see
[Configuration Reference](Configuration-Reference.md#scraper-overrides)).
DAT files are walked in list order; first hash hit wins.

## Resume after crash

A batch scrape periodically snapshots its progress to
`pending-scrape.json` in the config directory. If Kartend exits
mid-batch (crash, power loss, user kill), the next launch detects the
snapshot and prompts:

- **Resume** — picks back up where it left off
- **Discard** — throws the snapshot away and starts fresh next time

To make this silent — useful for unattended overnight batches — turn on
**Auto-resume** in **Settings → Scrapers** (or set
`scrapeAutoResume=true` in `[ScraperOptions]`).

## Scrape diagnostic logging

A GUI build has no visible stderr, so when a scrape misbehaves there's
no obvious way to see what it actually did. Turn on
**Scrape logging** in **Settings → Scrapers** (or set
`scrapeLogging=true` in `[ScraperOptions]`) to:

- Raise the `kartend.scrape*` logging categories to debug+info
- Tee the output to a size-capped `scrape.log` in the config directory

See [Logging & Diagnostics](Logging-and-Diagnostics.md) for the
matching environment variables and `kartend.*` category list.

## Web Search fallback

If no API provider matches the collection's type, the context menu
still shows **Look up online ▶** with one entry per registered web
provider. These don't write anything to your library — they open a
prepared search URL in your default browser so you can find the
metadata yourself.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| "Please set credentials in Settings → Scrapers" | Provider expects an API key that isn't configured | **Help → Scraper Credentials…**, fill in the required fields |
| "Scrape stopped — quota exhausted" (ScreenScraper) | Daily quota hit | Wait for the reset, or add user credentials to raise the cap |
| Items skipped silently in batch | `Fill missing` or `Skip` mode is honoring the **Skip recent** window | Lower `skipRecentScrapeDays`, or switch to `Overwrite` |
| Wrong title matched for a ROM | DAT lookup not configured or hash didn't match the DAT entry | Add a more complete DAT file, or override the title with [Item Metadata](Item-Metadata.md) |
| Tile still shows the old cover after a scrape | A cover you linked by hand wins over a scraped one — the scrape leaves that artwork type alone and reports it as skipped | Clear the link in [Item Artwork Links](Artwork.md), then scrape again |
| `Resume / Discard` prompt every launch | A previous scrape didn't finish | Pick Discard once to clear the snapshot |

## Where to next

- [Keychain](Keychain.md) — how credentials are stored
- [Configuration Reference](Configuration-Reference.md#scrapers--credential-storage) —
  every scraper-related INI key
- [Item Metadata](Item-Metadata.md) — what the scraped fields surface
  as in the UI
- [Artwork](Artwork.md) — auto-discovery, manual links, the artwork
  fallback chain

## For developers

- Provider base interface:
  [src/modules/data/scraper/providers/metadataprovider.h](../../src/modules/data/scraper/providers/metadataprovider.h)
  — `id`, `displayName`, `categories`, `Capabilities` (WebSearch /
  MetadataLookup / MediaFetch).
- Provider registry:
  [metadataproviderregistry.cpp](../../src/modules/data/scraper/providers/metadataproviderregistry.cpp).
- Provider implementations: `tmdbprovider.cpp`,
  `screenscraperprovider.cpp`, `musicbrainzprovider.cpp`,
  `openlibraryprovider.cpp`, `websearchprovider.cpp` (Stage-1
  fallback).
- Parsers turn provider responses into Kartend's internal
  representation:
  [src/modules/data/scraper/parsers/](../../src/modules/data/scraper/parsers/).
- Core engine:
  [scraperservice.cpp](../../src/modules/data/scraper/core/scraperservice.cpp),
  batch runner
  [batchscraperunner.cpp](../../src/modules/data/scraper/core/batchscraperunner.cpp),
  HTTP client
  [httpclient.cpp](../../src/modules/data/scraper/core/httpclient.cpp),
  write worker
  [scrapewriteworker.cpp](../../src/modules/data/scraper/core/scrapewriteworker.cpp),
  resume snapshot
  [scrapepersistence.cpp](../../src/modules/data/scraper/core/scrapepersistence.cpp).
- ScreenScraper-specific helpers (quota tracking, system-id catalog,
  media-type cache) live alongside its provider.
- Result UI:
  [src/ui/dialogs/scraper/result/](../../src/ui/dialogs/scraper/result/)
  (`scraperesultdialogunified.cpp` is the modern unified dialog;
  `scraperesultdialog.cpp` is its host).
- Adding a new provider: implement `MetadataProvider`, register it
  via `MetadataProviderRegistry`, add a credentials panel in
  [scrapercredentialspanel.cpp](../../src/ui/dialogs/scraper/scrapercredentialspanel.cpp)
  if it needs auth, and (when adding `MetadataLookup` / `MediaFetch`)
  a parser. A dedicated dev doc for the provider plugin pattern is
  planned.
