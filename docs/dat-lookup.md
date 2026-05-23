# DAT-file lookup architecture

Contributor-facing reference for the offline ROM-identification
subsystem. User-facing intro lives in
[Item-Metadata → DAT-file identification](guide/Item-Metadata.md#dat-file-identification-rom-collections);
the scrape integration is in
[Scraper](guide/Scraper.md).

## What it does

For a media file, Kartend can:

1. Hash the file (CRC32 / MD5 / SHA-1).
2. Look the hash up in a parsed DAT.
3. Return the canonical title (and `romName` for scraper input).

The DAT-side is two cooperating modules:

| Module | Role |
|--------|------|
| `DatLookup` | XML parsers + in-memory indexed `Store` |
| `DatCache` | On-disk SQLite cache for big DATs (typically MAME `listxml`) |

```
DAT file (.dat / .xml)
        │
        ▼
DatLookup::detectDialect ──┐
        │                  │
        ▼                  ▼
parseLogiqxDat        parseMameListXml
        │                  │
        └──────┬───────────┘
               ▼
       QList<DatRecord>
               │
        ┌──────┴──────┐
        ▼             ▼
DatLookup::Store    DatCache::Store
(in-memory, O(1)    (sqlite-backed,
 hash maps)          per-source rows)
```

## Supported dialects

Two XML schemas, auto-detected from the root element by
`DatLookup::detectDialect`:

| Dialect | Root | Coverage |
|---------|------|----------|
| **Logiqx** | `<datafile>` | No-Intro, Redump, TOSEC (they all share this schema) |
| **MAME** | `<mame>` | MAME `listxml` output (`mame -listxml > mame.xml`) |
| Unknown | (anything else) | Returns `Dialect::Unknown`; parsers refuse |

### Logiqx quirks

- `<game>` children carry one or more `<rom>` hash entries.
- TOSEC's optional `<release>` child of `<game>` is tolerated and
  ignored — it carries region/language metadata, not hashes.
- Streaming parser; memory is bounded by the result list, not the
  source buffer.

### MAME quirks

- `<machine>` children, not `<game>` — but the data shape is similar.
- `gameName` comes from `<description>` text when present (e.g. "Pac-Man
  (Midway)") and falls back to the cryptic `name=` set id ("pacman")
  otherwise.
- `<rom status="nodump">` entries are skipped — they're placeholders
  for undumped chips and carry no usable hash.

## `DatRecord`

```cpp
struct DatRecord {
  QString gameName;   // "Game Title (USA) (Rev 1)"
  QString romName;    // "Game Title (USA).gb"
  qint64 size = -1;   // bytes; -1 when the DAT didn't declare it
  QString crc;        // lowercase hex, possibly empty
  QString md5;        // lowercase hex, possibly empty
  QString sha1;       // lowercase hex, possibly empty
};
```

Hashes are normalised to **lowercase hex** at parse time so callers
don't need to per-call normalise — and so the SQLite cache's
indexes match across case variants.

`romName` is preferred over `gameName` when feeding ScreenScraper's
`romnom` param because SS expects a filename with extension and
will strip the extension itself.

## In-memory store

`DatLookup::Store` indexes a record list at construction:

- O(1) lookup via `QHash<QString, int>` for each hash kind.
- Hash collisions (two records with the same SHA-1, etc.) keep the
  **first-seen** record. DATs rarely collide; when they do the
  conservative default is "older entry wins."

### Lookup priority

```cpp
const DatRecord *Store::lookup(md5, sha1, crc) const;
```

Tries hash kinds in descending reliability:

1. `sha1` (if non-empty)
2. `md5` (if non-empty)
3. `crc` (if non-empty)

The first non-empty hash that matches an indexed record wins. A
caller with a SHA-1 doesn't pay the CRC32 path.

Returns `nullptr` on miss.

## On-disk cache (`DatCache`)

For sub-second DATs, the in-memory `Store` is fine. For MAME's full
`listxml` (~100 MB, ~250k entries) the parse cost is multiple
seconds — `DatCache` persists parsed records to a SQLite file so the
next launch is instant.

### Schema

```sql
CREATE TABLE dat_sources(
  id INTEGER PRIMARY KEY,
  path TEXT UNIQUE,
  mtime_unix_ms INTEGER,
  dialect INTEGER,
  record_count INTEGER,
  ingested_at_unix_ms INTEGER
);

CREATE TABLE dat_records(
  source_id INTEGER REFERENCES dat_sources(id),
  game_name TEXT,
  rom_name TEXT,
  size INTEGER,
  crc TEXT,
  md5 TEXT,
  sha1 TEXT
);

CREATE INDEX dat_records_source_sha1 ON dat_records(source_id, sha1);
CREATE INDEX dat_records_source_md5  ON dat_records(source_id, md5);
CREATE INDEX dat_records_source_crc  ON dat_records(source_id, crc);
```

### Cache invalidation

`openOrIngest(datPath)` checks the cache for a row matching
`path = datPath`. Behavior:

| Cache state | Action |
|-------------|--------|
| Hit, `mtime` matches | Single `SELECT` — fast |
| Hit, `mtime` differs | Drop old `dat_records` rows + re-ingest from XML |
| Miss | Parse via `DatLookup`, bulk-insert into cache |
| Unparseable / missing DAT | Return an error result; UI degrades to filename-only scraping |

An edited DAT picks up automatically on next open — no manual cache
flush. The "Clear caches" UI calls `Store::clearAll()` which drops
every source.

### Cache location

`DatCache::defaultPath()` returns
`QStandardPaths::CacheLocation/datcache.sqlite` —
`~/.cache/kartend/datcache.sqlite` on Linux. Tests pass a
`QTemporaryDir` path to avoid cross-test contamination.

### Connection-name handling

Each `Store` instance reserves a unique `QSqlDatabase` connection
name (the registry is global to the process). This lets tests
construct multiple `Store`s pointing at different files without
colliding; production code only ever needs one `Store` per process.

## Integration with the scraper

The scraper's pre-lookup hook hashes the file path passed in the
`LookupContext`:

```
provider.lookup(LookupContext{query="Game Title", filePath="/roms/file.bin"})
        │
        ▼
ScreenScraperProvider:
  - hash file (crc32, md5, sha1)
  - DatCache::openOrIngest(<every datFilePaths entry>)
  - DatCache::lookup(source, md5, sha1, crc) for each in order
  - if hit: replace query with DatRecord.romName
  - issue ScreenScraper request
```

Walks `CollectionConfig::scraperOverrides.datFilePaths` in list order;
**first hash hit wins**. The legacy single `datFilePath` field is
read on load and migrated into the array on next save (see
[settingsmanagercollections.cpp](../src/modules/data/settings/settingsmanagercollections.cpp)).

## Limitations / known gaps

- **No automatic DAT discovery** — Kartend doesn't crawl for DATs;
  the user points the picker at the file. A "match against the
  whole library of DATs you've configured globally" workflow could
  be added later.
- **No SHA-256 support** — DAT files don't ship SHA-256 hashes (the
  community catalogues predate it), so neither do we. If a future
  DAT format adds SHA-256, extend `DatRecord` + the schema +
  `lookup` to try it first.
- **`size` is informational only** — no size-pre-filter happens
  before hashing. If a future DAT format omits hashes for some
  entries (TOSEC variants do this) we could add a size fast-path.

## Adding a new DAT dialect

1. Add a new `Dialect` enum value in
   [datlookup.h](../src/modules/data/dat/datlookup.h).
2. Add a sniffer branch in `detectDialect()` so the auto-router
   picks the new dialect.
3. Add a `parseX(xml)` function returning
   `ErrorUtils::Result<QList<DatRecord>>`. Reuse `DatRecord` —
   if the new dialect carries information the struct can't
   represent, decide whether to add a field (everyone pays a few
   bytes per record) or drop the data.
4. Add a switch case in `parseDat()`.
5. Add a parser unit test (see
   `tests/utils/test_datlookup.cpp` for the existing patterns).

## Related code

| Concern | File |
|---------|------|
| Parsers + in-memory store | [src/modules/data/dat/datlookup.{h,cpp}](../src/modules/data/dat/) |
| SQLite cache | [src/modules/data/dat/datcache.{h,cpp}](../src/modules/data/dat/) |
| Per-collection DAT config | `CollectionConfig::scraperOverrides.datFilePaths` in [src/utils/app/collection/scraperoverrides.h](../src/utils/app/collection/scraperoverrides.h) |
| File hashing | [src/utils/fs/romhasher.{h,cpp}](../src/utils/fs/) |
| Scraper consumer | [src/modules/data/scraper/providers/screenscraperprovider.cpp](../src/modules/data/scraper/providers/screenscraperprovider.cpp) |
| User docs | [Item-Metadata → DAT-file identification](guide/Item-Metadata.md#dat-file-identification-rom-collections) |
