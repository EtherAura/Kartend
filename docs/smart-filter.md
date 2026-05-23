# Smart-filter DSL

The smart-filter DSL is what backs [Smart Playlists](guide/Smart-Playlists.md).
It's a small discriminated-union spec that describes "what items
belong in this playlist," serialised as JSON and stored on the
`playlists.smart_filter` column. Membership is re-evaluated every
time the playlist is opened.

This page is the contributor-facing spec for the format and the
evaluator. The user-facing page is the
[Smart Playlists guide](guide/Smart-Playlists.md).

## The Filter struct

```cpp
// src/utils/db/smartfilter.h
namespace SmartFilter {

enum class Kind {
  RecentlyLaunched, // params.limit
  TopPlayed,        // params.limit
  NeverPlayed,      // params.limit
  ByExtension,      // params.extensions
  HasArtwork,       // (no params)
  ByDateAdded,      // params.days
};

struct Filter {
  Kind kind = Kind::RecentlyLaunched;
  int limit = 50;                 // counted kinds only
  QStringList extensions;          // ByExtension only
  int days = 30;                  // ByDateAdded only
};

} // namespace SmartFilter
```

The struct is intentionally flat — `kind` is the discriminator, and
the other fields are populated only for the kinds that consume them.
Kinds that don't use a field leave it at the default; serialization
preserves the default so a JSON consumer always sees a stable shape.

## JSON shape

`SmartFilter::toJson(Filter)` produces:

```json
{
  "kind": "<tag>",
  "limit": 50,
  "extensions": [],
  "days": 30
}
```

`kind` is a stable string tag (see [Tag table](#tag-table) below) —
never the C++ enum integer. Tags survive renames of the enum.

All four payload fields are always emitted, even for kinds that
don't consume them, so:

- Tooling can read the JSON without checking the discriminator first.
- Round-trip is value-stable: encode + decode reproduces the same
  bytes for the same struct.
- Adding a new field is additive — old code reading a forward-
  compatible payload sees the new field as JSON noise.

`fromJson(QJsonObject)` returns an `ErrorUtils::Result<Filter>`. It
errors on:

- Missing or non-string `kind` field.
- Unknown `kind` tag (forward-compat — newer code wrote a kind we
  don't recognise).

It does *not* error on out-of-range numeric values; clamping happens
downstream in the evaluator (see [Clamping](#clamping)).

### Tag table

| Kind enum | JSON tag |
|-----------|----------|
| `RecentlyLaunched` | `"recently_launched"` |
| `TopPlayed` | `"top_played"` |
| `NeverPlayed` | `"never_played"` |
| `ByExtension` | `"by_extension"` |
| `HasArtwork` | `"has_artwork"` |
| `ByDateAdded` | `"by_date_added"` |

Tags are lowercase snake_case ASCII. The C++ enumerator names are
PascalCase, but those are private to the source — never serialize the
enumerator name as a tag.

## Storage

The serialized JSON lives in `playlists.smart_filter` (TEXT), with
`playlists.is_smart = 1` as the type tag. The schema was added via
`ALTER TABLE playlists ADD COLUMN` in
[playlistmanager.cpp](../src/modules/data/playlist/playlistmanager.cpp)
— safe on upgrade from older databases.

`SmartFilter::toJsonString(Filter)` produces a compact (no
whitespace) UTF-8 JSON string suitable for the column;
`fromJsonString(QString)` is the inverse.

## Evaluation

`SmartPlaylistEvaluator::evaluate(QSqlDatabase &, const Filter &)`
returns `QList<Match>` where:

```cpp
struct Match {
  QString collectionUuid; // matches items.collection_uuid
  QString path;            // matches items.path
};
```

This is the canonical "playlist member" tuple used everywhere a
membership snapshot is needed — Smart-playlist open, JSON export,
sidebar member count.

The function runs **on the QueryManager worker thread / connection**.
Callers must marshal it via the same signal/slot machinery used for
regular collection queries (see
[architecture.md](architecture.md#threading-model)).

### Per-kind SQL

Each branch dispatches to a small SQL helper:

| Kind | Query | Order |
|------|-------|-------|
| `RecentlyLaunched` | `SELECT ... FROM items WHERE last_played IS NOT NULL ORDER BY last_played DESC LIMIT ?` | Newest first |
| `TopPlayed` | `SELECT ... FROM items WHERE play_count > 0 ORDER BY play_count DESC, last_played DESC LIMIT ?` | Highest count first; ties broken by `last_played` |
| `NeverPlayed` | `SELECT ... FROM items WHERE play_count = 0 OR play_count IS NULL ORDER BY name COLLATE NOCASE ASC LIMIT ?` | Alphabetical — matches the Statistics dialog's Never-played tab |
| `ByExtension` | `SELECT ... FROM items WHERE LOWER(ext) IN (?, ?, ...)` | Default item order |
| `HasArtwork` | `SELECT ... FROM items WHERE artwork_path != ''` | Default item order |
| `ByDateAdded` | `SELECT ... FROM items WHERE date_added >= ? ORDER BY date_added DESC` | Newest first |

All queries are prepared via `QSqlQuery::prepare` + `addBindValue` —
no string interpolation, no SQL injection surface for hand-edited
filters.

### Clamping

The evaluator clamps the spec values to defensible ranges so a
hand-edited `smart_filter` JSON can't make the query pathological:

| Field | Clamp range | Source |
|-------|-------------|--------|
| `limit` | `[1, 1000]` | `SmartPlaylistEvaluator::clampLimit` |
| `days` | `[1, 3650]` | Per-kind in `evalByDateAdded` |

Out-of-range values are silently clamped (no error). The dialog UI
enforces the same ranges via `QSpinBox` minimums and maximums so
typical writes produce in-range values; clamping is the defensive
backstop.

### Empty / pathological inputs

| Input | Behavior |
|-------|----------|
| `ByExtension` with empty `extensions` list | Returns zero matches without hitting the DB. |
| `Database not open` for any kind | Logs a warning to `lcErrors`, returns zero matches. |
| Unknown kind tag | `fromJson` returns an error; the playlist is treated as inert (no members) rather than throwing. |

## Adding a new filter kind

1. **Add the enum entry** to `SmartFilter::Kind` in
   [smartfilter.h](../src/utils/db/smartfilter.h).
2. **Add a tag string** to the `TAG_*` constants in
   [smartfilter.cpp](../src/utils/db/smartfilter.cpp), and update
   `kindToTag` / `tagToKind`. Pick a stable snake_case ASCII tag
   that won't change.
3. **Add a UI page** in
   [createsmartplaylistdialog.cpp](../src/ui/dialogs/collection/createsmartplaylistdialog.cpp):
   a new entry in `m_kindCombo`, a new stacked-widget page for the
   per-kind parameter inputs, and `onKindChanged` routing.
4. **Add a query** in
   [smartplaylistevaluator.cpp](../src/utils/db/smartplaylistevaluator.cpp):
   an `eval<NewKind>` helper and a switch case in `evaluate()`.
5. **Add the human label** in `SmartFilter::humanLabel` so the
   sidebar tooltip and dialog summary read sensibly.
6. **If the new kind uses a new parameter type**, extend the
   `Filter` struct and the `toJson` / `fromJson` round-trip. Keep
   the existing payload fields in the JSON shape; just add yours.

The JSON shape always emits every payload field, so existing serialised
filters keep round-tripping unchanged. The forward-compat path is:
**older code reading newer JSON ignores the extra field; older code
reading a newer `kind` tag errors out and treats the playlist as
inert**.

## Related code

| Concern | File |
|---------|------|
| Filter struct + JSON | [src/utils/db/smartfilter.{h,cpp}](../src/utils/db/) |
| Evaluator | [src/utils/db/smartplaylistevaluator.{h,cpp}](../src/utils/db/) |
| Create / edit dialog | [src/ui/dialogs/collection/createsmartplaylistdialog.{h,cpp}](../src/ui/dialogs/collection/) |
| Storage / virtual-collection synthesis | [src/modules/data/playlist/playlistmanager.{h,cpp}](../src/modules/data/playlist/) |
| Context-menu entries | [src/modules/input/interaction/interactionmanagercontextmenu.cpp](../src/modules/input/interaction/) |
| User docs | [docs/guide/Smart-Playlists.md](guide/Smart-Playlists.md) |
