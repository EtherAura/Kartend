# Developer documentation

Index of the `docs/dev/` reference docs. Start with **building**, **testing**,
and **architecture**; the rest are subsystem deep-dives you can reach for when
you touch that area. The contributor contract itself lives in the repo root
([CONTRIBUTING.md](../../CONTRIBUTING.md)).

## Build, test & CI

- [building.md](building.md) — Build commands, presets, and the `.scripts/build.sh` flags.
- [testing.md](testing.md) — Test harness, `ctest`, and the no-DB-mocking rule.
- [ci-local.md](ci-local.md) — Reproducing the CI jobs locally (`act` / the `kartend-ci` container).
- [git-hooks.md](git-hooks.md) — The pre-commit / pre-push hooks and the gates each one runs.
- [sanitizer-suppressions.md](sanitizer-suppressions.md) — ASan/TSan/UBSan suppression policy and files.

## Architecture & conventions

- [architecture.md](architecture.md) — Module layout, manager hierarchy, and ctx / setup-struct ownership.
- [layering.md](layering.md) — The layering DAG and the `check-layering` lint.
- [constants.md](constants.md) — Where constants live (`UIConstants`) and the no-magic-numbers rule.
- [mainwindow-partials.md](mainwindow-partials.md) — How `MainWindow` is split across partial `.cpp` files.
- [adding-a-setting.md](adding-a-setting.md) — End-to-end steps to add a new INI setting.
- [i18n.md](i18n.md) — Translation workflow (`lupdate` / `lrelease`).
- [dev-debugging.md](dev-debugging.md) — Logging categories and debugging tips.

## Data & persistence

- [db-migrations.md](db-migrations.md) — Database schema migration workflow.
- [settings-migrations.md](settings-migrations.md) — Settings INI schema migrations.
- [settings-hotreload.md](settings-hotreload.md) — Settings hot-reload signal coverage.
- [seed-data.md](seed-data.md) — Generating seed / sample data.
- [kart-format.md](kart-format.md) — The `.kart` personal backup / migration bundle format.

## Subsystems & features

- [scraper-architecture.md](scraper-architecture.md) — The metadata / artwork scraper pipeline.
- [dat-lookup.md](dat-lookup.md) — DAT-file lookup architecture.
- [smart-filter.md](smart-filter.md) — The smart-filter query DSL.
- [detail-page.md](detail-page.md) — Detail-page architecture.
- [selection-restore.md](selection-restore.md) — The selection-restore state machine.
- [subfolder-artwork.md](subfolder-artwork.md) — The subfolder artwork generator.
