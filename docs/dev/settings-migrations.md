# Settings INI schema migrations

Kartend stamps every settings save with `[General/schemaVersion]` (defined as
`kSettingsSchemaVersion` in
[settingsmanager.cpp](../../src/modules/data/settings/settingsmanager.cpp)) and
runs a step-function migration dispatcher on load when the stored version is
behind the current build. The dispatcher lives in
[settingsmigrations.h](../../src/modules/data/settings/settingsmigrations.h) /
[settingsmigrations.cpp](../../src/modules/data/settings/settingsmigrations.cpp).

This doc describes the contract for adding a new migration. The parallel
database-schema dispatcher (see [db-migrations.md](db-migrations.md)) uses the
same pattern; consult that file for the design rationale.

## When to bump `kSettingsSchemaVersion`

Bump it whenever the **shape** of an existing key changes in a way the
current-version reader can't handle gracefully:

- Renaming a key
- Splitting one key into several (or merging several into one)
- Changing the type of a stored value (e.g. `int` → `string` with an enum)
- Restructuring a `beginReadArray` block
- Removing a key whose absence the reader doesn't already treat as "use the
  default"

You do **not** need a bump for additive-only changes — a new key with a
default value loads cleanly out of a pre-bump INI because `QSettings::value`
returns the supplied default when the key is missing. Adding fields is
free; reshaping them needs a migration.

## Anatomy of a migration step

A `MigrationStep` is a frozen record that owns:

- `from`: the schemaVersion this step upgrades **from**
- `to`: must equal `from + 1` — every step is a single-version increment
- `apply(QSettings &)`: a lambda that mutates the settings in place
- `summary`: a short string for logging

Steps are registered in the `registeredSteps()` table inside
`settingsmigrations.cpp`. The dispatcher walks the table in declaration order,
applying each step whose `from` matches the current cursor. Gaps in the
sequence (e.g. `0->1` followed by `2->3`) are detected at runtime and abort
the chain with a structured log.

## Walking the dispatcher

`applyMigrations` (in `settingsmigrations.cpp`) implements the walk:

1. If the loaded version is already `>= currentVersion`, return immediately
   (the future-version case is handled by the caller's separate
   `loadedVersion > kSettingsSchemaVersion` warning path in
   `loadGeneralSettings`).
2. For each registered step whose `from == cursor`, run `apply` and bump
   `cursor` to `step.to`.
3. If a step's `from` doesn't match the cursor (gap), log a warning naming
   the missing version and stop. Subsequent fields will read with build
   defaults — the user's data is preserved as raw keys in the INI but the
   in-memory model loses the upgrade.

The dispatcher is called from `SettingsManager::loadGeneralSettings`
**before** any `s.value(...)` reads, so the migration body is free to rename
keys before the loader touches them.

## Adding a new migration

When bumping the schema (say, v1 → v2 to rename `oldKey` → `newKey`):

1. **Bump the constant.** In `settingsmanager.cpp`, increment
   `kSettingsSchemaVersion`. This is what new saves will stamp.

2. **Append a step.** In `settingsmigrations.cpp`'s `registeredSteps()`,
   add an entry with `from = 1`, `to = 2`, and a body that mutates the
   settings handle. Use the commented template at the bottom of the
   `registeredSteps()` table as a starting point.

   ```cpp
   MigrationStep{
       /*from=*/1,
       /*to=*/2,
       [](QSettings &settings) {
         settings.beginGroup(QStringLiteral("General"));
         const QVariant legacy = settings.value(QStringLiteral("oldKey"));
         if (legacy.isValid()) {
           settings.setValue(QStringLiteral("newKey"), legacy);
           settings.remove(QStringLiteral("oldKey"));
         }
         settings.endGroup();
       },
       "v1->v2: rename oldKey to newKey under [General]",
   },
   ```

3. **Add a fixture.** Drop a representative `tests/modules/settings/
   fixtures/vN.ini` showing the pre-bump shape. The existing
   `tests/modules/settings/test_settingsmigration.cpp` is the template —
   install the fixture, load via `SettingsManager::loadGeneralSettings`,
   and assert the post-migration `GeneralSettings` reflects the new shape.

4. **Update this doc.** Add the new step to the
   [Step history](#step-history) table below.

## Hard rules

- **Never edit an existing step after release.** A user's INI may have been
  upgraded through that step on a prior session; rewriting the body
  retroactively changes what their on-disk file means. If a released step is
  wrong, ship a follow-up step from `to` → `to+1` that corrects it.

- **Single-version increments only.** `step.to == step.from + 1`. The
  dispatcher enforces this with a defensive check and refuses to apply
  multi-version jumps.

- **No `beginGroup` carryover into the step body.** The dispatcher hands
  the step a `QSettings &` at the root group; the step is responsible for
  its own `beginGroup`/`endGroup` pairing for whatever sections it
  touches.

- **No reads outside the step.** Each step is self-contained: it doesn't
  reach back into `SettingsManager` state or other singletons. The only
  input is the `QSettings &` it receives.

## v0: the legacy unversioned baseline

INIs written by builds that predate the `[General/schemaVersion]` key are
treated as **version 0**. The dispatcher has a registered `v0 -> v1` no-op
step (Kartend-h16z) whose body intentionally does nothing — every v1 key
already loads with its declared default when the reader sees an unversioned
INI. The step exists so:

- The migration registration table is non-empty (tests exercise the
  dispatcher walk path, not just the early-return path).
- Future maintainers reading the table see that v0 has been formally
  acknowledged rather than treated as an undefined state.

## Step history

| from → to | Summary | Issue |
|-----------|---------|-------|
| 0 → 1 | Legacy unversioned INI baseline (no-op) | Kartend-h16z |

Append new rows at the bottom in chronological order as schema bumps land.

## Related

- [db-migrations.md](db-migrations.md) — sibling pattern for the SQLite
  schema. The two dispatchers share the structural design.
- [testing.md](testing.md) — explains how
  `tests/modules/settings/test_settingsmigration.cpp` is wired (fixture
  copy into `QStandardPaths` test mode, etc.).
