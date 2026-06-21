# Contributing to Kartend

Thank you for considering contributing to Kartend! This document outlines the process for contributing.

## Development Setup

1. Install dependencies (see [docs/dev/building.md](docs/dev/building.md)):
   - Qt6 (Core, Gui, Widgets, Sql, Concurrent, Multimedia, MultimediaWidgets)
   - CMake 3.20+
   - C++23 compiler (Clang or GCC)

2. Build:
   ```bash
   .scripts/build.sh --debug
   ```

3. Run tests:
   ```bash
   .scripts/build.sh --tests --run-tests
   ```

## Submitting Changes

1. Fork the repository and create a feature branch from `main`. Use one
   branch per logical change, branched fresh off `main` — don't stack a new
   change onto a prior, still-unmerged branch.
2. Make your changes. Follow the existing code style (enforced by
   `.clang-format` and `.clang-tidy`) and the architecture rules below.
3. Add tests for new functionality where applicable. Tests that exercise
   migrations or real SQL paths **must not mock the database** — use the real
   on-disk SQLite via the integration harness (see
   [docs/dev/testing.md](docs/dev/testing.md)).
4. Run the maintenance gate before pushing:
   ```bash
   .scripts/build.sh --maintenance   # clang-format, clang-tidy, IWYU, cppcheck, layering
   ```
   **clang-format drift is fatal** and the formatter is pinned to
   **clang-format-19** — a newer system clang-format reformats differently and
   fails CI. clang-tidy / cppcheck / raw-`qDebug` findings surface as
   non-fatal notices. Local Qt is newer than CI's pinned **Qt 6.4.2**, so
   reproduce CI failures in the pinned image with `.scripts/ci-local.sh` (see
   [docs/dev/ci-local.md](docs/dev/ci-local.md)).
5. Open a pull request with a clear description of the changes.

> **Automated agents** (Copilot, Claude Code, Codex, …) working in this repo
> must get explicit maintainer approval before each `git commit` and
> `git push`, and must not pass `--no-verify` / `--no-gpg-sign` unless asked.

### Quarterly lint review

The `.clang-tidy` file disables a number of `modernize-*` / `readability-*` /
`misc-*` checks that were too noisy at one point in the project's history.
Each disabled check carries an inline rationale comment, but the codebase
moves fast and what was unworkable six months ago may be tractable now.
Maintainers: scan the disabled-check list each quarter and try re-enabling
one or two that look ripe. Even partial wins (a handful of cleanups followed
by re-enabling a check) compound over time.

The same cadence applies to **sanitizer suppressions**. The TSan and LSan
lists live in [`tests/suppressions/tsan.txt`](tests/suppressions/tsan.txt) and
[`tests/suppressions/lsan.txt`](tests/suppressions/lsan.txt); each entry cites
the bd issue that justified it. These are meant to be *transient* workarounds
for upstream (Qt, glib, PulseAudio, …) races and leaks, but without a review
cadence they accrete forever and become permanent fixtures. Each quarter,
re-evaluate whether a suppressed issue has since been fixed upstream and the
entry can be dropped. The policy and audit notes are in
[docs/dev/sanitizer-suppressions.md](docs/dev/sanitizer-suppressions.md).

**This review is automated** (Kartend-b7nf8). The
[`quarterly-lint-review`](.github/workflows/quarterly-lint-review.yml) workflow
runs on a quarterly cron (1st of Jan/Apr/Jul/Oct) and opens a dated tracking
issue (label `lint-review`) that enumerates the current disabled clang-tidy
checks and every TSan/LSan suppression entry with its cited bd ID — so the
review is a worked checklist, not a reminder you have to remember. You don't
need to scan the files by hand; just work the issue the job files and close it.
Trigger it on demand any time via the workflow's **Run workflow**
(`workflow_dispatch`) button.

### Optional pre-commit hook

An opt-in git pre-commit hook lives in `.scripts/git-hooks/pre-commit`. It
runs `clang-format --dry-run --Werror` on every staged `.cpp`/`.h` file and
exits non-zero if any drift, so a `git commit` fails fast instead of letting
the same drift surface in CI's `--maintenance --format-check`. The hook also
checks version-string consistency across `VERSION`, `PKGBUILD`, the ebuild
filename, and the AppStream metainfo so a version bump can't be partial.

Install with:

```
.scripts/git-hooks/install.sh
```

If the hook ever needs to be bypassed for an emergency commit, use
`git commit --no-verify` — but please don't habituate; the same checks run
in CI and will catch the drift there.

## Code Style

- 2-space indentation, 100 column limit
- Implicit boolean null checks (`if (!ptr)` not `if (ptr == nullptr)`)
- All `QTimer::singleShot` calls must have a comment explaining the delay
- UI constants go in the topical `src/utils/uiconstants/<area>.h` subheader (e.g. `grid.h`, `dialog.h`, `icons.h`) — no magic numbers
- Use `[[nodiscard]]` on const getters and factory functions
- See [docs/dev/architecture.md](docs/dev/architecture.md), [docs/dev/building.md](docs/dev/building.md), and the rules baked into `.clang-format` / `.clang-tidy` for full conventions
- Debugging: [docs/dev/dev-debugging.md](docs/dev/dev-debugging.md) covers logging-category filters (`lcPerfTrace`, `lcSearchDiag`, …), ASan / TSan / UBSan triage, attaching GDB/LLDB, Valgrind/heaptrack recipes, the Qt-specific crash patterns that come up most, and the rationale for the `QTimer::singleShot` "why" comment rule.

## Architecture

See [docs/dev/architecture.md](docs/dev/architecture.md) for module hierarchy, signal
flow, and ownership model. A few rules are load-bearing and enforced:

- **Layering DAG.** Module dependencies must follow the directed acyclic graph
  enforced by `.scripts/check-layering.py` (run as part of `--maintenance`).
  Introducing an edge that violates it fails the build. See
  [docs/dev/layering.md](docs/dev/layering.md).
- **`ApplicationContext` (ctx) access; no sibling-manager pointers in setup
  structs.** A `*Setup` struct carries `const ApplicationContext *ctx` plus
  only non-manager refs (widgets, value containers, callbacks); sibling
  manager/service pointers are read through `ctx` so a manager can't pin its
  siblings' lifetimes. `check-layering.py` fails the build if a `*Manager *` /
  `*Service *` field is added to a `*Setup` struct. See
  [docs/dev/layering.md](docs/dev/layering.md).
- **`parent()` is a runtime lifetime guard, not just ownership metadata.**
  Some call sites check `parent()` to decide whether an object is still
  attached to its QObject tree before touching siblings. Before changing a
  constructor's parent (e.g. `this` → `nullptr`), `grep -rn "parent()"
  src/<module>` and confirm no live path depends on it. See
  [docs/dev/architecture.md](docs/dev/architecture.md) (§ QObject lifecycle).

## Issue tracking conventions (beads / `bd`)

Kartend uses [beads](https://github.com/steveyegge/beads) for internal task
tracking. Most contributors don't need to interact with it directly — GitHub
issues remain the public-facing entry point for bug reports and feature
requests (see below). For PRs that touch the codebase, the convention is:

**Beads IDs (`Kartend-XXXX`) may appear in:** code comments, commit messages
and PR descriptions, `CHANGELOG.md`, and contributor-facing dev docs
under `docs/` (architecture, layering, testing, building, …). The IDs serve
as cross-references to historical extraction or refactor work.

**Beads IDs must NOT appear in:** user-facing string literals (tooltips,
labels, dialogs, error messages), `readme.md`, AppStream metadata under
`packaging/`, or the wiki content under `docs/user/*.md`. End users see
those surfaces and a `Kartend-XXXX` reference there is noise to them.

**The `.beads/` database is not committed.** It's gitignored, so a fresh
clone has no way to resolve a `Kartend-XXXX` reference back to its issue —
`bd show Kartend-XXXX` only works for maintainers who carry the local
database. Treat the IDs as opaque provenance markers, not links: every commit
message, PR description, and code comment that cites one is written to stand
on its own, so you are never expected to look an ID up to understand a change.
Maintainers can still resolve them through the bd/Dolt sync when deeper
archaeology is needed.

## Reporting Issues

Use the GitHub issue templates for bugs and feature requests.

## Translations

All user-visible strings are wrapped in `tr()` and extracted into Qt
Linguist `.ts` files under `translations/`. The baseline locale is
`translations/kartend_en.ts`; `lrelease` runs as part of the regular build
and embeds compiled `.qm` files into the binary under `:/i18n/`. `main.cpp`
loads the locale-matching `.qm` at startup, falling back to source-text
English when no match ships.

To refresh the source-text database after adding or changing `tr()` calls:

```bash
cmake --build build --target update_translations
```

To start a new locale (replace `de` with the target ISO code):

```bash
cp translations/kartend_en.ts translations/kartend_de.ts
```

Edit `kartend_de.ts` in Qt Linguist (`lupdate` / `lrelease` ship with
`qt6-tools`), then submit a PR. CMakeLists.txt globs `translations/kartend_*.ts`
with `CONFIGURE_DEPENDS`, so the new file is picked up automatically on the next
configure — no CMakeLists edit is needed. `qt_add_lupdate` merges new source
strings into every `.ts` without dropping existing translations, and
`qt_add_lrelease` compiles each non-source locale to a `.qm` at build time.

## Cutting a Release

Run `.scripts/bump-version.sh <X.Y.Z>` — it updates every synced file in
one shot and is the only supported way to prep a release. Do **not** bump
the files by hand from this list; the list exists so reviewers know what
the script touches, and it has drifted before (the v0.0.13 prep missed
`vcpkg.json` by following an older copy of this section).

Before pushing a `v<X.Y.Z>` tag, every file below must agree on the new
version. The release workflow (`.github/workflows/release.yml`) fails the
tag build if any are out of sync.

1. `VERSION` — single source of truth; CMake reads this at configure time.
2. `packaging/PKGBUILD` — `pkgver=<X.Y.Z>`.
3. `packaging/kartend-<X.Y.Z>.ebuild` — the ebuild file must be renamed
   (or copied) to match the new version.
4. `packaging/io.github.EtherAura.Kartend.metainfo.xml` — add a new
   `<release version="X.Y.Z" date="YYYY-MM-DD">` entry **with a
   `<description>` block** at the top of `<releases>`. The CI check only
   compares the most-recent `<release>` version; a missing description
   passes CI but ships a release-notes-less entry to GNOME Software,
   KDE Discover, and Flathub. (The script inserts a bare entry — fill in
   the description before tagging.)
5. `vcpkg.json` — `version-string` (Kartend-szdbl keeps the Windows
   dependency manifest's version honest).
6. `CHANGELOG.md` — promote `[Unreleased]` to `[X.Y.Z] - YYYY-MM-DD` and
   open a fresh `[Unreleased]` section (manual — the script does not
   edit the changelog).

After tagging, the release workflow updates `PKGBUILD` `sha256sums=` on
`main` automatically — do not pre-commit a placeholder hash.

### Changelog archiving (cadence)

The root `CHANGELOG.md` keeps `[Unreleased]` plus the **three most recent
releases**; everything older lives in `docs/changelogs/v0.0.x.md`. Promoting
`[Unreleased]` in step 6 leaves four release sections in the root, so the same
release moves the now-oldest one into the archive: cut its `## [X.Y.Z]` block
(and its comparison link-ref) into `docs/changelogs/`, then refresh the root's
bottom-of-file link-refs and the "Older releases" pointer. This keeps the root
file small enough to grep and to render cleanly in PR diffs (Kartend-zdgj).
