# `.kart` format

The `.kart` file is Kartend's portable, self-contained collection
bundle — config + metadata + every media / artwork / video / manual
file the collection references, packed into one append-only stream.

Used by **Backup & Migration** (export from one of your machines,
import on another) and by the headless `--kart-export` /
`--kart-import` CLI.

User-facing overview: [Backup & Migration](../user/Backup-and-Migration.md).
This page is the on-disk format spec.

## Byte layout

Little-endian throughout.

```
┌─────────────────────────────────────────────────────────────┐
│ magic                  8 bytes                              │
├─────────────────────────────────────────────────────────────┤
│ manifest_size           4 bytes (u32)                       │
├─────────────────────────────────────────────────────────────┤
│ manifest                manifest_size bytes (UTF-8 JSON)    │
├─────────────────────────────────────────────────────────────┤
│ ┌─ entry 0 ───────────────────────────────────────────────┐ │
│ │ flags                1 byte (bitfield)                  │ │
│ │ compression          1 byte                             │ │
│ │ path_len             2 bytes (u16)                      │ │
│ │ path                 path_len bytes (UTF-8 relative)    │ │
│ │ original_size        8 bytes (u64)                      │ │
│ │ payload_size         8 bytes (u64)                      │ │
│ │ sha256               32 bytes (of UNCOMPRESSED payload) │ │
│ │ payload              payload_size bytes                 │ │
│ └─────────────────────────────────────────────────────────┘ │
│ ┌─ entry 1 ───────────────────────────────────────────────┐ │
│ │ ...                                                     │ │
│ └─────────────────────────────────────────────────────────┘ │
│ ...entries continue until EOF                               │
└─────────────────────────────────────────────────────────────┘
```

### Magic

Eight bytes: `'K' 'A' 'R' 'T' 0x00 0x00 0x00 0x01`. The **trailing
byte is the binary-format version** — bumping it signals a
backwards-incompatible on-disk layout change. Manifest schema changes
are versioned independently via the `format_version` field in the
manifest JSON.

`KartFormat::hasMagic(bytes, len)` does the check; readers reject
files where the first eight bytes don't match exactly.

### Manifest

A single UTF-8 JSON object, length-prefixed by a `u32`. Maximum
manifest size is `KartFormat::MAX_MANIFEST_SIZE` (64 MiB) — readers
refuse anything larger.

Shape:

```json
{
  "format_version": 1,
  "uuid": "abcd-1234-…",
  "version": "1.0",
  "created_at": "2026-05-22T19:20:00Z",
  "name": "Films",
  "author": "etheraura",
  "description": "Personal film collection",
  "license": "CC0-1.0",
  "collection_config": { ...full CollectionConfig... },
  "launchers": [ { LauncherPreset }, ... ],
  "items": [
    {
      "media_path": "items/film-1.mkv",
      "artwork_path": "artwork/film-1.jpg",
      "video_path": "video/film-1.webm",
      "manual_path": "",
      "title": "Film 1 (Director's Cut)",
      "metadata": { ... ItemMetadata ... },
      "launcher_index": -1
    }
  ]
}
```

`format_version` is the **schema** version of the manifest JSON;
changes (new top-level fields, renames) bump it. Readers fail-loudly
on a missing or unparseable `format_version` field.

The string paths inside each item entry (`media_path`,
`artwork_path`, …) are **relative paths inside the kart** — they
reference entries by the `path` field of the entry header. Readers
look the entry up by path, decompress the payload, verify the
SHA-256, and stream the bytes back into the destination layout.

`collection_config` round-trips a full `CollectionConfig` struct,
including every leaf cluster (`CollectionBackground`,
`GridLayoutPreferences`, `SidebarAppearance`, `LauncherProfile`,
`ScraperOverrides`, `CollectionTreeSettings`, `SystemIconSettings`,
etc.) — see
[Configuration Reference](../user/Configuration-Reference.md) for the
field list.

Adding a field to a cluster does **not** bump `format_version`: every
reader defaults a missing key to the struct's own default, and ignores
keys it does not know, so bundles stay readable in both directions. The
version exists for changes that break that — a rename, a retype, or a
new *top-level* field a reader must have.

Two of those clusters carry values that become filesystem path
components downstream, and the reader treats them as untrusted:
`system_icon_name` and `system_icon_pack` are **identities** (a libretro
system name and a pack directory name), not paths, and are resolved
against whatever RetroArch the importing machine has. They are checked
with `PathUtils::isSafePathComponent` on import and dropped if they
carry a separator or traversal, which leaves the row without a glyph —
the same outcome as naming a system the importer does not have.

### Entry header

Per file inside the kart:

| Field | Size | Notes |
|-------|------|-------|
| `flags` | 1 byte | Bitfield: `0x01 Media`, `0x02 Artwork`, `0x04 Video`, `0x08 Manual` |
| `compression` | 1 byte | `0 None`, `1 Zstd`, `2 Zlib` |
| `path_len` | 2 bytes | Length of `path` in bytes; max `KartFormat::MAX_PATH_LEN` (4096) |
| `path` | `path_len` bytes | UTF-8, relative to a virtual kart root |
| `original_size` | 8 bytes | Uncompressed payload byte count |
| `payload_size` | 8 bytes | Compressed (or raw) bytes that follow. Max `KartFormat::MAX_ENTRY_SIZE` (8 GiB) |
| `sha256` | 32 bytes | SHA-256 of the **uncompressed** payload (so compression mode swaps don't break the hash) |
| `payload` | `payload_size` bytes | Compressed or raw bytes |

The hash is computed against the **original (uncompressed)** payload
so a file re-emitted with a different compression mode (e.g. zstd
build vs zlib build) keeps the same hash.

## Compression

`compression` byte in each entry header selects the codec:

| Code | Codec | Available when |
|------|-------|----------------|
| `0` | None | Always |
| `1` | Zstd | `KARTEND_HAVE_ZSTD` build flag |
| `2` | Zlib | Always (via Qt's built-in `qCompress` / `qUncompress`) |

`kartcompression.cpp` picks **Zstd** when available, **Zlib** as a
fallback. The build-flag is set by CMake when `find_package(zstd
QUIET)` succeeds; absence means the binary still **reads** Zstd
entries (well — only if it was built with zstd) but **writes** with
Zlib. Practically, builds without zstd:

- Can read Zlib- or None-compressed entries.
- Can read Zstd entries **only if linked against zstd** — a non-zstd
  build returns a `KartFormat::CodecUnavailable` error for any
  Zstd entry.
- Write everything as Zlib.

So a kart written on a Zstd-capable machine is portable to a
Zlib-only build only if the writer used `compression=Zlib`. The
writer always picks the strongest codec the build supports; if you
move karts between your own machines, build all of them with zstd
to avoid surprises.

Zstd uses compression level 3 — the same default `zstd(1)` picks,
chosen for the speed/ratio trade-off on media bundles where the
items are usually already-compressed binary data.

## Streaming

Both reader and writer are streaming:

- **Writer** ([kartwriter.cpp](../../src/modules/data/kart/kartwriter.cpp))
  emits the header, manifest, then one entry at a time. The output
  is written via `QSaveFile` so a crash mid-write doesn't leave a
  partial `.kart` at the destination path — see
  [architecture.md → Atomic File Writes](architecture.md#atomic-file-writes).
- **Reader** ([kartreader.cpp](../../src/modules/data/kart/kartreader.cpp))
  validates the magic, parses the manifest, then iterates entries
  until EOF. Each entry is decompressed and verified against its
  SHA-256 before its bytes hit the destination filesystem.

The whole stream is **append-only** — there's no central directory
table. Concatenating two karts isn't supported (the second's magic
ends up in the middle of the first's entry payload region), but
karts can be re-emitted with a subset of entries via the merger
([kartmerge.cpp](../../src/modules/data/kart/kartmerge.cpp)).

## Merging two karts

`KartMerge` reads two `.kart` files, dedupes by SHA-256 across
entries (same uncompressed bytes ⇒ one entry kept), and emits the
union. The manifest is merged item-by-item using the source kart's
item UUID as the dedupe key; the **later** kart's manifest fields
win on conflict (a deliberate "import-overrides-existing" stance).

The kart-merge dialog (`src/ui/dialogs/kart/kartmergedialog.cpp`)
surfaces this from the UI when an import would collide with an
existing collection.

## Limits

| Constant | Value | Purpose | Enforced |
|----------|-------|---------|----------|
| `KartFormat::MAGIC_SIZE` | 8 | Magic byte count | parse |
| `KartFormat::CURRENT_VERSION` | 2 | In-band schema version | parse |
| `KartFormat::SHA256_SIZE` | 32 | Hash byte count | parse |
| `KartFormat::MAX_PATH_LEN` | 4096 | Per-entry path cap | parse |
| `KartFormat::MAX_MANIFEST_SIZE` | 64 MiB | Manifest cap | parse |
| `KartFormat::MAX_ENTRY_SIZE` | 8 GiB | Per-entry payload cap | parse (declared) |
| `KartFormat::MAX_ENTRY_COUNT` | 200000 | Total entry-count cap | extract |
| `KartFormat::MAX_MANIFEST_ITEMS` | 200000 | Manifest array ceiling | parse |
| `KartFormat::MAX_TOTAL_EXTRACTED_BYTES` | 2 TiB | Aggregate payload backstop | extract |

Header-declared caps (`MAX_PATH_LEN`, `MAX_MANIFEST_SIZE`,
`MAX_ENTRY_SIZE`) are checked at parse time: the reader returns a
structured error when a header advertises a value above the cap rather
than allocating an attacker-controlled buffer.

`MAX_ENTRY_COUNT` and `MAX_TOTAL_EXTRACTED_BYTES` are **enforced during
extraction, against bytes actually written** — not against declared
sizes. That is the stronger property, and the distinction matters: a
header may under-declare an entry's size, so the streaming zstd path
re-checks real decompressed bytes per block and in total.

`MAX_TOTAL_EXTRACTED_BYTES` (2 TiB) is a deliberately generous sanity
backstop, **not** a disk-fill guard — it exceeds most target volumes,
and there is no free-space clamp on this path (contrast the DAT
downloader, which clamps to free space minus a reserve).

When adding a decompression branch, keep the streaming/chunked shape the
zstd path uses: bound the output against bytes actually produced, before
allocating, rather than trusting any size the input declares about
itself.

## Version compatibility

| Change | Bump |
|--------|------|
| Add a new entry flag bit | Magic byte (new layout / new mandatory header semantics) |
| Add a new compression codec id | None (existing readers fail with `CodecUnavailable` for the new id, which is the desired forward-compat behavior) |
| Add a new manifest JSON top-level field | None — JSON ignores unknown fields. Readers continue to work. |
| Rename a manifest field | `format_version` |
| Remove a manifest field | `format_version` |
| Change the entry-header binary layout | Magic byte |

The split between **binary layout** (in the magic) and **manifest
schema** (in `format_version`) keeps the version surface small for
the common case (adding a metadata field) while still giving us a
clean break for hard backwards-incompatible layout changes.

## Related code

| Concern | File |
|---------|------|
| Format constants & magic | [src/modules/data/kart/kartformat.h](../../src/modules/data/kart/kartformat.h) |
| Manifest struct + JSON | [kartmanifest.{h,cpp}](../../src/modules/data/kart/) |
| Reader (streaming, hash-verified) | [kartreader.cpp](../../src/modules/data/kart/kartreader.cpp) |
| Writer (streaming, atomic via QSaveFile) | [kartwriter.cpp](../../src/modules/data/kart/kartwriter.cpp) |
| Zstd / Zlib selection | [kartcompression.cpp](../../src/modules/data/kart/kartcompression.cpp) |
| Merger (dedupe by SHA-256) | [kartmerge.cpp](../../src/modules/data/kart/kartmerge.cpp) |
| Manager (high-level export / import) | [kartmanager.{h,cpp}](../../src/modules/data/kart/) |
| User docs | [docs/user/Backup-and-Migration.md](../user/Backup-and-Migration.md) |
