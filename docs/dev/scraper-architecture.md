# Scraper architecture

Contributor-facing overview of the metadata-scraper subsystem. For
the user-facing workflow (rescrape modes, quotas, DAT lookup, resume-
after-crash) see [Scraper](../user/Scraper.md).

## Layout

```
src/modules/data/scraper/
├── core/         # engine: service, batch runner, HTTP, persistence
├── parsers/      # provider-response → ScrapedItem translators
└── providers/    # MetadataProvider implementations + registry
```

| Layer | Responsibility |
|-------|----------------|
| `providers/` | Adapter per upstream service (TMDB, MusicBrainz, OpenLibrary, ScreenScraper, WebSearch). Each provider knows its own auth, endpoints, and rate-limit semantics. |
| `parsers/` | Pure translator from a provider-specific JSON/XML payload to Kartend's neutral `Scraper::ScrapedItem` / `MediaAsset` shape. Stateless — easy to unit-test in isolation. |
| `core/` | UI-agnostic engine: lookup orchestration, batch sequencing, HTTP throttling, write-out of files + DB rows, resume-after-crash snapshots. |

## The provider interface

Two-tier hierarchy: a base `MetadataProvider` for the
"capabilities + identity" surface, and a derived
`MetadataLookupProvider` for the async lookup / detail / media
methods.

### `MetadataProvider` (base)

`src/modules/data/scraper/providers/metadataprovider.h`. Every
provider implements:

| Method | Returns | What for |
|--------|---------|----------|
| `id()` | stable lowercase ASCII tag (`tmdb`, `screenscraper`, `musicbrainz`, …) | Persisted in `ItemMetadata.source` and `CollectionConfig::scraperProviderId`. **Renaming is a wire-format break.** |
| `displayName()` | human label | Settings panel, context menu |
| `categories()` | lowercase tags this provider applies to (`{"video","movies"}`, `{"games"}`, …) | Auto-routing from a collection's `type` |
| `capabilities()` | bit flags: `WebSearch`, `MetadataLookup`, `MediaFetch` | What the provider can actually do |
| `searchUrl(query)` | `QUrl` (or invalid) | Only when `WebSearch` is in `capabilities()` |

### `MetadataLookupProvider` (Stage 2)

Subclass of the base, adding the three async callback methods every
API-backed scraper needs:

| Method | Pipeline stage |
|--------|----------------|
| `lookup(query, callback)` or `lookup(LookupContext, callback)` | Search the upstream and return up to N `ScrapeCandidate`s. The context form lets providers read the local file path for hash-based ID (used by ScreenScraper). |
| `fetchDetail(candidate, callback)` | Pull the full `ScrapedItem` for the candidate the user picked. |
| `fetchMediaBytes(url, callback)` | Download one media asset. Provider-owned so each upstream can route through its own throttled HTTP client / auth headers. |

Two optional hooks for upstream health and quotas:

- `fetchHealthStatus(callback)` — provider's view of upstream
  availability; the result dialog surfaces `humanStatus` verbatim
  and disables the Apply button when `refuseScrape` is true.
  ScreenScraper overrides to query `ssinfraInfos.php`; everyone else
  stays silent.
- `quotaStatus()` — synchronous accessor for the most-recent quota
  snapshot. The batch driver polls this after every item so the
  dialog can render a live "N / M requests today" counter. Only
  ScreenScraper today; default is an invalid (`valid == false`)
  status.

### Why callbacks, not `QFuture`

Qt 6's `QFuture<T>` is tied to `QThreadPool`. Kartend's HTTP layer
runs on the **main thread** and uses signal/slot for async — adding a
threadpool just to satisfy `QFuture`'s API would mean trampolining
every reply through a worker thread for no real benefit. Callbacks
match the existing async shape.

## The registry

`src/modules/data/scraper/providers/metadataproviderregistry.h`. A
**compile-time** registry — no plugin loader, no dynamic
registration, no DLL surface. Adding a provider means subclassing
and registering in `builtIn()`.

Key entry points:

- `builtIn(settingsAccessor, collectionAccessor)` — every built-in
  provider as `std::vector<std::unique_ptr<MetadataProvider>>`. The
  accessors are functions returning the live `GeneralSettings` /
  `CollectionConfig` so providers can read credentials / per-
  collection overrides at request time without holding stale copies.
- `forCategory(all, "games")` — filters by category with a synonym
  table so `"game"` matches `"games"`, `"movie"` matches `"video"`,
  etc.
- `scrapingProviders()` — only the providers that advertise
  `MetadataLookup`, in display order. Drives the Settings → Collection
  → Scraper picker.
- `defaultScraperForType("audio")` — the first scraping provider
  whose category matches the (normalised) collection type. Used at
  collection-create time to auto-associate a sensible scraper.

## Request lifecycle

```
User picks "Scraper…"
        │
        ▼
ScrapeResultDialogUnified ◄── result UI (single & batch)
        │
        ▼
ScraperService   ◄── orchestration, persistence, retry policy
        │
        ▼
provider.lookup(query) ──▶ HttpClient ──▶ upstream API
        │                                       │
        │  ◄────────────  raw JSON/XML  ────────┘
        ▼
parser::parse(...) ──▶ Scraper::ScrapedItem  (neutral)
        │
        ▼
user picks candidate → provider.fetchDetail(candidate)
        │
        ▼
ScrapeWriteWorker
   │  ├─ write item_metadata rows
   │  ├─ download media via provider.fetchMediaBytes()
   │  └─ write artwork files via QSaveFile + syncDirectory
   ▼
ScrapePersistence snapshot (per-batch, for resume)
```

The provider is a **pure adapter** — it never writes to the DB or to
disk. All write-side concerns live in `core/scrapewriteworker.cpp`.

## HTTP

`src/modules/data/scraper/core/httpclient.cpp` is shared across
providers. Highlights:

- HTTP/2 multiplexed connections where the upstream supports it.
  Eliminates the TCP-fairness collapse that limits parallel media
  downloads when each stream needs its own connection. (Hence the
  `mediaConcurrency` default of `2` rather than higher — set with
  HTTP/1.1-era pre-multiplexing in mind.)
- Throttling (`mediaThrottleMs`) is enforced before every request.
- Per-provider auth headers stay inside the provider — `HttpClient`
  is auth-agnostic.

## Batch scrape & resume

`core/batchscraperunner.cpp` drives whole-collection scrapes:

- Sequences items through `provider.lookup → fetchDetail →
  fetchMediaBytes` with `batchItemConcurrency` items in flight.
- Honors the **rescrape mode** (`Overwrite`, `FillMissing`,
  `UpdateChanged`, `Skip`) and the **skip-recent window**
  (`skipRecentScrapeDays`) — see
  [Configuration Reference](../user/Configuration-Reference.md#scraperoptions--global-scraper-performance--behavior).
- Snapshots batch state to `pending-scrape.json` in the config dir
  via `ScrapePersistence`. On the next launch, MainWindow detects
  the snapshot and either auto-resumes (when `scrapeAutoResume` is
  on) or prompts the user with **Resume / Discard**.

## ScreenScraper-specific helpers

ScreenScraper has the most domain logic of any provider because of
its quota / region / archive-hashing peculiarities. The
provider-specific helpers (`providers/screenscraper*.{h,cpp}`):

| File | What it does |
|------|--------------|
| `screenscrapersystems.cpp` | Static `systemeid` table and a `system → id` lookup |
| `screenscrapersystemcache.cpp` | Per-collection cached system match (stable across rescrapes) |
| `screenscrapermediatypecache.cpp` | Cache of which `media_type` strings the user's account is allowed to fetch |
| `screenscraperquotamanager.cpp` | Parses the `ssuser` block from every response and exposes the running quota; drives the dialog's live counter |
| `screenscrapercatalogmanager.cpp` | First-time catalog warm (system list, regions) |

## Adding a new provider

1. **Pick a stable id**. Lowercase ASCII, no whitespace, no
   underscores-or-dashes-with-a-strong-opinion (e.g. `igdb`,
   `discogs`). The id goes into the wire format
   (`ItemMetadata.source`, `CollectionConfig::scraperProviderId`)
   — renaming later is a config-migration story.

2. **Subclass `MetadataProvider`** (if URL-only / WebSearch) or
   `MetadataLookupProvider` (if API + media). Implement at minimum
   `id()`, `displayName()`, `categories()`, `capabilities()`. For
   Stage-2 providers, also `lookup()`, `fetchDetail()`,
   `fetchMediaBytes()`.

3. **Add a parser** if the upstream returns JSON/XML the existing
   parsers don't already cover. Parsers live in
   [src/modules/data/scraper/parsers/](../../src/modules/data/scraper/parsers/),
   take a raw `QByteArray` (or `QJsonDocument`), and return a
   `Scraper::ScrapedItem`. Keep them stateless and unit-testable.

4. **Register in `MetadataProviderRegistry::builtIn()`**. Append a
   `std::make_unique<MyProvider>(...)` to the returned vector. The
   `settingsAccessor` / `collectionAccessor` parameters are
   available if your provider needs credentials or per-collection
   context.

5. **Add a credentials panel** in
   [scrapercredentialspanel.cpp](../../src/ui/dialogs/scraper/scrapercredentialspanel.cpp)
   if your provider has auth. The panel writes into
   `GeneralSettings::scraperCredentials[<provider_id>]`, which the
   provider reads at request time via the `settingsAccessor`.

6. **(If applicable) Add the category to the synonym table** in
   `metadataproviderregistry.cpp`'s `normaliseCategory()` so the
   free-form `CollectionConfig::type` resolves to your category
   without users having to type the canonical tag exactly.

7. **Add unit tests** for the parser. The HTTP path is hard to
   unit-test (live upstream), so parser tests are the cheap and
   high-value place to defend regressions. See
   `tests/utils/test_screenscraperparser.cpp` for the existing
   pattern.

8. **Document the credentials** in
   [docs/user/Scraper.md](../user/Scraper.md) (user-facing) and
   [docs/user/Keychain.md](../user/Keychain.md) (storage).

## Related code

| Concern | File |
|---------|------|
| Base interface | [providers/metadataprovider.h](../../src/modules/data/scraper/providers/metadataprovider.h) |
| Async API | [providers/metadatalookupprovider.h](../../src/modules/data/scraper/providers/metadatalookupprovider.h) |
| Registry | [providers/metadataproviderregistry.cpp](../../src/modules/data/scraper/providers/metadataproviderregistry.cpp) |
| Neutral types (`ScrapedItem`, `MediaAsset`, `QuotaStatus`) | [core/scrapertypes.h](../../src/modules/data/scraper/core/scrapertypes.h) |
| Engine | [core/scraperservice.cpp](../../src/modules/data/scraper/core/scraperservice.cpp) |
| Batch driver | [core/batchscraperunner.cpp](../../src/modules/data/scraper/core/batchscraperunner.cpp) |
| HTTP | [core/httpclient.cpp](../../src/modules/data/scraper/core/httpclient.cpp) |
| Write worker | [core/scrapewriteworker.cpp](../../src/modules/data/scraper/core/scrapewriteworker.cpp) |
| Resume persistence | [core/scrapepersistence.cpp](../../src/modules/data/scraper/core/scrapepersistence.cpp) |
| Result UI | [src/ui/dialogs/scraper/result/](../../src/ui/dialogs/scraper/result/) |
| User docs | [docs/user/Scraper.md](../user/Scraper.md) |
