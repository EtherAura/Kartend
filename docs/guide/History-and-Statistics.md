# History & Statistics

Kartend tracks how often you launch each item, when you last launched
it, and (optionally) for how long. The **Statistics Dialog** rolls
this up into per-item, per-collection, and library-wide views.

> **Where to find this** — Help → **Statistics**. Master toggles in
> Settings → General: `historyEnabled`, `historyMaxEntries`,
> `runtimeDetectionEnabled`.

## What gets tracked

| Datum | Source | Always? | Notes |
|-------|--------|---------|-------|
| Play count per item | Database (`items.play_count`) | Yes (if `historyEnabled`) | Incremented on every successful launch. |
| Last played per item | Database (`items.last_played`) | Yes | Timestamp of most recent launch. |
| Launch history | Database (`launch_history`) | Yes (if `historyEnabled`) | One row per launch: timestamp, collection, item, optional duration. |
| Time played per item | Database (`launch_history.duration_seconds`) | **Only if `runtimeDetectionEnabled`** | Sum of durations across history rows. |

Without **runtime detection**, Kartend launches items detached and
forgets them — it knows you launched, but not how long the item ran.
Play counts and last-played still record. Time played is `0`.

With runtime detection on, Kartend tracks the child process via
`QProcess`, so `launch_history.duration_seconds` is the real wall-clock
runtime. See [Splash & Now Playing → Runtime detection](Splash-and-Now-Playing.md#runtime-detection)
for what that mode also turns on (the Now Playing overlay) and what it
costs (Kartend stays focused on the launch instead of going to the
background).

## Master toggles

```ini
[General]
historyEnabled=true             ; record launch history rows (default true)
historyMaxEntries=500           ; soft cap; 0 or negative = unlimited
runtimeDetectionEnabled=false   ; track child process for time-played
```

| Setting | Effect when on | Effect when off |
|---------|----------------|-----------------|
| `historyEnabled` | Each launch writes a `launch_history` row. Statistics dialog History tab is populated. | No history rows are written. Play count and last-played still update. |
| `runtimeDetectionEnabled` | Kartend stays focused; tracks the child via `QProcess`; populates `duration_seconds`; renders the Now Playing overlay; suspends [attract mode](Attract-Mode.md). | Kartend launches items detached. No duration tracked; no overlay; attract mode keeps running. |

`historyMaxEntries` is a *soft* cap. After each insert, Kartend
prunes rows older than the Nth most recent (oldest-first). The newest
N stay. Setting it to `0` or a negative number disables pruning entirely
(keep everything). Default `500` is a comfortable middle ground; bump
to `5000` if you want a lot more history without filling the database.

## Statistics Dialog

Open via **Help → Statistics**. Tabbed dialog:

```
┌────────────────────────────────────────────────────────────┐
│ Statistics                                                 │
│                                                            │
│ Total items: 1234         Total launches: 5678             │
│ Total time played: 42h 31m   (or "Disabled" if runtime    │
│                              detection is off)             │
│                                                            │
│ ┌── Most Played ─ Recently Played ─ By Collection ──────┐  │
│ │ ── History ──┐                                        │  │
│ ├────────────────────────────────────────────────────┐  │  │
│ │  (tab content)                                      │  │  │
│ │                                                     │  │  │
│ │                                                     │  │  │
│ └────────────────────────────────────────────────────┘  │  │
│                                                            │
│ [ Reset usage stats… ]              [ Close ]             │
└────────────────────────────────────────────────────────────┘
```

### Header row

| Field | Source |
|-------|--------|
| Total items | Sum of items across all collections |
| Total launches | Sum of `play_count` across all items |
| Total time played | Sum of `duration_seconds` across all `launch_history` rows |

When `runtimeDetectionEnabled=false`, "Total time played" shows
`Disabled — enable runtime detection in Settings to track time` instead
of a duration.

### Most Played tab

Tree view of items, sorted by `play_count` descending. Columns:

- Item name (with collection icon)
- Play count
- Time played (or `—` if not tracked)
- Last played

Click a row to see the item highlighted in its collection. Double-click
launches the item directly from the dialog.

### Recently Played tab

Same shape, sorted by `last_played` descending. Useful as a quick
"return to what I was doing yesterday" view.

### By Collection tab

Per-collection summary:

| Column | Source |
|--------|--------|
| Collection | Name (with icon) |
| Items | Count of items in the collection |
| Launches | Sum of `play_count` for items in this collection |
| Time played | Sum of `duration_seconds` for items in this collection |
| Avg / item | Time played / number of items launched |

Sortable by any column. Useful for "which collection do I actually use
the most?"

### History tab

Chronological log of every launch row in `launch_history`. Columns:

- Timestamp
- Collection
- Item name
- Duration (or `—` if not tracked)

Above the table:

| Control | Effect |
|---------|--------|
| **Record history** checkbox | Live toggle for `historyEnabled`. Saves to settings on click. |
| **Clear history…** button | Confirm prompt → drops all `launch_history` rows. Play counts and last-played remain. |

The History tab is the only place where you can clear history without
also resetting play counts.

## Reset usage stats

The dialog footer has a **Reset usage stats…** button (red / cautionary
styling). It clears:

- All `launch_history` rows
- Every item's `play_count` (set to 0)
- Every item's `last_played` (set to NULL)

Confirmation prompt — destructive and not reversible from within
Kartend. (You could restore from a backup of `kartend.db`.)

## Quick recipes

### "Most played in the last month"

Statistics doesn't filter by date today. Workaround: open the database
directly:

```bash
sqlite3 ~/.local/share/kartend/kartend.db "
SELECT collection_uuid, source_path, COUNT(*) AS launches
FROM launch_history
WHERE timestamp > datetime('now', '-30 days')
GROUP BY collection_uuid, source_path
ORDER BY launches DESC
LIMIT 20;
"
```

A built-in date filter for the History tab is on the wishlist.

### Disable history but keep play counts

```ini
historyEnabled=false
```

Play counts (`items.play_count`) and `last_played` still update on each
launch — those are independent of `launch_history`. The History tab
will show "History is disabled" instead of rows.

### Move history to a different machine

The launch_history table travels with `kartend.db`. Copy the database
file (or use the [`.kart` export](Backup-and-Sharing.md) which doesn't
include it today, but a future option may).

To merge history from two installations, you'd need to dump and import
manually; no built-in merge.

### Leaderboard for one collection

Filter by collection in SQL:

```bash
sqlite3 ~/.local/share/kartend/kartend.db "
SELECT source_path, play_count, last_played
FROM items
WHERE collection_uuid = 'YOUR-UUID'
ORDER BY play_count DESC
LIMIT 10;
"
```

The collection UUID is visible in the Statistics Dialog's By
Collection tab if you hover (or from the collection's settings).

## Privacy

Everything is local. Kartend never sends usage data anywhere — there's
no telemetry, no opt-in beacon, no analytics. The only network call
the app makes is to Qt-internal services if they're enabled at
platform level (e.g. Qt's logging framework can talk to `QSystemTrayIcon`
on some desktops).

To make doubly sure: launch with all categories disabled (`KARTEND_LOG_RULES=
"kartend.*=false"`) and Kartend produces zero log output. The database
stays local; the config stays local; no remote services are ever
contacted.

If you don't want any history recorded:

```ini
[General]
historyEnabled=false
runtimeDetectionEnabled=false
```

Then Kartend records play count and last-played per launch but no
history rows and no durations. To stop recording even those, you'd
need to compile out the relevant SQL (no runtime toggle today).

## Where to next

- [Splash & Now Playing](Splash-and-Now-Playing.md) — runtime detection
  and the Now Playing overlay
- [Attract Mode](Attract-Mode.md) — the attract scroll suspends
  during runs, so attract + runtime detection compose nicely
- [Item Metadata](Item-Metadata.md) — sidebar's per-item stats display
- [File Locations](File-Locations.md#database) — the SQLite file's path

## For developers

- Stores: [src/utils/db/](../../src/utils/db/)
  (`HistoryStore`, `UsageStatsStore`).
- Statistics dialog: [src/ui/dialogs/statisticsdialog.h](../../src/ui/dialogs/).
- Schema: see the migrations in
  [src/utils/db/dbmigrations.cpp](../../src/utils/db/dbmigrations.cpp).
  `items` (with `play_count`, `last_played`), `launch_history`
  (`timestamp`, `collection_uuid`, `source_path`, `duration_seconds`).
- Launch hook: `LaunchManager` calls `UsageStatsStore::recordLaunch()`
  and (when runtime detection is on) follows up with
  `recordCompletion()` after the `QProcess::finished` signal.
- Pruning: `HistoryStore::pruneToMaxEntries()` runs after each insert
  if `historyMaxEntries > 0`.
- Adding a new metric (e.g. "first played"): extend the relevant table
  via a new migration, add an accessor on the store, surface in the
  Statistics dialog and the sidebar.
