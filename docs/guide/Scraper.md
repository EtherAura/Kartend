# Scraper

The scraper fetches metadata (titles, descriptions, genres, release
dates) and downloadable media (cover art, fanart, screenshots) for the
items in your library from external services. It can run against a
single item, the current selection, or every item in the current
collection.

Kartend ships with built-in adapters for several services:

| Provider | Categories it applies to | Capabilities |
|----------|--------------------------|--------------|
| **TMDB** (The Movie Database) | `video`, `movies`, `tv` | Metadata + media |
| **MusicBrainz** | `audio`, `music` | Metadata + media |
| **OpenLibrary** | `book`, `documents`, `reference` | Metadata + media |
| **ScreenScraper.fr** | `games`, `emulation`, `retro` | Metadata + media (quota-limited) |
| **Web Search** *(fallback)* | every collection | URL only — opens the matched query in your browser |

The provider that runs for a collection is normally picked
automatically from the collection's [`type`](Collections.md). You can
override the pick per collection — see [Pinning a provider](#pinning-a-provider).

> **Where to find this** — Application menu **Tools → Scraper…** for
> batch / collection scrape, or right-click an item → **Scraper…** for
> single-item scrape. Credentials live in **Tools → Scraper
> Credentials…** or **Settings → Scrapers**.

## Setting up credentials

Some providers require API keys or accounts before they'll return
results.

| Provider | What you need |
|----------|---------------|
| TMDB | API read-access token |
| MusicBrainz | Application name + contact (optional but recommended) |
| OpenLibrary | None |
| ScreenScraper | `dev_id` + `dev_password` (developer credentials) + optionally a `user_id` / `user_password` (raises your daily quota above the anonymous floor) |

Open **Tools → Scraper Credentials…** or **Settings → Scrapers**.
Each provider has its own panel with the fields it expects. After
saving, the values are stored in the OS keychain when QtKeychain is
available, or in `~/.config/kartend/kartend.cfg` under `[Scrapers]`
as plaintext otherwise. See [Keychain](Keychain.md) for the storage
model.

> **Web Search** has no credentials and is always available — it falls
> back to a search-engine URL for the matched query, useful when no
> API provider matches the collection's type.

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

### Re-scrape policy

The **Rescrape mode** in **Settings → Scrapers** controls what happens
when an item already has metadata:

| Mode | What it does | When to use |
|------|--------------|-------------|
| **Overwrite** | Always replace existing files and DB rows | Migrating from one provider to another |
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
- **Cover artwork** — saved to the collection's `artworkDirectory`
  with the standard fallback filename (see [Artwork](Artwork.md)).
- **Custom artwork types** — fanart, screenshot, logo, etc., for
  providers that supply them. Saved into the directory layout
  Kartend expects so they show up in the artwork gallery.

Kartend never overwrites a user-supplied artwork file silently — if
an item already has a manual override via
[Item Artwork Links](Artwork.md), the scrape skips that asset.

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
3. Enter your `dev_id` + `dev_password` in **Scraper Credentials**;
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
| "Please set credentials in Settings → Scrapers" | Provider expects an API key that isn't configured | **Tools → Scraper Credentials…**, fill in the required fields |
| "Scrape stopped — quota exhausted" (ScreenScraper) | Daily quota hit | Wait for the reset, or add user credentials to raise the cap |
| Items skipped silently in batch | `Fill missing` or `Skip` mode is honoring the **Skip recent** window | Lower `skipRecentScrapeDays`, or switch to `Overwrite` |
| Wrong title matched for a ROM | DAT lookup not configured or hash didn't match the DAT entry | Add a more complete DAT file, or override the title with [Item Metadata](Item-Metadata.md) |
| Artwork not refreshed even with Overwrite | Item has a manual artwork link via [Item Artwork Links](Artwork.md) — those are never overwritten | Remove the link first, or use a different file path |
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
  planned (see Kartend-6qs0).
