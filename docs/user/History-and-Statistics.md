# History & Statistics

Kartend tracks how often you launch each item, when you last launched
it, and (optionally) for how long. The **Statistics Dialog** rolls
this up into per-item, per-collection, and library-wide views.

> **Where to find this** — Help → **Usage Statistics…**. Master
> toggles in Settings → General: `historyEnabled`, `historyMaxEntries`,
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
runtime. See [Splash & Now Playing → Runtime detection](Splash-and-Now-Playing.md#what-runtime-detection-turns-on)
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

Open via **Help → Usage Statistics…**. Tabbed dialog:

```
┌────────────────────────────────────────────────────────────┐
│ Usage Statistics                                           │
│                                                            │
│ Total items:                 1,234                         │
│ Items launched at least once:  421                         │
│ Played in last 7 days:          63                         │
│ Total launches:              5,678                         │
│ Total time played:           42h 31m                       │
│                                                            │
│ (Time played is only tracked when Runtime Detection        │
│  is enabled in Settings → General → Runtime Detection.)    │
│                                                            │
│ ┌ Most played │ Recently played │ Never played │           │
│ │ By collection │ History ───────────────────┐ │           │
│ ├──────────────────────────────────────────────┤           │
│ │  (tab content)                               │           │
│ └──────────────────────────────────────────────┘           │
│                                                            │
│ [ Reset usage stats… ]      [ Refresh ]   [ Close ]        │
└────────────────────────────────────────────────────────────┘
```

### Header row

A small grid above the tabs with running totals:

| Field | Source |
|-------|--------|
| Total items | Sum of items across all collections |
| Items launched at least once | Count of items with `play_count > 0` |
| Played in last 7 days | Count of distinct items with a `launch_history` row in the past 7 days |
| Total launches | Sum of `play_count` across all items |
| Total time played | Sum of `duration_seconds` across all `launch_history` rows. **Shows "Disabled" when [`runtimeDetectionEnabled=false`](Splash-and-Now-Playing.md#what-runtime-detection-turns-on)** — and the inline note explains how to turn it on. |

### Most played tab

Tree view of items, sorted by `play_count` descending. Columns:

| Column | Source |
|--------|--------|
| Item | Item name |
| Collection | Owning collection (with icon) |
| Plays | `items.play_count` |
| Time | Sum of `duration_seconds` (or `—` if runtime detection is off) |
| Last played | `items.last_played` timestamp |

**Double-click a row to navigate to the item** — the dialog closes,
Kartend switches to the item's owning collection, and the item gains
focus in the grid. Single-click selects the row inside the dialog
without leaving it. The same double-click behavior applies on the
Recently Played, Never Played, and History tabs.

### Recently played tab

Same item / collection columns, sorted by `last_played` descending,
with **Last played** as the second column for at-a-glance scanning.
Useful as a quick "return to what I was doing yesterday" view.

### Never played tab

Items with zero recorded launches. Two-column tree:

| Column | Source |
|--------|--------|
| Item | Item name |
| Collection | Owning collection |

Above the tree, a short summary line shows the count and a hint that
these are good candidates for a *Never launched* [Smart
Playlist](Smart-Playlists.md#never-launched) if you want a persistent
backlog tile.

### By collection tab

Per-collection summary:

| Column | Source |
|--------|--------|
| Collection | Name (with icon) |
| Items | Count of items in the collection |
| Launches | Sum of `play_count` for items in this collection |
| Time | Sum of `duration_seconds` for items in this collection (or `—` if not tracked) |

Sortable by any column. Useful for "which collection do I actually
use the most?"

### History tab

Chronological log of every launch row in `launch_history`. Columns:

| Column | Source |
|--------|--------|
| Launched at | Timestamp |
| Item | Item name |
| Collection | Owning collection |
| Path | Absolute file path at launch time |

Above the table:

| Control | Effect |
|---------|--------|
| **Record launch history** checkbox | Live toggle for `historyEnabled`. Saves to settings immediately on click. |
| **Entries:** label | Live count of rows in the table |
| **Clear history…** button | Confirm prompt → drops all `launch_history` rows. Play counts and last-played remain. |

When history recording is off, an inline note explains that existing
entries are still listed (until cleared).

The History tab is the only place where you can clear history without
also resetting play counts.

### Footer controls

- **Reset usage stats…** — red / cautionary; see
  [Reset usage stats](#reset-usage-stats) below.
- **Refresh** — re-runs every query against the database. Use after
  launching items mid-dialog if the counts haven't updated yet.
- **Close** — dismisses the dialog (it's modeless; you can leave it
  open while you browse the grid).

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
file directly when migrating between your own machines —
[`.kart` exports](Backup-and-Migration.md) carry per-item metadata
and playlists but deliberately omit launch history.

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
