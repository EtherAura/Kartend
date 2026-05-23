# Database migration workflow

Kartend's SQLite database is migrated in-place at startup. Schema
changes ship as numbered steps in
[dbmigrations.cpp](../src/utils/db/dbmigrations.cpp); each step is
guarded by `PRAGMA user_version` so applied changes are skipped on
subsequent launches and partial-state migrations are rare.

## How it runs

```
DatabaseManager (main thread) ─┐
                               │
                               ▼ Qt::QueuedConnection
QueryManager (worker thread)
  │
  └─ openDatabase(...) ──▶ DbMigrations::applySchemaMigrations(db, origin)
                              │
                              ▼
                          read PRAGMA user_version
                              │
                              ▼
                          if version >= CURRENT_SCHEMA_VERSION → return
                              │
                              ▼
                          run each step where version < N {
                            do the work
                            PRAGMA user_version = N
                          }
```

The call is **idempotent**, runs on every database open, and lives on
the QueryManager worker thread so the main thread never blocks on
migrations. A fresh install lands at the highest version in one pass;
an old database lands there in N hops.

## The user_version pragma

```cpp
constexpr int CURRENT_SCHEMA_VERSION = 13; // in dbmigrations.cpp
```

`CURRENT_SCHEMA_VERSION` **must equal the highest migration block
below it**. Bumping a migration without bumping the constant leaves
the early-return gate skipping the new block, so the schema silently
lags the code (the canonical regression: a missing
`items.date_added` column breaks the scanner upsert).

The migration uses SQLite's built-in `PRAGMA user_version` integer
field — no separate `schema_versions` table.

## The step shape

Each step is an `if (mutableVersion < N) { … setUserVersion(db, N); mutableVersion = N; }`
block:

```cpp
if (mutableVersion < 5) {
  // v5: short comment explaining the schema change and why.
  ensureColumn(db, "items", "date_added", "INTEGER DEFAULT 0", origin);
  ensureIndex(db,
              "CREATE INDEX IF NOT EXISTS idx_items_date_added ON items(date_added)",
              origin, "idx_items_date_added");

  setUserVersion(db, 5);
  mutableVersion = 5;
}
```

Important conventions:

- **Idempotent SQL** — use `ensureColumn`, `ensureIndex`, and
  `CREATE TABLE IF NOT EXISTS` so re-running a step on an already-
  migrated DB is a no-op. Don't write raw `ALTER TABLE ADD COLUMN`;
  use the helper that checks first.
- **Forward-only** — there's no downgrade path. Once a migration
  ships, the only way back is restore-from-backup. Don't drop
  columns; SQLite's `DROP COLUMN` was added in 3.35 and we don't
  use it.
- **Best-effort on optional features** — e.g. the FTS5 index in
  v3 is wrapped so SQLite builds without FTS5 keep working with
  LIKE fallback. Migrations that *require* a feature should fail
  loudly; migrations that *prefer* a feature should degrade.
- **Single-purpose per step** — keep each `mutableVersion < N`
  block focused on one schema change. Mixing two unrelated
  changes in one step makes a future bisect for "which change
  broke things" harder.
- **Bump `CURRENT_SCHEMA_VERSION`** alongside any new block. The
  comment above the constant is there as a reminder.

## Helpers

| Helper | Purpose |
|--------|---------|
| `ensureColumn(db, table, column, defn, origin)` | `ALTER TABLE ... ADD COLUMN` only if absent (checked via `PRAGMA table_info`). |
| `ensureIndex(db, ddl, origin, name)` | `CREATE INDEX IF NOT EXISTS` with structured error logging on failure. |
| `ensureUniqueIndexItemsUuidPath(db, origin)` | One-shot for the historical de-dupe + retry path on a uniqueness violation. |
| `tableHasColumn(db, table, column)` | Returns whether a column exists. Use as the gate when a step needs a value before the `ALTER TABLE`. |
| `setUserVersion(db, version)` | Writes `PRAGMA user_version`. Errors are logged but not propagated — the next launch retries the same block. |

All helpers log via `ErrorUtils::logError` with structured context
keyed on the origin string, so a failure surfaces in
`kartend.database` logs rather than silently dropping work.

## Adding a new migration

1. **Pick the next version number.** Look at the existing blocks in
   [dbmigrations.cpp](../src/utils/db/dbmigrations.cpp) — they're
   sequential (1, 2, 3, …). The new block is `CURRENT_SCHEMA_VERSION
   + 1`.

2. **Add the block.** Append after the last existing migration:

   ```cpp
   if (mutableVersion < 14) {
     // v14: <short why>
     ensureColumn(db, "items", "my_new_field", "TEXT DEFAULT ''", origin);

     setUserVersion(db, 14);
     mutableVersion = 14;
   }
   ```

3. **Bump `CURRENT_SCHEMA_VERSION`** to the same number. The early-
   return guard at the top relies on this matching the highest
   block.

4. **If the new schema affects load/save**, update the read/write
   code that touches the column. Code paths reading the column
   before the migration runs would see the default; code paths
   writing the column expect it to exist.

5. **Test the migration:**

   - **Fresh install path**: delete `~/.local/share/kartend/kartend.db`,
     launch Kartend, confirm the new schema lands in one pass and
     items render normally.
   - **Incremental upgrade path**: copy a `kartend.db` from before
     your change (or stash one with `cp`), launch with your branch,
     confirm the new column appears and the version bumps.
   - **No-op re-run**: launch twice. The second launch should
     short-circuit at the `version >= CURRENT_SCHEMA_VERSION` gate.

6. **Don't backport silent changes.** If a migration is risky
   (drops data, rebuilds a table, does a long backfill), surface
   it: log loudly on entry/exit so users tailing logs see the
   progress.

## Cross-cutting concerns

### Worker-thread context

The migration runs **on the QueryManager worker thread** — the main
thread is blocked on the database-open signal. Migrations that take
seconds will appear as a startup hang to the user. For multi-second
migrations (the FTS5 backfill is the largest today), consider:

- Splitting the heavy work out of the migration block and into a
  background pass that runs after first-paint.
- Surfacing progress through the splash overlay.

### Transactions

The whole migration is **not** wrapped in a transaction. Individual
DDL statements (`CREATE TABLE`, `ALTER TABLE`, `CREATE INDEX`) are
atomic per-statement in SQLite; bundling them in a transaction
doesn't add safety and complicates the helpers. The cost is that a
mid-step crash leaves the database at the in-between state with
`user_version` not yet bumped — the next launch re-runs the step,
which is why idempotency is non-negotiable.

### `kart_*` and `playlists` tables

The migrations cover `items`, `collections`, and the schema indexes.
Other tables that ship later (e.g. `playlists.is_smart`,
`playlists.smart_filter`) are added via their own modules'
`ensureSchema()` methods, *also* using `ALTER TABLE ADD COLUMN IF
NOT EXISTS` semantics so a missing column doesn't block startup.
Eventually those should be folded into the central migration list,
but the current split is fine for now — each module is the source
of truth for its own schema.

## Debugging a failed migration

```bash
QT_LOGGING_RULES="kartend.database=true" kartend
```

Then look for:

- `setUserVersion` warnings — the `PRAGMA user_version = N` write
  itself failed.
- `Failed to migrate database schema` — an `ALTER TABLE` failed (most
  often: column already exists from a manual edit, or the DB is
  locked by another process).

If you need to manually reset:

```bash
# Inspect current version
sqlite3 ~/.local/share/kartend/kartend.db 'PRAGMA user_version'

# Walk back one step (DON'T do this casually — it'll re-run the
# migration on next launch, which had better be idempotent)
sqlite3 ~/.local/share/kartend/kartend.db 'PRAGMA user_version = N'
```

The standard recovery is restore-from-backup. There's no built-in
downgrade tooling.

## Related code

| Concern | File |
|---------|------|
| Migration entry point | [src/utils/db/dbmigrations.cpp](../src/utils/db/dbmigrations.cpp) |
| Helper functions | Same file (`ensureColumn`, `ensureIndex`, `tableHasColumn`) |
| Worker-thread DB open | [src/modules/data/database/databasemanager.cpp](../src/modules/data/database/databasemanager.cpp) |
| Schema callers (playlists / scrape persistence / etc.) | Per-module `ensureSchema()` |
